// sundog: BSDF sampling/evaluation. Lambert (cosine), metal (GGX + VNDF,
// Schlick F0 = base color), smooth dielectric. Parity mode reproduces the
// original cxxrt sampling for the CPU-vs-GPU benchmark.
//
// Conventions: wo points AWAY from the surface (toward the previous vertex),
// n is the shading normal on the same side as wo (already flipped by the
// hit-processing code), both unit length.
#ifndef SUNDOG_BSDF_CUH
#define SUNDOG_BSDF_CUH

#include "math.cuh"
#include "params.h"
#include "rng.cuh"

namespace sd {

struct BsdfSample {
  float3 wi;
  float3 weight;  // f * |cos| / pdf (or the delta weight)
  float pdf;      // solid-angle pdf; 0 for delta samples
  bool isDelta;
  bool valid;
};

SD_HD bool bsdfIsDelta(const MaterialDesc& m, int parity) {
  if (m.kind == MT_DIELECTRIC) return true;
  if (m.kind == MT_METAL) return parity ? true : (m.roughness < 1e-3f);
  return false;
}

// ---------- GGX (Trowbridge-Reitz) ----------

SD_HD float ggxD(float3 h, float alpha) {  // h in local frame (n = +Z)
  float c2 = h.z * h.z;
  float k = c2 * (alpha * alpha - 1.0f) + 1.0f;
  return alpha * alpha / (SD_PI * k * k);
}

SD_HD float ggxLambda(float3 w, float alpha) {
  float c2 = w.z * w.z;
  if (c2 >= 1.0f) return 0.0f;
  float t2 = (1.0f - c2) / c2;
  return 0.5f * (-1.0f + sqrtf(1.0f + alpha * alpha * t2));
}

SD_HD float ggxG1(float3 w, float alpha) { return 1.0f / (1.0f + ggxLambda(w, alpha)); }
SD_HD float ggxG(float3 wo, float3 wi, float alpha) {
  return 1.0f / (1.0f + ggxLambda(wo, alpha) + ggxLambda(wi, alpha));
}

// Heitz 2018 VNDF sampling; wo in local frame, wo.z > 0.
SD_HD float3 ggxSampleVndf(float3 wo, float alpha, float2 u) {
  float3 vh = normalize(f3(alpha * wo.x, alpha * wo.y, wo.z));
  float len2 = vh.x * vh.x + vh.y * vh.y;
  float3 T1 = len2 > 0.0f ? f3(-vh.y, vh.x, 0.0f) * (1.0f / sqrtf(len2)) : f3(1.0f, 0.0f, 0.0f);
  float3 T2 = cross(vh, T1);
  float r = sqrtf(u.x);
  float phi = 2.0f * SD_PI * u.y;
  float t1 = r * cosf(phi);
  float t2 = r * sinf(phi);
  float s = 0.5f * (1.0f + vh.z);
  t2 = (1.0f - s) * sqrtf(fmaxf(0.0f, 1.0f - t1 * t1)) + s * t2;
  float3 nh = t1 * T1 + t2 * T2 + sqrtf(fmaxf(0.0f, 1.0f - t1 * t1 - t2 * t2)) * vh;
  return normalize(f3(alpha * nh.x, alpha * nh.y, fmaxf(1e-6f, nh.z)));
}

// pdf of wi given VNDF sampling: G1(wo) * D(h) / (4 * |wo.z|)
SD_HD float ggxPdf(float3 wo, float3 wi, float alpha) {
  if (wo.z <= 0.0f || wi.z <= 0.0f) return 0.0f;
  float3 h = normalize(wo + wi);
  return ggxG1(wo, alpha) * ggxD(h, alpha) / (4.0f * wo.z);
}

// ---------- eval / pdf (non-delta lobes only; local computation in world space) ----------

SD_HD float3 bsdfEval(const MaterialDesc& m, float3 albedo, float3 wo, float3 wi, float3 n) {
  float ci = dot(wi, n);
  float co = dot(wo, n);
  if (ci <= 0.0f || co <= 0.0f) return f3(0.0f);
  if (m.kind == MT_LAMBERT) return albedo * SD_INV_PI;
  if (m.kind == MT_METAL && m.roughness >= 1e-3f) {
    float alpha = m.roughness * m.roughness;
    Onb onb(n);
    float3 lo = onb.toLocal(wo), li = onb.toLocal(wi);
    float3 h = normalize(lo + li);
    float3 F = schlick3(dot(lo, h), albedo);
    return F * (ggxD(h, alpha) * ggxG(lo, li, alpha) / (4.0f * lo.z * li.z));
  }
  return f3(0.0f);
}

SD_HD float bsdfPdf(const MaterialDesc& m, float3 wo, float3 wi, float3 n) {
  float ci = dot(wi, n);
  float co = dot(wo, n);
  if (ci <= 0.0f || co <= 0.0f) return 0.0f;
  if (m.kind == MT_LAMBERT) return ci * SD_INV_PI;
  if (m.kind == MT_METAL && m.roughness >= 1e-3f) {
    Onb onb(n);
    return ggxPdf(onb.toLocal(wo), onb.toLocal(wi), m.roughness * m.roughness);
  }
  return 0.0f;
}

// ---------- sample ----------

// `frontface`: true when the ray hit the geometric front side (dielectric
// entering). `rayDir` = incident direction (unit, toward surface) = -wo.
SD_HD BsdfSample bsdfSample(const MaterialDesc& m, float3 albedo, float3 rayDir,
                            float3 n, bool frontface, int parity, Pcg32& rng) {
  BsdfSample s;
  s.valid = false;
  s.pdf = 0.0f;
  s.isDelta = false;
  float3 wo = -rayDir;

  switch (m.kind) {
    case MT_LAMBERT: {
      if (parity) {
        // cxxrt: scattered = normalize(normal + randomInUnitSphere())
        float3 u3 = f3(rng.rnd(), rng.rnd(), rng.rnd());
        float3 d = n + uniformInBall(u3);
        if (length2(d) < 1e-12f) d = n;
        s.wi = normalize(d);
        s.weight = albedo;      // cxxrt's implicit importance-sampling weight
        s.pdf = 1.0f;           // opaque to MIS (parity mode has none)
        s.valid = true;
      } else {
        Onb onb(n);
        float3 li = cosineHemisphere(rng.rnd2());
        s.wi = onb.toWorld(li);
        s.pdf = fmaxf(li.z, 1e-8f) * SD_INV_PI;
        s.weight = albedo;      // f*cos/pdf = albedo for cosine sampling
        s.valid = li.z > 0.0f;
      }
      return s;
    }

    case MT_METAL: {
      if (parity || m.roughness < 1e-3f) {
        // cxxrt: reflect + fuzz * randomInUnitSphere; absorb if below surface
        float3 refl = reflect(rayDir, n);
        if (parity && m.roughness > 0.0f) {
          float3 u3 = f3(rng.rnd(), rng.rnd(), rng.rnd());
          refl = refl + m.roughness * uniformInBall(u3);
        }
        if (length2(refl) < 1e-12f) return s;
        s.wi = normalize(refl);
        if (dot(s.wi, n) <= 0.0f) return s;  // cxxrt kills the path
        float3 F = parity ? albedo : schlick3(dot(s.wi, n), albedo);
        s.weight = F;
        s.isDelta = true;
        s.valid = true;
      } else {
        float alpha = m.roughness * m.roughness;
        Onb onb(n);
        float3 lo = onb.toLocal(wo);
        if (lo.z <= 0.0f) return s;
        float3 h = ggxSampleVndf(lo, alpha, rng.rnd2());
        float3 li = reflect(-lo, h);
        if (li.z <= 0.0f) return s;
        float3 F = schlick3(dot(lo, h), albedo);
        // weight = f*cos/pdf = F * G / G1(wo) for VNDF sampling
        s.wi = onb.toWorld(li);
        s.weight = F * (ggxG(lo, li, alpha) / ggxG1(lo, alpha));
        s.pdf = ggxPdf(lo, li, alpha);
        s.valid = s.pdf > 0.0f;
      }
      return s;
    }

    case MT_DIELECTRIC: {
      s.isDelta = true;
      float3 refr;
      if (parity) {
        // cxxrt always uses eta = 1/ior with the flipped normal and computes
        // the Schlick term with the raw ior.
        float eta = 1.0f / m.ior;
        if (refract(rayDir, n, eta, refr)) {
          float cosine = -dot(rayDir, n);
          float f0 = (1.0f - m.ior) / (1.0f + m.ior);
          f0 = f0 * f0;
          float reflectProb = schlick(cosine, f0);
          if (rng.rnd() >= reflectProb) {
            s.wi = normalize(refr);
            s.weight = f3(1.0f);
            s.valid = true;
            return s;
          }
        }
        s.wi = normalize(reflect(rayDir, n));
        s.weight = f3(1.0f);
        s.valid = true;
        return s;
      }
      float eta = frontface ? 1.0f / m.ior : m.ior;
      float f0 = (1.0f - eta) / (1.0f + eta);
      f0 = f0 * f0;
      if (refract(rayDir, n, eta, refr)) {
        // Schlick needs the cosine on the low-IOR side: the incident angle
        // when entering, the transmitted angle when exiting the denser
        // medium (continuous with the TIR branch: cosT -> 0 => R -> 1).
        float cosine = frontface ? -dot(rayDir, n) : -dot(refr, n);
        float reflectProb = schlick(cosine, f0);
        if (rng.rnd() < reflectProb) {
          s.wi = normalize(reflect(rayDir, n));
        } else {
          s.wi = normalize(refr);
        }
      } else {
        s.wi = normalize(reflect(rayDir, n));  // total internal reflection
      }
      s.weight = f3(1.0f);
      s.valid = true;
      return s;
    }

    default:
      return s;  // emissive: no scattering
  }
}

}  // namespace sd

#endif

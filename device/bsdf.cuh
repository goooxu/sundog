// sundog: BSDF sampling/evaluation. Lambert (cosine), metal (GGX + VNDF,
// Schlick F0 = base color), dielectric (smooth delta below the roughness
// gate; GGX microfacet reflection + transmission above it — Walter et al.
// 2007, VNDF-sampled half vectors), plastic (Fresnel-coupled Lambert base
// under a GGX dielectric coat — the first two-lobe mixture; see plasticTerms).
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

SD_HD bool bsdfIsDelta(const MaterialDesc& m) {
  // Water stays a smooth interface unconditionally: its waves arrive via the
  // fbm normal perturbation (ch14), not microfacets.
  if (m.kind == MT_WATER) return true;
  if (m.kind == MT_DIELECTRIC || m.kind == MT_METAL) return m.roughness < 1e-3f;
  return false;
}

// ---------- GGX (Trowbridge-Reitz) ----------

SD_HD float ggxD(float3 h, float alpha) {  // h in local frame (n = +Z)
  float c2 = h.z * h.z;
  // Numerically stable form of c2*(alpha^2-1)+1: at tiny alpha the compact
  // form rounds (alpha^2 - 1) to -1 and lands on k = 0 exactly when h.z = 1,
  // blowing D (and every pdf built on it) up to inf. Grouping as
  // alpha^2*c2 + (1-c2) keeps k >= alpha^2 > 0 for any gated roughness.
  float k = alpha * alpha * c2 + (1.0f - c2);
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

// Dielectric Fresnel with the smooth branch's conventions: Schlick at the
// low-IOR-side cosine (incident when eta < 1, transmitted otherwise), and
// F = 1 past the critical angle. cosI is the incident cosine against the
// (micro)facet normal, > 0. Continuous with TIR (cosT -> 0 => F -> 1), and
// sqrtf(k) equals -dot(refr, h) from refract()'s construction, so this is
// mutually exact with the sampled refraction direction.
SD_HD float fresnelDielectric(float cosI, float eta, float f0) {
  float k = 1.0f - eta * eta * (1.0f - cosI * cosI);
  if (k <= 0.0f) return 1.0f;  // total internal reflection at the facet
  return schlick(eta < 1.0f ? cosI : sqrtf(k), f0);
}

// pdf of a transmitted wi under VNDF sampling: D_vis(h) times the refraction
// Jacobian |dwh/dwi| = |wi.h| / (eta*(wo.h) + (wi.h))^2 (Walter et al. 2007
// eq. 17 in relative-eta form, eta = eta_o_side / eta_i_side). h canonical
// on the wo side (h.z > 0), wo above, wi below.
SD_HD float ggxPdfTransmit(float3 lo, float3 li, float3 h, float eta, float alpha) {
  float loh = dot(lo, h), lih = dot(li, h);
  if (lo.z <= 0.0f || li.z >= 0.0f || loh <= 0.0f || lih >= 0.0f) return 0.0f;
  float denom = eta * loh + lih;
  if (fabsf(denom) < 1e-6f) return 0.0f;  // index-matched degeneracy
  return ggxG1(lo, alpha) * ggxD(h, alpha) * loh / lo.z *
         fabsf(lih) / (denom * denom);
}

// ---------- plastic (GGX dielectric coat over a Fresnel-coupled Lambert base) ----------
// Coat Fresnel is Schlick at f0 = ((ior-1)/(ior+1))^2, always evaluated
// air->plastic from m.ior: the surface is opaque, never joins the medium
// stack, and shades both faces identically (n is pre-flipped to the wo side),
// so the eta interface parameter is deliberately ignored. The diffuse base is
// scaled by (1-F(cos_o))(1-F(cos_i)) — reciprocal, and E(wo) <= 1 at every
// angle because the base dims exactly where the coat saturates (the price:
// whites sit ~10% below lambert; the hemispheric Schlick average is
// f0 + (1-f0)/21 ~= 0.086 at ior 1.5). Roughness is floored at 1e-3: the
// coat is never a delta lobe (bsdfIsDelta stays false, NEE always connects).
// Both lobes overlap in one hemisphere, so — unlike the rough dielectric —
// the lobe-choice probability never cancels: sample/eval/pdf all go through
// this one helper and the constant 0.5-mixture pdf, keeping f*cos/pdf ==
// weight exact by construction.
struct PlasticTerms {
  float fSpec;   // coat BRDF value (scalar: untinted coat)
  float3 fDiff;  // coupled diffuse BRDF value
  float pSpec;   // VNDF reflection pdf
  float pDiff;   // cosine-hemisphere pdf
};

SD_HD PlasticTerms plasticTerms(const MaterialDesc& m, float3 albedo,
                                float3 lo, float3 li) {  // lo.z, li.z > 0
  PlasticTerms t;
  float rough = fmaxf(m.roughness, 1e-3f);
  float alpha = rough * rough;
  float f0 = (m.ior - 1.0f) / (m.ior + 1.0f);
  f0 = f0 * f0;
  float3 h = normalize(lo + li);
  float F = schlick(dot(lo, h), f0);
  t.fSpec = F * ggxD(h, alpha) * ggxG(lo, li, alpha) / (4.0f * lo.z * li.z);
  t.fDiff = albedo * (SD_INV_PI * (1.0f - schlick(lo.z, f0)) *
                      (1.0f - schlick(li.z, f0)));
  t.pSpec = ggxPdf(lo, li, alpha);
  t.pDiff = li.z * SD_INV_PI;
  return t;
}

// ---------- eval / pdf (non-delta lobes only; local computation in world space) ----------
// eta: relative IOR at this interface (outside/inside as seen from wo, i.e.
// frontface ? etaExt/ior : ior/etaExt) — consumed only by the rough
// dielectric, whose transmission lobe lives in the wi-below-surface
// hemisphere that every other material rejects.

SD_HD float3 bsdfEval(const MaterialDesc& m, float3 albedo, float3 wo, float3 wi,
                      float3 n, float eta) {
  float ci = dot(wi, n);
  float co = dot(wo, n);
  if (co <= 0.0f) return f3(0.0f);
  bool roughGlass = m.kind == MT_DIELECTRIC && m.roughness >= 1e-3f;

  if (ci <= 0.0f) {
    // Transmission hemisphere: only the rough dielectric reaches through.
    if (!roughGlass || ci == 0.0f) return f3(0.0f);
    // Walter et al. 2007 eq. 21 in relative-eta form, INCLUDING the eta^2
    // radiance factor: it is not optional — NEE consumes this eval for
    // unpaired single-interface connections, which are biased by eta^2
    // without it. Along a full BSDF path the entry (eta^2 < 1) and exit
    // (1/eta^2) factors cancel, so closed glass bodies stay consistent with
    // the smooth branch's weight = 1 convention.
    float alpha = m.roughness * m.roughness;
    Onb onb(n);
    float3 lo = onb.toLocal(wo), li = onb.toLocal(wi);
    float3 h = normalize(eta * lo + li);  // ∝ the refraction half vector
    if (h.z < 0.0f) h = -h;               // canonical: wo side
    float loh = dot(lo, h), lih = dot(li, h);
    if (loh <= 0.0f || lih >= 0.0f) return f3(0.0f);
    float denom = eta * loh + lih;
    if (fabsf(denom) < 1e-6f) return f3(0.0f);
    float f0 = (1.0f - eta) / (1.0f + eta);
    f0 = f0 * f0;
    float F = fresnelDielectric(loh, eta, f0);  // F = 1 inside the TIR cone
    float ft = eta * eta * (1.0f - F) * ggxD(h, alpha) * ggxG(lo, li, alpha) *
               fabsf(loh * lih) / (lo.z * fabsf(li.z) * denom * denom);
    return f3(ft);  // untinted: glass color arrives via absorb, not albedo
  }

  if (m.kind == MT_LAMBERT) return albedo * SD_INV_PI;
  if (m.kind == MT_PLASTIC) {
    // Both lobes, always: NEE consumes this eval with the mixture pdfB, whose
    // balance weight assumes f_total — returning one lobe would lose the coat
    // highlight from area lights (or nearly everything, the other way round).
    Onb onb(n);
    PlasticTerms t = plasticTerms(m, albedo, onb.toLocal(wo), onb.toLocal(wi));
    return f3(t.fSpec) + t.fDiff;
  }
  if (m.kind == MT_METAL && m.roughness >= 1e-3f) {
    float alpha = m.roughness * m.roughness;
    Onb onb(n);
    float3 lo = onb.toLocal(wo), li = onb.toLocal(wi);
    float3 h = normalize(lo + li);
    float3 F = schlick3(dot(lo, h), albedo);
    return F * (ggxD(h, alpha) * ggxG(lo, li, alpha) / (4.0f * lo.z * li.z));
  }
  if (roughGlass) {
    // Reflection lobe: the metal microfacet formula with dielectric Fresnel.
    float alpha = m.roughness * m.roughness;
    Onb onb(n);
    float3 lo = onb.toLocal(wo), li = onb.toLocal(wi);
    float3 h = normalize(lo + li);
    float f0 = (1.0f - eta) / (1.0f + eta);
    f0 = f0 * f0;
    float F = fresnelDielectric(dot(lo, h), eta, f0);
    return f3(F * ggxD(h, alpha) * ggxG(lo, li, alpha) / (4.0f * lo.z * li.z));
  }
  return f3(0.0f);
}

SD_HD float bsdfPdf(const MaterialDesc& m, float3 wo, float3 wi, float3 n,
                    float eta) {
  float ci = dot(wi, n);
  float co = dot(wo, n);
  if (co <= 0.0f) return 0.0f;
  bool roughGlass = m.kind == MT_DIELECTRIC && m.roughness >= 1e-3f;

  if (ci <= 0.0f) {
    if (!roughGlass || ci == 0.0f) return 0.0f;
    // Transmission pdf INCLUDES the Fresnel lobe-choice probability so it
    // matches bs.pdf from sampling (the emissive-hit MIS depends on this).
    float alpha = m.roughness * m.roughness;
    Onb onb(n);
    float3 lo = onb.toLocal(wo), li = onb.toLocal(wi);
    float3 h = normalize(eta * lo + li);
    if (h.z < 0.0f) h = -h;
    float loh = dot(lo, h);
    if (loh <= 0.0f) return 0.0f;
    float f0 = (1.0f - eta) / (1.0f + eta);
    f0 = f0 * f0;
    float F = fresnelDielectric(loh, eta, f0);
    return (1.0f - F) * ggxPdfTransmit(lo, li, h, eta, alpha);
  }

  if (m.kind == MT_LAMBERT) return ci * SD_INV_PI;
  if (m.kind == MT_PLASTIC) {
    // Mixture marginal with the constant lobe probability INSIDE it — must
    // match bs.pdf from sampling exactly (NEE pdfB and the emissive-hit MIS).
    Onb onb(n);
    PlasticTerms t = plasticTerms(m, f3(0.0f) /* pdfs ignore albedo */,
                                  onb.toLocal(wo), onb.toLocal(wi));
    return 0.5f * (t.pSpec + t.pDiff);
  }
  if (m.kind == MT_METAL && m.roughness >= 1e-3f) {
    Onb onb(n);
    return ggxPdf(onb.toLocal(wo), onb.toLocal(wi), m.roughness * m.roughness);
  }
  if (roughGlass) {
    float alpha = m.roughness * m.roughness;
    Onb onb(n);
    float3 lo = onb.toLocal(wo), li = onb.toLocal(wi);
    float3 h = normalize(lo + li);
    float f0 = (1.0f - eta) / (1.0f + eta);
    f0 = f0 * f0;
    float F = fresnelDielectric(dot(lo, h), eta, f0);
    return F * ggxPdf(lo, li, alpha);
  }
  return 0.0f;
}

// ---------- sample ----------

// `frontface`: true when the ray hit the geometric front side (dielectric
// entering). `rayDir` = incident direction (unit, toward surface) = -wo.
// `etaExt`: IOR of the medium on the OUTSIDE of this interface (the raygen
// medium stack; 1.0 in vacuum) — glass under water refracts at 1.5/1.33.
SD_HD BsdfSample bsdfSample(const MaterialDesc& m, float3 albedo, float3 rayDir,
                            float3 n, bool frontface, float etaExt, Pcg32& rng) {
  BsdfSample s;
  s.valid = false;
  s.pdf = 0.0f;
  s.isDelta = false;
  float3 wo = -rayDir;

  switch (m.kind) {
    case MT_LAMBERT: {
      Onb onb(n);
      float3 li = cosineHemisphere(rng.rnd2());
      s.wi = onb.toWorld(li);
      s.pdf = fmaxf(li.z, 1e-8f) * SD_INV_PI;
      s.weight = albedo;      // f*cos/pdf = albedo for cosine sampling
      s.valid = li.z > 0.0f;
      return s;
    }

    case MT_METAL: {
      if (m.roughness < 1e-3f) {
        // delta mirror
        s.wi = normalize(reflect(rayDir, n));
        if (dot(s.wi, n) <= 0.0f) return s;
        s.weight = schlick3(dot(s.wi, n), albedo);
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

    case MT_WATER:  // stays a smooth interface unconditionally; waves arrive
                    // via n, absorption is tracked by the caller (raygen)
    case MT_DIELECTRIC: {
      float3 refr;
      float eta = frontface ? etaExt / m.ior : m.ior / etaExt;
      float f0 = (1.0f - eta) / (1.0f + eta);
      f0 = f0 * f0;
      if (m.kind == MT_WATER || m.roughness < 1e-3f) {
        // ---- smooth delta interface (the original branch, byte-identical:
        // one rnd() on refract success, zero on TIR — goldens pin it) ----
        s.isDelta = true;
        if (refract(rayDir, n, eta, refr)) {
          // Schlick needs the cosine on the low-IOR side: the incident angle
          // when eta < 1, the transmitted angle otherwise (continuous with the
          // TIR branch: cosT -> 0 => R -> 1). With etaExt = 1 this reduces to
          // the old entering/exiting split.
          float cosine = eta < 1.0f ? -dot(rayDir, n) : -dot(refr, n);
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
      // ---- rough dielectric: GGX reflection + transmission (Walter et al.
      // 2007), VNDF half vector as in the metal branch (Heitz 2018). The
      // Fresnel lobe-choice probability cancels F in the BSDF, so the weight
      // is G/G1 <= 1 for either lobe — the transmission continuation of the
      // metal branch's F*(G/G1). Draw order: VNDF rnd2() first (F depends on
      // the sampled facet), lobe rnd() second; facet-level TIR reflects
      // deterministically with no lobe draw, mirroring the smooth branch.
      float alpha = m.roughness * m.roughness;
      Onb onb(n);
      float3 lo = onb.toLocal(wo);
      if (lo.z <= 0.0f) return s;
      float3 h = ggxSampleVndf(lo, alpha, rng.rnd2());
      float loh = dot(lo, h);
      if (loh <= 0.0f) return s;
      float3 li;
      if (!refract(-lo, h, eta, refr)) {   // all in the local frame
        li = reflect(-lo, h);  // TIR at the microfacet: lobe prob = 1
        if (li.z <= 0.0f) return s;
        s.pdf = ggxPdf(lo, li, alpha);
      } else {
        float cosine = eta < 1.0f ? loh : -dot(refr, h);
        float reflectProb = schlick(cosine, f0);
        if (rng.rnd() < reflectProb) {
          li = reflect(-lo, h);
          if (li.z <= 0.0f) return s;
          s.pdf = reflectProb * ggxPdf(lo, li, alpha);
        } else {
          li = normalize(refr);
          if (li.z >= 0.0f) return s;
          s.pdf = (1.0f - reflectProb) * ggxPdfTransmit(lo, li, h, eta, alpha);
        }
      }
      s.wi = onb.toWorld(li);
      // weight = f*|cos|/pdf with the lobe probability cancelled: G/G1 for
      // reflection, eta^2 * G/G1 for transmission (the Walter eta^2 radiance
      // factor — entry and exit cancel along a closed glass body, keeping
      // parity with the smooth branch's weight = 1).
      float wgt = ggxG(lo, li, alpha) / ggxG1(lo, alpha);
      if (li.z < 0.0f) wgt *= eta * eta;
      s.weight = f3(wgt);
      s.valid = s.pdf > 0.0f;
      return s;
    }

    case MT_PLASTIC: {
      // Constant 0.5 lobe choice FIRST (nothing facet-dependent to defer,
      // unlike the rough dielectric), then the chosen lobe's 2D draw: exactly
      // 3 draws on every plastic hit, branch-independent.
      Onb onb(n);
      float3 lo = onb.toLocal(wo);
      if (lo.z <= 0.0f) return s;
      bool coat = rng.rnd() < 0.5f;
      float3 li;
      if (coat) {
        float rough = fmaxf(m.roughness, 1e-3f);
        float3 h = ggxSampleVndf(lo, rough * rough, rng.rnd2());
        li = reflect(-lo, h);
      } else {
        li = cosineHemisphere(rng.rnd2());
      }
      if (li.z <= 0.0f) return s;  // below the horizon: dead sample (metal convention)
      PlasticTerms t = plasticTerms(m, albedo, lo, li);
      s.pdf = 0.5f * (t.pSpec + t.pDiff);
      if (s.pdf <= 0.0f) return s;
      s.wi = onb.toWorld(li);
      // weight = (fSpec + fDiff) * cos / mixture pdf, all full expressions:
      // the lobes overlap, so nothing cancels — the metal-style G/G1 shortcut
      // is the single-lobe cancellation and would be wrong under a mixture.
      // Bounded by 2 * max(F * G/G1, coupled albedo) <= 2 (mediant bound:
      // each lobe's f * cos is <= its own pdf term).
      s.weight = (f3(t.fSpec) + t.fDiff) * (li.z / s.pdf);
      s.valid = true;
      return s;
    }

    default:
      return s;  // emissive: no scattering
  }
}

}  // namespace sd

#endif

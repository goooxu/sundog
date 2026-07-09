// sundog: explicit light sampling (NEE) + solid-angle pdfs for MIS.
#ifndef SUNDOG_LIGHT_SAMPLE_CUH
#define SUNDOG_LIGHT_SAMPLE_CUH

#include "math.cuh"
#include "params.h"
#include "rng.cuh"
#include "texture_eval.cuh"

namespace sd {

struct LightSample {
  float3 wi;      // unit direction from shading point to light
  float dist;     // distance to the light sample (shadow ray tmax)
  float3 Li;      // incident radiance (delta lights: pre-divided by pdf)
  float pdf;      // solid-angle pdf; delta lights use pdf=1 with Li folded
  bool isDelta;
  bool valid;
};

// x: shading point (world). Uniform pick over lights happens in the caller.
// textures: for textured rect/disk emitters (Li must match the emitter-hit
// evaluation exactly or MIS is biased).
SD_HD LightSample sampleLight(const LightDesc& lt, float3 x, int parity, Pcg32& rng,
                              const TextureDesc* textures) {
  LightSample s;
  s.valid = false;
  s.isDelta = false;
  s.pdf = 0.0f;

  switch (lt.kind) {
    case LT_RECT:
    case LT_DISK: {
      float3 p;
      float tu, tv;  // canonical uv of the sampled point (matches intersect.cuh)
      if (lt.kind == LT_RECT) {
        float a = 2.0f * rng.rnd() - 1.0f;
        float b = 2.0f * rng.rnd() - 1.0f;
        p = lt.p + a * lt.u + b * lt.v;
        tu = (a + 1.0f) * 0.5f;
        tv = (b + 1.0f) * 0.5f;
      } else {
        float2 d = concentricDisk(rng.rnd2());
        p = lt.p + d.x * lt.u + d.y * lt.v;
        tu = (atan2f(d.y, d.x) + SD_PI) / (2.0f * SD_PI);
        tv = sqrtf(d.x * d.x + d.y * d.y);
      }
      float3 to = p - x;
      float d2 = length2(to);
      if (d2 < 1e-10f) return s;
      float dist = sqrtf(d2);
      float3 wi = to / dist;
      float cosL = -dot(wi, lt.n);          // light faces the shading point?
      if (!lt.twoSided && cosL <= 0.0f) return s;
      cosL = fabsf(cosL);
      if (cosL < 1e-6f) return s;
      s.wi = wi;
      s.dist = dist;
      s.Li = lt.L;
      if (lt.texId >= 0 && textures) {
        float4 c = evalTexture(textures[lt.texId], tu, tv);
        s.Li = lt.L * f3(c.x, c.y, c.z);
      }
      s.pdf = d2 / (cosL * lt.area);        // area pdf -> solid angle
      s.valid = true;
      return s;
    }

    case LT_SPHERE: {
      float3 to = lt.p - x;
      float d2 = length2(to);
      float r2 = lt.radius * lt.radius;
      if (d2 <= r2 * 1.0001f) return s;     // inside the emitter
      float dist = sqrtf(d2);
      float3 wc = to / dist;
      float sin2Max = r2 / d2;
      float cosMax = sqrtf(fmaxf(0.0f, 1.0f - sin2Max));
      float2 u = rng.rnd2();
      float cosT = 1.0f - u.x * (1.0f - cosMax);
      float sinT = sqrtf(fmaxf(0.0f, 1.0f - cosT * cosT));
      float phi = 2.0f * SD_PI * u.y;
      Onb onb(wc);
      s.wi = onb.toWorld(f3(sinT * cosf(phi), sinT * sinf(phi), cosT));
      // distance to the sphere surface along wi (cone sample always hits)
      float b = dot(s.wi, to);
      float det = fmaxf(0.0f, b * b - d2 + r2);
      s.dist = b - sqrtf(det);
      s.Li = lt.L;
      s.pdf = 1.0f / (2.0f * SD_PI * (1.0f - cosMax));
      s.valid = true;
      return s;
    }

    case LT_POINT: {
      s.isDelta = true;
      if (parity) {
        // cxxrt PointLight::illuminate, formula for formula.
        float3 u3 = f3(rng.rnd(), rng.rnd(), rng.rnd());
        float3 to = lt.p + lt.radius * uniformInBall(u3) - x;
        float d2 = length2(to);
        if (d2 < 1e-10f) return s;
        float dist = sqrtf(d2);
        s.wi = to / dist;
        s.dist = fmaxf(dist - lt.radius, 1e-4f);
        s.Li = lt.L * (SD_INV_PI * 0.25f / d2);
        s.pdf = 1.0f;
        s.valid = true;
        return s;
      }
      // Physical soft point light: sample a disk of radius r facing x.
      float3 to = lt.p - x;
      float d2c = length2(to);
      if (d2c < 1e-10f) return s;
      float3 wc = to / sqrtf(d2c);
      float2 d = concentricDisk(rng.rnd2()) * lt.radius;
      Onb onb(wc);
      float3 p = lt.p + d.x * onb.t + d.y * onb.b;
      float3 top = p - x;
      float d2 = length2(top);
      float dist = sqrtf(d2);
      s.wi = top / dist;
      s.dist = fmaxf(dist - 1e-4f, 1e-4f);
      s.Li = lt.L / d2;                     // L holds radiant intensity
      s.pdf = 1.0f;
      s.valid = true;
      return s;
    }

    case LT_DISTANT: {
      s.isDelta = true;
      s.wi = -lt.dir;
      s.dist = 1e16f;
      s.Li = lt.L;
      s.pdf = 1.0f;
      s.valid = true;
      return s;
    }
  }
  return s;
}

// Solid-angle pdf of NEE producing direction (hitP - from) for area light lt.
// Used for MIS weighting when a BSDF sample hits an emitter. Delta lights
// return 0 (BSDF rays cannot hit them).
SD_HD float lightPdfSolidAngle(const LightDesc& lt, float3 from, float3 hitP) {
  switch (lt.kind) {
    case LT_RECT:
    case LT_DISK: {
      float3 to = hitP - from;
      float d2 = length2(to);
      if (d2 < 1e-10f) return 0.0f;
      float dist = sqrtf(d2);
      float cosL = fabsf(dot(to / dist, lt.n));
      if (cosL < 1e-6f) return 0.0f;
      return d2 / (cosL * lt.area);
    }
    case LT_SPHERE: {
      float3 to = lt.p - from;
      float d2 = length2(to);
      float r2 = lt.radius * lt.radius;
      if (d2 <= r2 * 1.0001f) return 0.0f;
      float cosMax = sqrtf(fmaxf(0.0f, 1.0f - r2 / d2));
      return 1.0f / (2.0f * SD_PI * (1.0f - cosMax));
    }
    default:
      return 0.0f;
  }
}

}  // namespace sd

#endif

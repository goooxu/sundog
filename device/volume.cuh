// sundog: emissive participating media — procedural flames (device).
//
// Model: emission + absorption only (no scattering). Along a ray segment the
// medium adds  ∫ T(t)·ε(p(t)) dt  and attenuates everything behind it by the
// transmittance  T = exp(-∫ σ(p(t)) dt).  Both fields are pure functions of
// position (value-noise fbm shaping a teardrop profile), so results are
// deterministic; the march start is jittered from the per-sample PCG stream.
//
// Flames never enter the OptiX scene graph: each is bounded by an upright
// cylinder that raygen intersects analytically before marching.
#ifndef SUNDOG_VOLUME_CUH
#define SUNDOG_VOLUME_CUH

#include "math.cuh"
#include "noise.cuh"
#include "params.h"
#include "rng.cuh"

namespace sd {

// ---- flame field ------------------------------------------------------------

// σ (absorption density) and ε (emitted radiance per unit length) at p.
SD_HD void flameField(const FlameDesc& fl, float3 p, float& sigma, float3& emission) {
  sigma = 0.0f;
  emission = f3(0.0f);
  float h = (p.y - fl.base.y) / fl.height;
  if (h <= 0.0f || h >= 1.0f) return;
  float rx = p.x - fl.base.x, rz = p.z - fl.base.z;
  float d = sqrtf(rx * rx + rz * rz) / fl.radius;

  // Teardrop profile: widest near h = 1/(1+2·1.55) ≈ 0.24, tapering to the
  // tip; 3.2 normalizes the peak to ≈1.
  float prof = 3.2f * sqrtf(h) * powf(1.0f - h, 1.55f);

  // fbm-warped boundary; noise sampled in flame-local coords compressed in y
  // so features stretch into upward licks.
  float3 q = f3(rx / fl.radius, (p.y - fl.base.y) / fl.radius * 0.32f, rz / fl.radius);
  float n = fbm3(q * fl.noiseScale, fl.seed);
  float dd = d / fmaxf(prof, 1e-3f) + (n - 0.45f) * 1.0f;

  float mask = smoothstepf(1.0f, 0.62f, dd) * smoothstepf(0.0f, 0.12f, h);
  if (mask <= 0.0f) return;

  sigma = fl.sigma * mask;
  // Heat: hottest in the core near the base, cooling outward and upward.
  float heat = clampf((0.88f - dd) * (1.25f - h), 0.0f, 1.0f);
  // Fire ramp in linear space: red leads, green follows (yellow), blue last (white core).
  float3 col = f3(fminf(heat * 2.6f, 1.0f),
                  fminf(heat * heat * 2.1f, 1.0f),
                  fminf(heat * heat * heat * 1.7f, 1.0f));
  emission = col * (fl.intensity * mask);
}

// Ray vs the flame's upright bounding cylinder; clips [t0,t1] in place.
SD_HD bool clipFlameBounds(const FlameDesc& fl, float3 o, float3 d, float& t0, float& t1) {
  // y slab
  if (fabsf(d.y) < 1e-8f) {
    if (o.y < fl.base.y || o.y > fl.base.y + fl.height) return false;
  } else {
    float ta = (fl.base.y - o.y) / d.y;
    float tb = (fl.base.y + fl.height - o.y) / d.y;
    if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
    t0 = fmaxf(t0, ta);
    t1 = fminf(t1, tb);
  }
  // infinite cylinder |xz| = radius
  float ox = o.x - fl.base.x, oz = o.z - fl.base.z;
  float a = d.x * d.x + d.z * d.z;
  if (a < 1e-12f) {
    if (ox * ox + oz * oz > fl.radius * fl.radius) return false;
  } else {
    float b = ox * d.x + oz * d.z;
    float c = ox * ox + oz * oz - fl.radius * fl.radius;
    float disc = b * b - a * c;
    if (disc <= 0.0f) return false;
    float s = sqrtf(disc);
    t0 = fmaxf(t0, (-b - s) / a);
    t1 = fminf(t1, (-b + s) / a);
  }
  return t0 < t1;
}

// Marches all flames along [tmin, tEnd]. Adds emission (premultiplied by the
// evolving local transmittance) to the return value and folds the segment's
// total transmittance into `trans`. Flames are assumed not to overlap.
SD_HD float3 marchFlames(const FlameDesc* flames, int numFlames, float3 o, float3 d,
                         float tEnd, Pcg32& rng, float3& trans) {
  const int STEPS = 32;
  float3 Lv = f3(0.0f);
  for (int i = 0; i < numFlames; i++) {
    const FlameDesc& fl = flames[i];
    float t0 = 1e-4f, t1 = tEnd;
    if (!clipFlameBounds(fl, o, d, t0, t1)) continue;
    float dt = (t1 - t0) / STEPS;
    float jitter = rng.rnd();
    float tLocal = 1.0f;  // scalar transmittance through this flame
    float3 acc = f3(0.0f);
    for (int k = 0; k < STEPS; k++) {
      float3 p = o + (t0 + (k + jitter) * dt) * d;
      float sigma;
      float3 eps;
      flameField(fl, p, sigma, eps);
      acc += (tLocal * dt) * eps;
      tLocal *= expf(-sigma * dt);
      if (tLocal < 1e-4f) break;
    }
    Lv += trans * acc;   // attenuated by media already traversed
    trans *= tLocal;
  }
  return Lv;
}

}  // namespace sd

#endif

// sundog: procedural noise stack (device) — integer hash -> trilinear value
// noise -> fBm — plus the consumers' small helpers. Shared by the volumetric
// flames (device/volume.cuh) and the water-surface wave normals.
#ifndef SUNDOG_NOISE_CUH
#define SUNDOG_NOISE_CUH

#include "math.cuh"

namespace sd {

SD_HD float fractf(float x) { return x - floorf(x); }

// Handles inverted edges (e0 > e1) as a descending ramp.
SD_HD float smoothstepf(float e0, float e1, float x) {
  float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

SD_HD unsigned hashU(unsigned x) {
  // PCG output permutation on a Weyl-advanced state (cf. rng.cuh).
  x = x * 747796405u + 2891336453u;
  x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
  return (x >> 22u) ^ x;
}

SD_HD float hashLattice(int x, int y, int z, unsigned seed) {
  unsigned h = hashU((unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^
                     (unsigned)z * 83492791u ^ seed);
  return (float)h * (1.0f / 4294967296.0f);
}

// Trilinear value noise in [0,1).
SD_HD float vnoise(float3 p, unsigned seed) {
  float fx = floorf(p.x), fy = floorf(p.y), fz = floorf(p.z);
  int ix = (int)fx, iy = (int)fy, iz = (int)fz;
  float tx = p.x - fx, ty = p.y - fy, tz = p.z - fz;
  tx = tx * tx * (3.0f - 2.0f * tx);
  ty = ty * ty * (3.0f - 2.0f * ty);
  tz = tz * tz * (3.0f - 2.0f * tz);
  float c000 = hashLattice(ix, iy, iz, seed),     c100 = hashLattice(ix + 1, iy, iz, seed);
  float c010 = hashLattice(ix, iy + 1, iz, seed), c110 = hashLattice(ix + 1, iy + 1, iz, seed);
  float c001 = hashLattice(ix, iy, iz + 1, seed), c101 = hashLattice(ix + 1, iy, iz + 1, seed);
  float c011 = hashLattice(ix, iy + 1, iz + 1, seed), c111 = hashLattice(ix + 1, iy + 1, iz + 1, seed);
  float x00 = c000 + (c100 - c000) * tx;
  float x10 = c010 + (c110 - c010) * tx;
  float x01 = c001 + (c101 - c001) * tx;
  float x11 = c011 + (c111 - c011) * tx;
  float y0 = x00 + (x10 - x00) * ty;
  float y1 = x01 + (x11 - x01) * ty;
  return y0 + (y1 - y0) * tz;
}

// Three octaves; range ~[0, 0.875].
SD_HD float fbm3(float3 p, unsigned seed) {
  float amp = 0.5f, sum = 0.0f;
  for (int i = 0; i < 3; i++) {
    sum += amp * vnoise(p, seed + (unsigned)i * 101u);
    p = 2.03f * p + f3(11.7f, 5.3f, 7.9f);
    amp *= 0.5f;
  }
  return sum;
}

// Water-surface wave normal: an fbm height field over the world xz plane
// (water is horizontal by convention), central differences for the slope.
// Shading-normal perturbation only — no displacement; falls back to the flat
// normal when the perturbed one would back-face the viewer (grazing angles).
SD_HD float3 waterNormal(float3 x, float3 ns, float3 wo, float amp, float freq) {
  const unsigned SEED = 1337u;
  const float e = 0.05f;
  float3 p = f3(x.x * freq, 0.0f, x.z * freq);
  float hx1 = fbm3(p + f3(e, 0.0f, 0.0f), SEED), hx0 = fbm3(p - f3(e, 0.0f, 0.0f), SEED);
  float hz1 = fbm3(p + f3(0.0f, 0.0f, e), SEED), hz0 = fbm3(p - f3(0.0f, 0.0f, e), SEED);
  float dhdx = (hx1 - hx0) / (2.0f * e);
  float dhdz = (hz1 - hz0) / (2.0f * e);
  float side = ns.y >= 0.0f ? 1.0f : -1.0f;  // keep the perturbed normal on ns's side
  float3 n = normalize(f3(-amp * dhdx, 1.0f, -amp * dhdz)) * side;
  if (dot(n, wo) <= 1e-4f) return ns;
  return n;
}

}  // namespace sd

#endif

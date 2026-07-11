// sundog: PCG32 RNG (O'Neill, pcg-random.org). Identical on host and device
// so unit tests and golden determinism share one implementation.
#ifndef SUNDOG_RNG_CUH
#define SUNDOG_RNG_CUH

#include "math.cuh"
#include <stdint.h>

namespace sd {

struct Pcg32 {
  uint64_t state;
  uint64_t inc;

  SD_HD uint32_t next() {
    uint64_t old = state;
    state = old * 6364136223846793005ULL + inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
  }

  SD_HD static Pcg32 init(uint64_t seed, uint64_t seq) {
    Pcg32 r;
    r.state = 0u;
    r.inc = (seq << 1u) | 1u;
    r.next();
    r.state += seed;
    r.next();
    return r;
  }

  // float in [0, 1)
  SD_HD float rnd() { return (float)(next() >> 8) * 0x1p-24f; }
  SD_HD float2 rnd2() { float a = rnd(); float b = rnd(); return make_float2(a, b); }
};

// Uniform direction on the unit sphere.
SD_HD float3 uniformOnSphere(float2 u) {
  float z = 1.0f - 2.0f * u.x;
  float r = sqrtf(fmaxf(0.0f, 1.0f - z * z));
  float phi = 2.0f * SD_PI * u.y;
  return f3(r * cosf(phi), r * sinf(phi), z);
}

// Concentric disk sample in [-1,1]^2 -> unit disk.
SD_HD float2 concentricDisk(float2 u) {
  float ox = 2.0f * u.x - 1.0f, oy = 2.0f * u.y - 1.0f;
  if (ox == 0.0f && oy == 0.0f) return make_float2(0.0f, 0.0f);
  float r, theta;
  if (fabsf(ox) > fabsf(oy)) { r = ox; theta = (SD_PI / 4.0f) * (oy / ox); }
  else { r = oy; theta = (SD_PI / 2.0f) - (SD_PI / 4.0f) * (ox / oy); }
  return make_float2(r * cosf(theta), r * sinf(theta));
}

// Cosine-weighted hemisphere direction around +Z (local frame).
SD_HD float3 cosineHemisphere(float2 u) {
  float2 d = concentricDisk(u);
  float z = sqrtf(fmaxf(0.0f, 1.0f - d.x * d.x - d.y * d.y));
  return f3(d.x, d.y, z);
}

}  // namespace sd

#endif

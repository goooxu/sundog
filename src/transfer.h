// sundog: HDR output transfer — SMPTE ST 2084 (PQ) and BT.709 -> BT.2020.
//
// The renderer accumulates linear radiance in Rec.709/sRGB primaries. For
// HDR AVIF output the pipeline is: exposure -> 709->2020 gamut matrix ->
// anchor linear 1.0 at 203 cd/m^2 (BT.2408 reference white) -> PQ OETF ->
// 12-bit quantize. There is no SDR/tonemap path (v0.18).
#ifndef SUNDOG_TRANSFER_H
#define SUNDOG_TRANSFER_H

#include "math.cuh"

#include <cmath>

namespace sd {

// Linear 1.0 in the framebuffer maps to this display luminance (cd/m^2).
constexpr float kPqWhiteNits = 203.0f;
constexpr float kPqPeakNits = 10000.0f;

// SMPTE ST 2084 perceptual quantizer. `lin` is display luminance divided
// by 10000 cd/m^2, clamped to [0,1]; returns the PQ-encoded signal [0,1].
inline float pqOetf(float lin) {
  const float m1 = 2610.0f / 16384.0f;
  const float m2 = 2523.0f / 4096.0f * 128.0f;
  const float c1 = 3424.0f / 4096.0f;
  const float c2 = 2413.0f / 4096.0f * 32.0f;
  const float c3 = 2392.0f / 4096.0f * 32.0f;
  float y = powf(fminf(fmaxf(lin, 0.0f), 1.0f), m1);
  return powf((c1 + c2 * y) / (1.0f + c3 * y), m2);
}

// Inverse (EOTF direction): PQ signal [0,1] -> display luminance / 10000.
inline float pqEotf(float e) {
  const float m1 = 2610.0f / 16384.0f;
  const float m2 = 2523.0f / 4096.0f * 128.0f;
  const float c1 = 3424.0f / 4096.0f;
  const float c2 = 2413.0f / 4096.0f * 32.0f;
  const float c3 = 2392.0f / 4096.0f * 32.0f;
  float p = powf(fminf(fmaxf(e, 0.0f), 1.0f), 1.0f / m2);
  float num = fmaxf(p - c1, 0.0f);
  return powf(num / (c2 - c3 * p), 1.0f / m1);
}

// Linear RGB, BT.709/sRGB primaries -> BT.2020 primaries (both D65).
inline float3 bt709To2020(float3 c) {
  return f3(0.627404f * c.x + 0.329283f * c.y + 0.043313f * c.z,
            0.069097f * c.x + 0.919540f * c.y + 0.011362f * c.z,
            0.016391f * c.x + 0.088013f * c.y + 0.895595f * c.z);
}

}  // namespace sd

#endif

// sundog: host-side output tone mapping (Film::writePng only; the device
// accumulation buffer stays linear HDR throughout).
//
// ACES filmic via Stephen Hill's fit ("ACESFitted", BakingLab, MIT — see
// THIRD_PARTY.md): linear sRGB -> [input 3x3: sRGB->AP1 with RRT_SAT folded
// in] -> per-channel RRT+ODT rational fit -> [output 3x3: ODT_SAT->sRGB] ->
// saturate. The result is still linear sRGB; gamma encoding comes after.
#ifndef SUNDOG_TONEMAP_H
#define SUNDOG_TONEMAP_H

#include "math.cuh"
#include <string>

namespace sd {

enum TonemapMode : int { TM_ACES = 0, TM_CLAMP };

inline bool tonemapFromString(const std::string& s, TonemapMode& out) {
  if (s == "aces") { out = TM_ACES; return true; }
  if (s == "clamp") { out = TM_CLAMP; return true; }
  return false;
}

// sRGB => XYZ => D65->D60 => AP1, with the RRT saturation matrix folded in.
// Row-major; rows sum to ~1 so the gray axis stays neutral.
inline float3 acesInputMat(float3 v) {
  return f3(0.59719f * v.x + 0.35458f * v.y + 0.04823f * v.z,
            0.07600f * v.x + 0.90834f * v.y + 0.01566f * v.z,
            0.02840f * v.x + 0.13383f * v.y + 0.83777f * v.z);
}

// ODT_SAT => XYZ => D60->D65 => sRGB.
inline float3 acesOutputMat(float3 v) {
  return f3(1.60475f * v.x - 0.53108f * v.y - 0.07367f * v.z,
            -0.10208f * v.x + 1.10813f * v.y - 0.00605f * v.z,
            -0.00327f * v.x - 0.07276f * v.y + 1.07602f * v.z);
}

// Per-channel RRT+ODT rational fit — the S-curve itself. The denominator's
// discriminant is negative, so there is no pole on the real axis.
inline float3 acesRrtOdtFit(float3 v) {
  float3 a = v * (v + f3(0.0245786f)) - f3(0.000090537f);
  float3 b = v * (0.983729f * v + f3(0.4329510f)) + f3(0.238081f);
  return a / b;
}

// Unbounded linear sRGB -> [0,1] linear sRGB. max(0) at the entry guards
// against occasional negative denoiser output; the final saturate absorbs
// the fit's slight negative lobe at 0 and its >1 asymptote (1/0.983729).
inline float3 acesFitted(float3 c) {
  c = f3(fmaxf(c.x, 0.0f), fmaxf(c.y, 0.0f), fmaxf(c.z, 0.0f));
  return clamp3(acesOutputMat(acesRrtOdtFit(acesInputMat(c))), 0.0f, 1.0f);
}

}  // namespace sd

#endif

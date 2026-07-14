// sundog: HDR environment light — equirect evaluation, CDF importance
// sampling, and the solid-angle pdf used by MIS. The CDFs are built
// host-side (src/env_light.cpp) over luminance * sin(theta).
#ifndef SUNDOG_ENV_LIGHT_CUH
#define SUNDOG_ENV_LIGHT_CUH

#include "light_sample.cuh"
#include "math.cuh"
#include "params.h"
#include "rng.cuh"

namespace sd {

// Unit world direction -> equirect uv, applying the inverse Y-rotation.
// v = 0 is the zenith (+Y) = top row of the HDR; no v flip (unlike surface
// textures, stbi row order and the lat-long convention already agree).
SD_HD float2 envDirToUv(const EnvDesc& env, float3 dir) {
  float u = (atan2f(dir.z, dir.x) - env.rotate + SD_PI) * (0.5f * SD_INV_PI);
  u -= floorf(u);  // wrap to [0,1): the pdf query below indexes with this u
  float v = acosf(clampf(dir.y, -1.0f, 1.0f)) * SD_INV_PI;
  return make_float2(u, v);
}

SD_HD float3 envEval(const EnvDesc& env, float3 dir) {
#if defined(__CUDA_ARCH__)
  float2 uv = envDirToUv(env, dir);
  float4 c = tex2D<float4>(env.tex, uv.x, uv.y);
  return f3(c.x, c.y, c.z) * env.intensity;
#else
  (void)env; (void)dir;
  return f3(1.0f, 0.0f, 1.0f);  // host compilation stub (tests never render)
#endif
}

// Largest i in [0, n-2] with cdf[i] <= u (cdf has n entries, n-1 intervals).
SD_HD int findInterval(const float* cdf, int n, float u) {
  int lo = 0, hi = n - 1;
  while (lo + 1 < hi) {
    int mid = (lo + hi) >> 1;
    if (cdf[mid] <= u) lo = mid; else hi = mid;
  }
  return lo;
}

// NEE sample of the environment light. Solid-angle pdf; dist = 1e16f like
// LT_DISTANT (an escaped shadow ray means the sample is visible).
//
// pdf derivation: picking texel (row,col) has probability dm*dc over an image
// area of 1/(W*H), so p_img(u,v) = dm*dc*W*H. With u,v spanning phi in 2*pi
// and theta in pi, the solid-angle element is d_omega = 2*pi^2*sin(theta)
// * du*dv, giving pdf_omega = dm*dc*W*H / (2*pi^2*sin(theta)). Sanity check:
// a uniform white image has dm ~ sin(theta_row)/sum, dc = 1/W, which reduces
// to pdf_omega = 1/(4*pi).
SD_HD LightSample sampleEnv(const EnvDesc& env, Pcg32& rng) {
  LightSample s;
  s.valid = false;
  s.isDelta = false;
  s.pdf = 0.0f;
  s.dist = 1e16f;
  float2 rn = rng.rnd2();  // same RNG consumption in both modes

  if (!env.importance) {  // uniform-sphere comparison mode (importance:false)
    s.wi = uniformOnSphere(rn);
    s.pdf = 0.25f * SD_INV_PI;
    s.Li = envEval(env, s.wi);
    s.valid = true;
    return s;
  }

  // marginal: rn.x -> row, then conditional: rn.y -> column
  int row = findInterval(env.margCdf, env.height + 1, rn.x);
  float dm = env.margCdf[row + 1] - env.margCdf[row];
  float dv = dm > 0.0f ? (rn.x - env.margCdf[row]) / dm : 0.5f;
  float v = (row + dv) / env.height;

  const float* cdf = env.condCdf + (size_t)row * (env.width + 1);
  int col = findInterval(cdf, env.width + 1, rn.y);
  float dc = cdf[col + 1] - cdf[col];
  float du = dc > 0.0f ? (rn.y - cdf[col]) / dc : 0.5f;
  float u = (col + du) / env.width;

  float theta = v * SD_PI;
  float sinT = sinf(theta);
  if (sinT < 1e-6f || dm <= 0.0f || dc <= 0.0f) return s;  // pole / flat spot

  float phi = (2.0f * SD_PI * u - SD_PI) + env.rotate;  // texture -> world
  s.wi = f3(sinT * cosf(phi), cosf(theta), sinT * sinf(phi));
  s.pdf = dm * dc * env.width * env.height /
          (2.0f * SD_PI * SD_PI * sinT);
  // Li via envEval(wi): the miss branch integrates the same Li(omega), so
  // MIS sees one radiance definition (uv round-trip error is ULP-level).
  s.Li = envEval(env, s.wi);
  s.valid = s.pdf > 0.0f;
  return s;
}

// Solid-angle pdf of sampleEnv producing `dir` — the reverse-MIS query for a
// BSDF ray that escapes to the environment. Mirrors sampleEnv exactly.
//
// Note the piecewise-constant pdf vs bilinear Li mismatch at bright-texel
// edges (filtering bleeds radiance into texels whose pdf is 0): NEE cannot
// sample those directions, but the BSDF path covers them with MIS weight
// prevPdf/(prevPdf+0) = 1, so this is variance, not bias. Do not "fix" it.
SD_HD float envPdfSolidAngle(const EnvDesc& env, float3 dir) {
  if (!env.importance) return 0.25f * SD_INV_PI;
  float sinT = sqrtf(fmaxf(0.0f, 1.0f - dir.y * dir.y));
  if (sinT < 1e-6f) return 0.0f;
  float2 uv = envDirToUv(env, dir);
  int col = min((int)(uv.x * env.width), env.width - 1);
  int row = min((int)(uv.y * env.height), env.height - 1);
  float dm = env.margCdf[row + 1] - env.margCdf[row];
  const float* cdf = env.condCdf + (size_t)row * (env.width + 1);
  float dc = cdf[col + 1] - cdf[col];
  return dm * dc * env.width * env.height /
         (2.0f * SD_PI * SD_PI * sinT);
}

}  // namespace sd

#endif

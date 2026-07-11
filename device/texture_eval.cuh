// sundog: texture evaluation (device; image textures via tex2D).
#ifndef SUNDOG_TEXTURE_EVAL_CUH
#define SUNDOG_TEXTURE_EVAL_CUH

#include "math.cuh"
#include "params.h"

namespace sd {

// Returns rgb + alpha (alpha meaningful for TX_IMAGE cutouts; 1 otherwise).
SD_HD float4 evalTexture(const TextureDesc& tx, float u, float v) {
  switch (tx.kind) {
    case TX_SOLID:
      return make_float4(tx.a.x, tx.a.y, tx.a.z, 1.0f);
    case TX_CHECKER: {
      int iu = (int)floorf(u * tx.scale.x);
      int iv = (int)floorf(v * tx.scale.y);
      float3 c = (((iu + iv) & 1) == 0) ? tx.a : tx.b;
      return make_float4(c.x, c.y, c.z, 1.0f);
    }
    case TX_GRID: {
      float fu = u * tx.scale.x - floorf(u * tx.scale.x);
      float fv = v * tx.scale.y - floorf(v * tx.scale.y);
      float w = tx.width;
      bool line = fu < w || fv < w || fu > 1.0f - w || fv > 1.0f - w;
      float3 c = line ? tx.b : tx.a;
      return make_float4(c.x, c.y, c.z, 1.0f);
    }
    case TX_IMAGE: {
#if defined(__CUDA_ARCH__)
      return tex2D<float4>(tx.tex, u, 1.0f - v);
#else
      return make_float4(1.0f, 0.0f, 1.0f, 1.0f);  // unreachable in host compilation
#endif
    }
    default:
      return make_float4(1.0f, 0.0f, 1.0f, 1.0f);
  }
}

SD_HD float3 materialAlbedo(const MaterialDesc& m, const TextureDesc* textures,
                            float u, float v) {
  if (m.texId < 0) return m.color;
  float4 c = evalTexture(textures[m.texId], u, v);
  return f3(c.x, c.y, c.z);
}

}  // namespace sd

#endif

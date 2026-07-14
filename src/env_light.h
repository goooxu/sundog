// sundog: HDR environment map upload (stbi_loadf -> float4 texture object)
// plus the sin(theta)-weighted luminance CDFs for importance sampling.
#ifndef SUNDOG_ENV_LIGHT_H
#define SUNDOG_ENV_LIGHT_H

#include "cuda_check.h"
#include "scene.h"
#include <cuda_runtime.h>

namespace sd {

class EnvMap {
 public:
  // Loads scene.env.file (.hdr), builds the 2D piecewise-constant CDFs on
  // the host, uploads texture + CDFs, returns a fully-populated EnvDesc.
  EnvDesc upload(const Scene& scene);
  ~EnvMap();

 private:
  cudaArray_t array_ = nullptr;
  cudaTextureObject_t tex_ = 0;
  CudaBuffer margCdf_, condCdf_;
};

}  // namespace sd

#endif

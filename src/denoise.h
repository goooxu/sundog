// sundog: OptiX HDR AI denoiser with albedo+normal guide layers.
#ifndef SUNDOG_DENOISE_H
#define SUNDOG_DENOISE_H

#include "cuda_check.h"
#include <optix.h>

namespace sd {

class Denoiser {
 public:
  Denoiser(OptixDeviceContext ctx, int width, int height);
  ~Denoiser();

  // All buffers are float4[w*h], device pointers, linear HDR.
  void run(CUdeviceptr beauty, CUdeviceptr albedo, CUdeviceptr normal,
           CUdeviceptr output);

 private:
  OptixDenoiser denoiser_ = nullptr;
  int width_, height_;
  CudaBuffer state_, scratch_, intensity_;
  OptixDenoiserSizes sizes_{};
};

}  // namespace sd

#endif

#include "denoise.h"

#include <optix_stubs.h>

namespace sd {

static OptixImage2D image2d(CUdeviceptr data, int w, int h) {
  OptixImage2D img{};
  img.data = data;
  img.width = (unsigned)w;
  img.height = (unsigned)h;
  img.rowStrideInBytes = (unsigned)(w * sizeof(float4));
  img.pixelStrideInBytes = sizeof(float4);
  img.format = OPTIX_PIXEL_FORMAT_FLOAT4;
  return img;
}

Denoiser::Denoiser(OptixDeviceContext ctx, int width, int height)
    : width_(width), height_(height) {
  OptixDenoiserOptions opts{};
  opts.guideAlbedo = 1;
  opts.guideNormal = 1;
  OPTIX_CHECK(optixDenoiserCreate(ctx, OPTIX_DENOISER_MODEL_KIND_HDR, &opts,
                                  &denoiser_));
  OPTIX_CHECK(optixDenoiserComputeMemoryResources(denoiser_, width, height,
                                                  &sizes_));
  state_.alloc(sizes_.stateSizeInBytes);
  scratch_.alloc(sizes_.withoutOverlapScratchSizeInBytes);
  intensity_.alloc(sizeof(float));
  OPTIX_CHECK(optixDenoiserSetup(denoiser_, 0, width, height, state_.ptr,
                                 state_.size, scratch_.ptr, scratch_.size));
}

void Denoiser::run(CUdeviceptr beauty, CUdeviceptr albedo, CUdeviceptr normal,
                   CUdeviceptr output) {
  OptixDenoiserLayer layer{};
  layer.input = image2d(beauty, width_, height_);
  layer.output = image2d(output, width_, height_);

  OptixDenoiserGuideLayer guide{};
  guide.albedo = image2d(albedo, width_, height_);
  guide.normal = image2d(normal, width_, height_);

  OptixDenoiserParams params{};
  params.hdrIntensity = intensity_.ptr;
  OPTIX_CHECK(optixDenoiserComputeIntensity(denoiser_, 0, &layer.input,
                                            intensity_.ptr, scratch_.ptr,
                                            scratch_.size));
  OPTIX_CHECK(optixDenoiserInvoke(denoiser_, 0, &params, state_.ptr, state_.size,
                                  &guide, &layer, 1, 0, 0, scratch_.ptr,
                                  scratch_.size));
  CUDA_CHECK(cudaDeviceSynchronize());
}

Denoiser::~Denoiser() {
  if (denoiser_) optixDenoiserDestroy(denoiser_);
}

}  // namespace sd

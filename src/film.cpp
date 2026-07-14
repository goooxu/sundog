#include "film.h"

#include "math.cuh"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace sd {

Film::Film(int width, int height) : width_(width), height_(height) {
  size_t n = (size_t)width * height * sizeof(float4);
  accum_.allocZero(n);
  albedo_.allocZero(n);
  normal_.allocZero(n);
  denoised_.allocZero(n);
}

static std::vector<float4> downloadBuf(CUdeviceptr src, int w, int h) {
  std::vector<float4> host((size_t)w * h);
  CUDA_CHECK(cudaMemcpy(host.data(), (void*)src, host.size() * sizeof(float4),
                        cudaMemcpyDeviceToHost));
  return host;
}

void Film::writePng(CUdeviceptr src, const std::string& path, float exposure,
                    float gamma, TonemapMode tonemap) const {
  std::vector<float4> hdr = downloadBuf(src, width_, height_);
  std::vector<unsigned char> rgba((size_t)width_ * height_ * 4);
  float scale = exp2f(exposure);
  float invGamma = 1.0f / gamma;
  for (size_t i = 0; i < hdr.size(); i++) {
    // exposure -> tone map -> gamma -> quantize (report ch01 walks the order)
    float3 c = f3(hdr[i].x, hdr[i].y, hdr[i].z) * scale;
    c = tonemap == TM_ACES ? acesFitted(c) : clamp3(c, 0.0f, 1.0f);
    rgba[4 * i + 0] = (unsigned char)lroundf(powf(c.x, invGamma) * 255.0f);
    rgba[4 * i + 1] = (unsigned char)lroundf(powf(c.y, invGamma) * 255.0f);
    rgba[4 * i + 2] = (unsigned char)lroundf(powf(c.z, invGamma) * 255.0f);
    rgba[4 * i + 3] = 255;
  }
  if (!stbi_write_png(path.c_str(), width_, height_, 4, rgba.data(), width_ * 4)) {
    throw std::runtime_error("failed to write " + path);
  }
}

void Film::writeAovPng(CUdeviceptr src, const std::string& path, bool remap) const {
  std::vector<float4> buf = downloadBuf(src, width_, height_);
  std::vector<unsigned char> rgba((size_t)width_ * height_ * 4);
  for (size_t i = 0; i < buf.size(); i++) {
    float c[3] = {buf[i].x, buf[i].y, buf[i].z};
    for (int k = 0; k < 3; k++) {
      float v = remap ? 0.5f * (c[k] + 1.0f) : c[k];
      rgba[4 * i + k] = (unsigned char)lroundf(clampf(v, 0.0f, 1.0f) * 255.0f);
    }
    rgba[4 * i + 3] = 255;
  }
  if (!stbi_write_png(path.c_str(), width_, height_, 4, rgba.data(), width_ * 4)) {
    throw std::runtime_error("failed to write " + path);
  }
}

}  // namespace sd

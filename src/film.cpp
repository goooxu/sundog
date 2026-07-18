#include "film.h"

#include "math.cuh"
#include "transfer.h"

#include <avif/avif.h>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace sd {

// Fixed encoder thread count: part of the determinism contract (thread
// count must not depend on the host's core count), see report ch11.
static constexpr int kAvifThreads = 4;

Film::Film(int width, int height, bool withDenoised)
    : width_(width), height_(height) {
  size_t n = (size_t)width * height * sizeof(float4);
  accum_.allocZero(n);
  albedo_.allocZero(n);
  normal_.allocZero(n);
  if (withDenoised) denoised_.allocZero(n);
}

static std::vector<float4> downloadBuf(CUdeviceptr src, int w, int h) {
  std::vector<float4> host((size_t)w * h);
  CUDA_CHECK(cudaMemcpy(host.data(), (void*)src, host.size() * sizeof(float4),
                        cudaMemcpyDeviceToHost));
  return host;
}

// Encode a prepared RGB buffer (values already in [0, 2^depth-1]) as
// lossless AVIF: YUV444 + identity matrix + full range keeps the RGB
// samples mathematically intact; quality LOSSLESS pins the AV1 coder.
// libavif reads avifRGBImage at 1 byte/channel for depth <= 8 and
// 2 bytes/channel above, so the buffer is repacked tight for 8-bit.
static void encodeAvif(const std::string& path, int w, int h, int depth,
                       const std::vector<uint16_t>& rgb,
                       avifColorPrimaries primaries,
                       avifTransferCharacteristics transfer) {
  avifImage* image = avifImageCreate(w, h, depth, AVIF_PIXEL_FORMAT_YUV444);
  if (!image) throw std::runtime_error("avifImageCreate failed");
  image->colorPrimaries = primaries;
  image->transferCharacteristics = transfer;
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
  image->yuvRange = AVIF_RANGE_FULL;

  std::vector<uint8_t> narrow;
  if (depth <= 8) {
    narrow.resize(rgb.size());
    for (size_t i = 0; i < rgb.size(); i++) narrow[i] = (uint8_t)rgb[i];
  }
  avifRGBImage rgbView;
  avifRGBImageSetDefaults(&rgbView, image);
  rgbView.format = AVIF_RGB_FORMAT_RGB;
  rgbView.depth = (uint32_t)depth;
  rgbView.pixels = depth <= 8 ? narrow.data() : (uint8_t*)rgb.data();
  rgbView.rowBytes =
      (uint32_t)(w * 3 * (depth <= 8 ? 1 : sizeof(uint16_t)));
  avifResult r = avifImageRGBToYUV(image, &rgbView);
  if (r != AVIF_RESULT_OK) {
    avifImageDestroy(image);
    throw std::runtime_error(std::string("avifImageRGBToYUV: ") +
                             avifResultToString(r));
  }

  avifEncoder* enc = avifEncoderCreate();
  if (!enc) {
    avifImageDestroy(image);
    throw std::runtime_error("avifEncoderCreate failed");
  }
  enc->quality = AVIF_QUALITY_LOSSLESS;
  enc->speed = 6;
  enc->maxThreads = kAvifThreads;
  avifRWData out = AVIF_DATA_EMPTY;
  r = avifEncoderWrite(enc, image, &out);
  avifEncoderDestroy(enc);
  avifImageDestroy(image);
  if (r != AVIF_RESULT_OK) {
    avifRWDataFree(&out);
    throw std::runtime_error(std::string("avifEncoderWrite: ") +
                             avifResultToString(r));
  }
  size_t want = out.size;  // avifRWDataFree zeroes out.size
  FILE* f = fopen(path.c_str(), "wb");
  size_t wrote = f ? fwrite(out.data, 1, out.size, f) : 0;
  bool closed = f && fclose(f) == 0;
  avifRWDataFree(&out);
  if (wrote != want || !closed)
    throw std::runtime_error("failed to write " + path);
}

void Film::writeAvif(CUdeviceptr src, const std::string& path,
                     float exposure) const {
  std::vector<float4> hdr = downloadBuf(src, width_, height_);
  std::vector<uint16_t> rgb((size_t)width_ * height_ * 3);
  float scale = exp2f(exposure);
  const float toPq = kPqWhiteNits / kPqPeakNits;  // linear 1.0 -> 203 nits
  const float maxCode = 4095.0f;                  // 12-bit
  for (size_t i = 0; i < hdr.size(); i++) {
    // exposure -> 709->2020 gamut -> 203-nit anchor -> PQ -> 12-bit
    // (report ch01 walks the order; no tonemap — the range survives)
    float3 c = f3(hdr[i].x, hdr[i].y, hdr[i].z) * scale;
    c = bt709To2020(c);
    rgb[3 * i + 0] = (uint16_t)lroundf(pqOetf(c.x * toPq) * maxCode);
    rgb[3 * i + 1] = (uint16_t)lroundf(pqOetf(c.y * toPq) * maxCode);
    rgb[3 * i + 2] = (uint16_t)lroundf(pqOetf(c.z * toPq) * maxCode);
  }
  encodeAvif(path, width_, height_, 12, rgb,
             AVIF_COLOR_PRIMARIES_BT2020,
             AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084);
}

void Film::writeAovAvif(CUdeviceptr src, const std::string& path,
                        bool remap) const {
  std::vector<float4> buf = downloadBuf(src, width_, height_);
  std::vector<uint16_t> rgb((size_t)width_ * height_ * 3);
  for (size_t i = 0; i < buf.size(); i++) {
    float c[3] = {buf[i].x, buf[i].y, buf[i].z};
    for (int k = 0; k < 3; k++) {
      float v = remap ? 0.5f * (c[k] + 1.0f) : c[k];
      // sRGB encode: the guide data is LDR by construction
      v = clampf(v, 0.0f, 1.0f);
      v = v <= 0.0031308f ? 12.92f * v
                          : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
      rgb[3 * i + k] = (uint16_t)lroundf(v * 255.0f);
    }
  }
  encodeAvif(path, width_, height_, 8, rgb,
             AVIF_COLOR_PRIMARIES_BT709,
             AVIF_TRANSFER_CHARACTERISTICS_SRGB);
}

}  // namespace sd

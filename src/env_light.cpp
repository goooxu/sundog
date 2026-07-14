#include "env_light.h"

#include "stb_image.h"  // implementation lives in textures.cpp

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace sd {

EnvDesc EnvMap::upload(const Scene& scene) {
  std::string path = scene.env.file;
  if (!path.empty() && path[0] != '/') path = scene.baseDir + "/" + path;
  int w = 0, h = 0, comp = 0;
  float* pixels = stbi_loadf(path.c_str(), &w, &h, &comp, 4);
  if (!pixels)
    throw std::runtime_error("cannot load envmap '" + path +
                             "' (run scripts/fetch-assets.sh to download HDR assets)");

  // ---- sin(theta)-weighted luminance CDFs -------------------------------
  // Target pdf over the image is luminance * sin(theta): the sin factor
  // cancels the equirect area distortion so equal pdf means equal power per
  // solid angle, not per pixel. Prefix sums accumulate in double; the stored
  // CDFs are float with the last entry pinned to 1.0 so the device binary
  // search can never run past the end on u ~ 1.
  std::vector<float> marg(h + 1), cond((size_t)h * (w + 1));
  double total = 0.0;
  marg[0] = 0.0f;
  for (int row = 0; row < h; row++) {
    float sinT = sinf(SD_PI * (row + 0.5f) / h);
    float* cdf = &cond[(size_t)row * (w + 1)];
    double sum = 0.0;
    cdf[0] = 0.0f;
    for (int col = 0; col < w; col++) {
      const float* p = pixels + 4 * ((size_t)row * w + col);
      float lum = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
      sum += (double)fmaxf(lum, 0.0f) * sinT;
      cdf[col + 1] = (float)sum;
    }
    if (sum > 0.0) {
      for (int c = 1; c <= w; c++) cdf[c] = (float)(cdf[c] / sum);
    } else {  // all-black row: uniform ramp keeps the inversion well-defined
      for (int c = 0; c <= w; c++) cdf[c] = (float)c / w;
    }
    cdf[w] = 1.0f;
    total += sum;
    marg[row + 1] = (float)total;
  }
  bool degenerate = total <= 0.0;
  if (!degenerate) {
    for (int r = 1; r <= h; r++) marg[r] = (float)(marg[r] / total);
  } else {
    for (int r = 0; r <= h; r++) marg[r] = (float)r / h;
  }
  marg[h] = 1.0f;
  if (degenerate && scene.env.importance)
    fprintf(stderr, "envmap: %s has zero luminance, falling back to uniform sampling\n",
            path.c_str());

  // ---- texture: float4, element-type read (radiance > 1 must survive) ---
  cudaChannelFormatDesc ch = cudaCreateChannelDesc<float4>();
  CUDA_CHECK(cudaMallocArray(&array_, &ch, w, h));
  CUDA_CHECK(cudaMemcpy2DToArray(array_, 0, 0, pixels, (size_t)w * 16,
                                 (size_t)w * 16, h, cudaMemcpyHostToDevice));
  stbi_image_free(pixels);

  cudaResourceDesc res{};
  res.resType = cudaResourceTypeArray;
  res.res.array.array = array_;
  cudaTextureDesc td{};
  td.addressMode[0] = cudaAddressModeWrap;   // u: longitude seam
  td.addressMode[1] = cudaAddressModeClamp;  // v: poles must not wrap
  td.filterMode = cudaFilterModeLinear;
  td.readMode = cudaReadModeElementType;     // float data, no normalization
  td.normalizedCoords = 1;
  td.sRGB = 0;                               // HDR is linear
  CUDA_CHECK(cudaCreateTextureObject(&tex_, &res, &td, nullptr));

  margCdf_.upload(marg);
  condCdf_.upload(cond);

  EnvDesc d{};
  d.tex = tex_;
  d.margCdf = margCdf_.as<float>();
  d.condCdf = condCdf_.as<float>();
  d.width = w;
  d.height = h;
  d.rotate = scene.env.rotateDeg * SD_PI / 180.0f;
  d.intensity = scene.env.intensity;
  d.importance = (scene.env.importance && !degenerate) ? 1 : 0;
  return d;
}

EnvMap::~EnvMap() {
  if (tex_) cudaDestroyTextureObject(tex_);
  if (array_) cudaFreeArray(array_);
}

}  // namespace sd

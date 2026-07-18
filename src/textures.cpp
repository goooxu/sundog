#include "textures.h"

#include "cuda_check.h"

// stb stays for one job only: decoding the Radiance .hdr environment map
// (env_light.cpp uses stbi_loadf; the implementation lives in this TU).
// All LDR texture input is AVIF (v0.18) — decoded via libavif below.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR
#include "stb_image.h"

#include <avif/avif.h>

#include <stdexcept>
#include <vector>

namespace sd {

// Decode an AVIF file to tightly-packed 8-bit RGBA.
static std::vector<unsigned char> loadAvifRgba8(const std::string& path,
                                                int& w, int& h) {
  avifDecoder* dec = avifDecoderCreate();
  if (!dec) throw std::runtime_error("avifDecoderCreate failed");
  dec->maxThreads = 4;
  avifResult r = avifDecoderSetIOFile(dec, path.c_str());
  if (r == AVIF_RESULT_OK) r = avifDecoderParse(dec);
  if (r == AVIF_RESULT_OK) r = avifDecoderNextImage(dec);
  if (r != AVIF_RESULT_OK) {
    avifDecoderDestroy(dec);
    throw std::runtime_error("cannot load texture " + path + ": " +
                             avifResultToString(r));
  }
  w = (int)dec->image->width;
  h = (int)dec->image->height;
  avifRGBImage rgb;
  avifRGBImageSetDefaults(&rgb, dec->image);
  rgb.format = AVIF_RGB_FORMAT_RGBA;
  rgb.depth = 8;
  std::vector<unsigned char> px((size_t)w * h * 4);
  rgb.pixels = px.data();
  rgb.rowBytes = (uint32_t)(w * 4);
  r = avifImageYUVToRGB(dec->image, &rgb);
  avifDecoderDestroy(dec);
  if (r != AVIF_RESULT_OK)
    throw std::runtime_error("cannot convert texture " + path + ": " +
                             avifResultToString(r));
  return px;
}

std::vector<TextureDesc> TextureSet::upload(Scene& scene) {
  std::vector<TextureDesc> out;
  out.reserve(scene.textures.size());
  for (SceneTexture& st : scene.textures) {
    TextureDesc d = st.desc;
    if (st.desc.kind == TX_IMAGE) {
      std::string path = st.imageFile;
      if (!path.empty() && path[0] != '/') path = scene.baseDir + "/" + path;
      int w = 0, h = 0;
      std::vector<unsigned char> pixels = loadAvifRgba8(path, w, h);

      cudaChannelFormatDesc ch = cudaCreateChannelDesc<uchar4>();
      cudaArray_t arr = nullptr;
      CUDA_CHECK(cudaMallocArray(&arr, &ch, w, h));
      CUDA_CHECK(cudaMemcpy2DToArray(arr, 0, 0, pixels.data(), (size_t)w * 4,
                                     (size_t)w * 4, h, cudaMemcpyHostToDevice));
      arrays_.push_back(arr);

      cudaResourceDesc res{};
      res.resType = cudaResourceTypeArray;
      res.res.array.array = arr;
      cudaTextureDesc td{};
      td.addressMode[0] = cudaAddressModeWrap;
      td.addressMode[1] = cudaAddressModeWrap;
      td.filterMode = cudaFilterModeLinear;
      td.readMode = cudaReadModeNormalizedFloat;
      td.normalizedCoords = 1;
      td.sRGB = st.srgb ? 1 : 0;
      cudaTextureObject_t tex = 0;
      CUDA_CHECK(cudaCreateTextureObject(&tex, &res, &td, nullptr));
      texObjs_.push_back(tex);
      d.tex = tex;
    }
    out.push_back(d);
  }
  return out;
}

TextureSet::~TextureSet() {
  for (auto t : texObjs_) cudaDestroyTextureObject(t);
  for (auto a : arrays_) cudaFreeArray(a);
}

}  // namespace sd

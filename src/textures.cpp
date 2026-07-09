#include "textures.h"

#include "cuda_check.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#include <stdexcept>

namespace sd {

std::vector<TextureDesc> TextureSet::upload(Scene& scene) {
  std::vector<TextureDesc> out;
  out.reserve(scene.textures.size());
  for (SceneTexture& st : scene.textures) {
    TextureDesc d = st.desc;
    if (st.desc.kind == TX_IMAGE) {
      std::string path = st.imageFile;
      if (!path.empty() && path[0] != '/') path = scene.baseDir + "/" + path;
      int w = 0, h = 0, comp = 0;
      unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
      if (!pixels) throw std::runtime_error("cannot load texture " + path);

      cudaChannelFormatDesc ch = cudaCreateChannelDesc<uchar4>();
      cudaArray_t arr = nullptr;
      CUDA_CHECK(cudaMallocArray(&arr, &ch, w, h));
      CUDA_CHECK(cudaMemcpy2DToArray(arr, 0, 0, pixels, (size_t)w * 4,
                                     (size_t)w * 4, h, cudaMemcpyHostToDevice));
      stbi_image_free(pixels);
      arrays_.push_back(arr);

      cudaResourceDesc res{};
      res.resType = cudaResourceTypeArray;
      res.res.array.array = arr;
      cudaTextureDesc td{};
      td.addressMode[0] = cudaAddressModeWrap;
      td.addressMode[1] = cudaAddressModeWrap;
      td.filterMode = st.nearest ? cudaFilterModePoint : cudaFilterModeLinear;
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

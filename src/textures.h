// sundog: image texture upload (stb_image -> cudaArray + texture object).
#ifndef SUNDOG_TEXTURES_H
#define SUNDOG_TEXTURES_H

#include "scene.h"
#include <cuda_runtime.h>
#include <vector>

namespace sd {

class TextureSet {
 public:
  // Loads TX_IMAGE textures from disk, fills desc.tex, returns final descs.
  std::vector<TextureDesc> upload(Scene& scene);
  ~TextureSet();

 private:
  std::vector<cudaArray_t> arrays_;
  std::vector<cudaTextureObject_t> texObjs_;
};

}  // namespace sd

#endif

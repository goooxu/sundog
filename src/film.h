// sundog: accumulation buffers, tonemap, PNG output.
#ifndef SUNDOG_FILM_H
#define SUNDOG_FILM_H

#include "cuda_check.h"
#include "tonemap.h"
#include <string>

namespace sd {

class Film {
 public:
  Film(int width, int height);

  CUdeviceptr accum() const { return accum_.ptr; }
  CUdeviceptr aovAlbedo() const { return albedo_.ptr; }
  CUdeviceptr aovNormal() const { return normal_.ptr; }
  CUdeviceptr denoised() const { return denoised_.ptr; }

  // exposure in EV stops; gamma e.g. 2.2. src: accum() or denoised().
  void writePng(CUdeviceptr src, const std::string& path, float exposure,
                float gamma, TonemapMode tonemap) const;
  // AOV debug output ([-1,1] normals remapped to [0,1]).
  void writeAovPng(CUdeviceptr src, const std::string& path, bool remap) const;

  int width() const { return width_; }
  int height() const { return height_; }

 private:
  int width_, height_;
  CudaBuffer accum_, albedo_, normal_, denoised_;
};

}  // namespace sd

#endif

// sundog: accumulation buffers and HDR AVIF output.
#ifndef SUNDOG_FILM_H
#define SUNDOG_FILM_H

#include "cuda_check.h"
#include <string>

namespace sd {

class Film {
 public:
  // withDenoised: allocate the denoiser output buffer (skipped for
  // --no-denoise renders — it would be pure dead VRAM).
  Film(int width, int height, bool withDenoised);

  CUdeviceptr accum() const { return accum_.ptr; }
  CUdeviceptr aovAlbedo() const { return albedo_.ptr; }
  CUdeviceptr aovNormal() const { return normal_.ptr; }
  CUdeviceptr denoised() const { return denoised_.ptr; }

  // Beauty output: linear HDR -> PQ (BT.2100) 12-bit lossless AVIF.
  // exposure in EV stops. src: accum() or denoised().
  void writeAvif(CUdeviceptr src, const std::string& path,
                 float exposure) const;
  // AOV debug output ([-1,1] normals remapped to [0,1]): the data is LDR
  // by construction, stored as sRGB 8-bit lossless AVIF.
  void writeAovAvif(CUdeviceptr src, const std::string& path,
                    bool remap) const;

 private:
  int width_, height_;
  CudaBuffer accum_, albedo_, normal_, denoised_;
};

}  // namespace sd

#endif

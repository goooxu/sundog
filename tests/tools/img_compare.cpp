// sundog: image comparison tool for golden tests.
//   usage: img_compare a.png b.png [minPSNR]
// Prints "PSNR: xx.xx dB". Exit codes: 0 ok, 1 PSNR below minPSNR,
// 2 usage/load error or size mismatch.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
  if (argc < 3 || argc > 4) {
    std::fprintf(stderr, "usage: img_compare a.png b.png [minPSNR]\n");
    return 2;
  }
  int wa, ha, ca, wb, hb, cb;
  unsigned char* a = stbi_load(argv[1], &wa, &ha, &ca, 3);
  if (!a) {
    std::fprintf(stderr, "img_compare: cannot load %s: %s\n", argv[1],
                 stbi_failure_reason());
    return 2;
  }
  unsigned char* b = stbi_load(argv[2], &wb, &hb, &cb, 3);
  if (!b) {
    std::fprintf(stderr, "img_compare: cannot load %s: %s\n", argv[2],
                 stbi_failure_reason());
    stbi_image_free(a);
    return 2;
  }
  if (wa != wb || ha != hb) {
    std::fprintf(stderr, "img_compare: size mismatch: %dx%d vs %dx%d\n", wa, ha,
                 wb, hb);
    stbi_image_free(a);
    stbi_image_free(b);
    return 2;
  }

  const size_t n = (size_t)wa * (size_t)ha * 3;
  double sqErr = 0.0;
  for (size_t i = 0; i < n; i++) {
    double d = (double)a[i] - (double)b[i];
    sqErr += d * d;
  }
  stbi_image_free(a);
  stbi_image_free(b);

  double mse = sqErr / (double)n;
  double psnr = mse > 0.0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : INFINITY;
  std::printf("PSNR: %.2f dB\n", psnr);

  if (argc == 4) {
    double minPsnr = std::atof(argv[3]);
    if (psnr < minPsnr) {
      std::fprintf(stderr, "img_compare: PSNR %.2f dB below threshold %.2f dB\n",
                   psnr, minPsnr);
      return 1;
    }
  }
  return 0;
}

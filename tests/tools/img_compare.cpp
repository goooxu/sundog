// sundog img_compare — PSNR between two AVIF images, in the normalized
// code-value domain of whatever depth the files carry (12-bit PQ beauty
// renders, 8-bit sRGB AOVs alike). Usage:
//   img_compare A.avif B.avif [MIN_PSNR]
// Prints "PSNR: X dB"; exits 1 if below MIN_PSNR, 2 on shape mismatch.
#include <avif/avif.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static bool loadAvif(const char* path, int& w, int& h, int& depth,
                     std::vector<uint16_t>& rgb) {
  avifDecoder* dec = avifDecoderCreate();
  if (!dec) return false;
  dec->maxThreads = 4;
  avifResult r = avifDecoderSetIOFile(dec, path);
  if (r == AVIF_RESULT_OK) r = avifDecoderParse(dec);
  if (r == AVIF_RESULT_OK) r = avifDecoderNextImage(dec);
  if (r != AVIF_RESULT_OK) {
    fprintf(stderr, "img_compare: %s: %s\n", path, avifResultToString(r));
    avifDecoderDestroy(dec);
    return false;
  }
  w = (int)dec->image->width;
  h = (int)dec->image->height;
  depth = (int)dec->image->depth;
  avifRGBImage view;
  avifRGBImageSetDefaults(&view, dec->image);
  view.format = AVIF_RGB_FORMAT_RGB;
  view.depth = (uint32_t)depth;
  rgb.resize((size_t)w * h * 3);
  view.pixels = (uint8_t*)rgb.data();
  // 8-bit output is packed as bytes: keep rows tight so the widen pass
  // below can treat the buffer as contiguous
  view.rowBytes = (uint32_t)(w * 3 * (depth > 8 ? sizeof(uint16_t) : 1));
  r = avifImageYUVToRGB(dec->image, &view);
  avifDecoderDestroy(dec);
  if (r != AVIF_RESULT_OK) return false;
  if (depth <= 8) {
    // libavif packs 8-bit output as bytes; widen to uint16 in place
    const uint8_t* b = (const uint8_t*)rgb.data();
    std::vector<uint16_t> wide(rgb.size());
    for (size_t i = 0; i < wide.size(); i++) wide[i] = b[i];
    rgb.swap(wide);
  }
  return true;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: img_compare A.avif B.avif [MIN_PSNR]\n");
    return 2;
  }
  int wa, ha, da, wb, hb, db;
  std::vector<uint16_t> a, b;
  if (!loadAvif(argv[1], wa, ha, da, a)) return 2;
  if (!loadAvif(argv[2], wb, hb, db, b)) return 2;
  if (wa != wb || ha != hb || da != db) {
    fprintf(stderr, "img_compare: shape mismatch (%dx%d/%dbit vs %dx%d/%dbit)\n",
            wa, ha, da, wb, hb, db);
    return 2;
  }
  double peak = (double)((1 << da) - 1);
  double mse = 0.0;
  for (size_t i = 0; i < a.size(); i++) {
    double d = ((double)a[i] - (double)b[i]) / peak;
    mse += d * d;
  }
  mse /= (double)a.size();
  double psnr = mse <= 0.0 ? INFINITY : 10.0 * log10(1.0 / mse);
  printf("PSNR: %.2f dB\n", psnr);
  if (argc >= 4) {
    double minPsnr = atof(argv[3]);
    if (!(psnr >= minPsnr)) return 1;
  }
  return 0;
}

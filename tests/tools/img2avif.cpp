// sundog img2avif — one-shot converter between legacy SDR images and the
// project's AVIF-only world. Two modes:
//
//   img2avif encode IN.(png|jpg|bmp|tga|hdr) OUT.avif [pq]
//     stb-decoded 8-bit RGBA -> lossless AVIF (alpha kept if present;
//     YUV444 + identity matrix + full range = mathematically lossless).
//     Default CICP is sRGB; the optional "pq" tag stamps BT.2020/PQ
//     instead — for panels stitched from the renderer's PQ output, whose
//     pixels are already PQ code values and must stay declared as such.
//
//   img2avif decode IN.avif OUT.(pam|ppm)
//     AVIF -> 8-bit PAM (P7, RGBA) or PPM (P6, RGB — readable by PIL),
//     for verification round-trips and as a bridge for tools that cannot
//     read AVIF yet. Pixels are code values; no transfer conversion.
//
// Build: make $(BUILD)/img2avif   (links the static libavif+libaom stack)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <avif/avif.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int fail(const std::string& msg) {
  fprintf(stderr, "img2avif: %s\n", msg.c_str());
  return 1;
}

static int encode(const char* inPath, const char* outPath, bool pq) {
  int w = 0, h = 0, comp = 0;
  unsigned char* px = stbi_load(inPath, &w, &h, &comp, 4);
  if (!px) return fail(std::string("cannot read ") + inPath);
  bool hasAlpha = comp == 4;

  avifImage* image = avifImageCreate(w, h, 8, AVIF_PIXEL_FORMAT_YUV444);
  if (!image) return fail("avifImageCreate failed");
  image->colorPrimaries =
      pq ? AVIF_COLOR_PRIMARIES_BT2020 : AVIF_COLOR_PRIMARIES_BT709;
  image->transferCharacteristics = pq ? AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084
                                      : AVIF_TRANSFER_CHARACTERISTICS_SRGB;
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
  image->yuvRange = AVIF_RANGE_FULL;

  avifRGBImage rgb;
  avifRGBImageSetDefaults(&rgb, image);
  rgb.format = hasAlpha ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGBA;
  rgb.ignoreAlpha = hasAlpha ? AVIF_FALSE : AVIF_TRUE;
  rgb.depth = 8;
  rgb.pixels = px;
  rgb.rowBytes = (uint32_t)(w * 4);
  avifResult r = avifImageRGBToYUV(image, &rgb);
  if (r != AVIF_RESULT_OK)
    return fail(std::string("RGBToYUV: ") + avifResultToString(r));

  avifEncoder* enc = avifEncoderCreate();
  enc->quality = AVIF_QUALITY_LOSSLESS;
  enc->qualityAlpha = AVIF_QUALITY_LOSSLESS;
  enc->speed = 6;
  enc->maxThreads = 4;
  avifRWData out = AVIF_DATA_EMPTY;
  r = avifEncoderWrite(enc, image, &out);
  avifEncoderDestroy(enc);
  avifImageDestroy(image);
  stbi_image_free(px);
  if (r != AVIF_RESULT_OK)
    return fail(std::string("EncoderWrite: ") + avifResultToString(r));
  FILE* f = fopen(outPath, "wb");
  if (!f || fwrite(out.data, 1, out.size, f) != out.size)
    return fail(std::string("cannot write ") + outPath);
  fclose(f);
  avifRWDataFree(&out);
  printf("img2avif: %s -> %s (%dx%d%s%s)\n", inPath, outPath, w, h,
         hasAlpha ? " +alpha" : "", pq ? " PQ" : "");
  return 0;
}

static int decode(const char* inPath, const char* outPath) {
  avifDecoder* dec = avifDecoderCreate();
  if (!dec) return fail("avifDecoderCreate failed");
  dec->maxThreads = 4;
  avifResult r = avifDecoderSetIOFile(dec, inPath);
  if (r == AVIF_RESULT_OK) r = avifDecoderParse(dec);
  if (r == AVIF_RESULT_OK) r = avifDecoderNextImage(dec);
  if (r != AVIF_RESULT_OK) {
    avifDecoderDestroy(dec);
    return fail(std::string("decode ") + inPath + ": " + avifResultToString(r));
  }
  int w = (int)dec->image->width, h = (int)dec->image->height;
  size_t len = strlen(outPath);
  bool ppm = len > 4 && !strcmp(outPath + len - 4, ".ppm");
  int ch = ppm ? 3 : 4;
  avifRGBImage rgb;
  avifRGBImageSetDefaults(&rgb, dec->image);
  rgb.format = ppm ? AVIF_RGB_FORMAT_RGB : AVIF_RGB_FORMAT_RGBA;
  rgb.depth = 8;
  std::vector<unsigned char> px((size_t)w * h * ch);
  rgb.pixels = px.data();
  rgb.rowBytes = (uint32_t)(w * ch);
  r = avifImageYUVToRGB(dec->image, &rgb);
  avifDecoderDestroy(dec);
  if (r != AVIF_RESULT_OK)
    return fail(std::string("YUVToRGB: ") + avifResultToString(r));

  FILE* f = fopen(outPath, "wb");
  if (!f) return fail(std::string("cannot write ") + outPath);
  if (ppm)
    fprintf(f, "P6\n%d %d\n255\n", w, h);
  else
    fprintf(f,
            "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE "
            "RGB_ALPHA\nENDHDR\n",
            w, h);
  fwrite(px.data(), 1, px.size(), f);
  fclose(f);
  printf("img2avif: %s -> %s (%dx%d %s)\n", inPath, outPath, w, h,
         ppm ? "PPM" : "PAM");
  return 0;
}

int main(int argc, char** argv) {
  bool pq = argc == 5 && !strcmp(argv[4], "pq");
  if ((argc == 4 || (argc == 5 && pq)) && !strcmp(argv[1], "encode"))
    return encode(argv[2], argv[3], pq);
  if (argc == 4 && !strcmp(argv[1], "decode")) return decode(argv[2], argv[3]);
  fprintf(stderr, "usage: img2avif encode IN.img OUT.avif [pq]\n"
                  "       img2avif decode IN.avif OUT.(pam|ppm)\n");
  return 2;
}

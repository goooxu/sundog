#include "cli.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sd {

static const char* USAGE = R"(sundog - OptiX path tracer for NVIDIA RTX GPUs

usage: sundog --scene FILE --out FILE.png [options]
       sundog --probe

options:
  --scene FILE        scene JSON
  --out FILE.png      output image
  --spp N             samples per pixel
  --size WxH          resolution
  --seed N            RNG seed (fixed seed => deterministic image)
  --denoise           run the OptiX AI denoiser
  --no-denoise        disable denoiser (overrides scene default)
  --clamp F           firefly clamp for indirect light (0 = off)
  --gamma F           output gamma (default 2.2)
  --tonemap MODE      output tone mapping: aces (default) | clamp (linear)
  --physics-time F    freeze-frame: bake rigid bodies at sim time F seconds
                      instead of settling (0 = force settle-to-sleep)
  --stats FILE.json   write render statistics
  --aov-albedo F.png  write albedo guide AOV
  --aov-normal F.png  write normal guide AOV
  --probe             print GPU/driver/OptiX info and exit
  --quiet             suppress progress output
)";

static void usageDie(const char* msg) {
  if (msg) fprintf(stderr, "error: %s\n\n", msg);
  fputs(USAGE, stderr);
  exit(msg ? 2 : 0);
}

CliOptions parseCli(int argc, char** argv) {
  CliOptions o;
  auto need = [&](int& i) -> const char* {
    if (i + 1 >= argc) usageDie("missing value for option");
    return argv[++i];
  };
  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
    if (!strcmp(a, "--scene")) o.scene = need(i);
    else if (!strcmp(a, "--out")) o.out = need(i);
    else if (!strcmp(a, "--spp")) o.spp = atoi(need(i));
    else if (!strcmp(a, "--size")) {
      const char* v = need(i);
      if (sscanf(v, "%dx%d", &o.width, &o.height) != 2) usageDie("--size wants WxH");
    }
    else if (!strcmp(a, "--seed")) o.seed = atol(need(i));
    else if (!strcmp(a, "--denoise")) o.denoise = 1;
    else if (!strcmp(a, "--no-denoise")) o.denoise = 0;
    else if (!strcmp(a, "--clamp")) o.clampVal = (float)atof(need(i));
    else if (!strcmp(a, "--gamma")) o.gamma = (float)atof(need(i));
    else if (!strcmp(a, "--tonemap")) o.tonemap = need(i);
    else if (!strcmp(a, "--physics-time")) {
      o.physicsTime = (float)atof(need(i));
      if (o.physicsTime < 0.0f) usageDie("--physics-time wants a value >= 0");
    }
    else if (!strcmp(a, "--stats")) o.statsPath = need(i);
    else if (!strcmp(a, "--aov-albedo")) o.aovAlbedoPath = need(i);
    else if (!strcmp(a, "--aov-normal")) o.aovNormalPath = need(i);
    else if (!strcmp(a, "--probe")) o.probe = true;
    else if (!strcmp(a, "--quiet")) o.quiet = true;
    else if (!strcmp(a, "--help") || !strcmp(a, "-h")) usageDie(nullptr);
    else usageDie((std::string("unknown option ") + a).c_str());
  }
  if (!o.probe && (o.scene.empty() || o.out.empty()))
    usageDie("--scene and --out are required");
  return o;
}

}  // namespace sd

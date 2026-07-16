// sundog: render statistics -> .stats.json sidecar. Hand-written serializer
// (the only JSON left in the tree is this OUTPUT format; scene input crosses
// the C ABI directly). Consumers are `python json.load` in the bench/gallery
// scripts — key order and whitespace are free, values must round-trip.
#include "stats.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace sd {

static std::string jstr(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((unsigned char)c < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", c);
      o += buf;
    } else o += c;
  }
  return o + "\"";
}

void writeStats(const RenderStats& st, const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) throw std::runtime_error("cannot write " + path);
  std::fprintf(f, "{\n");
  std::fprintf(f, "  \"scene\": %s,\n", jstr(st.scene).c_str());
  std::fprintf(f, "  \"width\": %d,\n  \"height\": %d,\n  \"spp\": %d,\n",
               st.width, st.height, st.spp);
  std::fprintf(f, "  \"max_depth\": %d,\n  \"seed\": %u,\n", st.maxDepth, st.seed);
  std::fprintf(f, "  \"denoised\": %s,\n", st.denoised ? "true" : "false");
  std::fprintf(f,
               "  \"timings_ms\": {\n"
               "    \"scene_load\": %.17g,\n    \"physics\": %.17g,\n"
               "    \"gas_build\": %.17g,\n    \"render\": %.17g,\n"
               "    \"denoise\": %.17g,\n    \"total\": %.17g\n  },\n",
               st.sceneLoadMs, st.physicsMs, st.gasBuildMs, st.renderMs,
               st.denoiseMs, st.totalMs);
  std::fprintf(f, "  \"rays_traced\": %llu,\n  \"mrays_per_sec\": %.17g,\n",
               st.raysTraced, st.mraysPerSec);
  std::fprintf(f, "  \"peak_vram_mb\": %zu,\n", st.peakVramMB);
  std::fprintf(f,
               "  \"scene_stats\": {\n"
               "    \"objects\": %d,\n    \"lights\": %d,\n"
               "    \"meshes\": %d,\n    \"mesh_triangles\": %zu\n  },\n",
               st.numObjects, st.numLights, st.numMeshes, st.meshTriangles);
  std::fprintf(f,
               "  \"device\": {\n"
               "    \"name\": %s,\n    \"compute\": \"sm_%d%d\",\n"
               "    \"vram_mb\": %zu,\n    \"cuda_driver\": %d,\n"
               "    \"cuda_runtime\": %d,\n    \"optix_header\": %u\n  }\n",
               jstr(st.device.name).c_str(), st.device.ccMajor, st.device.ccMinor,
               st.device.totalMemMB, st.device.driverVersion,
               st.device.runtimeVersion, st.device.optixHeaderVersion);
  std::fprintf(f, "}\n");
  std::fclose(f);
}

}  // namespace sd

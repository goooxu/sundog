#include "stats.h"

#include "json.hpp"
#include <fstream>
#include <stdexcept>

namespace sd {

void writeStats(const RenderStats& st, const std::string& path) {
  nlohmann::json j;
  j["scene"] = st.scene;
  j["width"] = st.width;
  j["height"] = st.height;
  j["spp"] = st.spp;
  j["max_depth"] = st.maxDepth;
  j["seed"] = st.seed;
  j["denoised"] = st.denoised;
  j["timings_ms"] = {{"scene_load", st.sceneLoadMs},
                     {"gas_build", st.gasBuildMs},
                     {"render", st.renderMs},
                     {"denoise", st.denoiseMs},
                     {"total", st.totalMs}};
  j["rays_traced"] = st.raysTraced;
  j["mrays_per_sec"] = st.mraysPerSec;
  j["peak_vram_mb"] = st.peakVramMB;
  j["scene_stats"] = {{"objects", st.numObjects},
                      {"lights", st.numLights},
                      {"meshes", st.numMeshes},
                      {"mesh_triangles", st.meshTriangles}};
  j["device"] = {{"name", st.device.name},
                 {"compute", "sm_" + std::to_string(st.device.ccMajor) +
                                 std::to_string(st.device.ccMinor)},
                 {"vram_mb", st.device.totalMemMB},
                 {"cuda_driver", st.device.driverVersion},
                 {"cuda_runtime", st.device.runtimeVersion},
                 {"optix_header", st.device.optixHeaderVersion}};
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write " + path);
  out << j.dump(2) << "\n";
}

}  // namespace sd

// sundog: render statistics -> .stats.json sidecar.
#ifndef SUNDOG_STATS_H
#define SUNDOG_STATS_H

#include "context.h"
#include <string>

namespace sd {

struct RenderStats {
  std::string scene;
  int width = 0, height = 0, spp = 0, maxDepth = 0;
  unsigned seed = 0;
  bool denoised = false, parity = false;
  double sceneLoadMs = 0, gasBuildMs = 0, renderMs = 0, denoiseMs = 0, totalMs = 0;
  unsigned long long raysTraced = 0;
  double mraysPerSec = 0;
  size_t peakVramMB = 0;
  int numObjects = 0, numLights = 0, numMeshes = 0;
  size_t meshTriangles = 0;
  DeviceInfo device;
};

void writeStats(const RenderStats& st, const std::string& path);

}  // namespace sd

#endif

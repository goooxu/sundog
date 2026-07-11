// sundog: command-line interface.
#ifndef SUNDOG_CLI_H
#define SUNDOG_CLI_H

#include <string>

namespace sd {

struct CliOptions {
  std::string scene;
  std::string out;
  std::string statsPath;
  std::string aovAlbedoPath, aovNormalPath;
  // -1 / NaN = "not set on the command line" (scene JSON value wins)
  int spp = -1;
  int width = -1, height = -1;
  int maxDepth = -1;
  long seed = -1;
  int chunk = -1;
  int denoise = -1;   // -1 unset, 0 off, 1 on
  float clampVal = -1.0f;
  float gamma = -1.0f;
  float exposure = -1e30f;
  bool probe = false;
  bool quiet = false;
};

// Exits with a message on bad usage.
CliOptions parseCli(int argc, char** argv);

}  // namespace sd

#endif

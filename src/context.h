// sundog: CUDA/OptiX bring-up and device info.
#ifndef SUNDOG_CONTEXT_H
#define SUNDOG_CONTEXT_H

#include <optix.h>
#include <string>

namespace sd {

struct DeviceInfo {
  std::string name;
  int ccMajor = 0, ccMinor = 0;
  int driverVersion = 0, runtimeVersion = 0;
  size_t totalMemMB = 0;
  unsigned optixHeaderVersion = 0;
  unsigned rtcoreVersion = 0;
};

OptixDeviceContext createContext(bool verboseLog);
DeviceInfo queryDeviceInfo(OptixDeviceContext ctx);

}  // namespace sd

#endif

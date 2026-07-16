#include "context.h"

#include "cuda_check.h"
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <cstdio>

namespace sd {

static void logCallback(unsigned level, const char* tag, const char* msg, void*) {
  fprintf(stderr, "[optix][%u][%s] %s\n", level, tag, msg);
}

OptixDeviceContext createContext(bool verboseLog) {
  CUDA_CHECK(cudaFree(nullptr));  // establish the primary context
  OPTIX_CHECK(optixInit());
  OptixDeviceContextOptions opts{};
  opts.logCallbackFunction = logCallback;
  const char* lvl = getenv("SUNDOG_LOG_LEVEL");
  opts.logCallbackLevel = lvl ? atoi(lvl) : (verboseLog ? 4 : 2);
  OptixDeviceContext ctx = nullptr;
  OPTIX_CHECK(optixDeviceContextCreate(0, &opts, &ctx));
  return ctx;
}

DeviceInfo queryDeviceInfo(OptixDeviceContext ctx) {
  DeviceInfo di;
  cudaDeviceProp prop{};
  CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
  di.name = prop.name;
  di.ccMajor = prop.major;
  di.ccMinor = prop.minor;
  di.totalMemMB = prop.totalGlobalMem >> 20;
  CUDA_CHECK(cudaDriverGetVersion(&di.driverVersion));
  CUDA_CHECK(cudaRuntimeGetVersion(&di.runtimeVersion));
  di.optixHeaderVersion = OPTIX_VERSION;
  if (ctx) {
    unsigned v = 0;
    if (optixDeviceContextGetProperty(ctx, OPTIX_DEVICE_PROPERTY_RTCORE_VERSION,
                                      &v, sizeof(v)) == OPTIX_SUCCESS) {
      di.rtcoreVersion = v;
    }
  }
  return di;
}


}  // namespace sd

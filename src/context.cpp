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

void printProbe(const DeviceInfo& di) {
  printf("GPU:            %s\n", di.name.c_str());
  printf("Compute:        sm_%d%d\n", di.ccMajor, di.ccMinor);
  printf("VRAM:           %zu MB\n", di.totalMemMB);
  printf("CUDA driver:    %d.%d\n", di.driverVersion / 1000, (di.driverVersion % 1000) / 10);
  printf("CUDA runtime:   %d.%d\n", di.runtimeVersion / 1000, (di.runtimeVersion % 1000) / 10);
  printf("OptiX header:   %u.%u.%u\n", di.optixHeaderVersion / 10000,
         (di.optixHeaderVersion % 10000) / 100, di.optixHeaderVersion % 100);
  printf("RT core:        %u.%u\n", di.rtcoreVersion / 10, di.rtcoreVersion % 10);
}

}  // namespace sd

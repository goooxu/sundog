// sundog: shared internals of the C API implementation (capi_scene.cpp /
// capi_render.cpp). The handle wraps sd::Scene plus the phase state machine
// that turns the light-ordering contract into an API invariant.
#ifndef SUNDOG_CAPI_INTERNAL_H
#define SUNDOG_CAPI_INTERNAL_H

#include "scene.h"
#include "sundog_api.h"

#include <cmath>
#include <stdexcept>
#include <string>

// Phases: 0 config/registries -> 1 objects -> 2 flames -> 3 explicit lights.
// Monotonic; a lower-phase call after a higher phase is an error.
struct sundog_scene {
  sd::Scene scene;
  int phase = 0;
  bool hasCamera = false;
};

namespace sd_capi {

void setLastError(const std::string& msg);

// Narrowing helpers: exactly one double->float conversion at the ABI
// boundary, before any arithmetic — the same single IEEE narrowing the old
// JSON loader performed with get<float>() at its leaves. Never compute in
// double past this point.
inline bool isset(double v) { return !std::isnan(v); }
inline float nf(double v, float dflt) { return std::isnan(v) ? dflt : (float)v; }
inline float3 nf3(const double* p, float3 dflt) {
  return p ? sd::f3((float)p[0], (float)p[1], (float)p[2]) : dflt;
}

inline void phaseAdvance(sundog_scene* h, int needed, const char* what) {
  if (h->phase > needed)
    throw std::runtime_error(std::string("scene: ") + what +
                             " must come before objects/flames/lights "
                             "(call order: config, objects, flames, lights)");
  h->phase = needed;
}

}  // namespace sd_capi

#define SUNDOG_TRY try {
#define SUNDOG_CATCH(errval)                          \
  }                                                   \
  catch (const std::exception& e) {                   \
    sd_capi::setLastError(e.what());                  \
    return errval;                                    \
  }                                                   \
  catch (...) {                                       \
    sd_capi::setLastError("unknown error");           \
    return errval;                                    \
  }

#endif

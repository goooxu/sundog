// sundog: scene derivation — the loader-independent half of scene assembly.
// Composes transform chains, registers emissive rect/disk/sphere objects as
// NEE area lights, expands flames into their two embedded point lights, and
// enforces the physics collider constraints. Extracted verbatim from the old
// JSON loader; the float math in here is golden-image load-bearing.
#ifndef SUNDOG_SCENE_BUILD_H
#define SUNDOG_SCENE_BUILD_H

#include "scene.h"

namespace sd {

[[noreturn]] void sceneFail(const std::string& msg);  // throws "scene: " + msg

// Transform step, scalars already narrowed to float at the API boundary.
// Kind values mirror SUNDOG_XF_* in sundog_api.h.
enum XformKind {
  XF_SCALE = 0,
  XF_TRANSLATE = 1,
  XF_ROTATE_X = 2,
  XF_ROTATE_Y = 3,
  XF_ROTATE_Z = 4,
};
struct XformStep {
  int kind;
  float a, b, c;  // SCALE/TRANSLATE: vector; ROTATE_*: degrees in a
};

// Top-down composition: later steps wrap earlier ones (m = mul(step, m)).
Affine composeTransform(const XformStep* steps, int n);

// `so` arrives with geometry/materials/cutout/xform/nee/physics fields set;
// this validates the physics constraints, auto-registers the NEE area light
// (setting so.lightId), and appends to s.objects.
void addObjectDerived(Scene& s, SceneObject& so);

// Domain-checks the flame, registers its two warm point lights, appends.
void addFlameDerived(Scene& s, const FlameDesc& fd, float lightIntensity);

}  // namespace sd

#endif

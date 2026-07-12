// sundog: PhysX GPU rigid-body settling. Scene JSON declares initial poses,
// velocities and physics parameters; runPhysics() simulates to rest on the
// GPU and bakes the final poses back into each object's xform, after which
// the renderer proceeds as if the scene had been authored that way.
// This header is PhysX-free; all PhysX code lives in physics.cpp.
#ifndef SUNDOG_PHYSICS_H
#define SUNDOG_PHYSICS_H

#include "mesh_obj.h"
#include "scene.h"
#include <vector>

namespace sd {

struct PhysicsStats {
  int bodies = 0;    // dynamic rigid bodies
  int statics = 0;   // static colliders
  int steps = 0;
  double simSeconds = 0.0;
  double wallMs = 0.0;
  bool settled = false;
};

// Requires a working CUDA context (GPU simulation is mandatory, no CPU
// fallback); throws std::runtime_error on any setup failure or unsupported
// transform (shear/mirror; dynamic bodies additionally need uniform scale).
PhysicsStats runPhysics(Scene& scene, const std::vector<GpuMesh>& meshes);

}  // namespace sd

#endif

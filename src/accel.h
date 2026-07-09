// sundog: acceleration-structure builds (quadric-AABB GAS, triangle GAS, IAS).
#ifndef SUNDOG_ACCEL_H
#define SUNDOG_ACCEL_H

#include "cuda_check.h"
#include "mesh_obj.h"
#include "scene.h"
#include <optix.h>
#include <vector>

namespace sd {

struct Gas {
  OptixTraversableHandle handle = 0;
  CudaBuffer buffer;
};

Gas buildQuadricGas(OptixDeviceContext ctx, int geomKind);
Gas buildTriangleGas(OptixDeviceContext ctx, const GpuMesh& mesh);

// One OptixInstance per scene object; sbtOffset = 2*i. Opaque instances get
// OPTIX_INSTANCE_FLAG_DISABLE_ANYHIT.
Gas buildIas(OptixDeviceContext ctx, const Scene& scene,
             const std::vector<Gas>& quadricGas,  // indexed by GeomKind
             const std::vector<Gas>& meshGas);    // indexed by meshId

}  // namespace sd

#endif

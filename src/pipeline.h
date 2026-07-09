// sundog: OptiX module/program-group/pipeline/SBT assembly.
#ifndef SUNDOG_PIPELINE_H
#define SUNDOG_PIPELINE_H

#include "cuda_check.h"
#include "mesh_obj.h"
#include "scene.h"
#include <optix.h>
#include <vector>

namespace sd {

class Pipeline {
 public:
  Pipeline(OptixDeviceContext ctx, bool debug);
  ~Pipeline();

  // Builds hitgroup records: 2 per object (radiance, shadow).
  void buildSbt(const Scene& scene, const std::vector<GpuMesh>& meshes);

  void launch(const LaunchParams& params, int width, int height);

 private:
  OptixDeviceContext ctx_;
  OptixModule module_ = nullptr;
  OptixPipeline pipeline_ = nullptr;
  std::vector<OptixProgramGroup> groups_;
  OptixShaderBindingTable sbt_{};
  CudaBuffer raygenRec_, missRec_, hitRec_, paramsBuf_;
};

}  // namespace sd

#endif

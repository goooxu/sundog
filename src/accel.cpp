#include "accel.h"

#include "intersect.cuh"
#include <cstring>

namespace sd {

static Gas buildAndCompact(OptixDeviceContext ctx, const OptixBuildInput& input) {
  OptixAccelBuildOptions opts{};
  opts.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE | OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
  opts.operation = OPTIX_BUILD_OPERATION_BUILD;

  OptixAccelBufferSizes sizes{};
  OPTIX_CHECK(optixAccelComputeMemoryUsage(ctx, &opts, &input, 1, &sizes));

  CudaBuffer temp, full;
  temp.alloc(sizes.tempSizeInBytes);
  full.alloc(sizes.outputSizeInBytes);

  CudaBuffer compactedSizeBuf;
  compactedSizeBuf.alloc(sizeof(uint64_t));
  OptixAccelEmitDesc emit{};
  emit.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
  emit.result = compactedSizeBuf.ptr;

  Gas gas;
  OPTIX_CHECK(optixAccelBuild(ctx, 0, &opts, &input, 1, temp.ptr,
                              sizes.tempSizeInBytes, full.ptr,
                              sizes.outputSizeInBytes, &gas.handle, &emit, 1));
  CUDA_CHECK(cudaDeviceSynchronize());

  uint64_t compactedSize = 0;
  CUDA_CHECK(cudaMemcpy(&compactedSize, (void*)compactedSizeBuf.ptr,
                        sizeof(uint64_t), cudaMemcpyDeviceToHost));
  if (compactedSize < sizes.outputSizeInBytes) {
    gas.buffer.alloc(compactedSize);
    OPTIX_CHECK(optixAccelCompact(ctx, 0, gas.handle, gas.buffer.ptr,
                                  compactedSize, &gas.handle));
    CUDA_CHECK(cudaDeviceSynchronize());
  } else {
    gas.buffer = std::move(full);
  }
  return gas;
}

Gas buildQuadricGas(OptixDeviceContext ctx, int geomKind) {
  float lo[3], hi[3];
  quadricAabb(geomKind, lo, hi);
  OptixAabb aabb{lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]};

  CudaBuffer aabbBuf;
  aabbBuf.upload(std::vector<OptixAabb>{aabb});

  static const unsigned flags = OPTIX_GEOMETRY_FLAG_REQUIRE_SINGLE_ANYHIT_CALL;
  OptixBuildInput input{};
  input.type = OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;
  input.customPrimitiveArray.aabbBuffers = &aabbBuf.ptr;
  input.customPrimitiveArray.numPrimitives = 1;
  input.customPrimitiveArray.flags = &flags;
  input.customPrimitiveArray.numSbtRecords = 1;

  return buildAndCompact(ctx, input);
}

Gas buildTriangleGas(OptixDeviceContext ctx, const GpuMesh& mesh) {
  static const unsigned flags = OPTIX_GEOMETRY_FLAG_REQUIRE_SINGLE_ANYHIT_CALL;
  OptixBuildInput input{};
  input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
  input.triangleArray.vertexBuffers = &mesh.positions.ptr;
  input.triangleArray.numVertices = (unsigned)mesh.numVerts;
  input.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
  input.triangleArray.vertexStrideInBytes = sizeof(float3);
  input.triangleArray.indexBuffer = mesh.indices.ptr;
  input.triangleArray.numIndexTriplets = (unsigned)mesh.numTris;
  input.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
  input.triangleArray.indexStrideInBytes = sizeof(uint3);
  input.triangleArray.flags = &flags;
  input.triangleArray.numSbtRecords = 1;

  return buildAndCompact(ctx, input);
}

Gas buildIas(OptixDeviceContext ctx, const Scene& scene,
             const std::vector<Gas>& quadricGas, const std::vector<Gas>& meshGas) {
  std::vector<OptixInstance> instances(scene.objects.size());
  for (size_t i = 0; i < scene.objects.size(); i++) {
    const SceneObject& o = scene.objects[i];
    OptixInstance& inst = instances[i];
    std::memset(&inst, 0, sizeof(inst));
    std::memcpy(inst.transform, o.xform.m, sizeof(float) * 12);
    inst.instanceId = (unsigned)i;
    inst.sbtOffset = (unsigned)(2 * i);
    inst.visibilityMask = 0xFF;
    bool masked = o.matFront == MAT_NONE || o.matBack == MAT_NONE || o.cutoutTexId >= 0;
    inst.flags = masked ? OPTIX_INSTANCE_FLAG_NONE : OPTIX_INSTANCE_FLAG_DISABLE_ANYHIT;
    inst.traversableHandle = o.geomKind == GK_MESH ? meshGas[o.meshId].handle
                                                   : quadricGas[o.geomKind].handle;
  }

  CudaBuffer instBuf;
  instBuf.upload(instances);

  OptixBuildInput input{};
  input.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
  input.instanceArray.instances = instBuf.ptr;
  input.instanceArray.numInstances = (unsigned)instances.size();

  return buildAndCompact(ctx, input);
}

}  // namespace sd

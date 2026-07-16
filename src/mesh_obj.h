// sundog: OBJ loading (tinyobjloader) + device upload.
#ifndef SUNDOG_MESH_OBJ_H
#define SUNDOG_MESH_OBJ_H

#include "cuda_check.h"
#include "scene.h"
#include <string>

namespace sd {

struct GpuMesh {
  CudaBuffer positions;  // float3[numVerts]
  CudaBuffer indices;    // uint3[numTris]
  CudaBuffer normals;    // float3[numVerts] or empty (faceted)
  CudaBuffer uvs;        // float2[numVerts] or empty (no OBJ vt)
  size_t numVerts = 0;
  size_t numTris = 0;
  std::vector<float3> hostPositions;  // kept for physics convex cooking
  std::vector<uint3> hostIndices;     // kept for mesh-light CDF build
};

// Loads, optionally computes area-weighted smooth normals, uploads.
// usemtlFilter: empty = all faces; otherwise only faces whose usemtl group
// matches (multi-material OBJs split into per-group sub-meshes this way).
GpuMesh loadObjMesh(const std::string& path, bool smoothNormals,
                    const std::string& usemtlFilter = "");

}  // namespace sd

#endif

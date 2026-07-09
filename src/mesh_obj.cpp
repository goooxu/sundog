#include "mesh_obj.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <stdexcept>
#include <vector>

namespace sd {

GpuMesh loadObjMesh(const std::string& path, bool smoothNormals) {
  tinyobj::ObjReaderConfig cfg;
  cfg.triangulate = true;
  cfg.vertex_color = false;
  tinyobj::ObjReader reader;
  if (!reader.ParseFromFile(path, cfg)) {
    throw std::runtime_error("OBJ load failed: " + path + ": " + reader.Error());
  }

  const auto& attrib = reader.GetAttrib();
  size_t numVerts = attrib.vertices.size() / 3;
  std::vector<float3> positions(numVerts);
  for (size_t i = 0; i < numVerts; i++) {
    positions[i] = f3(attrib.vertices[3 * i], attrib.vertices[3 * i + 1],
                      attrib.vertices[3 * i + 2]);
  }

  std::vector<uint3> indices;
  for (const auto& shape : reader.GetShapes()) {
    const auto& mesh = shape.mesh;
    for (size_t f = 0; f + 2 < mesh.indices.size(); f += 3) {
      uint3 t = {(unsigned)mesh.indices[f].vertex_index,
                 (unsigned)mesh.indices[f + 1].vertex_index,
                 (unsigned)mesh.indices[f + 2].vertex_index};
      // tinyobjloader does not bounds-check positive indices; a corrupt OBJ
      // would otherwise corrupt the heap in the normal pass / OOB on device.
      if (t.x >= numVerts || t.y >= numVerts || t.z >= numVerts) {
        throw std::runtime_error("OBJ has out-of-range vertex index: " + path);
      }
      indices.push_back(t);
    }
  }
  if (indices.empty()) throw std::runtime_error("OBJ has no triangles: " + path);

  GpuMesh gm;
  gm.numVerts = numVerts;
  gm.numTris = indices.size();
  gm.positions.upload(positions);
  gm.indices.upload(indices);

  if (smoothNormals) {
    // Area-weighted vertex normals over the position-indexed mesh (OBJ
    // normals are ignored: bunny-class scans ship without usable ones).
    std::vector<float3> normals(numVerts, f3(0.0f));
    for (const uint3& t : indices) {
      float3 e1 = positions[t.y] - positions[t.x];
      float3 e2 = positions[t.z] - positions[t.x];
      float3 n = cross(e1, e2);  // length ~ 2*area
      normals[t.x] += n;
      normals[t.y] += n;
      normals[t.z] += n;
    }
    for (float3& n : normals) {
      float l = length(n);
      n = l > 1e-12f ? n / l : f3(0.0f, 1.0f, 0.0f);
    }
    gm.normals.upload(normals);
  }
  return gm;
}

}  // namespace sd

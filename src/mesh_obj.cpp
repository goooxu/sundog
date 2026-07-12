#include "mesh_obj.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <cstdio>
#include <stdexcept>
#include <unordered_map>
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
  size_t numPos = attrib.vertices.size() / 3;
  size_t numTc = attrib.texcoords.size() / 2;
  auto pos = [&](unsigned i) {
    return f3(attrib.vertices[3 * i], attrib.vertices[3 * i + 1],
              attrib.vertices[3 * i + 2]);
  };

  // Gather validated corner (vertex, texcoord) index pairs. tinyobjloader
  // does not bounds-check positive indices; a corrupt OBJ would otherwise
  // corrupt the heap in the normal pass / OOB on device.
  std::vector<std::pair<unsigned, int>> corners;
  bool allHaveUv = numTc > 0;
  for (const auto& shape : reader.GetShapes()) {
    const auto& mesh = shape.mesh;
    for (size_t f = 0; f + 2 < mesh.indices.size(); f += 3) {
      for (int k = 0; k < 3; k++) {
        const tinyobj::index_t& ix = mesh.indices[f + k];
        unsigned v = (unsigned)ix.vertex_index;
        if (v >= numPos) {
          throw std::runtime_error("OBJ has out-of-range vertex index: " + path);
        }
        int vt = ix.texcoord_index;
        if (vt >= 0 && (size_t)vt >= numTc) {
          throw std::runtime_error("OBJ has out-of-range texcoord index: " + path);
        }
        if (vt < 0) allHaveUv = false;
        corners.push_back({v, vt});
      }
    }
  }
  if (corners.empty()) throw std::runtime_error("OBJ has no triangles: " + path);
  if (numTc > 0 && !allHaveUv) {
    fprintf(stderr, "[mesh] %s: some faces lack vt — ignoring texture coords\n",
            path.c_str());
  }

  // Build the vertex set. Without (usable) UVs, vertices are the OBJ
  // positions as-is. With UVs, re-index by unique (vertex, texcoord) pair —
  // seam vertices get duplicated so each copy carries its own uv.
  std::vector<float3> positions;
  std::vector<float2> uvs;
  std::vector<unsigned> posOf;  // new vertex -> original position index
  std::vector<uint3> indices(corners.size() / 3);
  if (allHaveUv) {
    std::unordered_map<uint64_t, unsigned> remap;
    remap.reserve(numPos * 2);
    for (size_t c = 0; c < corners.size(); c++) {
      uint64_t key = ((uint64_t)corners[c].first << 32) | (unsigned)corners[c].second;
      auto [it, fresh] = remap.try_emplace(key, (unsigned)positions.size());
      if (fresh) {
        positions.push_back(pos(corners[c].first));
        uvs.push_back(make_float2(attrib.texcoords[2 * corners[c].second],
                                  attrib.texcoords[2 * corners[c].second + 1]));
        posOf.push_back(corners[c].first);
      }
      (&indices[c / 3].x)[c % 3] = it->second;
    }
  } else {
    positions.resize(numPos);
    posOf.resize(numPos);
    for (size_t i = 0; i < numPos; i++) {
      positions[i] = pos((unsigned)i);
      posOf[i] = (unsigned)i;
    }
    for (size_t c = 0; c < corners.size(); c++) {
      (&indices[c / 3].x)[c % 3] = corners[c].first;
    }
  }

  GpuMesh gm;
  gm.numVerts = positions.size();
  gm.numTris = indices.size();
  gm.hostPositions = positions;
  gm.positions.upload(positions);
  gm.indices.upload(indices);
  if (allHaveUv) gm.uvs.upload(uvs);

  if (smoothNormals) {
    // Area-weighted vertex normals, aggregated by ORIGINAL position index so
    // uv-seam duplicates still shade smoothly, then scattered to duplicates.
    // (OBJ normals are ignored: scan-class assets ship without usable ones.)
    std::vector<float3> accum(numPos, f3(0.0f));
    for (const uint3& t : indices) {
      float3 e1 = positions[t.y] - positions[t.x];
      float3 e2 = positions[t.z] - positions[t.x];
      float3 n = cross(e1, e2);  // length ~ 2*area
      accum[posOf[t.x]] += n;
      accum[posOf[t.y]] += n;
      accum[posOf[t.z]] += n;
    }
    std::vector<float3> normals(positions.size());
    for (size_t i = 0; i < positions.size(); i++) {
      float3 n = accum[posOf[i]];
      float l = length(n);
      normals[i] = l > 1e-12f ? n / l : f3(0.0f, 1.0f, 0.0f);
    }
    gm.normals.upload(normals);
  }
  return gm;
}

}  // namespace sd

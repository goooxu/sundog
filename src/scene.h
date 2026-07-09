// sundog: host-side scene representation (parsed from JSON, then uploaded).
#ifndef SUNDOG_SCENE_H
#define SUNDOG_SCENE_H

#include "params.h"
#include <string>
#include <vector>

namespace sd {

struct RenderSettings {
  int width = 1280, height = 720;
  int spp = 64;
  int maxDepth = 16;
  float clampVal = 50.0f;
  unsigned seed = 7;
  float gamma = 2.2f;
  float exposure = 0.0f;  // EV
  int chunk = 16;
  bool denoise = false;
  bool parity = false;
};

struct CameraSettings {
  float3 lookfrom{0, 2, 8}, lookat{0, 0, 0}, up{0, 1, 0};
  float vfov = 40.0f;
  float aperture = 0.0f;
  float focusDist = 0.0f;  // 0 = |lookfrom-lookat|
};

struct SceneTexture {
  TextureDesc desc{};       // desc.tex filled at upload time for TX_IMAGE
  std::string imageFile;    // TX_IMAGE source path (resolved)
  bool srgb = true;
  bool nearest = false;     // point sampling (procedural-pattern images)
};

struct SceneMesh {
  std::string objFile;
  bool smoothNormals = true;
};

struct SceneObject {
  int geomKind = GK_SPHERE;
  int meshId = -1;                 // GK_MESH
  uint16_t matFront = MAT_NONE, matBack = MAT_NONE;
  int cutoutTexId = -1;
  Affine xform = Affine::identity();
  int lightId = -1;                // filled when auto-registered as NEE light
  bool nee = true;
};

struct Scene {
  RenderSettings render;
  CameraSettings camera;
  BgDesc bg{BG_SOLID, {0, 0, 0}, {0, 0, 0}};
  std::vector<SceneTexture> textures;
  std::vector<MaterialDesc> materials;
  std::vector<SceneMesh> meshes;
  std::vector<SceneObject> objects;
  std::vector<LightDesc> lights;
  std::string baseDir;  // for resolving relative asset paths
};

// scene_json.cpp
Scene loadScene(const std::string& path);

// Build the device CameraData the same way cxxrt's Camera did.
CameraData makeCamera(const CameraSettings& cs, int width, int height);

}  // namespace sd

#endif

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

// Rigid-body settling (PhysX GPU, src/physics.cpp). Parsing stays PhysX-free:
// these are plain descriptors; the simulation runs after mesh load and bakes
// final poses back into each object's xform before accel builds.
struct PhysicsObject {
  bool enabled = false;   // object has a "physics" key
  bool dynamic = false;   // false = static collider
  float density = 250.0f;
  float3 velocity{0, 0, 0};
  float3 angularVelocity{0, 0, 0};
  float friction = -1.0f, restitution = -1.0f;  // <0 = scene default
  float thickness = 0.2f;  // rect collider: slab depth behind the +Y face
};

struct PhysicsSettings {
  bool enabled = false;  // scene has a "physics" block
  float3 gravity{0.0f, -9.81f, 0.0f};
  float timestep = 1.0f / 240.0f;
  float maxTime = 15.0f;  // sim-time cap; bake anyway on timeout
  float friction = 0.6f, restitution = 0.1f;
  int posIters = 8, velIters = 2;
  float sleepThreshold = -1.0f;  // <0 = PhysX default
};

struct SceneObject {
  int geomKind = GK_SPHERE;
  int meshId = -1;                 // GK_MESH
  uint16_t matFront = MAT_NONE, matBack = MAT_NONE;
  int cutoutTexId = -1;
  Affine xform = Affine::identity();
  int lightId = -1;                // filled when auto-registered as NEE light
  bool nee = true;
  PhysicsObject physics;
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
  PhysicsSettings physics;
  std::string baseDir;  // for resolving relative asset paths
};

// scene_json.cpp
Scene loadScene(const std::string& path);

// Build the device CameraData (thin-lens look-at construction).
CameraData makeCamera(const CameraSettings& cs, int width, int height);

}  // namespace sd

#endif

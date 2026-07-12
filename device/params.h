// sundog: launch parameters and scene descriptors shared by host and device.
#ifndef SUNDOG_PARAMS_H
#define SUNDOG_PARAMS_H

#include "math.cuh"
#include <stdint.h>

#include <optix.h>

namespace sd {

enum GeomKind : int { GK_SPHERE = 0, GK_RECT, GK_DISK, GK_CYLINDER, GK_PARABOLA, GK_MESH, GK_COUNT };
enum TexKind : int { TX_SOLID = 0, TX_IMAGE, TX_CHECKER, TX_GRID };
enum MatKind : int { MT_LAMBERT = 0, MT_METAL, MT_DIELECTRIC, MT_EMISSIVE };
enum LightKind : int { LT_RECT = 0, LT_DISK, LT_SPHERE, LT_POINT, LT_DISTANT };
enum BgKind : int { BG_SOLID = 0, BG_GRADIENT };

constexpr uint16_t MAT_NONE = 0xFFFF;  // pass-through face (no material)

struct TextureDesc {
  int kind;                 // TexKind
  float3 a, b;              // solid color / checker-grid colors
  float2 scale;             // checker/grid tiling
  float width;              // grid line width in cell fraction
  cudaTextureObject_t tex;  // TX_IMAGE
};

struct MaterialDesc {
  int kind;        // MatKind
  int texId;       // -1 -> use .color
  float3 color;
  float roughness; // metal (GGX alpha = roughness^2); <1e-3 -> delta mirror
  float ior;       // dielectric
  float intensity; // emissive scale
  int twoSided;    // emissive visible from both faces
};

struct LightDesc {
  int kind;      // LightKind
  float3 p;      // center / position (world)
  float3 u, v;   // area lights: world half-edge basis (rect/disk); unused otherwise
  float3 n;      // area lights: unit front normal
  float3 L;      // emitted radiance (area) / radiant intensity (point) / radiance
                 // (distant); textured area lights: intensity only (see texId)
  float radius;  // sphere/point
  float area;    // rect/disk world-space area
  float3 dir;    // distant: unit direction the light travels (from light)
  int twoSided;  // rect/disk emitters
  int texId;     // rect/disk emitters with a texture: Li = tex(uv) * L; -1 = none
};

struct BgDesc {
  int kind;        // BgKind
  float3 a;        // solid color / horizon
  float3 b;        // zenith
};

// Emissive participating medium (emission + absorption, no scattering):
// an upright procedural flame bounded by the cylinder {|xz - base.xz| <= radius,
// base.y <= y <= base.y + height}, ray-marched in raygen (device/volume.cuh).
struct FlameDesc {
  float3 base;       // world position of the flame root (axis is +y)
  float height;
  float radius;      // bounding-cylinder radius (profile stays inside)
  float intensity;   // emission scale
  float sigma;       // absorption density scale
  float noiseScale;  // fbm frequency
  unsigned seed;     // decorrelates multiple flames
};

struct CameraData {
  float3 origin;
  float3 lowerLeft;   // world position of image plane (0,0) corner point
  float3 horizontal;  // full-width vector along image plane
  float3 vertical;    // full-height vector
  float3 u, v, w;     // camera basis (right, up, backward)
  float lensRadius;
};

struct LaunchParams {
  int width, height;
  int sppTotal;       // for stratification
  int sppThisLaunch;
  int sampleOffset;   // global index of first sample this launch
  int maxDepth;
  float clampVal;     // 0 = off; applied to indirect (depth>=1) contributions
  unsigned int seed;
  int countRays;      // 1 = atomicAdd into rayCounter (stats runs only)

  float4* accum;      // running-mean linear HDR beauty
  float4* aovAlbedo;  // denoiser guide
  float4* aovNormal;  // denoiser guide (camera space)
  unsigned long long* rayCounter;

  CameraData cam;
  BgDesc bg;

  TextureDesc* textures;
  MaterialDesc* materials;
  LightDesc* lights;
  int numLights;
  FlameDesc* flames;
  int numFlames;

  OptixTraversableHandle handle;
};

// SBT hitgroup record payload (per instance x ray type).
struct HitRecordData {
  int geomKind;             // GeomKind
  // Mesh buffers (GK_MESH only)
  float3* positions;
  uint3* indices;
  float3* normals;          // per-vertex, may be null (use geometric)
  float2* uvs;              // per-vertex from OBJ vt (seam-duplicated verts);
                            // null -> device falls back to barycentrics
  uint16_t matFront, matBack;  // MAT_NONE = pass-through
  int lightId;              // index into params.lights, -1 = not an NEE light
  int cutoutTexId;          // texture id with alpha mask, -1 = none
};

}  // namespace sd

#endif

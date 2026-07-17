// sundog: C API — scene construction surface (no CUDA calls in this TU; host
// tests compile it standalone). Thin shims: narrow at the boundary, fill the
// sd::Scene structs, delegate every derived decision to scene_build.cpp.
// Renderer defaults live HERE (and only here) — mirroring what the old JSON
// loader applied when a key was absent.
#include "capi_internal.h"
#include "scene_build.h"
#include "tonemap.h"

#include <algorithm>
#include <cstring>
#include <vector>

using namespace sd;
using namespace sd_capi;

namespace sd_capi {
static thread_local std::string t_lastError;
void setLastError(const std::string& msg) { t_lastError = msg; }
}  // namespace sd_capi

extern "C" const char* sundog_last_error(void) { return t_lastError.c_str(); }

extern "C" sundog_scene* sundog_scene_create(const char* base_dir) {
  SUNDOG_TRY
  sundog_scene* h = new sundog_scene();
  h->scene.baseDir = (base_dir && *base_dir) ? base_dir : ".";
  return h;
  SUNDOG_CATCH(nullptr)
}

extern "C" void sundog_scene_destroy(sundog_scene* h) { delete h; }

extern "C" int sundog_set_render(sundog_scene* h, int width, int height, int spp,
                                 int max_depth, double clamp, int64_t seed,
                                 double gamma, double exposure, int tonemap,
                                 int transparent_shadows) {
  SUNDOG_TRY
  phaseAdvance(h, 0, "render settings");
  RenderSettings& r = h->scene.render;
  if (width > 0) r.width = width;
  if (height > 0) r.height = height;
  if (spp > 0) r.spp = spp;
  if (max_depth > 0) r.maxDepth = max_depth;
  if (isset(clamp)) r.clampVal = (float)clamp;
  if (seed >= 0) r.seed = (unsigned)seed;
  if (isset(gamma)) r.gamma = (float)gamma;
  if (isset(exposure)) r.exposure = (float)exposure;
  if (tonemap == SUNDOG_TM_ACES) r.tonemap = TM_ACES;
  else if (tonemap == SUNDOG_TM_CLAMP) r.tonemap = TM_CLAMP;
  else if (tonemap != -1) sceneFail("render: unknown tonemap value");
  if (transparent_shadows >= 0) r.transparentShadows = transparent_shadows == 1;
  return SUNDOG_OK;
  SUNDOG_CATCH(SUNDOG_ERROR)
}

extern "C" int sundog_set_camera(sundog_scene* h, const double lookfrom[3],
                                 const double lookat[3], const double up[3],
                                 double vfov, double aperture, double focus_dist) {
  SUNDOG_TRY
  phaseAdvance(h, 0, "camera");
  if (!lookfrom || !lookat) sceneFail("camera: lookfrom and lookat are required");
  CameraSettings& c = h->scene.camera;
  c.lookfrom = nf3(lookfrom, c.lookfrom);
  c.lookat = nf3(lookat, c.lookat);
  c.up = nf3(up, c.up);
  c.vfov = nf(vfov, 40.0f);
  c.aperture = nf(aperture, 0.0f);
  c.focusDist = nf(focus_dist, 0.0f);
  h->hasCamera = true;
  return SUNDOG_OK;
  SUNDOG_CATCH(SUNDOG_ERROR)
}

extern "C" int sundog_set_background_solid(sundog_scene* h, const double color[3]) {
  SUNDOG_TRY
  phaseAdvance(h, 0, "background");
  if (!color) sceneFail("background: color is required");
  h->scene.bg.kind = BG_SOLID;
  h->scene.bg.a = nf3(color, f3(0.0f));
  return SUNDOG_OK;
  SUNDOG_CATCH(SUNDOG_ERROR)
}

extern "C" int sundog_set_background_gradient(sundog_scene* h,
                                              const double horizon[3],
                                              const double zenith[3]) {
  SUNDOG_TRY
  phaseAdvance(h, 0, "background");
  if (!horizon || !zenith) sceneFail("background: horizon and zenith are required");
  h->scene.bg.kind = BG_GRADIENT;
  h->scene.bg.a = nf3(horizon, f3(0.0f));
  h->scene.bg.b = nf3(zenith, f3(0.0f));
  return SUNDOG_OK;
  SUNDOG_CATCH(SUNDOG_ERROR)
}

extern "C" int sundog_set_background_envmap(sundog_scene* h, const char* file,
                                            double rotate_deg, double intensity,
                                            int importance) {
  SUNDOG_TRY
  phaseAdvance(h, 0, "background");
  if (!file || !*file) sceneFail("envmap: file is required");
  h->scene.bg.kind = BG_ENVMAP;
  h->scene.env.file = file;
  h->scene.env.rotateDeg = nf(rotate_deg, 0.0f);
  h->scene.env.intensity = nf(intensity, 1.0f);
  h->scene.env.importance = importance != 0;  // -1/1 -> true, 0 -> false
  if (h->scene.env.intensity < 0.0f) sceneFail("envmap: intensity must be >= 0");
  return SUNDOG_OK;
  SUNDOG_CATCH(SUNDOG_ERROR)
}

extern "C" int sundog_set_physics(sundog_scene* h, const double gravity[3],
                                  double timestep, double max_time,
                                  double friction, double restitution,
                                  int pos_iters, int vel_iters,
                                  double sleep_threshold, double stop_time) {
  SUNDOG_TRY
  phaseAdvance(h, 0, "physics settings");
  PhysicsSettings& p = h->scene.physics;
  p.enabled = true;
  p.gravity = nf3(gravity, p.gravity);
  p.timestep = nf(timestep, p.timestep);
  p.maxTime = nf(max_time, p.maxTime);
  p.friction = nf(friction, p.friction);
  p.restitution = nf(restitution, p.restitution);
  if (pos_iters >= 0) p.posIters = pos_iters;
  if (vel_iters >= 0) p.velIters = vel_iters;
  p.sleepThreshold = nf(sleep_threshold, p.sleepThreshold);
  p.stopTime = nf(stop_time, p.stopTime);
  if (p.timestep <= 0.0f || p.maxTime <= 0.0f)
    sceneFail("physics: timestep and max_time must be positive");
  if (p.stopTime < 0.0f)
    sceneFail("physics: stop_time must be >= 0 (0 = settle to sleep)");
  return SUNDOG_OK;
  SUNDOG_CATCH(SUNDOG_ERROR)
}

// ---- registries --------------------------------------------------------------

static int pushTexture(sundog_scene* h, const SceneTexture& st, const char* what) {
  phaseAdvance(h, 0, what);
  int id = (int)h->scene.textures.size();
  h->scene.textures.push_back(st);
  return id;
}

extern "C" int sundog_add_texture_solid(sundog_scene* h, const double color[3]) {
  SUNDOG_TRY
  if (!color) sceneFail("texture: color is required");
  SceneTexture st;
  st.desc.width = 0.05f;
  st.desc.kind = TX_SOLID;
  st.desc.a = nf3(color, f3(0.0f));
  return pushTexture(h, st, "textures");
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_texture_checker(sundog_scene* h, const double a[3],
                                          const double b[3], const double scale[2]) {
  SUNDOG_TRY
  if (!a || !b) sceneFail("texture: a and b are required");
  SceneTexture st;
  st.desc.width = 0.05f;
  st.desc.kind = TX_CHECKER;
  st.desc.a = nf3(a, f3(0.0f));
  st.desc.b = nf3(b, f3(0.0f));
  st.desc.scale = scale ? make_float2((float)scale[0], (float)scale[1])
                        : make_float2(8.0f, 8.0f);
  return pushTexture(h, st, "textures");
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_texture_grid(sundog_scene* h, const double a[3],
                                       const double b[3], const double scale[2],
                                       double width) {
  SUNDOG_TRY
  if (!a || !b) sceneFail("texture: a and b are required");
  SceneTexture st;
  st.desc.kind = TX_GRID;
  st.desc.a = nf3(a, f3(0.0f));
  st.desc.b = nf3(b, f3(0.0f));
  st.desc.scale = scale ? make_float2((float)scale[0], (float)scale[1])
                        : make_float2(8.0f, 8.0f);
  st.desc.width = nf(width, 0.05f);
  return pushTexture(h, st, "textures");
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_texture_image(sundog_scene* h, const char* file, int srgb) {
  SUNDOG_TRY
  if (!file || !*file) sceneFail("texture: file is required");
  SceneTexture st;
  st.desc.width = 0.05f;
  st.desc.kind = TX_IMAGE;
  st.imageFile = file;
  st.srgb = srgb != 0;  // -1/1 -> true, 0 -> false
  return pushTexture(h, st, "textures");
  SUNDOG_CATCH(-1)
}

static MaterialDesc baseMaterial() {
  MaterialDesc md{};
  md.texId = -1;
  md.color = f3(0.8f);
  md.ior = 1.5f;
  md.roughness = 0.0f;
  md.intensity = 1.0f;
  md.twoSided = 0;
  return md;
}

static int pushMaterial(sundog_scene* h, const MaterialDesc& md, int texId) {
  phaseAdvance(h, 0, "materials");
  if (texId != -1 && (texId < 0 || texId >= (int)h->scene.textures.size()))
    sceneFail("material: texture id out of range");
  if (h->scene.materials.size() >= MAT_NONE) sceneFail("too many materials");
  int id = (int)h->scene.materials.size();
  h->scene.materials.push_back(md);
  return id;
}

extern "C" int sundog_add_material_lambert(sundog_scene* h, const double color[3],
                                           int tex_id) {
  SUNDOG_TRY
  MaterialDesc md = baseMaterial();
  md.kind = MT_LAMBERT;
  md.color = nf3(color, md.color);
  md.texId = tex_id;
  return pushMaterial(h, md, tex_id);
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_material_metal(sundog_scene* h, const double color[3],
                                         int tex_id, double roughness) {
  SUNDOG_TRY
  MaterialDesc md = baseMaterial();
  md.kind = MT_METAL;
  md.color = nf3(color, md.color);
  md.texId = tex_id;
  md.roughness = nf(roughness, md.roughness);
  return pushMaterial(h, md, tex_id);
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_material_dielectric(sundog_scene* h, double ior,
                                              const double absorb[3],
                                              const double color[3], int tex_id,
                                              double roughness) {
  SUNDOG_TRY
  MaterialDesc md = baseMaterial();
  md.kind = MT_DIELECTRIC;
  md.color = nf3(color, md.color);
  md.texId = tex_id;
  md.ior = nf(ior, md.ior);
  md.roughness = nf(roughness, md.roughness);  // NaN -> 0 = smooth delta glass
  if (absorb) {
    // Beer-Lambert absorption inside the glass (tinted transparent shadows
    // and interiors); vacuum-clear when absent.
    md.absorb = nf3(absorb, f3(0.0f));
    if (md.absorb.x < 0.0f || md.absorb.y < 0.0f || md.absorb.z < 0.0f)
      sceneFail("dielectric: absorb components must be >= 0");
  }
  return pushMaterial(h, md, tex_id);
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_material_emissive(sundog_scene* h, const double color[3],
                                            int tex_id, double intensity,
                                            int two_sided) {
  SUNDOG_TRY
  MaterialDesc md = baseMaterial();
  md.kind = MT_EMISSIVE;
  md.color = nf3(color, md.color);
  md.texId = tex_id;
  md.intensity = nf(intensity, md.intensity);
  md.twoSided = two_sided == 1 ? 1 : 0;
  return pushMaterial(h, md, tex_id);
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_material_water(sundog_scene* h, double ior,
                                         const double absorb[3], double wave_amp,
                                         double wave_freq, const double color[3]) {
  SUNDOG_TRY
  MaterialDesc md = baseMaterial();
  md.kind = MT_WATER;
  // Smooth dielectric interface at 1.33 + fbm wave normals + Beer-Lambert
  // absorption inside (red dies first -> depth shifts blue-green).
  md.ior = nf(ior, 1.33f);
  md.absorb = nf3(absorb, f3(0.45f, 0.08f, 0.035f));
  md.waveAmp = nf(wave_amp, 0.05f);
  md.waveFreq = nf(wave_freq, 2.0f);
  md.color = nf3(color, f3(1.0f));  // AOV guide only
  md.texId = -1;
  if (md.waveAmp < 0.0f || md.waveFreq < 0.0f)
    sceneFail("water: wave_amp and wave_freq must be >= 0");
  if (md.absorb.x < 0.0f || md.absorb.y < 0.0f || md.absorb.z < 0.0f)
    sceneFail("water: absorb components must be >= 0");
  return pushMaterial(h, md, -1);
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_material_plastic(sundog_scene* h, const double color[3],
                                           int tex_id, double roughness) {
  SUNDOG_TRY
  MaterialDesc md = baseMaterial();
  // Diffuse base under a GGX dielectric coat; ior stays baseMaterial's 1.5
  // (the coat Fresnel, f0 = 0.04). Roughness is stored verbatim — the device
  // floors it at 1e-3 so the coat never degenerates to a delta lobe.
  md.kind = MT_PLASTIC;
  md.color = nf3(color, md.color);
  md.texId = tex_id;
  md.roughness = nf(roughness, 0.15f);
  return pushMaterial(h, md, tex_id);
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_mesh(sundog_scene* h, const char* obj_file,
                               int smooth_normals, const char* usemtl) {
  SUNDOG_TRY
  phaseAdvance(h, 0, "meshes");
  if (!obj_file || !*obj_file) sceneFail("mesh: obj file is required");
  SceneMesh sm;
  sm.objFile = obj_file;
  sm.usemtl = usemtl ? usemtl : "";
  sm.smoothNormals = smooth_normals != 0;  // -1/1 -> true, 0 -> false
  int id = (int)h->scene.meshes.size();
  h->scene.meshes.push_back(sm);
  return id;
  SUNDOG_CATCH(-1)
}

// ---- objects / flames / lights ------------------------------------------------

extern "C" int sundog_add_object(sundog_scene* h, int geom_kind, int mesh_id,
                                 int mat_front, int mat_back, int cutout_tex_id,
                                 const sundog_xform_step* steps, int num_steps,
                                 int nee, const sundog_physics_body* physics) {
  SUNDOG_TRY
  phaseAdvance(h, 1, "objects");
  SceneObject so;
  if (geom_kind < 0 || geom_kind > GK_MESH) sceneFail("object: unknown shape");
  so.geomKind = geom_kind;
  if (geom_kind == GK_MESH) {
    if (mesh_id < 0 || mesh_id >= (int)h->scene.meshes.size())
      sceneFail("object: mesh id out of range");
    so.meshId = mesh_id;
  }
  auto matIdOf = [&](int id) -> uint16_t {
    if (id == SUNDOG_MAT_NONE) return MAT_NONE;
    if (id < 0 || id >= (int)h->scene.materials.size())
      sceneFail("object: material id out of range");
    return (uint16_t)id;
  };
  so.matFront = matIdOf(mat_front);
  so.matBack = mat_back == SUNDOG_MAT_DEFAULT ? so.matFront : matIdOf(mat_back);
  if (cutout_tex_id != -1) {
    if (cutout_tex_id < 0 || cutout_tex_id >= (int)h->scene.textures.size())
      sceneFail("object: cutout texture id out of range");
    so.cutoutTexId = cutout_tex_id;
  }

  if (num_steps > 0 && !steps) sceneFail("object: transform steps missing");
  std::vector<XformStep> xs((size_t)std::max(num_steps, 0));
  for (int i = 0; i < num_steps; i++) {
    xs[i].kind = steps[i].kind;
    xs[i].a = (float)steps[i].a;  // narrow at the boundary, before any math
    xs[i].b = (float)steps[i].b;
    xs[i].c = (float)steps[i].c;
  }
  so.xform = composeTransform(xs.data(), num_steps);
  so.nee = nee != 0;  // -1/1 -> true, 0 -> false

  if (physics) {
    so.physics.enabled = true;
    so.physics.dynamic = physics->dynamic == 1;
    so.physics.density = nf(physics->density, so.physics.density);
    if (physics->has_velocity)
      so.physics.velocity = nf3(physics->velocity, so.physics.velocity);
    if (physics->has_angular_velocity)
      so.physics.angularVelocity =
          nf3(physics->angular_velocity, so.physics.angularVelocity);
    so.physics.friction = nf(physics->friction, so.physics.friction);
    so.physics.restitution = nf(physics->restitution, so.physics.restitution);
    so.physics.thickness = nf(physics->thickness, so.physics.thickness);
  }

  int id = (int)h->scene.objects.size();
  addObjectDerived(h->scene, so);  // constraints + NEE light registration + push
  return id;
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_flame(sundog_scene* h, const double base[3],
                                double height, double radius, double intensity,
                                double sigma, double noise_scale, int64_t seed,
                                double light_intensity) {
  SUNDOG_TRY
  phaseAdvance(h, 2, "flames");
  if (!base) sceneFail("flame: base is required");
  FlameDesc fd{};
  fd.base = nf3(base, f3(0.0f));
  fd.height = (float)height;
  fd.radius = (float)radius;
  fd.intensity = nf(intensity, 20.0f);
  fd.sigma = nf(sigma, 4.0f);
  fd.noiseScale = nf(noise_scale, 3.0f);
  fd.seed = seed >= 0 ? (unsigned)seed : 0u;
  float lightI = nf(light_intensity, 12.0f);
  int id = (int)h->scene.flames.size();
  addFlameDerived(h->scene, fd, lightI);  // domain checks + 2 point lights + push
  return id;
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_point_light(sundog_scene* h, const double position[3],
                                      const double intensity[3], double radius) {
  SUNDOG_TRY
  phaseAdvance(h, 3, "lights");
  if (!position || !intensity) sceneFail("point light: position and intensity are required");
  LightDesc ld{};
  ld.texId = -1;
  ld.flameId = -1;
  ld.kind = LT_POINT;
  ld.p = nf3(position, f3(0.0f));
  ld.radius = nf(radius, 0.0f);
  ld.L = nf3(intensity, f3(0.0f));
  int id = (int)h->scene.lights.size();
  h->scene.lights.push_back(ld);
  return id;
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_add_distant_light(sundog_scene* h, const double direction[3],
                                        const double radiance[3]) {
  SUNDOG_TRY
  phaseAdvance(h, 3, "lights");
  if (!direction || !radiance) sceneFail("distant light: direction and radiance are required");
  LightDesc ld{};
  ld.texId = -1;
  ld.flameId = -1;
  ld.kind = LT_DISTANT;
  ld.dir = normalize(nf3(direction, f3(0.0f)));
  ld.L = nf3(radiance, f3(0.0f));
  int id = (int)h->scene.lights.size();
  h->scene.lights.push_back(ld);
  return id;
  SUNDOG_CATCH(-1)
}

extern "C" int sundog_scene_validate(sundog_scene* h) {
  SUNDOG_TRY
  if (!h->hasCamera) sceneFail("missing camera");
  return SUNDOG_OK;
  SUNDOG_CATCH(SUNDOG_ERROR)
}

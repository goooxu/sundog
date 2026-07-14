// sundog: JSON scene loader. Validates references, composes object-space
// transform chains into one affine, and auto-registers emissive rect/disk/
// sphere objects as NEE area lights.
#include "scene.h"

#include "json.hpp"
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>

using nlohmann::json;

namespace sd {

static float3 jf3(const json& j) {
  if (!j.is_array() || j.size() != 3) throw std::runtime_error("expected [x,y,z]: " + j.dump());
  return f3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

static void fail(const std::string& msg) { throw std::runtime_error("scene: " + msg); }

CameraData makeCamera(const CameraSettings& cs, int width, int height) {
  CameraData c{};
  float aspect = (float)width / (float)height;
  float theta = cs.vfov * SD_PI / 180.0f;
  float halfH = tanf(theta / 2.0f);
  float halfW = aspect * halfH;
  float focus = cs.focusDist > 0.0f ? cs.focusDist : length(cs.lookfrom - cs.lookat);
  c.w = normalize(cs.lookfrom - cs.lookat);
  c.u = normalize(cross(cs.up, c.w));
  c.v = cross(c.w, c.u);
  c.origin = cs.lookfrom;
  c.lowerLeft = c.origin - halfW * focus * c.u - halfH * focus * c.v - focus * c.w;
  c.horizontal = 2.0f * halfW * focus * c.u;
  c.vertical = 2.0f * halfH * focus * c.v;
  c.lensRadius = cs.aperture / 2.0f;
  return c;
}

static Affine parseTransform(const json& arr) {
  Affine m = Affine::identity();
  if (arr.is_null()) return m;
  if (!arr.is_array()) fail("transform must be a list of steps");
  const float d2r = SD_PI / 180.0f;
  for (const auto& e : arr) {
    if (!e.is_object() || e.size() != 1) fail("each transform step must be a single-key object");
    const auto it = e.begin();
    const std::string& k = it.key();
    Affine step;
    if (k == "scale") {
      if (it.value().is_number()) step = affineScale(f3(it.value().get<float>()));
      else step = affineScale(jf3(it.value()));
    } else if (k == "translate") {
      step = affineTranslate(jf3(it.value()));
    } else if (k == "rotate_x") {
      step = affineRotateX(it.value().get<float>() * d2r);
    } else if (k == "rotate_y") {
      step = affineRotateY(it.value().get<float>() * d2r);
    } else if (k == "rotate_z") {
      step = affineRotateZ(it.value().get<float>() * d2r);
    } else {
      fail("unknown transform step '" + k + "'");
    }
    m = mul(step, m);  // top-down: later steps wrap earlier ones
  }
  return m;
}

Scene loadScene(const std::string& path) {
  std::ifstream in(path);
  if (!in) fail("cannot open " + path);
  json j;
  try {
    j = json::parse(in);
  } catch (const std::exception& e) {
    fail(std::string("JSON parse error in ") + path + ": " + e.what());
  }

  Scene s;
  size_t slash = path.find_last_of('/');
  s.baseDir = slash == std::string::npos ? "." : path.substr(0, slash);

  if (j.contains("render")) {
    const auto& r = j["render"];
    s.render.width = r.value("width", s.render.width);
    s.render.height = r.value("height", s.render.height);
    s.render.spp = r.value("spp", s.render.spp);
    s.render.maxDepth = r.value("max_depth", s.render.maxDepth);
    s.render.clampVal = r.value("clamp", s.render.clampVal);
    s.render.seed = r.value("seed", s.render.seed);
    s.render.gamma = r.value("gamma", s.render.gamma);
    s.render.exposure = r.value("exposure", s.render.exposure);
  }

  if (j.contains("physics")) {
    const auto& p = j["physics"];
    s.physics.enabled = true;
    if (p.contains("gravity")) s.physics.gravity = jf3(p["gravity"]);
    s.physics.timestep = p.value("timestep", s.physics.timestep);
    s.physics.maxTime = p.value("max_time", s.physics.maxTime);
    s.physics.friction = p.value("friction", s.physics.friction);
    s.physics.restitution = p.value("restitution", s.physics.restitution);
    if (p.contains("solver_iterations")) {
      const auto& si = p["solver_iterations"];
      if (!si.is_array() || si.size() != 2) fail("physics: solver_iterations must be [pos, vel]");
      s.physics.posIters = si[0].get<int>();
      s.physics.velIters = si[1].get<int>();
    }
    s.physics.sleepThreshold = p.value("sleep_threshold", s.physics.sleepThreshold);
    s.physics.stopTime = p.value("stop_time", s.physics.stopTime);
    if (s.physics.timestep <= 0.0f || s.physics.maxTime <= 0.0f)
      fail("physics: timestep and max_time must be positive");
    if (s.physics.stopTime < 0.0f)
      fail("physics: stop_time must be >= 0 (0 = settle to sleep)");
  }

  if (!j.contains("camera")) fail("missing camera");
  {
    const auto& c = j["camera"];
    s.camera.lookfrom = jf3(c.at("lookfrom"));
    s.camera.lookat = jf3(c.at("lookat"));
    if (c.contains("up")) s.camera.up = jf3(c["up"]);
    s.camera.vfov = c.value("vfov", 40.0f);
    s.camera.aperture = c.value("aperture", 0.0f);
    s.camera.focusDist = c.value("focus_dist", 0.0f);
  }

  if (j.contains("background")) {
    const auto& b = j["background"];
    std::string kind = b.value("type", "solid");
    if (kind == "solid") {
      s.bg.kind = BG_SOLID;
      s.bg.a = jf3(b.at("color"));
    } else if (kind == "gradient") {
      s.bg.kind = BG_GRADIENT;
      s.bg.a = jf3(b.at("horizon"));
      s.bg.b = jf3(b.at("zenith"));
    } else if (kind == "envmap") {
      s.bg.kind = BG_ENVMAP;
      s.env.file = b.at("file").get<std::string>();
      s.env.rotateDeg = b.value("rotate", 0.0f);
      s.env.intensity = b.value("intensity", 1.0f);
      s.env.importance = b.value("importance", true);
      if (s.env.intensity < 0.0f) fail("envmap: intensity must be >= 0");
    } else {
      fail("unknown background type '" + kind + "'");
    }
  }

  std::map<std::string, int> texIds;
  if (j.contains("textures")) {
    for (const auto& [name, t] : j["textures"].items()) {
      SceneTexture st;
      std::string kind = t.value("type", "solid");
      st.desc.width = 0.05f;
      if (kind == "solid") {
        st.desc.kind = TX_SOLID;
        st.desc.a = jf3(t.at("color"));
      } else if (kind == "checker") {
        st.desc.kind = TX_CHECKER;
        st.desc.a = jf3(t.at("a"));
        st.desc.b = jf3(t.at("b"));
        st.desc.scale = make_float2(t.value("scale", json::array({8, 8}))[0].get<float>(),
                                    t.value("scale", json::array({8, 8}))[1].get<float>());
      } else if (kind == "grid") {
        st.desc.kind = TX_GRID;
        st.desc.a = jf3(t.at("a"));
        st.desc.b = jf3(t.at("b"));
        auto sc = t.value("scale", json::array({8, 8}));
        st.desc.scale = make_float2(sc[0].get<float>(), sc[1].get<float>());
        st.desc.width = t.value("width", 0.05f);
      } else if (kind == "image") {
        st.desc.kind = TX_IMAGE;
        st.imageFile = t.at("file").get<std::string>();
        st.srgb = t.value("srgb", true);
      } else {
        fail("unknown texture type '" + kind + "'");
      }
      texIds[name] = (int)s.textures.size();
      s.textures.push_back(st);
    }
  }
  auto texId = [&](const json& v, const char* ctx) -> int {
    std::string name = v.get<std::string>();
    auto it = texIds.find(name);
    if (it == texIds.end()) fail(std::string(ctx) + ": unknown texture '" + name + "'");
    return it->second;
  };

  std::map<std::string, int> matIds;
  if (!j.contains("materials")) fail("missing materials");
  for (const auto& [name, m] : j["materials"].items()) {
    MaterialDesc md{};
    md.texId = -1;
    md.color = f3(0.8f);
    md.ior = 1.5f;
    md.roughness = 0.0f;
    md.intensity = 1.0f;
    md.twoSided = 0;
    std::string kind = m.at("type").get<std::string>();
    if (kind == "lambert") md.kind = MT_LAMBERT;
    else if (kind == "metal") md.kind = MT_METAL;
    else if (kind == "dielectric") md.kind = MT_DIELECTRIC;
    else if (kind == "emissive") md.kind = MT_EMISSIVE;
    else if (kind == "water") md.kind = MT_WATER;
    else fail("unknown material type '" + kind + "'");
    if (m.contains("color")) md.color = jf3(m["color"]);
    if (m.contains("texture")) md.texId = texId(m["texture"], name.c_str());
    md.roughness = m.value("roughness", md.roughness);
    md.ior = m.value("ior", md.ior);
    md.intensity = m.value("intensity", md.intensity);
    md.twoSided = m.value("two_sided", false) ? 1 : 0;
    if (md.kind == MT_WATER) {
      // Smooth dielectric interface at 1.33 + fbm wave normals + Beer-Lambert
      // absorption inside (red dies first -> depth shifts blue-green).
      if (!m.contains("ior")) md.ior = 1.33f;
      md.absorb = m.contains("absorb") ? jf3(m["absorb"]) : f3(0.45f, 0.08f, 0.035f);
      md.waveAmp = m.value("wave_amp", 0.05f);
      md.waveFreq = m.value("wave_freq", 2.0f);
      if (md.waveAmp < 0.0f || md.waveFreq < 0.0f)
        fail("water: wave_amp and wave_freq must be >= 0");
      if (md.absorb.x < 0.0f || md.absorb.y < 0.0f || md.absorb.z < 0.0f)
        fail("water: absorb components must be >= 0");
      if (m.contains("color") == false) md.color = f3(1.0f);  // AOV guide only
    }
    if (matIds.size() >= MAT_NONE) fail("too many materials");
    matIds[name] = (int)s.materials.size();
    s.materials.push_back(md);
  }

  std::map<std::string, int> meshIds;
  if (j.contains("meshes")) {
    for (const auto& [name, m] : j["meshes"].items()) {
      SceneMesh sm;
      sm.objFile = m.at("obj").get<std::string>();
      sm.smoothNormals = m.value("normals", "smooth") == std::string("smooth");
      meshIds[name] = (int)s.meshes.size();
      s.meshes.push_back(sm);
    }
  }

  if (!j.contains("objects")) fail("missing objects");
  for (const auto& o : j["objects"]) {
    SceneObject so;
    std::string shape = o.at("shape").get<std::string>();
    if (shape == "sphere") so.geomKind = GK_SPHERE;
    else if (shape == "rect") so.geomKind = GK_RECT;
    else if (shape == "disk") so.geomKind = GK_DISK;
    else if (shape == "cylinder") so.geomKind = GK_CYLINDER;
    else if (shape == "parabola") so.geomKind = GK_PARABOLA;
    else if (shape.rfind("mesh:", 0) == 0) {
      so.geomKind = GK_MESH;
      std::string mn = shape.substr(5);
      auto it = meshIds.find(mn);
      if (it == meshIds.end()) fail("unknown mesh '" + mn + "'");
      so.meshId = it->second;
    } else {
      fail("unknown shape '" + shape + "'");
    }

    auto matIdOf = [&](const json& v) -> uint16_t {
      std::string name = v.get<std::string>();
      auto it = matIds.find(name);
      if (it == matIds.end()) fail("unknown material '" + name + "'");
      return (uint16_t)it->second;
    };
    so.matFront = o.at("material").is_null() ? MAT_NONE : matIdOf(o.at("material"));
    if (o.contains("material_back")) {
      so.matBack = o["material_back"].is_null() ? MAT_NONE : matIdOf(o["material_back"]);
    } else {
      so.matBack = so.matFront;  // both sides by default (null front stays null)
    }
    if (so.matFront == MAT_NONE && so.matBack == MAT_NONE)
      fail("object has no material on either face");
    if (o.contains("cutout")) so.cutoutTexId = texId(o["cutout"], "cutout");
    so.xform = parseTransform(o.value("transform", json()));
    so.nee = o.value("nee", true);

    if (o.contains("physics")) {
      if (!s.physics.enabled)
        fail("physics: object opts in but the scene has no top-level physics block");
      const auto& p = o["physics"];
      so.physics.enabled = true;
      so.physics.dynamic = p.value("dynamic", false);
      so.physics.density = p.value("density", so.physics.density);
      if (p.contains("velocity")) so.physics.velocity = jf3(p["velocity"]);
      if (p.contains("angular_velocity")) so.physics.angularVelocity = jf3(p["angular_velocity"]);
      so.physics.friction = p.value("friction", so.physics.friction);
      so.physics.restitution = p.value("restitution", so.physics.restitution);
      so.physics.thickness = p.value("thickness", so.physics.thickness);
      if (so.geomKind == GK_DISK || so.geomKind == GK_CYLINDER || so.geomKind == GK_PARABOLA)
        fail("physics: shape '" + shape + "' is not supported as a collider "
             "(use sphere, rect, or mesh)");
      if (so.geomKind == GK_RECT && so.physics.dynamic)
        fail("physics: rect colliders are static-only (zero-thickness plates tunnel)");
      if (so.physics.dynamic && so.physics.density <= 0.0f)
        fail("physics: dynamic body needs positive density");
      if (so.physics.thickness <= 0.0f)
        fail("physics: thickness must be positive");
    }

    // Auto-register emissive rect/disk/sphere objects as NEE area lights.
    uint16_t emId = MAT_NONE;
    if (so.matFront != MAT_NONE && s.materials[so.matFront].kind == MT_EMISSIVE)
      emId = so.matFront;
    if (emId != MAT_NONE && so.nee &&
        (so.geomKind == GK_RECT || so.geomKind == GK_DISK || so.geomKind == GK_SPHERE)) {
      const MaterialDesc& em = s.materials[emId];
      LightDesc ld{};
      ld.texId = -1;
      ld.p = so.xform.applyPoint(f3(0, 0, 0));
      float3 ux = so.xform.applyVector(f3(1, 0, 0));
      float3 uy = so.xform.applyVector(f3(0, 1, 0));
      float3 uz = so.xform.applyVector(f3(0, 0, 1));
      // Sign of det(M): cross(uz,ux) picks up the determinant sign while the
      // device normal (inverse-transpose) does not — flip to stay consistent
      // under mirroring transforms.
      float detSign = dot(uy, cross(uz, ux)) < 0.0f ? -1.0f : 1.0f;
      ld.L = em.color * em.intensity;
      ld.twoSided = em.twoSided;
      if (em.texId >= 0) {
        if (so.geomKind == GK_SPHERE)
          fail("textured emissive sphere cannot be an NEE light (uv frame is "
               "object-space); use a solid color or set nee:false");
        ld.texId = em.texId;
        ld.L = f3(em.intensity);  // Li = tex(uv) * intensity, matching emitter hits
      }
      if (so.geomKind == GK_RECT) {
        ld.kind = LT_RECT;
        ld.u = ux;
        ld.v = uz;
        ld.n = normalize(cross(uz, ux)) * detSign;
        ld.area = 4.0f * length(cross(ux, uz));
      } else if (so.geomKind == GK_DISK) {
        float ru = length(ux), rz = length(uz);
        if (fabsf(ru - rz) > 1e-3f * fmaxf(ru, rz))
          fail("disk NEE light requires uniform XZ scale (pdf would be wrong); set nee:false");
        ld.kind = LT_DISK;
        ld.u = ux;
        ld.v = uz;
        ld.n = normalize(cross(uz, ux)) * detSign;
        ld.area = SD_PI * ru * rz;
      } else {
        float rx = length(ux), rz = length(uz);
        float ry = length(so.xform.applyVector(f3(0, 1, 0)));
        if (fabsf(rx - ry) > 1e-3f * rx || fabsf(rx - rz) > 1e-3f * rx)
          fail("sphere NEE light requires uniform scale; set nee:false");
        ld.kind = LT_SPHERE;
        ld.radius = rx;
      }
      so.lightId = (int)s.lights.size();
      s.lights.push_back(ld);
    }
    if (so.physics.dynamic && so.lightId != -1)
      fail("physics: a dynamic object cannot be an NEE area light (the light "
           "frame is baked at parse time); set nee:false or keep it static");
    s.objects.push_back(so);
  }

  if (j.contains("flames")) {
    if (!j["flames"].is_array()) fail("flames must be an array");
    for (const auto& fj : j["flames"]) {
      FlameDesc fd{};
      fd.base = jf3(fj.at("base"));
      fd.height = fj.at("height").get<float>();
      fd.radius = fj.at("radius").get<float>();
      fd.intensity = fj.value("intensity", 20.0f);
      fd.sigma = fj.value("sigma", 4.0f);
      fd.noiseScale = fj.value("noise_scale", 3.0f);
      fd.seed = (unsigned)fj.value("seed", 0);
      if (fd.height <= 0.0f || fd.radius <= 0.0f)
        fail("flame: height and radius must be positive");
      if (fd.intensity < 0.0f || fd.sigma < 0.0f)
        fail("flame: intensity and sigma must be >= 0");
      // The volume emission makes the flame visible; two embedded warm point
      // lights (soft shadows via radius) make it illuminate the scene through
      // the regular NEE machinery. Documented approximation: BSDF paths that
      // happen to cross the volume add a small extra on top of these lights.
      float lightI = fj.value("light_intensity", 12.0f);
      const float frac[2] = {0.65f, 0.35f};
      const float hfac[2] = {0.35f, 0.70f};
      const float3 warm[2] = {f3(1.0f, 0.50f, 0.15f), f3(1.0f, 0.62f, 0.25f)};
      for (int k = 0; k < 2; k++) {
        LightDesc ld{};
        ld.texId = -1;
        ld.kind = LT_POINT;
        ld.p = fd.base + f3(0.0f, hfac[k] * fd.height, 0.0f);
        ld.radius = 0.3f * fd.radius;
        ld.L = warm[k] * (lightI * frac[k]);
        s.lights.push_back(ld);
      }
      s.flames.push_back(fd);
    }
  }

  if (j.contains("lights")) {
    for (const auto& l : j["lights"]) {
      LightDesc ld{};
      ld.texId = -1;
      std::string kind = l.at("type").get<std::string>();
      if (kind == "point") {
        ld.kind = LT_POINT;
        ld.p = jf3(l.at("position"));
        ld.radius = l.value("radius", 0.0f);
        ld.L = jf3(l.at("intensity"));
      } else if (kind == "distant") {
        ld.kind = LT_DISTANT;
        ld.dir = normalize(jf3(l.at("direction")));
        ld.L = jf3(l.at("radiance"));
      } else {
        fail("unknown light type '" + kind + "' (area lights are emissive objects)");
      }
      s.lights.push_back(ld);
    }
  }

  return s;
}

}  // namespace sd

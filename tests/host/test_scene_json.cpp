// sundog host tests: src/scene_json.cpp loader — parses every scene in
// scenes/, detailed assertions on smoke.json / features.json, error paths.
// NOTE: uses relative path "scenes/", so run from the repo root (make does).
#include "scene_json.cpp"  // reuse the implementation directly

#include "check.h"

#include <cstdio>
#include <fstream>
#include <glob.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace sd;

static bool hasSuffix(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

static void testAllScenesLoad() {
  glob_t g{};
  int rc = glob("scenes/*.json", 0, nullptr, &g);
  CHECK_MSG(rc == 0 && g.gl_pathc >= 2,
            "no scenes/*.json found (rc=%d, count=%zu) — run from the repo root",
            rc, (size_t)g.gl_pathc);
  bool sawSmoke = false, sawFeatures = false;
  for (size_t i = 0; i < g.gl_pathc; i++) {
    std::string path = g.gl_pathv[i];
    try {
      Scene s = loadScene(path);
      CHECK_MSG(!s.objects.empty(), "%s: loaded but has no objects", path.c_str());
      CHECK_MSG(!s.materials.empty(), "%s: loaded but has no materials", path.c_str());
      CHECK_MSG(s.render.width > 0 && s.render.height > 0,
                "%s: bad render size", path.c_str());
      std::printf("  loaded %s: %zu objects, %zu materials, %zu lights\n",
                  path.c_str(), s.objects.size(), s.materials.size(),
                  s.lights.size());
    } catch (const std::exception& e) {
      std::fprintf(stderr, "loadScene(%s) threw: %s\n", path.c_str(), e.what());
      std::exit(1);
    }
    if (hasSuffix(path, "/smoke.json")) sawSmoke = true;
    if (hasSuffix(path, "/features.json")) sawFeatures = true;
  }
  globfree(&g);
  CHECK_MSG(sawSmoke, "scenes/smoke.json not found");
  CHECK_MSG(sawFeatures, "scenes/features.json not found");
}

static void testSmokeScene() {
  Scene s = loadScene("scenes/smoke.json");
  // render block
  CHECK(s.render.width == 256 && s.render.height == 256);
  CHECK(s.render.spp == 16);
  CHECK(s.render.maxDepth == 8);
  CHECK(s.render.seed == 7u);
  // camera
  CHECK_NEAR(s.camera.lookfrom.y, 1.5, 1e-6);
  CHECK_NEAR(s.camera.vfov, 35.0, 1e-6);
  // background gradient
  CHECK(s.bg.kind == BG_GRADIENT);
  CHECK_NEAR(s.bg.a.x, 1.0, 1e-6);   // horizon
  CHECK_NEAR(s.bg.b.z, 1.0, 1e-6);   // zenith blue
  // counts: 2 objects, 1 light, 2 materials (both lambert), 1 checker texture
  CHECK_MSG(s.objects.size() == 2, "smoke objects: %zu", s.objects.size());
  CHECK_MSG(s.lights.size() == 1, "smoke lights: %zu", s.lights.size());
  CHECK_MSG(s.materials.size() == 2, "smoke materials: %zu", s.materials.size());
  CHECK(s.textures.size() == 1);
  CHECK(s.textures[0].desc.kind == TX_CHECKER);
  CHECK_NEAR(s.textures[0].desc.scale.x, 8.0, 1e-6);
  for (const auto& m : s.materials) CHECK(m.kind == MT_LAMBERT);
  // one textured, one solid-colored
  int textured = 0, colored = 0;
  for (const auto& m : s.materials) {
    if (m.texId >= 0) { textured++; CHECK(m.texId == 0); }
    else { colored++; CHECK_NEAR(m.color.x, 0.7, 1e-6); }
  }
  CHECK(textured == 1 && colored == 1);
  // objects: rect (scale 4) then sphere (scale .7, translate y .7)
  CHECK(s.objects[0].geomKind == GK_RECT);
  CHECK(s.objects[1].geomKind == GK_SPHERE);
  float3 rx = s.objects[0].xform.applyVector(f3(1, 0, 0));
  CHECK_NEAR(rx.x, 4.0, 1e-5);
  float3 c = s.objects[1].xform.applyPoint(f3(0, 0, 0));
  CHECK_NEAR(c.y, 0.7, 1e-5);
  CHECK_NEAR(length(s.objects[1].xform.applyVector(f3(1, 0, 0))), 0.7, 1e-5);
  // lambert objects register no NEE area lights
  CHECK(s.objects[0].lightId == -1 && s.objects[1].lightId == -1);
  // point light
  CHECK(s.lights[0].kind == LT_POINT);
  CHECK_NEAR(s.lights[0].radius, 0.4, 1e-6);
  CHECK_NEAR(s.lights[0].p.x, 3.0, 1e-6);
  CHECK_NEAR(s.lights[0].L.x, 40.0, 1e-5);
  // default faces: material_back omitted -> same as front
  CHECK(s.objects[0].matFront == s.objects[0].matBack);
  CHECK(s.objects[0].matFront != MAT_NONE);
}

static void testFeaturesScene() {
  Scene s = loadScene("scenes/features.json");
  CHECK_MSG(s.objects.size() == 7, "features objects: %zu", s.objects.size());
  CHECK(s.textures.size() == 1);
  CHECK_MSG(s.materials.size() == 6, "features materials: %zu", s.materials.size());
  // material kind census: 2 lambert, 2 metal, 1 dielectric, 1 emissive
  int nLam = 0, nMet = 0, nDie = 0, nEmi = 0;
  for (const auto& m : s.materials) {
    if (m.kind == MT_LAMBERT) nLam++;
    else if (m.kind == MT_METAL) nMet++;
    else if (m.kind == MT_DIELECTRIC) nDie++;
    else if (m.kind == MT_EMISSIVE) nEmi++;
  }
  CHECK_MSG(nLam == 2 && nMet == 2 && nDie == 1 && nEmi == 1,
            "material kinds: lambert=%d metal=%d dielectric=%d emissive=%d",
            nLam, nMet, nDie, nEmi);

  // objects[4]: parabola with "material_back": null -> back face pass-through
  const SceneObject& par = s.objects[4];
  CHECK(par.geomKind == GK_PARABOLA);
  CHECK_MSG(par.matBack == MAT_NONE, "parabola matBack=%u", par.matBack);
  CHECK(par.matFront != MAT_NONE);
  CHECK(s.materials[par.matFront].kind == MT_METAL);
  CHECK_NEAR(s.materials[par.matFront].roughness, 0.0, 1e-6);  // mirror

  // no shipped object carries a cutout texture (the loader's "cutout" key
  // is covered by the temp-file test in testGoodPaths)
  for (const auto& o : s.objects) CHECK(o.cutoutTexId == -1);

  // objects[6]: emissive rect auto-registered as NEE area light (first light)
  const SceneObject& lamp = s.objects[6];
  CHECK(lamp.geomKind == GK_RECT);
  CHECK_MSG(lamp.lightId == 0, "lamp lightId=%d", lamp.lightId);
  CHECK(s.materials[lamp.matFront].kind == MT_EMISSIVE);
  CHECK_NEAR(s.materials[lamp.matFront].intensity, 20.0, 1e-5);
  // lights: auto rect light + explicit point + distant
  CHECK_MSG(s.lights.size() == 3, "features lights: %zu", s.lights.size());
  CHECK(s.lights[0].kind == LT_RECT);
  CHECK_NEAR(s.lights[0].area, 5.76, 1e-4);  // half-edges 1.2 -> 4*1.44
  CHECK_NEAR(s.lights[0].n.y, -1.0, 1e-5);   // rotate_x 180 -> faces down
  CHECK(s.lights[0].twoSided == 0);
  CHECK_NEAR(s.lights[0].L.x, 20.0, 1e-4);   // color 1.0 * intensity 20
  CHECK_NEAR(s.lights[0].p.y, 6.0, 1e-5);
  CHECK(s.lights[1].kind == LT_POINT);
  CHECK_NEAR(s.lights[1].radius, 0.3, 1e-6);
  CHECK(s.lights[2].kind == LT_DISTANT);
  CHECK_NEAR(length(s.lights[2].dir), 1.0, 1e-5);  // normalized on load
  CHECK(s.lights[2].dir.y < 0.0f);
  // camera aperture parsed
  CHECK_NEAR(s.camera.aperture, 0.02, 1e-6);
  // baseDir resolves relative to the scene file
  CHECK(s.baseDir == "scenes");
}

// ---------- error paths: bad JSON written to /tmp ----------

static std::string writeTmp(int idx, const std::string& content) {
  char path[128];
  std::snprintf(path, sizeof(path), "/tmp/sundog_test_scene_%d_%d.json",
                (int)getpid(), idx);
  std::ofstream out(path);
  out << content;
  out.close();
  return path;
}

static int g_tmpIdx = 0;

static void expectLoadFail(const std::string& content, const char* what) {
  std::string path = writeTmp(g_tmpIdx++, content);
  bool threw = false;
  std::string msg;
  try {
    loadScene(path);
  } catch (const std::exception& e) {
    threw = true;
    msg = e.what();
  }
  unlink(path.c_str());
  CHECK_MSG(threw, "expected loadScene to reject: %s", what);
  CHECK_MSG(!msg.empty(), "error had empty message: %s", what);
  std::printf("  rejected (%s): %s\n", what, msg.c_str());
}

static Scene expectLoadOk(const std::string& content, const char* what) {
  std::string path = writeTmp(g_tmpIdx++, content);
  try {
    Scene s = loadScene(path);
    unlink(path.c_str());
    return s;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "loadScene unexpectedly failed (%s): %s\n", what, e.what());
    unlink(path.c_str());
    std::exit(1);
  }
}

static const char* kMinimalScene = R"({
  "camera": { "lookfrom": [0,1,5], "lookat": [0,0,0] },
  "materials": { "m": { "type": "lambert", "color": [0.5,0.5,0.5] } },
  "objects": [ { "shape": "sphere", "material": "m" } ]
})";

static void testErrorPaths() {
  // the minimal skeleton itself is valid (defaults fill the rest)
  Scene mini = expectLoadOk(kMinimalScene, "minimal scene");
  CHECK(mini.render.width == 1280 && mini.render.height == 720);  // defaults
  CHECK(mini.objects.size() == 1 && mini.lights.empty());

  expectLoadFail("{ this is not json", "malformed JSON");
  expectLoadFail("", "empty file");
  expectLoadFail(R"({ "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"sphere","material":"m"}] })",
                 "missing camera");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "objects":[{"shape":"sphere","material":"m"}] })",
                 "missing materials");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert"}} })",
                 "missing objects");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"wood"}},
                     "objects":[{"shape":"sphere","material":"m"}] })",
                 "unknown material type");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert","texture":"nosuch"}},
                     "objects":[{"shape":"sphere","material":"m"}] })",
                 "unknown texture reference");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "textures": {"t":{"type":"perlin"}},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"sphere","material":"m"}] })",
                 "unknown texture type");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"torus","material":"m"}] })",
                 "unknown shape");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"mesh:nosuch","material":"m"}] })",
                 "unknown mesh");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"sphere","material":"ghost"}] })",
                 "unknown material reference");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"sphere","material":null,"material_back":null}] })",
                 "both faces null");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"sphere","material":"m",
                                 "transform":[{"warp":1}]}] })",
                 "unknown transform step");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"sphere","material":"m",
                                 "transform":{"scale":2}}] })",
                 "transform not a list");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "background": {"type":"plaid"},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"sphere","material":"m"}] })",
                 "unknown background type");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"e":{"type":"emissive","intensity":5}},
                     "objects":[{"shape":"disk","material":"e",
                                 "transform":[{"scale":[2,1,1]}]}] })",
                 "disk NEE light with non-uniform scale");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"e":{"type":"emissive","intensity":5}},
                     "objects":[{"shape":"sphere","material":"e",
                                 "transform":[{"scale":[2,1,1]}]}] })",
                 "sphere NEE light with non-uniform scale");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"sphere","material":"m"}],
                     "lights":[{"type":"laser","position":[0,1,0]}] })",
                 "unknown light type");

  // nonexistent path
  bool threw = false;
  try { loadScene("/tmp/sundog_definitely_missing_42.json"); }
  catch (const std::exception&) { threw = true; }
  CHECK_MSG(threw, "expected missing-file error");

  // non-uniform emissive with nee:false is allowed and registers no light
  Scene ok = expectLoadOk(R"({
      "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
      "materials": {"e":{"type":"emissive","intensity":5}},
      "objects":[{"shape":"disk","material":"e","nee":false,
                  "transform":[{"scale":[2,1,1]}]}] })",
      "nee:false non-uniform emissive disk");
  CHECK(ok.lights.empty());
  CHECK(ok.objects[0].lightId == -1);
  CHECK(ok.objects[0].nee == false);

  // emissive sphere with uniform scale becomes an LT_SPHERE light
  Scene sph = expectLoadOk(R"({
      "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
      "materials": {"e":{"type":"emissive","color":[1,1,1],"intensity":3}},
      "objects":[{"shape":"sphere","material":"e",
                  "transform":[{"scale":2},{"translate":[1,5,0]}]}] })",
      "uniform emissive sphere");
  CHECK(sph.lights.size() == 1 && sph.lights[0].kind == LT_SPHERE);
  CHECK_NEAR(sph.lights[0].radius, 2.0, 1e-5);
  CHECK_NEAR(sph.lights[0].p.y, 5.0, 1e-5);
  CHECK_NEAR(sph.lights[0].L.x, 3.0, 1e-5);
  CHECK(sph.objects[0].lightId == 0);

  // null front with real back face is a valid back-only surface
  Scene back = expectLoadOk(R"({
      "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
      "materials": {"m":{"type":"lambert"}},
      "objects":[{"shape":"parabola","material":null,"material_back":"m"}] })",
      "back-only surface");
  CHECK(back.objects[0].matFront == MAT_NONE);
  CHECK(back.objects[0].matBack != MAT_NONE);

  // "cutout" binds an alpha texture id to the object (no shipped scene uses
  // it anymore, so keep the loader path covered here)
  Scene cut = expectLoadOk(R"({
      "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
      "textures": {"holes":{"type":"image","file":"textures/spot_texture.png"}},
      "materials": {"m":{"type":"lambert"}},
      "objects":[{"shape":"rect","material":"m","cutout":"holes"}] })",
      "cutout binding");
  CHECK_MSG(cut.objects[0].cutoutTexId >= 0, "cutout not parsed");
  CHECK(cut.textures[cut.objects[0].cutoutTexId].desc.kind == TX_IMAGE);
}

static void testPhysicsParsing() {
  // full positive path: global block + static collider + dynamic mesh body
  Scene s = expectLoadOk(R"({
      "camera": {"lookfrom":[0,4,9],"lookat":[0,0,0]},
      "physics": {"gravity":[0,-9.81,0], "timestep":0.005, "max_time":8.0,
                  "friction":0.7, "restitution":0.2, "solver_iterations":[16,4],
                  "sleep_threshold":0.01, "stop_time":1.25},
      "materials": {"m":{"type":"lambert"}},
      "meshes": {"cow":{"obj":"../assets/spot.obj"}},
      "objects":[
        {"shape":"rect","material":"m","physics":{"thickness":0.4},
         "transform":[{"scale":5}]},
        {"shape":"mesh:cow","material":"m",
         "physics":{"dynamic":true,"density":300,"velocity":[0.1,-2,0],
                    "angular_velocity":[1,0,-1]},
         "transform":[{"scale":0.5},{"rotate_y":45},{"translate":[0,3,0]}]},
        {"shape":"sphere","material":"m","transform":[{"translate":[9,1,0]}]}
      ] })",
      "physics scene");
  CHECK(s.physics.enabled);
  CHECK_NEAR(s.physics.gravity.y, -9.81, 1e-6);
  CHECK_NEAR(s.physics.timestep, 0.005, 1e-7);
  CHECK_NEAR(s.physics.maxTime, 8.0, 1e-6);
  CHECK_NEAR(s.physics.friction, 0.7, 1e-6);
  CHECK_NEAR(s.physics.restitution, 0.2, 1e-6);
  CHECK(s.physics.posIters == 16 && s.physics.velIters == 4);
  CHECK_NEAR(s.physics.sleepThreshold, 0.01, 1e-7);
  CHECK_NEAR(s.physics.stopTime, 1.25, 1e-6);
  const PhysicsObject& floor = s.objects[0].physics;
  CHECK(floor.enabled && !floor.dynamic);
  CHECK_NEAR(floor.thickness, 0.4, 1e-6);
  CHECK(floor.friction < 0.0f && floor.restitution < 0.0f);  // scene defaults
  const PhysicsObject& cow = s.objects[1].physics;
  CHECK(cow.enabled && cow.dynamic);
  CHECK_NEAR(cow.density, 300.0, 1e-4);
  CHECK_NEAR(cow.velocity.y, -2.0, 1e-6);
  CHECK_NEAR(cow.angularVelocity.z, -1.0, 1e-6);
  CHECK(!s.objects[2].physics.enabled);  // opt-in only

  // scenes without a physics block stay disabled
  Scene mini = expectLoadOk(kMinimalScene, "minimal scene (no physics)");
  CHECK(!mini.physics.enabled);
  CHECK(!mini.objects[0].physics.enabled);

  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"sphere","material":"m",
                                 "physics":{"dynamic":true}}] })",
                 "object physics without top-level block");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "physics": {},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"cylinder","material":"m",
                                 "physics":{}}] })",
                 "cylinder collider unsupported");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "physics": {},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"rect","material":"m",
                                 "physics":{"dynamic":true}}] })",
                 "dynamic rect rejected");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "physics": {},
                     "materials": {"e":{"type":"emissive","intensity":5}},
                     "objects":[{"shape":"sphere","material":"e",
                                 "physics":{"dynamic":true}}] })",
                 "dynamic NEE area light rejected");

  // a dynamic emitter is fine when kept out of NEE
  Scene neeOff = expectLoadOk(R"({
      "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
      "physics": {},
      "materials": {"e":{"type":"emissive","intensity":5}},
      "objects":[{"shape":"sphere","material":"e","nee":false,
                  "physics":{"dynamic":true}}] })",
      "dynamic emissive with nee:false");
  CHECK(neeOff.objects[0].physics.dynamic && neeOff.lights.empty());
}

static void testFlameParsing() {
  Scene s = expectLoadOk(R"({
      "camera": {"lookfrom":[0,2,6],"lookat":[0,0,0]},
      "materials": {"m":{"type":"lambert"}},
      "objects":[{"shape":"rect","material":"m","transform":[{"scale":8}]}],
      "flames":[{"base":[1,0.2,-1], "height":1.5, "radius":0.4,
                 "intensity":25, "sigma":5, "noise_scale":2.5, "seed":7,
                 "light_intensity":16}]
      })",
      "flame scene");
  CHECK(s.flames.size() == 1);
  const FlameDesc& f = s.flames[0];
  CHECK_NEAR(f.base.x, 1.0, 1e-6);
  CHECK_NEAR(f.height, 1.5, 1e-6);
  CHECK_NEAR(f.radius, 0.4, 1e-6);
  CHECK_NEAR(f.intensity, 25.0, 1e-5);
  CHECK_NEAR(f.sigma, 5.0, 1e-6);
  CHECK_NEAR(f.noiseScale, 2.5, 1e-6);
  CHECK(f.seed == 7u);
  // two embedded warm point lights per flame, on the axis, soft radius
  CHECK_MSG(s.lights.size() == 2, "flame lights: %zu", s.lights.size());
  CHECK(s.lights[0].kind == LT_POINT && s.lights[1].kind == LT_POINT);
  CHECK_NEAR(s.lights[0].p.y, 0.2 + 0.35 * 1.5, 1e-5);
  CHECK_NEAR(s.lights[1].p.y, 0.2 + 0.70 * 1.5, 1e-5);
  CHECK_NEAR(s.lights[0].radius, 0.12, 1e-6);  // 0.3 * radius
  CHECK(s.lights[0].L.x > s.lights[1].L.x);    // 0.65/0.35 split

  // defaults
  Scene d = expectLoadOk(R"({
      "camera": {"lookfrom":[0,2,6],"lookat":[0,0,0]},
      "materials": {"m":{"type":"lambert"}},
      "objects":[{"shape":"sphere","material":"m"}],
      "flames":[{"base":[0,0,0], "height":1, "radius":0.5}] })",
      "flame defaults");
  CHECK_NEAR(d.flames[0].intensity, 20.0, 1e-5);
  CHECK_NEAR(d.flames[0].sigma, 4.0, 1e-6);
  CHECK_NEAR(d.flames[0].noiseScale, 3.0, 1e-6);
  CHECK(d.flames[0].seed == 0u);

  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"sphere","material":"m"}],
                     "flames":[{"base":[0,0,0],"height":0,"radius":0.5}] })",
                 "flame zero height");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"m":{"type":"lambert"}},
                     "objects":[{"shape":"sphere","material":"m"}],
                     "flames":{"base":[0,0,0]} })",
                 "flames not an array");
}

static void testWaterMaterial() {
  Scene s = expectLoadOk(R"({
      "camera": {"lookfrom":[0,2,8],"lookat":[0,0,0]},
      "materials": {
        "lake": { "type": "water" },
        "pool": { "type": "water", "ior": 1.34, "absorb": [0.6, 0.1, 0.05],
                  "wave_amp": 0.12, "wave_freq": 3.5 }
      },
      "objects":[
        {"shape":"rect","material":"lake","transform":[{"scale":30}]},
        {"shape":"rect","material":"pool","transform":[{"scale":5},{"translate":[0,-1,0]}]}
      ] })",
      "water materials");
  const MaterialDesc& lake = s.materials[0];
  CHECK(lake.kind == MT_WATER);
  CHECK_NEAR(lake.ior, 1.33, 1e-6);          // water default, not glass 1.5
  CHECK_NEAR(lake.absorb.x, 0.45, 1e-6);     // red absorbed fastest
  CHECK(lake.absorb.x > lake.absorb.y && lake.absorb.y > lake.absorb.z);
  CHECK_NEAR(lake.waveAmp, 0.05, 1e-6);
  CHECK_NEAR(lake.waveFreq, 2.0, 1e-6);
  CHECK_NEAR(lake.color.x, 1.0, 1e-6);       // AOV guide default
  const MaterialDesc& pool = s.materials[1];
  CHECK_NEAR(pool.ior, 1.34, 1e-6);
  CHECK_NEAR(pool.absorb.y, 0.1, 1e-6);
  CHECK_NEAR(pool.waveAmp, 0.12, 1e-6);
  CHECK_NEAR(pool.waveFreq, 3.5, 1e-6);
  CHECK(s.lights.empty());  // water is not emissive, registers no NEE light

  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"w":{"type":"water","wave_amp":-0.1}},
                     "objects":[{"shape":"rect","material":"w"}] })",
                 "negative wave_amp");
  expectLoadFail(R"({ "camera": {"lookfrom":[0,1,5],"lookat":[0,0,0]},
                     "materials": {"w":{"type":"water","absorb":[-0.1,0,0]}},
                     "objects":[{"shape":"rect","material":"w"}] })",
                 "negative absorb component");
}

static void testMakeCamera() {
  CameraSettings cs;
  cs.lookfrom = f3(0, 0, 5);
  cs.lookat = f3(0, 0, 0);
  cs.up = f3(0, 1, 0);
  cs.vfov = 90.0f;
  CameraData c = makeCamera(cs, 100, 100);  // square, halfH = tan(45) = 1
  CHECK_NEAR(c.w.z, 1.0, 1e-5);   // backward
  CHECK_NEAR(c.u.x, 1.0, 1e-5);   // right
  CHECK_NEAR(c.v.y, 1.0, 1e-5);   // up
  CHECK_NEAR(c.origin.z, 5.0, 1e-6);
  // focus = 5 -> image plane spans 10 units, lower-left at (-5,-5,0)
  CHECK_NEAR(c.horizontal.x, 10.0, 1e-4);
  CHECK_NEAR(c.vertical.y, 10.0, 1e-4);
  CHECK_NEAR(c.lowerLeft.x, -5.0, 1e-4);
  CHECK_NEAR(c.lowerLeft.y, -5.0, 1e-4);
  CHECK_NEAR(c.lowerLeft.z, 0.0, 1e-4);
  CHECK_NEAR(c.lensRadius, 0.0, 0.0);
}

int main() {
  testAllScenesLoad();
  testSmokeScene();
  testFeaturesScene();
  testErrorPaths();
  testPhysicsParsing();
  testFlameParsing();
  testWaterMaterial();
  testMakeCamera();
  TEST_DONE("test_scene_json");
}

// sundog host tests: the C scene-construction API (capi_scene.cpp +
// scene_build.cpp) — the same derivation the renderer sees when scenelib
// drives libsundog.so via ctypes. Compiles standalone (CUDA/OptiX headers
// only, no GPU, no link): the construction surface makes no CUDA calls.
// The numeric assertions are carried over verbatim from the JSON-loader era;
// scene-file coverage lives in tests/test_scenelib.py (every scenes/*.py
// builds + validates + programs) and in the smoke/golden renders.
#include "capi_scene.cpp"   // C ABI shims (includes capi_internal.h)
#include "scene_build.cpp"  // shared derivation: transforms, NEE lights, flames

#include "check.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace sd;

static const double kNaN = std::nan("");

// A double[3] literal that lives to the end of the full expression.
struct D3 {
  double v[3];
  D3(double x, double y, double z) { v[0] = x; v[1] = y; v[2] = z; }
  operator const double*() const { return v; }
};
struct D2 {
  double v[2];
  D2(double x, double y) { v[0] = x; v[1] = y; }
  operator const double*() const { return v; }
};

static void expectAddFail(int rc, const char* what) {
  CHECK_MSG(rc == -1, "expected add to fail: %s", what);
  CHECK_MSG(*sundog_last_error() != '\0', "error had empty message: %s", what);
  std::printf("  rejected (%s): %s\n", what, sundog_last_error());
}

static void expectSetFail(int rc, const char* what) {
  CHECK_MSG(rc != SUNDOG_OK, "expected set to fail: %s", what);
  CHECK_MSG(*sundog_last_error() != '\0', "error had empty message: %s", what);
  std::printf("  rejected (%s): %s\n", what, sundog_last_error());
}

static sundog_scene* freshMini() {
  sundog_scene* h = sundog_scene_create("scenes");
  CHECK(h != nullptr);
  CHECK(sundog_set_camera(h, D3(0, 1, 5), D3(0, 0, 0), nullptr, kNaN, kNaN,
                          kNaN) == SUNDOG_OK);
  CHECK(sundog_add_material_lambert(h, D3(0.5, 0.5, 0.5), -1) == 0);
  return h;
}

static int addSphere(sundog_scene* h, int mat) {
  return sundog_add_object(h, SUNDOG_GK_SPHERE, -1, mat, SUNDOG_MAT_DEFAULT,
                           -1, nullptr, 0, -1, nullptr);
}

static void testMinimalDefaults() {
  sundog_scene* h = freshMini();
  CHECK(addSphere(h, 0) == 0);
  CHECK(sundog_scene_validate(h) == SUNDOG_OK);
  const Scene& s = h->scene;
  CHECK(s.render.width == 1280 && s.render.height == 720);  // defaults
  CHECK(s.render.tonemap == TM_ACES);  // ACES is the default output mapping
  CHECK(s.render.transparentShadows == true);  // transmissive shadows on
  CHECK(s.objects.size() == 1 && s.lights.empty());
  CHECK(s.baseDir == "scenes");

  // validate without a camera fails
  sundog_scene* h2 = sundog_scene_create(nullptr);
  CHECK(sundog_scene_validate(h2) != SUNDOG_OK);
  std::printf("  rejected (missing camera): %s\n", sundog_last_error());
  sundog_scene_destroy(h2);

  // legacy opaque-shadows opt-out + tonemap clamp escape hatch
  sundog_scene* h3 = freshMini();
  CHECK(sundog_set_render(h3, -1, -1, -1, -1, kNaN, -1, kNaN, kNaN,
                          SUNDOG_TM_CLAMP, 0) == SUNDOG_OK);
  CHECK(h3->scene.render.transparentShadows == false);
  CHECK(h3->scene.render.tonemap == TM_CLAMP);
  expectSetFail(sundog_set_render(h3, -1, -1, -1, -1, kNaN, -1, kNaN, kNaN,
                                  99, -1), "unknown tonemap");
  sundog_scene_destroy(h3);
  sundog_scene_destroy(h);
}

// scenes/smoke.py replicated call-for-call (sorted-name id order: the ids
// scenelib::_program allocates are ball=0, ground=1; floor tex = 0).
static void testSmokeEquivalent() {
  sundog_scene* h = sundog_scene_create("scenes");
  CHECK(sundog_set_render(h, 256, 256, 16, 8, kNaN, 7, kNaN, kNaN, -1, -1) == 0);
  CHECK(sundog_set_camera(h, D3(0, 1.5, 5), D3(0, 0.7, 0), nullptr, 35.0,
                          kNaN, kNaN) == 0);
  CHECK(sundog_set_background_gradient(h, D3(1.0, 1.0, 1.0),
                                       D3(0.4, 0.6, 1.0)) == 0);
  CHECK(sundog_add_texture_checker(h, D3(0.85, 0.85, 0.85),
                                   D3(0.15, 0.15, 0.15), D2(8, 8)) == 0);
  int ball = sundog_add_material_lambert(h, D3(0.7, 0.3, 0.3), -1);
  int ground = sundog_add_material_lambert(h, nullptr, 0);
  CHECK(ball == 0 && ground == 1);
  sundog_xform_step rectSteps[1] = {{SUNDOG_XF_SCALE, 4, 4, 4}};
  CHECK(sundog_add_object(h, SUNDOG_GK_RECT, -1, ground, SUNDOG_MAT_DEFAULT,
                          -1, rectSteps, 1, -1, nullptr) == 0);
  sundog_xform_step ballSteps[2] = {{SUNDOG_XF_SCALE, 0.7, 0.7, 0.7},
                                    {SUNDOG_XF_TRANSLATE, 0, 0.7, 0}};
  CHECK(sundog_add_object(h, SUNDOG_GK_SPHERE, -1, ball, SUNDOG_MAT_DEFAULT,
                          -1, ballSteps, 2, -1, nullptr) == 1);
  CHECK(sundog_add_point_light(h, D3(3, 4, 2), D3(40, 40, 40), 0.4) == 0);
  CHECK(sundog_scene_validate(h) == SUNDOG_OK);

  const Scene& s = h->scene;
  CHECK(s.render.width == 256 && s.render.height == 256);
  CHECK(s.render.spp == 16);
  CHECK(s.render.maxDepth == 8);
  CHECK(s.render.seed == 7u);
  CHECK_NEAR(s.camera.lookfrom.y, 1.5, 1e-6);
  CHECK_NEAR(s.camera.vfov, 35.0, 1e-6);
  CHECK(s.bg.kind == BG_GRADIENT);
  CHECK_NEAR(s.bg.a.x, 1.0, 1e-6);   // horizon
  CHECK_NEAR(s.bg.b.z, 1.0, 1e-6);   // zenith blue
  CHECK_MSG(s.objects.size() == 2, "smoke objects: %zu", s.objects.size());
  CHECK_MSG(s.lights.size() == 1, "smoke lights: %zu", s.lights.size());
  CHECK_MSG(s.materials.size() == 2, "smoke materials: %zu", s.materials.size());
  CHECK(s.textures.size() == 1);
  CHECK(s.textures[0].desc.kind == TX_CHECKER);
  CHECK_NEAR(s.textures[0].desc.scale.x, 8.0, 1e-6);
  for (const auto& m : s.materials) CHECK(m.kind == MT_LAMBERT);
  int textured = 0, colored = 0;
  for (const auto& m : s.materials) {
    if (m.texId >= 0) { textured++; CHECK(m.texId == 0); }
    else { colored++; CHECK_NEAR(m.color.x, 0.7, 1e-6); }
  }
  CHECK(textured == 1 && colored == 1);
  CHECK(s.objects[0].geomKind == GK_RECT);
  CHECK(s.objects[1].geomKind == GK_SPHERE);
  float3 rx = s.objects[0].xform.applyVector(f3(1, 0, 0));
  CHECK_NEAR(rx.x, 4.0, 1e-5);
  float3 c = s.objects[1].xform.applyPoint(f3(0, 0, 0));
  CHECK_NEAR(c.y, 0.7, 1e-5);
  CHECK_NEAR(length(s.objects[1].xform.applyVector(f3(1, 0, 0))), 0.7, 1e-5);
  CHECK(s.objects[0].lightId == -1 && s.objects[1].lightId == -1);
  CHECK(s.lights[0].kind == LT_POINT);
  CHECK_NEAR(s.lights[0].radius, 0.4, 1e-6);
  CHECK_NEAR(s.lights[0].p.x, 3.0, 1e-6);
  CHECK_NEAR(s.lights[0].L.x, 40.0, 1e-5);
  CHECK(s.objects[0].matFront == s.objects[0].matBack);  // omitted back
  CHECK(s.objects[0].matFront != MAT_NONE);
  sundog_scene_destroy(h);
}

// scenes/features.py replicated (sorted ids: glass=0 gold=1 ground=2 lamp=3
// mirror=4 red=5).
static void testFeaturesEquivalent() {
  sundog_scene* h = sundog_scene_create("scenes");
  CHECK(sundog_set_render(h, 1920, 1080, 64, 16, kNaN, 7, kNaN, kNaN, -1, -1) == 0);
  CHECK(sundog_set_camera(h, D3(0, 3.2, 9), D3(0, 0.9, 0), nullptr, 38.0,
                          0.02, kNaN) == 0);
  CHECK(sundog_set_background_gradient(h, D3(0.9, 0.9, 0.95),
                                       D3(0.35, 0.55, 0.95)) == 0);
  CHECK(sundog_add_texture_checker(h, D3(0.8, 0.8, 0.8), D3(0.25, 0.25, 0.3),
                                   D2(12, 12)) == 0);
  int glass = sundog_add_material_dielectric(h, 1.5, nullptr, nullptr, -1);
  int gold = sundog_add_material_metal(h, D3(1.0, 0.78, 0.34), -1, 0.15);
  int ground = sundog_add_material_lambert(h, nullptr, 0);
  int lamp = sundog_add_material_emissive(h, D3(1.0, 0.95, 0.85), -1, 20.0, -1);
  int mirror = sundog_add_material_metal(h, D3(0.9, 0.9, 0.9), -1, 0.0);
  int red = sundog_add_material_lambert(h, D3(0.75, 0.25, 0.25), -1);
  CHECK(glass == 0 && gold == 1 && ground == 2 && lamp == 3 && mirror == 4 &&
        red == 5);

  sundog_xform_step floorS[1] = {{SUNDOG_XF_SCALE, 8, 8, 8}};
  CHECK(sundog_add_object(h, SUNDOG_GK_RECT, -1, ground, SUNDOG_MAT_DEFAULT,
                          -1, floorS, 1, -1, nullptr) == 0);
  sundog_xform_step glassS[2] = {{SUNDOG_XF_SCALE, 0.8, 0.8, 0.8},
                                 {SUNDOG_XF_TRANSLATE, -3.0, 0.8, 1.0}};
  sundog_add_object(h, SUNDOG_GK_SPHERE, -1, glass, SUNDOG_MAT_DEFAULT, -1,
                    glassS, 2, -1, nullptr);
  sundog_xform_step redS[2] = {{SUNDOG_XF_SCALE, 0.8, 0.8, 0.8},
                               {SUNDOG_XF_TRANSLATE, -1.0, 0.8, 1.0}};
  sundog_add_object(h, SUNDOG_GK_SPHERE, -1, red, SUNDOG_MAT_DEFAULT, -1,
                    redS, 2, -1, nullptr);
  sundog_xform_step cylS[2] = {{SUNDOG_XF_SCALE, 0.6, 0.8, 0.6},
                               {SUNDOG_XF_TRANSLATE, 1.0, 0.8, 1.0}};
  sundog_add_object(h, SUNDOG_GK_CYLINDER, -1, gold, SUNDOG_MAT_DEFAULT, -1,
                    cylS, 2, -1, nullptr);
  sundog_xform_step parS[3] = {{SUNDOG_XF_SCALE, 1.4, 1.4, 1.4},
                               {SUNDOG_XF_ROTATE_Z, 90, 0, 0},
                               {SUNDOG_XF_TRANSLATE, 3.4, 1.4, 0.0}};
  CHECK(sundog_add_object(h, SUNDOG_GK_PARABOLA, -1, mirror, SUNDOG_MAT_NONE,
                          -1, parS, 3, -1, nullptr) == 4);
  sundog_xform_step diskS[3] = {{SUNDOG_XF_SCALE, 1.6, 1.6, 1.6},
                                {SUNDOG_XF_ROTATE_X, 90, 0, 0},
                                {SUNDOG_XF_TRANSLATE, 0, 1.6, -3.2}};
  sundog_add_object(h, SUNDOG_GK_DISK, -1, mirror, SUNDOG_MAT_DEFAULT, -1,
                    diskS, 3, -1, nullptr);
  sundog_xform_step lampS[3] = {{SUNDOG_XF_ROTATE_X, 180, 0, 0},
                                {SUNDOG_XF_SCALE, 1.2, 1.2, 1.2},
                                {SUNDOG_XF_TRANSLATE, 0, 6, 2}};
  CHECK(sundog_add_object(h, SUNDOG_GK_RECT, -1, lamp, SUNDOG_MAT_DEFAULT, -1,
                          lampS, 3, -1, nullptr) == 6);
  CHECK(sundog_add_point_light(h, D3(5, 5, 4), D3(30, 30, 30), 0.3) >= 0);
  CHECK(sundog_add_distant_light(h, D3(-0.5, -1, -0.3),
                                 D3(0.6, 0.6, 0.55)) >= 0);
  CHECK(sundog_scene_validate(h) == SUNDOG_OK);

  const Scene& s = h->scene;
  CHECK_MSG(s.objects.size() == 7, "features objects: %zu", s.objects.size());
  CHECK(s.textures.size() == 1);
  CHECK_MSG(s.materials.size() == 6, "features materials: %zu", s.materials.size());
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
  const SceneObject& par = s.objects[4];
  CHECK(par.geomKind == GK_PARABOLA);
  CHECK_MSG(par.matBack == MAT_NONE, "parabola matBack=%u", par.matBack);
  CHECK(par.matFront != MAT_NONE);
  CHECK(s.materials[par.matFront].kind == MT_METAL);
  CHECK_NEAR(s.materials[par.matFront].roughness, 0.0, 1e-6);  // mirror
  for (const auto& o : s.objects) CHECK(o.cutoutTexId == -1);
  const SceneObject& lampObj = s.objects[6];
  CHECK(lampObj.geomKind == GK_RECT);
  CHECK_MSG(lampObj.lightId == 0, "lamp lightId=%d", lampObj.lightId);
  CHECK(s.materials[lampObj.matFront].kind == MT_EMISSIVE);
  CHECK_NEAR(s.materials[lampObj.matFront].intensity, 20.0, 1e-5);
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
  // no flame in this scene: every light is unowned
  CHECK(s.lights[0].flameId == -1 && s.lights[1].flameId == -1 &&
        s.lights[2].flameId == -1);
  CHECK_NEAR(s.camera.aperture, 0.02, 1e-6);
  sundog_scene_destroy(h);
}

static void testErrorPaths() {
  sundog_scene* h = freshMini();
  // reference range guards (the name->id resolution lives in scenelib)
  expectAddFail(sundog_add_material_lambert(h, nullptr, 7),
                "texture id out of range");
  expectAddFail(sundog_add_object(h, 99, -1, 0, SUNDOG_MAT_DEFAULT, -1,
                                  nullptr, 0, -1, nullptr), "unknown shape");
  expectAddFail(sundog_add_object(h, SUNDOG_GK_MESH, 3, 0, SUNDOG_MAT_DEFAULT,
                                  -1, nullptr, 0, -1, nullptr),
                "mesh id out of range");
  expectAddFail(addSphere(h, 42), "material id out of range");
  expectAddFail(sundog_add_object(h, SUNDOG_GK_SPHERE, -1, SUNDOG_MAT_NONE,
                                  SUNDOG_MAT_NONE, -1, nullptr, 0, -1, nullptr),
                "both faces null");
  expectAddFail(sundog_add_object(h, SUNDOG_GK_SPHERE, -1, 0,
                                  SUNDOG_MAT_DEFAULT, 5, nullptr, 0, -1,
                                  nullptr), "cutout texture id out of range");
  sundog_xform_step bad[1] = {{99, 1, 1, 1}};
  expectAddFail(sundog_add_object(h, SUNDOG_GK_SPHERE, -1, 0,
                                  SUNDOG_MAT_DEFAULT, -1, bad, 1, -1, nullptr),
                "unknown transform step kind");
  sundog_scene_destroy(h);

  // tinted dielectric parses; plain glass stays vacuum-clear; negative fails
  h = sundog_scene_create(nullptr);
  int g = sundog_add_material_dielectric(h, kNaN, D3(0.4, 0.05, 0.02), nullptr, -1);
  int p = sundog_add_material_dielectric(h, kNaN, nullptr, nullptr, -1);
  CHECK(g == 0 && p == 1);
  CHECK_NEAR(h->scene.materials[0].absorb.x, 0.4, 1e-6);
  CHECK_NEAR(h->scene.materials[1].absorb.x, 0.0, 0.0);
  expectAddFail(sundog_add_material_dielectric(h, kNaN, D3(-0.1, 0, 0),
                                               nullptr, -1),
                "dielectric negative absorb");
  sundog_scene_destroy(h);

  // non-uniform emissive NEE lights are rejected... (all registries filled
  // first: materials/textures are phase-0 calls)
  h = freshMini();
  int em = sundog_add_material_emissive(h, nullptr, -1, 5.0, -1);
  int se = sundog_add_material_emissive(h, D3(1, 1, 1), -1, 3.0, -1);
  int tex = sundog_add_texture_image(h, "textures/spot_texture.png", -1);
  int temEm = sundog_add_material_emissive(h, nullptr, tex, 2.0, -1);
  CHECK(em >= 0 && se >= 0 && tex >= 0 && temEm >= 0);
  sundog_xform_step nonUniform[1] = {{SUNDOG_XF_SCALE, 2, 1, 1}};
  expectAddFail(sundog_add_object(h, SUNDOG_GK_DISK, -1, em,
                                  SUNDOG_MAT_DEFAULT, -1, nonUniform, 1, -1,
                                  nullptr), "disk NEE light non-uniform");
  expectAddFail(sundog_add_object(h, SUNDOG_GK_SPHERE, -1, em,
                                  SUNDOG_MAT_DEFAULT, -1, nonUniform, 1, -1,
                                  nullptr), "sphere NEE light non-uniform");
  // ...but allowed with nee=0, registering no light
  int ok = sundog_add_object(h, SUNDOG_GK_DISK, -1, em, SUNDOG_MAT_DEFAULT,
                             -1, nonUniform, 1, 0, nullptr);
  CHECK(ok >= 0);
  CHECK(h->scene.lights.empty());
  CHECK(h->scene.objects.back().lightId == -1);
  CHECK(h->scene.objects.back().nee == false);

  // uniform emissive sphere becomes an LT_SPHERE light
  sundog_xform_step uni[2] = {{SUNDOG_XF_SCALE, 2, 2, 2},
                              {SUNDOG_XF_TRANSLATE, 1, 5, 0}};
  int sid = sundog_add_object(h, SUNDOG_GK_SPHERE, -1, se, SUNDOG_MAT_DEFAULT,
                              -1, uni, 2, -1, nullptr);
  CHECK(sid >= 0);
  CHECK(h->scene.lights.size() == 1 && h->scene.lights[0].kind == LT_SPHERE);
  CHECK_NEAR(h->scene.lights[0].radius, 2.0, 1e-5);
  CHECK_NEAR(h->scene.lights[0].p.y, 5.0, 1e-5);
  CHECK_NEAR(h->scene.lights[0].L.x, 3.0, 1e-5);
  CHECK(h->scene.objects.back().lightId == 0);

  // textured emissive sphere must opt out of NEE
  sundog_xform_step us[1] = {{SUNDOG_XF_SCALE, 1, 1, 1}};
  expectAddFail(sundog_add_object(h, SUNDOG_GK_SPHERE, -1, temEm,
                                  SUNDOG_MAT_DEFAULT, -1, us, 1, -1, nullptr),
                "textured emissive sphere");

  // null front with real back face is a valid back-only surface
  int m0 = 0;
  int backOnly = sundog_add_object(h, SUNDOG_GK_PARABOLA, -1, SUNDOG_MAT_NONE,
                                   m0, -1, nullptr, 0, -1, nullptr);
  CHECK(backOnly >= 0);
  CHECK(h->scene.objects.back().matFront == MAT_NONE);
  CHECK(h->scene.objects.back().matBack != MAT_NONE);

  // cutout binds an alpha texture id to the object
  int cut = sundog_add_object(h, SUNDOG_GK_RECT, -1, m0, SUNDOG_MAT_DEFAULT,
                              tex, nullptr, 0, -1, nullptr);
  CHECK(cut >= 0);
  CHECK_MSG(h->scene.objects.back().cutoutTexId >= 0, "cutout not bound");
  CHECK(h->scene.textures[h->scene.objects.back().cutoutTexId].desc.kind ==
        TX_IMAGE);
  sundog_scene_destroy(h);
}

static void testPhysics() {
  sundog_scene* h = sundog_scene_create(nullptr);
  CHECK(sundog_set_camera(h, D3(0, 4, 9), D3(0, 0, 0), nullptr, kNaN, kNaN,
                          kNaN) == 0);
  CHECK(sundog_set_physics(h, D3(0, -9.81, 0), 0.005, 8.0, 0.7, 0.2, 16, 4,
                           0.01, 1.25) == SUNDOG_OK);
  CHECK(sundog_add_material_lambert(h, nullptr, -1) == 0);
  CHECK(sundog_add_mesh(h, "../assets/spot.obj", -1, nullptr) == 0);
  CHECK(sundog_add_mesh(h, "../assets/sparky.obj", -1, "GlassHead") == 1);
  CHECK(h->scene.meshes[0].usemtl.empty());
  CHECK(h->scene.meshes[1].usemtl == "GlassHead");  // group filter recorded

  sundog_xform_step floorS[1] = {{SUNDOG_XF_SCALE, 5, 5, 5}};
  sundog_physics_body floorBody{};
  floorBody.dynamic = 0;
  floorBody.density = kNaN;
  floorBody.friction = kNaN;
  floorBody.restitution = kNaN;
  floorBody.thickness = 0.4;
  CHECK(sundog_add_object(h, SUNDOG_GK_RECT, -1, 0, SUNDOG_MAT_DEFAULT, -1,
                          floorS, 1, -1, &floorBody) == 0);
  sundog_xform_step cowS[3] = {{SUNDOG_XF_SCALE, 0.5, 0.5, 0.5},
                               {SUNDOG_XF_ROTATE_Y, 45, 0, 0},
                               {SUNDOG_XF_TRANSLATE, 0, 3, 0}};
  sundog_physics_body cowBody{};
  cowBody.dynamic = 1;
  cowBody.density = 300;
  cowBody.has_velocity = 1;
  cowBody.velocity[0] = 0.1; cowBody.velocity[1] = -2; cowBody.velocity[2] = 0;
  cowBody.has_angular_velocity = 1;
  cowBody.angular_velocity[0] = 1; cowBody.angular_velocity[1] = 0;
  cowBody.angular_velocity[2] = -1;
  cowBody.friction = kNaN;
  cowBody.restitution = kNaN;
  cowBody.thickness = kNaN;
  CHECK(sundog_add_object(h, SUNDOG_GK_MESH, 0, 0, SUNDOG_MAT_DEFAULT, -1,
                          cowS, 3, -1, &cowBody) == 1);
  sundog_xform_step ballS[1] = {{SUNDOG_XF_TRANSLATE, 9, 1, 0}};
  CHECK(sundog_add_object(h, SUNDOG_GK_SPHERE, -1, 0, SUNDOG_MAT_DEFAULT, -1,
                          ballS, 1, -1, nullptr) == 2);

  const Scene& s = h->scene;
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
  sundog_scene_destroy(h);

  // a scene without a physics block rejects opted-in objects
  h = freshMini();
  sundog_physics_body dyn{};
  dyn.dynamic = 1;
  dyn.density = kNaN; dyn.friction = kNaN; dyn.restitution = kNaN;
  dyn.thickness = kNaN;
  expectAddFail(sundog_add_object(h, SUNDOG_GK_SPHERE, -1, 0,
                                  SUNDOG_MAT_DEFAULT, -1, nullptr, 0, -1, &dyn),
                "object physics without top-level block");
  sundog_scene_destroy(h);

  h = freshMini();
  CHECK(sundog_set_physics(h, nullptr, kNaN, kNaN, kNaN, kNaN, -1, -1, kNaN,
                           kNaN) == SUNDOG_OK);
  int em = sundog_add_material_emissive(h, nullptr, -1, 5.0, -1);  // phase 0
  CHECK(em >= 0);
  sundog_physics_body stat{};
  stat.density = kNaN; stat.friction = kNaN; stat.restitution = kNaN;
  stat.thickness = kNaN;
  expectAddFail(sundog_add_object(h, SUNDOG_GK_CYLINDER, -1, 0,
                                  SUNDOG_MAT_DEFAULT, -1, nullptr, 0, -1, &stat),
                "cylinder collider unsupported");
  expectAddFail(sundog_add_object(h, SUNDOG_GK_RECT, -1, 0, SUNDOG_MAT_DEFAULT,
                                  -1, nullptr, 0, -1, &dyn),
                "dynamic rect rejected");
  expectAddFail(sundog_add_object(h, SUNDOG_GK_SPHERE, -1, em,
                                  SUNDOG_MAT_DEFAULT, -1, nullptr, 0, -1, &dyn),
                "dynamic NEE area light rejected");
  // a dynamic emitter is fine when kept out of NEE
  int ok = sundog_add_object(h, SUNDOG_GK_SPHERE, -1, em, SUNDOG_MAT_DEFAULT,
                             -1, nullptr, 0, 0, &dyn);
  CHECK(ok >= 0);
  CHECK(h->scene.objects.back().physics.dynamic && h->scene.lights.empty());
  sundog_scene_destroy(h);
}

static void testFlames() {
  sundog_scene* h = freshMini();
  sundog_xform_step big[1] = {{SUNDOG_XF_SCALE, 8, 8, 8}};
  CHECK(sundog_add_object(h, SUNDOG_GK_RECT, -1, 0, SUNDOG_MAT_DEFAULT, -1,
                          big, 1, -1, nullptr) >= 0);
  CHECK(sundog_add_flame(h, D3(1, 0.2, -1), 1.5, 0.4, 25, 5, 2.5, 7, 16) == 0);
  const Scene& s = h->scene;
  CHECK(s.flames.size() == 1);
  const FlameDesc& f = s.flames[0];
  CHECK_NEAR(f.base.x, 1.0, 1e-6);
  CHECK_NEAR(f.height, 1.5, 1e-6);
  CHECK_NEAR(f.radius, 0.4, 1e-6);
  CHECK_NEAR(f.intensity, 25.0, 1e-5);
  CHECK_NEAR(f.sigma, 5.0, 1e-6);
  CHECK_NEAR(f.noiseScale, 2.5, 1e-6);
  CHECK(f.seed == 7u);
  CHECK_MSG(s.lights.size() == 2, "flame lights: %zu", s.lights.size());
  CHECK(s.lights[0].kind == LT_POINT && s.lights[1].kind == LT_POINT);
  CHECK_NEAR(s.lights[0].p.y, 0.2 + 0.35 * 1.5, 1e-5);
  CHECK_NEAR(s.lights[1].p.y, 0.2 + 0.70 * 1.5, 1e-5);
  CHECK_NEAR(s.lights[0].radius, 0.12, 1e-6);  // 0.3 * radius
  CHECK(s.lights[0].L.x > s.lights[1].L.x);    // 0.65/0.35 split
  // Embedded lights back-link to their owning flame (self-shadow exemption).
  CHECK(s.lights[0].flameId == 0 && s.lights[1].flameId == 0);
  // A second flame's lights point at flame 1.
  CHECK(sundog_add_flame(h, D3(-2, 0, 1), 1.0, 0.3, kNaN, kNaN, kNaN, -1,
                         kNaN) == 1);
  CHECK(s.lights.size() == 4);
  CHECK(s.lights[2].flameId == 1 && s.lights[3].flameId == 1);
  sundog_scene_destroy(h);

  // defaults
  h = freshMini();
  CHECK(addSphere(h, 0) >= 0);
  CHECK(sundog_add_flame(h, D3(0, 0, 0), 1, 0.5, kNaN, kNaN, kNaN, -1,
                         kNaN) == 0);
  CHECK_NEAR(h->scene.flames[0].intensity, 20.0, 1e-5);
  CHECK_NEAR(h->scene.flames[0].sigma, 4.0, 1e-6);
  CHECK_NEAR(h->scene.flames[0].noiseScale, 3.0, 1e-6);
  CHECK(h->scene.flames[0].seed == 0u);
  expectAddFail(sundog_add_flame(h, D3(0, 0, 0), 0, 0.5, kNaN, kNaN, kNaN, -1,
                                 kNaN), "flame zero height");
  sundog_scene_destroy(h);
}

static void testWater() {
  sundog_scene* h = sundog_scene_create(nullptr);
  CHECK(sundog_set_camera(h, D3(0, 2, 8), D3(0, 0, 0), nullptr, kNaN, kNaN,
                          kNaN) == 0);
  int lake = sundog_add_material_water(h, kNaN, nullptr, kNaN, kNaN, nullptr);
  int pool = sundog_add_material_water(h, 1.34, D3(0.6, 0.1, 0.05), 0.12, 3.5,
                                       nullptr);
  CHECK(lake == 0 && pool == 1);
  sundog_xform_step s30[1] = {{SUNDOG_XF_SCALE, 30, 30, 30}};
  CHECK(sundog_add_object(h, SUNDOG_GK_RECT, -1, lake, SUNDOG_MAT_DEFAULT, -1,
                          s30, 1, -1, nullptr) >= 0);
  const MaterialDesc& lk = h->scene.materials[0];
  CHECK(lk.kind == MT_WATER);
  CHECK_NEAR(lk.ior, 1.33, 1e-6);          // water default, not glass 1.5
  CHECK_NEAR(lk.absorb.x, 0.45, 1e-6);     // red absorbed fastest
  CHECK(lk.absorb.x > lk.absorb.y && lk.absorb.y > lk.absorb.z);
  CHECK_NEAR(lk.waveAmp, 0.05, 1e-6);
  CHECK_NEAR(lk.waveFreq, 2.0, 1e-6);
  CHECK_NEAR(lk.color.x, 1.0, 1e-6);       // AOV guide default
  const MaterialDesc& pl = h->scene.materials[1];
  CHECK_NEAR(pl.ior, 1.34, 1e-6);
  CHECK_NEAR(pl.absorb.y, 0.1, 1e-6);
  CHECK_NEAR(pl.waveAmp, 0.12, 1e-6);
  CHECK_NEAR(pl.waveFreq, 3.5, 1e-6);
  CHECK(h->scene.lights.empty());  // water registers no NEE light
  expectAddFail(sundog_add_material_water(h, kNaN, nullptr, -0.1, kNaN,
                                          nullptr), "negative wave_amp");
  expectAddFail(sundog_add_material_water(h, kNaN, D3(-0.1, 0, 0), kNaN, kNaN,
                                          nullptr), "negative absorb");
  sundog_scene_destroy(h);
}

static void testEnvmap() {
  sundog_scene* h = freshMini();
  CHECK(sundog_set_background_envmap(h, "../assets/sky.hdr", 90, 2.5, 0) ==
        SUNDOG_OK);
  CHECK(h->scene.bg.kind == BG_ENVMAP);
  CHECK(h->scene.env.file == "../assets/sky.hdr");
  CHECK_NEAR(h->scene.env.rotateDeg, 90.0, 1e-6);
  CHECK_NEAR(h->scene.env.intensity, 2.5, 1e-6);
  CHECK(h->scene.env.importance == false);
  sundog_scene_destroy(h);

  h = freshMini();  // defaults: rotate 0, intensity 1, importance on
  CHECK(sundog_set_background_envmap(h, "sky.hdr", kNaN, kNaN, -1) ==
        SUNDOG_OK);
  CHECK_NEAR(h->scene.env.rotateDeg, 0.0, 1e-6);
  CHECK_NEAR(h->scene.env.intensity, 1.0, 1e-6);
  CHECK(h->scene.env.importance == true);
  expectSetFail(sundog_set_background_envmap(h, nullptr, kNaN, kNaN, -1),
                "envmap missing file");
  expectSetFail(sundog_set_background_envmap(h, "sky.hdr", kNaN, -1.0, -1),
                "envmap negative intensity");
  sundog_scene_destroy(h);
}

// The light-list layout contract: [NEE area lights in object order] ->
// [2 point lights per flame] -> [explicit lights]. The phase state machine
// makes violating call orders an API error, not a silent reorder.
static void testLightOrderAndPhases() {
  sundog_scene* h = freshMini();
  int mesh = sundog_add_mesh(h, "../assets/sparky.obj", -1, "EmitYellow");
  CHECK(mesh == 0);
  int em = sundog_add_material_emissive(h, D3(1, 1, 1), -1, 4.0, -1);
  sundog_xform_step us[1] = {{SUNDOG_XF_SCALE, 1, 1, 1}};
  CHECK(sundog_add_object(h, SUNDOG_GK_RECT, -1, em, SUNDOG_MAT_DEFAULT, -1,
                          us, 1, -1, nullptr) >= 0);
  CHECK(sundog_add_object(h, SUNDOG_GK_MESH, mesh, em, SUNDOG_MAT_DEFAULT, -1,
                          us, 1, -1, nullptr) >= 0);
  CHECK(sundog_add_flame(h, D3(0, 0, 0), 1, 0.5, kNaN, kNaN, kNaN, -1, kNaN)
        == 0);
  CHECK(sundog_add_point_light(h, D3(0, 5, 0), D3(10, 10, 10), kNaN) >= 0);
  const Scene& s = h->scene;
  CHECK_MSG(s.lights.size() == 5, "light order: %zu lights", s.lights.size());
  CHECK(s.lights[0].kind == LT_RECT);    // NEE area lights first,
  CHECK(s.lights[1].kind == LT_MESH);    // in object order
  CHECK(s.lights[2].kind == LT_POINT);   // flame light 1
  CHECK(s.lights[3].kind == LT_POINT);   // flame light 2
  CHECK(s.lights[4].kind == LT_POINT);   // explicit light last
  CHECK_NEAR(s.lights[4].p.y, 5.0, 1e-6);
  // flameId back-links: only the embedded flame lights carry an owner. Pins
  // every construction site against the LightDesc{} zero-init trap (0 is a
  // valid flame index; a forgotten assignment would silently self-exempt).
  CHECK(s.lights[0].flameId == -1 && s.lights[1].flameId == -1);
  CHECK(s.lights[2].flameId == 0 && s.lights[3].flameId == 0);
  CHECK(s.lights[4].flameId == -1);

  // phase guard: config/objects/flames after a later phase are rejected
  expectAddFail(sundog_add_object(h, SUNDOG_GK_SPHERE, -1, 0,
                                  SUNDOG_MAT_DEFAULT, -1, nullptr, 0, -1,
                                  nullptr), "object after lights");
  expectAddFail(sundog_add_flame(h, D3(0, 0, 0), 1, 0.5, kNaN, kNaN, kNaN, -1,
                                 kNaN), "flame after lights");
  expectSetFail(sundog_set_render(h, 64, 64, -1, -1, kNaN, -1, kNaN, kNaN, -1,
                                  -1), "render settings after lights");
  expectAddFail(sundog_add_material_lambert(h, nullptr, -1),
                "material after lights");
  sundog_scene_destroy(h);
}

// Emissive meshes auto-register as NEE lights. Build time only records a
// placeholder (kind + emission): the OBJ loads in sundog_render, so triangle
// geometry (world CDF, pointers, area) is patched there — these tests assert
// registration, not sampling data.
static void testMeshLights() {
  sundog_scene* h = freshMini();
  int mesh = sundog_add_mesh(h, "../assets/sparky.obj", -1, "EmitYellow");
  int tex = sundog_add_texture_image(h, "textures/spot_texture.png", -1);
  int em = sundog_add_material_emissive(h, D3(1.0, 0.85, 0.2), -1, 6.0, -1);
  int temEm = sundog_add_material_emissive(h, nullptr, tex, 3.0, -1);
  CHECK(mesh == 0 && tex == 0 && em >= 0 && temEm >= 0);

  // non-uniform scale is fine for mesh lights (areas come from world verts)
  sundog_xform_step pose[2] = {{SUNDOG_XF_SCALE, 2, 1, 3},
                               {SUNDOG_XF_TRANSLATE, 0, 1, 0}};
  CHECK(sundog_add_object(h, SUNDOG_GK_MESH, mesh, em, SUNDOG_MAT_DEFAULT, -1,
                          pose, 2, -1, nullptr) >= 0);
  const Scene& s = h->scene;
  CHECK(s.lights.size() == 1);
  CHECK(s.lights[0].kind == LT_MESH);
  CHECK(s.objects.back().lightId == 0);
  CHECK_NEAR(s.lights[0].L.x, 6.0, 1e-5);      // color 1.0 * intensity 6
  CHECK(s.lights[0].texId == -1);
  CHECK(s.lights[0].flameId == -1);
  CHECK(s.lights[0].mNumTris == 0);            // placeholder until render
  CHECK(s.lights[0].mPositions == nullptr);

  // a textured emissive MESH is a valid NEE light (per-vertex uvs are
  // stable — do not copy the textured-sphere rejection)
  CHECK(sundog_add_object(h, SUNDOG_GK_MESH, mesh, temEm, SUNDOG_MAT_DEFAULT,
                          -1, pose, 2, -1, nullptr) >= 0);
  CHECK(s.lights.size() == 2);
  CHECK(s.lights[1].kind == LT_MESH);
  CHECK(s.lights[1].texId == tex);
  CHECK_NEAR(s.lights[1].L.x, 3.0, 1e-5);      // Li = tex(uv) * intensity

  // nee=0 opts out: no light, no back-link
  CHECK(sundog_add_object(h, SUNDOG_GK_MESH, mesh, em, SUNDOG_MAT_DEFAULT, -1,
                          pose, 2, 0, nullptr) >= 0);
  CHECK(s.lights.size() == 2);
  CHECK(s.objects.back().lightId == -1);

  // cutout + NEE light rejected on ANY shape (NEE would sample emission
  // inside the holes — the MIS sides would disagree); nee=0 keeps it legal
  expectAddFail(sundog_add_object(h, SUNDOG_GK_MESH, mesh, em,
                                  SUNDOG_MAT_DEFAULT, tex, pose, 2, -1,
                                  nullptr),
                "cutout mesh NEE light rejected");
  expectAddFail(sundog_add_object(h, SUNDOG_GK_RECT, -1, em,
                                  SUNDOG_MAT_DEFAULT, tex, pose, 2, -1,
                                  nullptr),
                "cutout rect NEE light rejected");
  CHECK(s.lights.size() == 2);  // no orphan lights from the rejections
  CHECK(sundog_add_object(h, SUNDOG_GK_MESH, mesh, em, SUNDOG_MAT_DEFAULT,
                          tex, pose, 2, 0, nullptr) >= 0);
  CHECK(s.objects.back().lightId == -1);
  sundog_scene_destroy(h);

  // dynamic emissive mesh + nee is rejected BEFORE the light is pushed
  h = freshMini();
  CHECK(sundog_set_physics(h, nullptr, kNaN, kNaN, kNaN, kNaN, -1, -1, kNaN,
                           kNaN) == SUNDOG_OK);
  mesh = sundog_add_mesh(h, "../assets/spot.obj", -1, nullptr);
  em = sundog_add_material_emissive(h, nullptr, -1, 5.0, -1);
  sundog_physics_body dyn{};
  dyn.dynamic = 1;
  dyn.density = kNaN; dyn.friction = kNaN; dyn.restitution = kNaN;
  dyn.thickness = kNaN;
  expectAddFail(sundog_add_object(h, SUNDOG_GK_MESH, mesh, em,
                                  SUNDOG_MAT_DEFAULT, -1, nullptr, 0, -1,
                                  &dyn),
                "dynamic mesh NEE light rejected");
  CHECK(h->scene.lights.empty());              // no orphan light
  sundog_scene_destroy(h);
}

// detSign: mirroring transforms keep the NEE light normal consistent.
static void testDetSignUnderMirror() {
  sundog_scene* h = freshMini();
  int em = sundog_add_material_emissive(h, D3(1, 1, 1), -1, 2.0, -1);
  sundog_xform_step mirror[1] = {{SUNDOG_XF_SCALE, -1, 1, 1}};
  CHECK(sundog_add_object(h, SUNDOG_GK_RECT, -1, em, SUNDOG_MAT_DEFAULT, -1,
                          mirror, 1, -1, nullptr) >= 0);
  const LightDesc& ld = h->scene.lights.back();
  CHECK_NEAR(ld.n.y, 1.0, 1e-5);  // stays +y despite det < 0
  CHECK_NEAR(ld.area, 4.0, 1e-5);
  sundog_scene_destroy(h);
}

static void testMaterialCap() {
  sundog_scene* h = sundog_scene_create(nullptr);
  for (int i = 0; i < (int)MAT_NONE; i++)
    CHECK(sundog_add_material_lambert(h, nullptr, -1) == i);
  expectAddFail(sundog_add_material_lambert(h, nullptr, -1),
                "material cap at MAT_NONE");
  sundog_scene_destroy(h);
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
  CHECK_NEAR(c.horizontal.x, 10.0, 1e-4);
  CHECK_NEAR(c.vertical.y, 10.0, 1e-4);
  CHECK_NEAR(c.lowerLeft.x, -5.0, 1e-4);
  CHECK_NEAR(c.lowerLeft.y, -5.0, 1e-4);
  CHECK_NEAR(c.lowerLeft.z, 0.0, 1e-4);
  CHECK_NEAR(c.lensRadius, 0.0, 0.0);
}

int main() {
  testMinimalDefaults();
  testSmokeEquivalent();
  testFeaturesEquivalent();
  testErrorPaths();
  testPhysics();
  testFlames();
  testWater();
  testEnvmap();
  testLightOrderAndPhases();
  testMeshLights();
  testDetSignUnderMirror();
  testMaterialCap();
  testMakeCamera();
  TEST_DONE("test_scene_build");
}

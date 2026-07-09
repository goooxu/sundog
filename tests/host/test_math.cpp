// sundog host tests: device/math.cuh — vectors, reflect/refract, Onb, Affine.
#include "math.cuh"
#include "check.h"

using namespace sd;

static void checkF3Near(float3 a, float3 b, double eps, const char* what) {
  CHECK_MSG(std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps &&
                std::fabs(a.z - b.z) <= eps,
            "%s: (%g,%g,%g) vs (%g,%g,%g)", what, a.x, a.y, a.z, b.x, b.y, b.z);
}

static void testVectorOps() {
  float3 a = f3(1, 2, 3), b = f3(4, -5, 6);
  checkF3Near(a + b, f3(5, -3, 9), 0, "add");
  checkF3Near(a - b, f3(-3, 7, -3), 0, "sub");
  checkF3Near(-a, f3(-1, -2, -3), 0, "neg");
  checkF3Near(a * b, f3(4, -10, 18), 0, "cmul");
  checkF3Near(a * 2.0f, f3(2, 4, 6), 0, "smul");
  checkF3Near(2.0f * a, f3(2, 4, 6), 0, "smul2");
  checkF3Near(a / 2.0f, f3(0.5f, 1, 1.5f), 1e-7, "sdiv");
  checkF3Near(a / b, f3(0.25f, -0.4f, 0.5f), 1e-7, "cdiv");
  CHECK_NEAR(dot(a, b), 4 - 10 + 18, 1e-6);
  // cross: hand-computed (2*6-3*-5, 3*4-1*6, 1*-5-2*4) = (27, 6, -13)
  checkF3Near(cross(a, b), f3(27, 6, -13), 1e-5, "cross");
  checkF3Near(cross(f3(1, 0, 0), f3(0, 1, 0)), f3(0, 0, 1), 0, "cross axes");
  CHECK_NEAR(length(f3(3, 4, 0)), 5.0, 1e-6);
  CHECK_NEAR(length2(f3(3, 4, 0)), 25.0, 1e-5);
  float3 n = normalize(f3(3, 4, 0));
  checkF3Near(n, f3(0.6f, 0.8f, 0), 1e-6, "normalize");
  CHECK_NEAR(length(normalize(f3(-2, 7, 0.3f))), 1.0, 1e-6);
  CHECK_NEAR(clampf(5, 0, 1), 1, 0);
  CHECK_NEAR(clampf(-5, 0, 1), 0, 0);
  CHECK_NEAR(clampf(0.25f, 0, 1), 0.25, 0);
  checkF3Near(lerp3(f3(0, 0, 0), f3(2, 4, 8), 0.5f), f3(1, 2, 4), 1e-6, "lerp3");
  CHECK_NEAR(maxComp(f3(-1, 7, 3)), 7, 0);
}

static void testReflect() {
  // 45 degrees off a floor: (1,-1,0)/sqrt2 -> (1,1,0)/sqrt2
  float s = 1.0f / std::sqrt(2.0f);
  float3 r = reflect(f3(s, -s, 0), f3(0, 1, 0));
  checkF3Near(r, f3(s, s, 0), 1e-6, "reflect 45");
  CHECK_NEAR(length(r), 1.0, 1e-6);
  // grazing: tiny normal component flips sign, tangential preserved
  float3 v = normalize(f3(1, -1e-4f, 0));
  float3 g = reflect(v, f3(0, 1, 0));
  CHECK_NEAR(g.x, v.x, 1e-7);
  CHECK_NEAR(g.y, -v.y, 1e-7);
  CHECK_NEAR(length(g), 1.0, 1e-6);
  // normal incidence bounces straight back
  checkF3Near(reflect(f3(0, -1, 0), f3(0, 1, 0)), f3(0, 1, 0), 0, "reflect normal");
}

static void testRefract() {
  float3 n = f3(0, 1, 0);
  float3 out;
  // normal incidence: direction unchanged
  CHECK(refract(f3(0, -1, 0), n, 1.0f / 1.5f, out));
  checkF3Near(out, f3(0, -1, 0), 1e-6, "refract normal incidence");

  // 45 deg entering ior 1.5: Snell sin_t = sin_i / 1.5
  float s = 1.0f / std::sqrt(2.0f);
  float3 v = f3(s, -s, 0);
  CHECK(refract(v, n, 1.0f / 1.5f, out));
  CHECK_NEAR(length(out), 1.0, 1e-5);
  CHECK(out.y < 0.0f);  // continues into the surface
  float sinT = s / 1.5f;
  CHECK_NEAR(out.x, sinT, 1e-5);                        // tangential = sin_t
  CHECK_NEAR(out.y, -std::sqrt(1.0f - sinT * sinT), 1e-5);  // cos_t
  CHECK_NEAR(out.z, 0.0, 1e-7);                         // stays in plane

  // total internal reflection: 45 deg leaving ior 1.5 (critical ~41.8 deg)
  CHECK(!refract(v, n, 1.5f, out));

  // grazing incidence entering (eta<1): still refracts, sin_t ~= eta
  float3 gv = normalize(f3(1, -1e-3f, 0));
  CHECK(refract(gv, n, 1.0f / 1.5f, out));
  CHECK_NEAR(length(out), 1.0, 1e-4);
  CHECK_NEAR(out.x, gv.x / 1.5f, 1e-3);
  // grazing incidence leaving (eta>1): TIR
  CHECK(!refract(gv, n, 1.5f, out));
}

static void testOnb() {
  const float3 normals[] = {
      f3(0, 0, 1), f3(0, 0, -1), f3(0, 1, 0), f3(1, 0, 0),
      normalize(f3(1, 2, 3)), normalize(f3(-0.3f, 0.9f, -0.5f)),
      normalize(f3(1e-4f, -1e-4f, -1.0f)),  // near -z pole (Duff branch)
  };
  for (float3 n : normals) {
    Onb onb(n);
    CHECK_NEAR(length(onb.t), 1.0, 1e-5);
    CHECK_NEAR(length(onb.b), 1.0, 1e-5);
    CHECK_NEAR(length(onb.n), 1.0, 1e-5);
    CHECK_NEAR(dot(onb.t, onb.b), 0.0, 1e-6);
    CHECK_NEAR(dot(onb.t, onb.n), 0.0, 1e-6);
    CHECK_NEAR(dot(onb.b, onb.n), 0.0, 1e-6);
    // right-handed: t x b == n
    checkF3Near(cross(onb.t, onb.b), n, 1e-5, "onb handedness");
    // toWorld/toLocal round trip
    float3 v = f3(0.3f, -0.5f, 0.8f);
    float3 rt = onb.toLocal(onb.toWorld(v));
    checkF3Near(rt, v, 1e-5, "onb roundtrip");
    // toWorld(0,0,1) == n
    checkF3Near(onb.toWorld(f3(0, 0, 1)), n, 1e-6, "onb z->n");
  }
}

static void testRotateMatrices() {
  // rotate_z(30 deg): [c -s 0; s c 0; 0 0 1] hand-computed
  float rad = 30.0f * SD_PI / 180.0f;
  float c = std::cos(rad), s = std::sin(rad);
  Affine rz = affineRotateZ(rad);
  const float expZ[12] = {c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0};
  for (int i = 0; i < 12; i++) CHECK_NEAR(rz.m[i], expZ[i], 1e-6);
  Affine rx = affineRotateX(rad);
  const float expX[12] = {1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0};
  for (int i = 0; i < 12; i++) CHECK_NEAR(rx.m[i], expX[i], 1e-6);
  Affine ry = affineRotateY(rad);
  const float expY[12] = {c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0};
  for (int i = 0; i < 12; i++) CHECK_NEAR(ry.m[i], expY[i], 1e-6);

  // right-handed axis rotations by 90 deg
  float h = SD_PI / 2.0f;
  checkF3Near(affineRotateZ(h).applyVector(f3(1, 0, 0)), f3(0, 1, 0), 1e-6, "Rz x->y");
  checkF3Near(affineRotateX(h).applyVector(f3(0, 1, 0)), f3(0, 0, 1), 1e-6, "Rx y->z");
  checkF3Near(affineRotateY(h).applyVector(f3(0, 0, 1)), f3(1, 0, 0), 1e-6, "Ry z->x");
  checkF3Near(affineRotateY(h).applyVector(f3(1, 0, 0)), f3(0, 0, -1), 1e-6, "Ry x->-z");
}

static void testAffine() {
  Affine id = Affine::identity();
  checkF3Near(id.applyPoint(f3(1, 2, 3)), f3(1, 2, 3), 0, "identity point");

  // scale then rotate_x(90) then translate; SCENES.md: [scale, rotate,
  // translate] means translate(rotate(scale(p))). mul(a,b) applies b first.
  Affine S = affineScale(f3(2, 1, 2));
  Affine R = affineRotateX(SD_PI / 2.0f);
  Affine T = affineTranslate(f3(0, 2, 0));
  Affine total = mul(T, mul(R, S));
  // p=(1,0,1): scale->(2,0,2); rotX90: (x,-z,y)->(2,-2,0); translate->(2,0,0)
  checkF3Near(total.applyPoint(f3(1, 0, 1)), f3(2, 0, 0), 1e-5, "compose S->R->T");
  // matches step-by-step application
  float3 p = f3(-0.3f, 0.7f, 1.1f);
  float3 seq = T.applyPoint(R.applyPoint(S.applyPoint(p)));
  checkF3Near(total.applyPoint(p), seq, 1e-5, "compose vs sequential");
  // wrong order (scale last) gives a different point -> order matters
  Affine wrong = mul(S, mul(R, T));
  float3 wp = wrong.applyPoint(f3(1, 0, 1));
  CHECK(std::fabs(wp.x - 2) + std::fabs(wp.y - 0) + std::fabs(wp.z - 0) > 1e-3);

  // applyVector ignores translation, applies linear part
  checkF3Near(T.applyVector(f3(1, 2, 3)), f3(1, 2, 3), 0, "translate on vector");
  checkF3Near(affineScale(f3(2, 3, 4)).applyVector(f3(1, 1, 1)), f3(2, 3, 4), 0,
              "non-uniform scale vector");
  checkF3Near(total.applyVector(f3(1, 0, 1)), f3(2, -2, 0), 1e-5, "compose vector");

  // mul associativity on this triple
  Affine ab_c = mul(mul(T, R), S);
  for (int i = 0; i < 12; i++) CHECK_NEAR(ab_c.m[i], total.m[i], 1e-6);
}

static void testOffsetRay() {
  // at origin: offset exactly 1e-4 along n
  float3 n = f3(0, 1, 0);
  float3 q = offsetRay(f3(0, 0, 0), n);
  checkF3Near(q, f3(0, 1e-4f, 0), 1e-9, "offsetRay origin");
  // offset direction is +n (positive dot, no tangential drift)
  float3 p = f3(3, -2, 5);
  float3 nn = normalize(f3(1, 2, -2));
  float3 d = offsetRay(p, nn) - p;
  CHECK(dot(d, nn) > 0.0f);
  CHECK_NEAR(dot(normalize(d), nn), 1.0, 1e-6);  // parallel to n
  // scales up with distance from origin
  float3 far = f3(1000, 0, 0);
  float lenNear = length(offsetRay(p, nn) - p);
  float lenFar = length(offsetRay(far, nn) - far);
  CHECK(lenFar > lenNear);
  CHECK_NEAR(lenFar, 1e-4f * 1001.0f, 1e-4);  // float rounding at |p|~1000
}

int main() {
  testVectorOps();
  testReflect();
  testRefract();
  testOnb();
  testRotateMatrices();
  testAffine();
  testOffsetRay();
  TEST_DONE("test_math");
}

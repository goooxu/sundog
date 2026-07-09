// sundog host tests: device/intersect.cuh — analytic quadric intersections
// against hand-computed rays/hits/normals/UVs.
#include "intersect.cuh"
#include "check.h"

using namespace sd;

static const float TMIN = 1e-3f;
static const float TMAX = 1e16f;

static void checkF3Near(float3 a, float3 b, double eps, const char* what) {
  CHECK_MSG(std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps &&
                std::fabs(a.z - b.z) <= eps,
            "%s: (%g,%g,%g) vs (%g,%g,%g)", what, a.x, a.y, a.z, b.x, b.y, b.z);
}

static void testSphere() {
  // ray from +z toward origin: entry t=2 at (0,0,1), exit t=4 at (0,0,-1)
  QuadricHits r = intersectSphere(f3(0, 0, 3), f3(0, 0, -1), TMIN, TMAX);
  CHECK_MSG(r.count == 2, "sphere both roots: count=%d", r.count);
  CHECK_NEAR(r.hits[0].t, 2.0, 1e-5);
  CHECK_NEAR(r.hits[1].t, 4.0, 1e-5);
  checkF3Near(r.hits[0].n, f3(0, 0, 1), 1e-6, "sphere entry normal");
  checkF3Near(r.hits[1].n, f3(0, 0, -1), 1e-6, "sphere exit normal");
  // UV at (0,0,1): u=(atan2(1,0)+pi)/2pi=0.75, v=(asin(0)+pi/2)/pi=0.5
  CHECK_NEAR(r.hits[0].u, 0.75, 1e-5);
  CHECK_NEAR(r.hits[0].v, 0.5, 1e-5);
  // UV at (0,0,-1): u=(atan2(-1,0)+pi)/2pi=0.25
  CHECK_NEAR(r.hits[1].u, 0.25, 1e-5);
  // north pole hit: v=1
  QuadricHits rp = intersectSphere(f3(0, 3, 0), f3(0, -1, 0), TMIN, TMAX);
  CHECK(rp.count == 2);
  CHECK_NEAR(rp.hits[0].v, 1.0, 1e-4);
  CHECK_NEAR(rp.hits[1].v, 0.0, 1e-4);

  // from inside: first root t=-1 is behind tmin -> report second root only
  r = intersectSphere(f3(0, 0, 0), f3(1, 0, 0), TMIN, TMAX);
  CHECK_MSG(r.count == 1, "sphere inside: count=%d", r.count);
  CHECK_NEAR(r.hits[0].t, 1.0, 1e-6);
  checkF3Near(r.hits[0].n, f3(1, 0, 0), 1e-6, "sphere inside normal (outward)");

  // tmax filter drops the far root
  r = intersectSphere(f3(0, 0, 3), f3(0, 0, -1), TMIN, 3.0f);
  CHECK(r.count == 1);
  CHECK_NEAR(r.hits[0].t, 2.0, 1e-5);
  // tmin filter drops the near root
  r = intersectSphere(f3(0, 0, 3), f3(0, 0, -1), 2.5f, TMAX);
  CHECK(r.count == 1);
  CHECK_NEAR(r.hits[0].t, 4.0, 1e-5);
  // both outside window
  r = intersectSphere(f3(0, 0, 3), f3(0, 0, -1), 4.5f, TMAX);
  CHECK(r.count == 0);

  // clean miss
  r = intersectSphere(f3(0, 2, 3), f3(0, 0, -1), TMIN, TMAX);
  CHECK(r.count == 0);
  // non-unit direction: t scales (d length 2 -> t halves)
  r = intersectSphere(f3(0, 0, 3), f3(0, 0, -2), TMIN, TMAX);
  CHECK(r.count == 2);
  CHECK_NEAR(r.hits[0].t, 1.0, 1e-5);
}

static void testRect() {
  // straight down onto (0.5, 0, 0.5): u=(x+1)/2=0.75, v=(z+1)/2=0.75
  QuadricHits r = intersectRect(f3(0.5f, 2, 0.5f), f3(0, -1, 0), TMIN, TMAX);
  CHECK_MSG(r.count == 1, "rect hit: count=%d", r.count);
  CHECK_NEAR(r.hits[0].t, 2.0, 1e-6);
  checkF3Near(r.hits[0].n, f3(0, 1, 0), 0, "rect normal");
  CHECK_NEAR(r.hits[0].u, 0.75, 1e-6);
  CHECK_NEAR(r.hits[0].v, 0.75, 1e-6);
  // corner mapping: (-1,0,-1) -> uv (0,0)
  r = intersectRect(f3(-0.999f, 1, -0.999f), f3(0, -1, 0), TMIN, TMAX);
  CHECK(r.count == 1);
  CHECK_NEAR(r.hits[0].u, 0.0005, 1e-4);
  CHECK_NEAR(r.hits[0].v, 0.0005, 1e-4);
  // just outside the [-1,1]^2 boundary: miss
  CHECK(intersectRect(f3(1.0001f, 1, 0), f3(0, -1, 0), TMIN, TMAX).count == 0);
  CHECK(intersectRect(f3(0, 1, -1.0001f), f3(0, -1, 0), TMIN, TMAX).count == 0);
  // just inside: hit
  CHECK(intersectRect(f3(0.9999f, 1, 0.9999f), f3(0, -1, 0), TMIN, TMAX).count == 1);
  // parallel ray (d.y == 0): miss
  CHECK(intersectRect(f3(0, 0.5f, 0), f3(1, 0, 0), TMIN, TMAX).count == 0);
  // plane behind origin: t<0 filtered
  CHECK(intersectRect(f3(0, 1, 0), f3(0, 1, 0), TMIN, TMAX).count == 0);
  // from below still reports the +Y canonical normal
  r = intersectRect(f3(0, -1, 0), f3(0, 1, 0), TMIN, TMAX);
  CHECK(r.count == 1);
  checkF3Near(r.hits[0].n, f3(0, 1, 0), 0, "rect canonical normal from below");
}

static void testDisk() {
  // hit at (0.5, 0, 0): u=(atan2(0,0.5)+pi)/2pi=0.5, v=radius=0.5
  QuadricHits r = intersectDisk(f3(0.5f, 1, 0), f3(0, -1, 0), TMIN, TMAX);
  CHECK_MSG(r.count == 1, "disk hit: count=%d", r.count);
  CHECK_NEAR(r.hits[0].t, 1.0, 1e-6);
  checkF3Near(r.hits[0].n, f3(0, 1, 0), 0, "disk normal");
  CHECK_NEAR(r.hits[0].u, 0.5, 1e-5);
  CHECK_NEAR(r.hits[0].v, 0.5, 1e-6);
  // hit at (0, 0, -0.25): u=(atan2(-0.25,0)+pi)/2pi=0.25, v=0.25
  r = intersectDisk(f3(0, 1, -0.25f), f3(0, -1, 0), TMIN, TMAX);
  CHECK(r.count == 1);
  CHECK_NEAR(r.hits[0].u, 0.25, 1e-5);
  CHECK_NEAR(r.hits[0].v, 0.25, 1e-6);
  // radius boundary: rad=1.0001 miss, rad=0.9999 hit (v -> 1)
  CHECK(intersectDisk(f3(1.0001f, 1, 0), f3(0, -1, 0), TMIN, TMAX).count == 0);
  r = intersectDisk(f3(0.9999f, 1, 0), f3(0, -1, 0), TMIN, TMAX);
  CHECK(r.count == 1);
  CHECK_NEAR(r.hits[0].v, 0.9999, 1e-5);
  // corner of the bounding square but outside the circle: miss
  CHECK(intersectDisk(f3(0.9f, 1, 0.9f), f3(0, -1, 0), TMIN, TMAX).count == 0);
  // parallel ray: miss
  CHECK(intersectDisk(f3(0, 0.5f, 0), f3(1, 0, 0), TMIN, TMAX).count == 0);
}

static void testCylinder() {
  // ray along -x at y=0: entry (1,0,0) t=2, exit (-1,0,0) t=4
  QuadricHits r = intersectCylinder(f3(3, 0, 0), f3(-1, 0, 0), TMIN, TMAX);
  CHECK_MSG(r.count == 2, "cylinder both roots: count=%d", r.count);
  CHECK_NEAR(r.hits[0].t, 2.0, 1e-5);
  CHECK_NEAR(r.hits[1].t, 4.0, 1e-5);
  checkF3Near(r.hits[0].n, f3(1, 0, 0), 1e-6, "cyl entry normal");
  checkF3Near(r.hits[1].n, f3(-1, 0, 0), 1e-6, "cyl exit normal");
  // UV at (1,0,0): u=(atan2(0,1)+pi)/2pi=0.5, v=(0+1)/2=0.5
  CHECK_NEAR(r.hits[0].u, 0.5, 1e-5);
  CHECK_NEAR(r.hits[0].v, 0.5, 1e-5);
  // UV at (-1,0,0): atan2(0,-1)=pi -> u=1
  CHECK_NEAR(r.hits[1].u, 1.0, 1e-5);

  // |y|>1 filter: same ray at height 2 misses the open cylinder
  CHECK(intersectCylinder(f3(3, 2, 0), f3(-1, 0, 0), TMIN, TMAX).count == 0);
  // slanted ray: entry inside |y|<=1, exit filtered out
  r = intersectCylinder(f3(3, -0.9f, 0), f3(-1, 0.5f, 0), TMIN, TMAX);
  CHECK_MSG(r.count == 1, "cylinder y-filter: count=%d", r.count);
  CHECK_NEAR(r.hits[0].t, 2.0, 1e-5);          // p=(1, 0.1, 0)
  CHECK_NEAR(r.hits[0].v, 0.55, 1e-5);         // (0.1+1)/2
  checkF3Near(r.hits[0].n, f3(1, 0, 0), 1e-6, "cyl slanted normal (y-free)");
  // axis-parallel ray (a==0): open ends -> no hit even inside the tube
  CHECK(intersectCylinder(f3(0.5f, -3, 0), f3(0, 1, 0), TMIN, TMAX).count == 0);
  // miss to the side
  CHECK(intersectCylinder(f3(3, 0, 2), f3(-1, 0, 0), TMIN, TMAX).count == 0);
  // tmax filter
  r = intersectCylinder(f3(3, 0, 0), f3(-1, 0, 0), TMIN, 3.0f);
  CHECK(r.count == 1);
  CHECK_NEAR(r.hits[0].t, 2.0, 1e-5);
}

static void testParabola() {
  // vertical ray -> quadratic degenerates to linear (A=0):
  // o=(0.5,2,0), d=(0,-1,0): B=1, C=0.5*0.25-2 -> t=1.875, p=(0.5,0.125,0)
  QuadricHits r = intersectParabola(f3(0.5f, 2, 0), f3(0, -1, 0), TMIN, TMAX);
  CHECK_MSG(r.count == 1, "parabola vertical: count=%d", r.count);
  CHECK_NEAR(r.hits[0].t, 1.875, 1e-5);
  checkF3Near(r.hits[0].n, normalize(f3(0.5f, -1, 0)), 1e-5, "parabola normal");
  CHECK_NEAR(r.hits[0].u, 0.5, 1e-5);  // atan2(0,0.5)=0 -> 0.5
  CHECK_NEAR(r.hits[0].v, 0.5, 1e-5);  // radius
  // vertical ray outside r<=1 cap: surface point exists but is filtered
  CHECK(intersectParabola(f3(1.2f, 2, 0), f3(0, -1, 0), TMIN, TMAX).count == 0);

  // horizontal ray, two roots: o=(-2,0.125,0), d=(1,0,0)
  // A=0.5, B=-2, C=1.875 -> t=1.5 (x=-0.5) and t=2.5 (x=0.5); y=0.5*x^2=0.125
  r = intersectParabola(f3(-2, 0.125f, 0), f3(1, 0, 0), TMIN, TMAX);
  CHECK_MSG(r.count == 2, "parabola two roots: count=%d", r.count);
  CHECK_NEAR(r.hits[0].t, 1.5, 1e-5);
  CHECK_NEAR(r.hits[1].t, 2.5, 1e-5);
  checkF3Near(r.hits[0].n, normalize(f3(-0.5f, -1, 0)), 1e-5, "parabola n0");
  checkF3Near(r.hits[1].n, normalize(f3(0.5f, -1, 0)), 1e-5, "parabola n1");
  CHECK_NEAR(r.hits[0].v, 0.5, 1e-5);
  CHECK_NEAR(r.hits[1].v, 0.5, 1e-5);
  // u at (-0.5,...): atan2(0,-0.5)=pi -> u=1
  CHECK_NEAR(r.hits[0].u, 1.0, 1e-5);

  // horizontal ray above the rim (y>0.5 cap region): r^2>1 at intersections
  CHECK(intersectParabola(f3(-2, 0.72f, 0), f3(1, 0, 0), TMIN, TMAX).count == 0);
  // ray below the vertex pointing down: no intersection
  CHECK(intersectParabola(f3(0, -1, 0), f3(0, -1, 0), TMIN, TMAX).count == 0);
  // tmin/tmax filtering on the two-root case
  r = intersectParabola(f3(-2, 0.125f, 0), f3(1, 0, 0), 2.0f, TMAX);
  CHECK(r.count == 1);
  CHECK_NEAR(r.hits[0].t, 2.5, 1e-5);
  r = intersectParabola(f3(-2, 0.125f, 0), f3(1, 0, 0), TMIN, 2.0f);
  CHECK(r.count == 1);
  CHECK_NEAR(r.hits[0].t, 1.5, 1e-5);
}

static void testDispatchAndAabb() {
  // intersectQuadric routes by kind
  CHECK(intersectQuadric(GK_SPHERE, f3(0, 0, 3), f3(0, 0, -1), TMIN, TMAX).count == 2);
  CHECK(intersectQuadric(GK_RECT, f3(0, 1, 0), f3(0, -1, 0), TMIN, TMAX).count == 1);
  CHECK(intersectQuadric(GK_DISK, f3(0, 1, 0), f3(0, -1, 0), TMIN, TMAX).count == 1);
  CHECK(intersectQuadric(GK_CYLINDER, f3(3, 0, 0), f3(-1, 0, 0), TMIN, TMAX).count == 2);
  CHECK(intersectQuadric(GK_PARABOLA, f3(0, 2, 0), f3(0, -1, 0), TMIN, TMAX).count == 1);
  CHECK(intersectQuadric(999, f3(0, 0, 3), f3(0, 0, -1), TMIN, TMAX).count == 0);
  // AABBs contain the canonical shapes
  float lo[3], hi[3];
  quadricAabb(GK_PARABOLA, lo, hi);
  CHECK(lo[0] <= -1 && hi[0] >= 1 && lo[1] <= 0 && hi[1] >= 0.5f);
  quadricAabb(GK_SPHERE, lo, hi);
  CHECK(lo[1] <= -1 && hi[1] >= 1);
  quadricAabb(GK_RECT, lo, hi);
  CHECK(lo[1] <= 0 && hi[1] >= 0 && hi[0] >= 1);
}

int main() {
  testSphere();
  testRect();
  testDisk();
  testCylinder();
  testParabola();
  testDispatchAndAabb();
  TEST_DONE("test_intersect");
}

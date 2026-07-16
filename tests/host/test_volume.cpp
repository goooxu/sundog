// sundog host tests: flame shadow transmittance (device/volume.cuh, SD_HD so
// it host-compiles). Pins flameTransmittance's draw contract (exactly one
// rnd() per entered, non-skipped flame; zero otherwise), the skipFlame
// exemption, and — most load-bearing — its bit-exact agreement with
// marchFlames' transmittance track: the two marchers must share one
// discretization forever, or radiance and shadow attenuation drift apart.
#include "check.h"

#include "volume.cuh"

using namespace sd;

static FlameDesc mkFlame(float3 base, float height, float radius, float sigma,
                         unsigned seed) {
  FlameDesc fl{};
  fl.base = base;
  fl.height = height;
  fl.radius = radius;
  fl.intensity = 20.0f;  // irrelevant to transmittance, exercised by cross-check
  fl.sigma = sigma;
  fl.noiseScale = 3.0f;
  fl.seed = seed;
  return fl;
}

// A segment crossing the flame's core at mid-height.
static const float3 kOrigin = {-2.0f, 1.0f, 0.0f};
static const float3 kDir = {1.0f, 0.0f, 0.0f};

static void testMissDrawsNothing() {
  FlameDesc fl = mkFlame(f3(0, 0, 0), 2.0f, 0.5f, 8.0f, 7);
  Pcg32 rng = Pcg32::init(1, 2);
  Pcg32 before = rng;
  // Segment passes 5 units from the axis: bounding cylinder never entered.
  float tr = flameTransmittance(&fl, 1, f3(-2, 1, 5), kDir, 4.0f, rng, -1);
  CHECK(tr == 1.0f);                    // exact: the multiply never ran
  CHECK(rng.state == before.state);     // zero draws
  // Segment ends before the cylinder starts (x = -0.5 is at t = 1.5).
  rng = before;
  tr = flameTransmittance(&fl, 1, kOrigin, kDir, 1.0f, rng, -1);
  CHECK(tr == 1.0f && rng.state == before.state);
}

static void testHitDrawsOnce() {
  FlameDesc fl = mkFlame(f3(0, 0, 0), 2.0f, 0.5f, 8.0f, 7);
  Pcg32 rng = Pcg32::init(1, 2);
  Pcg32 clone = rng;
  float tr = flameTransmittance(&fl, 1, kOrigin, kDir, 4.0f, rng, -1);
  CHECK(tr > 0.0f && tr < 0.99f);       // sigma 8 through the core attenuates
  clone.next();                         // exactly one jitter draw
  CHECK(rng.state == clone.state);
}

static void testSkipFlameExempts() {
  FlameDesc fl = mkFlame(f3(0, 0, 0), 2.0f, 0.5f, 8.0f, 7);
  Pcg32 rng = Pcg32::init(1, 2);
  Pcg32 before = rng;
  float tr = flameTransmittance(&fl, 1, kOrigin, kDir, 4.0f, rng, 0);
  CHECK(tr == 1.0f);                    // owner skipped entirely
  CHECK(rng.state == before.state);     // and draws nothing

  // Two flames on the z axis, segment running straight down it: both
  // cylinders are entered (|x| = 0 < radius throughout).
  FlameDesc two[2] = {fl, mkFlame(f3(0, 0, 1.5f), 2.0f, 0.5f, 8.0f, 11)};
  float3 axis = {0.0f, 0.0f, 1.0f};
  rng = Pcg32::init(3, 4);
  Pcg32 cloneA = rng;
  float trBoth = flameTransmittance(two, 2, f3(0, 1, -1), axis, 4.0f, rng, -1);
  cloneA.next();
  cloneA.next();                        // both entered: two draws
  CHECK(rng.state == cloneA.state);
  rng = Pcg32::init(3, 4);
  Pcg32 cloneB = rng;
  float trSkip = flameTransmittance(two, 2, f3(0, 1, -1), axis, 4.0f, rng, 1);
  cloneB.next();                        // flame 1 skipped: one draw
  CHECK(rng.state == cloneB.state);
  CHECK(trSkip >= trBoth);              // skipping an absorber can only help
}

static void testMatchesMarchFlames() {
  // Same segment, cloned RNG: flameTransmittance must reproduce marchFlames'
  // transmittance output bit-for-bit (same jitter, same steps, same expf).
  FlameDesc fl = mkFlame(f3(0, 0, 0), 2.0f, 0.5f, 4.5f, 3);
  Pcg32 rngA = Pcg32::init(9, 5);
  Pcg32 rngB = rngA;
  float3 trans = f3(1.0f);
  marchFlames(&fl, 1, kOrigin, kDir, 4.0f, rngA, trans);
  float tr = flameTransmittance(&fl, 1, kOrigin, kDir, 4.0f, rngB, -1);
  CHECK(trans.x == tr && trans.y == tr && trans.z == tr);
  CHECK(rngA.state == rngB.state);      // identical draw counts too
}

static void testSigmaMonotonic() {
  // Same RNG state -> same jitter and sample points; doubling sigma can only
  // shrink every per-step factor.
  FlameDesc lo = mkFlame(f3(0, 0, 0), 2.0f, 0.5f, 4.0f, 7);
  FlameDesc hi = lo;
  hi.sigma = 8.0f;
  Pcg32 rngA = Pcg32::init(1, 2);
  Pcg32 rngB = rngA;
  float trLo = flameTransmittance(&lo, 1, kOrigin, kDir, 4.0f, rngA, -1);
  float trHi = flameTransmittance(&hi, 1, kOrigin, kDir, 4.0f, rngB, -1);
  CHECK(trHi <= trLo);
}

int main() {
  testMissDrawsNothing();
  testHitDrawsOnce();
  testSkipFlameExempts();
  testMatchesMarchFlames();
  testSigmaMonotonic();
  TEST_DONE("test_volume");
}

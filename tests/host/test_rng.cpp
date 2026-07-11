// sundog host tests: device/rng.cuh — PCG32 reference sequence, rnd() range,
// sphere/hemisphere sampling sanity.
#include "rng.cuh"
#include "check.h"

using namespace sd;

static void testPcg32Reference() {
  // PCG official reference: pcg32_srandom(42, 54) -> first outputs.
  // If these mismatch, the Pcg32 implementation diverges from pcg-random.org.
  Pcg32 rng = Pcg32::init(42, 54);
  const uint32_t expected[6] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u,
                                0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
  for (int i = 0; i < 6; i++) {
    uint32_t got = rng.next();
    CHECK_MSG(got == expected[i], "pcg32(42,54) output %d: got 0x%08x want 0x%08x",
              i, got, expected[i]);
  }
}

static void testDeterminism() {
  Pcg32 a = Pcg32::init(1234, 5);
  Pcg32 b = Pcg32::init(1234, 5);
  for (int i = 0; i < 100; i++) CHECK(a.next() == b.next());
  // different streams differ
  Pcg32 c = Pcg32::init(1234, 6);
  int same = 0;
  Pcg32 d = Pcg32::init(1234, 5);
  for (int i = 0; i < 100; i++) same += (c.next() == d.next());
  CHECK(same < 5);
}

static void testRndRange() {
  Pcg32 rng = Pcg32::init(7, 0);
  double sum = 0;
  const int N = 100000;
  for (int i = 0; i < N; i++) {
    float x = rng.rnd();
    CHECK_MSG(x >= 0.0f && x < 1.0f, "rnd() out of [0,1): %.9g (i=%d)", x, i);
    sum += x;
  }
  CHECK_NEAR(sum / N, 0.5, 0.01);  // mean of U[0,1)
  float2 u = rng.rnd2();
  CHECK(u.x >= 0.0f && u.x < 1.0f && u.y >= 0.0f && u.y < 1.0f);
}

static void testUniformOnSphere() {
  Pcg32 rng = Pcg32::init(99, 1);
  const int N = 100000;
  double mx = 0, my = 0, mz = 0;
  for (int i = 0; i < N; i++) {
    float3 w = uniformOnSphere(rng.rnd2());
    CHECK_NEAR(length(w), 1.0, 1e-4);
    mx += w.x; my += w.y; mz += w.z;
  }
  // uniform on sphere: component means -> 0
  CHECK_NEAR(mx / N, 0.0, 0.01);
  CHECK_NEAR(my / N, 0.0, 0.01);
  CHECK_NEAR(mz / N, 0.0, 0.01);
}

static void testCosineHemisphere() {
  Pcg32 rng = Pcg32::init(2024, 3);
  const int N = 100000;
  double mz = 0;
  for (int i = 0; i < N; i++) {
    float3 w = cosineHemisphere(rng.rnd2());
    CHECK_MSG(w.z >= 0.0f, "cosineHemisphere z<0: %.9g (i=%d)", w.z, i);
    CHECK_NEAR(length(w), 1.0, 1e-4);
    mz += w.z;
  }
  // pdf = cos(theta)/pi -> E[z] = 2/3
  CHECK_NEAR(mz / N, 2.0 / 3.0, 0.01);
}

int main() {
  testPcg32Reference();
  testDeterminism();
  testRndRange();
  testUniformOnSphere();
  testCosineHemisphere();
  TEST_DONE("test_rng");
}

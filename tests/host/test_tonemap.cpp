// sundog host tests: ACES fitted tone curve (src/tonemap.h). Pins the Hill
// fit against double-precision reference values so a transposed matrix or a
// mistyped constant fails loudly instead of baking a subtly wrong curve into
// regenerated goldens.
#include "check.h"

#include "tonemap.h"

using namespace sd;

static void testGrayAxis() {
  // Black stays black (the saturate absorbs the fit's -3.8e-4 at 0).
  float3 z = acesFitted(f3(0.0f));
  CHECK(z.x == 0.0f && z.y == 0.0f && z.z == 0.0f);

  // Reference values from a double-precision evaluation of the BakingLab
  // constants (gray in -> gray out; both matrices have rows summing to ~1).
  struct { float in, out; } pins[] = {
      {0.18f, 0.105591f},   // middle gray lands on the toe side
      {1.0f, 0.619115f},    // "linear white" is only ~62% out
      {10.0f, 0.973822f},   // shoulder: bright but not clipped
  };
  for (const auto& p : pins) {
    float3 v = acesFitted(f3(p.in));
    CHECK_NEAR(v.x, p.out, 1e-3);
    CHECK_NEAR(v.y, p.out, 1e-3);
    CHECK_NEAR(v.z, p.out, 1e-3);
    CHECK_NEAR(v.x, v.y, 1e-4);  // gray axis stays neutral
    CHECK_NEAR(v.y, v.z, 1e-4);
  }

  // Asymptote: approaches white from below, saturate keeps it in range.
  float3 hot = acesFitted(f3(100.0f));
  CHECK(hot.x > 0.99f && hot.x <= 1.0f);

  // Negative input (denoiser artifacts) is clamped, not propagated.
  float3 neg = acesFitted(f3(-0.5f));
  CHECK(neg.x == 0.0f && neg.y == 0.0f && neg.z == 0.0f);
}

static void testMonotone() {
  // The curve must be strictly increasing over the working range.
  float prev = -1.0f;
  for (float x = 0.01f; x < 20.0f; x *= 1.5f) {
    float y = acesFitted(f3(x)).x;
    CHECK_MSG(y > prev, "not monotone at x = %g", x);
    prev = y;
  }
}

static void testColorPin() {
  // A non-gray input pins all three channels — the only assertion that can
  // catch a transposed input/output matrix (gray inputs cannot).
  float3 v = acesFitted(f3(0.9f, 0.2f, 0.05f));
  CHECK_NEAR(v.x, 0.623531, 1e-3);
  CHECK_NEAR(v.y, 0.141354, 1e-3);
  CHECK_NEAR(v.z, 0.027788, 1e-3);
}

int main() {
  testGrayAxis();
  testMonotone();
  testColorPin();
  TEST_DONE("test_tonemap");
}

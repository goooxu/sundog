// sundog: host tests for the HDR output transfer (src/transfer.h) —
// PQ OETF/EOTF round-trip, ST 2084 anchor values, and the BT.709->2020
// gamut matrix's row sums (white maps to white).
#include "check.h"

#include "transfer.h"

using namespace sd;

int main() {
  // PQ anchors (SMPTE ST 2084): 0 -> 0, 1.0 (10000 nits) -> 1.0;
  // 100 nits (0.01) encodes to ~0.508 (the classic SDR-white landmark).
  CHECK_NEAR(pqOetf(0.0f), 0.0, 1e-6);
  CHECK_NEAR(pqOetf(1.0f), 1.0, 1e-5);
  CHECK_NEAR(pqOetf(0.01f), 0.5081, 5e-4);

  // The 203-nit reference-white anchor lands where BT.2408 says (~0.58).
  CHECK_NEAR(pqOetf(kPqWhiteNits / kPqPeakNits), 0.5806, 5e-4);

  // OETF/EOTF round-trip across the range.
  for (float lin : {1e-5f, 1e-3f, 0.0203f, 0.1f, 0.5f, 1.0f}) {
    CHECK_NEAR(pqEotf(pqOetf(lin)), lin, 1e-4 * (lin > 0.01f ? lin * 100 : 1));
  }

  // 709->2020: rows sum to 1 (D65 white preserved), all coefficients of
  // the first row positive, off-diagonal structure as published.
  float3 w = bt709To2020(f3(1.0f, 1.0f, 1.0f));
  CHECK_NEAR(w.x, 1.0, 1e-4);
  CHECK_NEAR(w.y, 1.0, 1e-4);
  CHECK_NEAR(w.z, 1.0, 1e-4);
  // Pure 709 red maps inside 2020 (positive components, dominated by R).
  float3 r = bt709To2020(f3(1.0f, 0.0f, 0.0f));
  CHECK(r.x > 0.6f && r.y > 0.0f && r.z > 0.0f && r.x > r.y && r.y > r.z);

  TEST_DONE("test_transfer");
}

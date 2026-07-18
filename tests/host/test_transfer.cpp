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

  // Clamping contract: the whole writeAvif chain's NaN/inf/negative safety
  // rests on pqOetf saturating its input — pin it. fmaxf(NaN, 0) == 0 per
  // IEEE, so NaN input encodes as black, never a poisoned code value.
  CHECK_NEAR(pqOetf(-3.0f), 0.0, 1e-6);
  CHECK_NEAR(pqOetf(50.0f), 1.0, 1e-5);
  CHECK_NEAR(pqOetf(std::numeric_limits<float>::infinity()), 1.0, 1e-5);
  float nanEnc = pqOetf(std::numeric_limits<float>::quiet_NaN());
  CHECK_NEAR(nanEnc, 0.0, 1e-6);
  CHECK_NEAR(pqEotf(-1.0f), 0.0, 1e-6);
  CHECK_NEAR(pqEotf(2.0f), 1.0, 1e-4);

  // 709->2020: rows sum to 1 (D65 white preserved), all coefficients of
  // the first row positive, off-diagonal structure as published.
  float3 w = bt709To2020(f3(1.0f, 1.0f, 1.0f));
  CHECK_NEAR(w.x, 1.0, 1e-4);
  CHECK_NEAR(w.y, 1.0, 1e-4);
  CHECK_NEAR(w.z, 1.0, 1e-4);
  // Each primary pins one matrix column against the published BT.2087
  // coefficients (so a within-row G/B swap cannot slip through).
  float3 r = bt709To2020(f3(1.0f, 0.0f, 0.0f));
  CHECK_NEAR(r.x, 0.627404, 1e-5);
  CHECK_NEAR(r.y, 0.069097, 1e-5);
  CHECK_NEAR(r.z, 0.016391, 1e-5);
  float3 g = bt709To2020(f3(0.0f, 1.0f, 0.0f));
  CHECK_NEAR(g.x, 0.329283, 1e-5);
  CHECK_NEAR(g.y, 0.919540, 1e-5);
  CHECK_NEAR(g.z, 0.088013, 1e-5);
  float3 bl = bt709To2020(f3(0.0f, 0.0f, 1.0f));
  CHECK_NEAR(bl.x, 0.043313, 1e-5);
  CHECK_NEAR(bl.y, 0.011362, 1e-5);
  CHECK_NEAR(bl.z, 0.895595, 1e-5);

  TEST_DONE("test_transfer");
}

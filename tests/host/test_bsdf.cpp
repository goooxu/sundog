// sundog host tests: device/bsdf.cuh — lambert cosine sampling, GGX white
// furnace + pdf consistency, dielectric energy/TIR/Snell, parity mode.
#include "bsdf.cuh"
#include "check.h"

using namespace sd;

static MaterialDesc makeMat(int kind, float3 color, float roughness = 0.0f,
                            float ior = 1.5f) {
  MaterialDesc m{};
  m.kind = kind;
  m.texId = -1;
  m.color = color;
  m.roughness = roughness;
  m.ior = ior;
  m.intensity = 1.0f;
  m.twoSided = 0;
  return m;
}

// Uniform direction on the hemisphere around n (pdf = 1/2pi).
static float3 uniformHemi(float3 n, Pcg32& rng) {
  float3 w = uniformOnSphere(rng.rnd2());
  return dot(w, n) >= 0.0f ? w : -w;
}

static void testLambert() {
  float3 albedo = f3(0.7f, 0.3f, 0.2f);
  MaterialDesc m = makeMat(MT_LAMBERT, albedo);
  float3 n = normalize(f3(0.2f, 0.9f, -0.3f));
  float3 wo = normalize(f3(0.3f, 1.0f, 0.4f));
  CHECK(dot(wo, n) > 0.0f);
  float3 rayDir = -wo;
  Pcg32 rng = Pcg32::init(1, 1);

  // sampled directions: weight == albedo exactly, pdf matches bsdfPdf
  for (int i = 0; i < 20000; i++) {
    BsdfSample s = bsdfSample(m, albedo, rayDir, n, true, 0, rng);
    CHECK(s.valid);
    CHECK(!s.isDelta);
    CHECK(s.weight.x == albedo.x && s.weight.y == albedo.y && s.weight.z == albedo.z);
    CHECK_MSG(dot(s.wi, n) > 0.0f, "lambert wi below surface (i=%d)", i);
    CHECK_NEAR(length(s.wi), 1.0, 1e-4);
    float pdf = bsdfPdf(m, wo, s.wi, n);
    CHECK_NEAR(s.pdf, pdf, 2e-4);
    CHECK_NEAR(pdf, dot(s.wi, n) * SD_INV_PI, 2e-4);
    // f * cos / pdf == albedo for cosine sampling
    float3 f = bsdfEval(m, albedo, wo, s.wi, n);
    CHECK_NEAR(f.x, albedo.x * SD_INV_PI, 1e-6);
  }

  // MC: integral of pdf over the hemisphere == 1 (uniform-sampling estimate)
  const int N = 400000;
  double acc = 0;
  for (int i = 0; i < N; i++) {
    float3 wi = uniformHemi(n, rng);
    acc += bsdfPdf(m, wo, wi, n) * (2.0 * SD_PI);
  }
  CHECK_NEAR(acc / N, 1.0, 0.01);

  // eval is zero below the surface, in either argument
  float3 below = normalize(n * -1.0f + f3(0.1f, 0, 0));
  float3 fb = bsdfEval(m, albedo, wo, below, n);
  CHECK(fb.x == 0.0f && fb.y == 0.0f && fb.z == 0.0f);
  CHECK(bsdfPdf(m, wo, below, n) == 0.0f);
  CHECK(bsdfIsDelta(m, 0) == false);
}

static void testGgx() {
  float3 n = f3(0, 0, 1);  // local frame == world frame
  const float roughs[] = {0.05f, 0.2f, 0.5f, 1.0f};
  const float cosThetas[] = {0.95f, 0.7f, 0.3f};
  Pcg32 rng = Pcg32::init(7, 3);

  for (float rough : roughs) {
    for (float ct : cosThetas) {
      float st = std::sqrt(1.0f - ct * ct);
      float3 wo = f3(st, 0, ct);
      float3 rayDir = -wo;
      // white furnace: F0 = 1 -> single-scatter weight must not gain energy
      MaterialDesc m = makeMat(MT_METAL, f3(1, 1, 1), rough);
      int valid = 0;
      for (int i = 0; i < 20000; i++) {
        BsdfSample s = bsdfSample(m, f3(1, 1, 1), rayDir, n, true, 0, rng);
        if (!s.valid) continue;
        valid++;
        CHECK_MSG(s.weight.x <= 1.0f + 1e-3f && s.weight.y <= 1.0f + 1e-3f &&
                      s.weight.z <= 1.0f + 1e-3f,
                  "white furnace violation: weight=(%g,%g,%g) rough=%g cos=%g",
                  s.weight.x, s.weight.y, s.weight.z, rough, ct);
        CHECK(s.weight.x >= 0.0f);
        CHECK_MSG(dot(s.wi, n) > 0.0f, "ggx wi below surface");
        CHECK_NEAR(length(s.wi), 1.0, 1e-4);
        // sample-reported pdf agrees with bsdfPdf(wo, wi)
        float pdf = bsdfPdf(m, wo, s.wi, n);
        CHECK_MSG(std::fabs(s.pdf - pdf) <= 1e-3 * pdf + 1e-6,
                  "pdf mismatch: sample=%g bsdfPdf=%g (rough=%g cos=%g)",
                  s.pdf, pdf, rough, ct);
        // weight consistent with eval*cos/pdf
        if (s.pdf > 1e-3f) {
          float3 f = bsdfEval(m, f3(1, 1, 1), wo, s.wi, n);
          float w = f.x * dot(s.wi, n) / s.pdf;
          CHECK_MSG(std::fabs(w - s.weight.x) <= 5e-3 * s.weight.x + 1e-4,
                    "weight != eval*cos/pdf: %g vs %g", w, s.weight.x);
        }
      }
      // rough + grazing rejects below-horizon wi often; still expect >25%
      CHECK_MSG(valid > 5000, "too few valid GGX samples: %d (rough=%g cos=%g)",
                valid, rough, ct);
    }
  }

  // pdf integrates to <= 1 over the hemisphere (VNDF loses only the
  // below-horizon part); near 1 for a moderately rough grazing-free setup
  {
    MaterialDesc m = makeMat(MT_METAL, f3(1, 1, 1), 0.5f);
    float3 wo = f3(0.5f, 0, std::sqrt(0.75f));  // 30 deg
    const int N = 500000;
    double acc = 0;
    for (int i = 0; i < N; i++) {
      float3 wi = uniformHemi(n, rng);
      acc += bsdfPdf(m, wo, wi, n) * (2.0 * SD_PI);
    }
    double integral = acc / N;
    CHECK_MSG(integral > 0.85 && integral < 1.01,
              "GGX pdf hemisphere integral = %g", integral);
  }

  // roughness below 1e-3: delta mirror path
  {
    MaterialDesc m = makeMat(MT_METAL, f3(0.9f, 0.8f, 0.7f), 5e-4f);
    CHECK(bsdfIsDelta(m, 0) == true);
    float3 wo = normalize(f3(0.4f, 0, 0.9f));
    float3 rayDir = -wo;
    BsdfSample s = bsdfSample(m, m.color, rayDir, n, true, 0, rng);
    CHECK(s.valid && s.isDelta);
    CHECK(s.pdf == 0.0f);
    float3 refl = reflect(rayDir, n);
    CHECK_NEAR(s.wi.x, refl.x, 1e-5);
    CHECK_NEAR(s.wi.y, refl.y, 1e-5);
    CHECK_NEAR(s.wi.z, refl.z, 1e-5);
    // delta weight = Schlick F at the reflection angle
    float3 F = schlick3(dot(s.wi, n), m.color);
    CHECK_NEAR(s.weight.x, F.x, 1e-5);
    CHECK_NEAR(s.weight.z, F.z, 1e-5);
    // rough metal is not delta
    MaterialDesc mr = makeMat(MT_METAL, f3(1, 1, 1), 0.5f);
    CHECK(bsdfIsDelta(mr, 0) == false);
    CHECK(bsdfIsDelta(mr, 1) == true);  // parity metal is always delta
  }
}

static void testDielectric() {
  MaterialDesc m = makeMat(MT_DIELECTRIC, f3(1, 1, 1), 0.0f, 1.5f);
  CHECK(bsdfIsDelta(m, 0) == true);
  float3 n = f3(0, 1, 0);
  Pcg32 rng = Pcg32::init(3, 9);

  // energy conservation: weight == 1 exactly on every sample, both faces
  for (int i = 0; i < 5000; i++) {
    float3 wo = uniformHemi(n, rng);
    if (dot(wo, n) < 1e-3f) continue;
    bool front = (i & 1) == 0;
    BsdfSample s = bsdfSample(m, m.color, -wo, n, front, 0, rng);
    CHECK(s.valid && s.isDelta);
    CHECK(s.weight.x == 1.0f && s.weight.y == 1.0f && s.weight.z == 1.0f);
    CHECK_NEAR(length(s.wi), 1.0, 1e-4);
  }

  // TIR: inside glass at 45 deg (critical angle ~41.8) -> always reflects
  {
    float c = 1.0f / std::sqrt(2.0f);
    float3 rayDir = f3(c, -c, 0);  // n already flipped to oppose rayDir
    for (int i = 0; i < 200; i++) {
      BsdfSample s = bsdfSample(m, m.color, rayDir, n, false, 0, rng);
      CHECK(s.valid);
      float3 refl = reflect(rayDir, n);
      CHECK_NEAR(s.wi.x, refl.x, 1e-5);
      CHECK_NEAR(s.wi.y, refl.y, 1e-5);
      CHECK_NEAR(s.wi.z, refl.z, 1e-5);
      CHECK(s.weight.x == 1.0f);
    }
  }

  // Snell's law, entering (eta = 1/1.5) at 45 deg: sin_t = sin_i/1.5
  {
    float c = 1.0f / std::sqrt(2.0f);
    float3 rayDir = f3(c, -c, 0);
    int refracted = 0, reflectedCnt = 0;
    for (int i = 0; i < 2000; i++) {
      BsdfSample s = bsdfSample(m, m.color, rayDir, n, true, 0, rng);
      CHECK(s.valid);
      CHECK_NEAR(length(s.wi), 1.0, 1e-5);
      CHECK_NEAR(s.wi.z, 0.0, 1e-6);  // stays in the plane of incidence
      if (dot(s.wi, n) < 0.0f) {
        refracted++;
        float sinT = c / 1.5f;
        CHECK_NEAR(s.wi.x, sinT, 1e-5);                          // Snell
        CHECK_NEAR(s.wi.y, -std::sqrt(1.0f - sinT * sinT), 1e-5);
      } else {
        reflectedCnt++;
        CHECK_NEAR(s.wi.x, c, 1e-5);  // mirror direction
        CHECK_NEAR(s.wi.y, c, 1e-5);
      }
    }
    // at 45 deg on glass, Fresnel reflectance ~5%: both branches occur,
    // refraction dominates
    CHECK_MSG(refracted > 1700, "refraction branch too rare: %d/2000", refracted);
    CHECK_MSG(reflectedCnt > 20, "reflection branch never taken: %d/2000",
              reflectedCnt);
  }

  // Snell's law, exiting below the critical angle (20 deg inside, eta=1.5)
  {
    float rad = 20.0f * SD_PI / 180.0f;
    float si = std::sin(rad), ci = std::cos(rad);
    float3 rayDir = f3(si, -ci, 0);
    int refracted = 0;
    for (int i = 0; i < 2000; i++) {
      BsdfSample s = bsdfSample(m, m.color, rayDir, n, false, 0, rng);
      CHECK(s.valid);
      if (dot(s.wi, n) < 0.0f) {
        refracted++;
        float sinT = si * 1.5f;
        CHECK_NEAR(s.wi.x, sinT, 1e-5);
        CHECK_NEAR(s.wi.y, -std::sqrt(1.0f - sinT * sinT), 1e-5);
      }
    }
    CHECK_MSG(refracted > 1500, "exit refraction too rare: %d/2000", refracted);
  }
}

static void testParity() {
  float3 n = normalize(f3(0.1f, 1.0f, 0.2f));
  Pcg32 rng = Pcg32::init(21, 4);

  // parity lambert: weight == albedo, pdf == 1 (opaque to MIS), wi unit
  {
    float3 albedo = f3(0.6f, 0.5f, 0.4f);
    MaterialDesc m = makeMat(MT_LAMBERT, albedo);
    for (int i = 0; i < 5000; i++) {
      BsdfSample s = bsdfSample(m, albedo, -n, n, true, 1, rng);
      CHECK(s.valid);
      CHECK(s.weight.x == albedo.x && s.weight.y == albedo.y && s.weight.z == albedo.z);
      CHECK(s.pdf == 1.0f);
      CHECK(!s.isDelta);
      CHECK_NEAR(length(s.wi), 1.0, 1e-4);
    }
  }

  // parity metal, roughness 0: pure mirror, weight = raw albedo (no Schlick)
  {
    float3 albedo = f3(0.8f, 0.6f, 0.2f);
    MaterialDesc m = makeMat(MT_METAL, albedo, 0.0f);
    float3 wo = normalize(f3(0.5f, 1.0f, -0.2f));
    float3 rayDir = -wo;
    BsdfSample s = bsdfSample(m, albedo, rayDir, n, true, 1, rng);
    CHECK(s.valid && s.isDelta);
    CHECK(s.weight.x == albedo.x && s.weight.y == albedo.y && s.weight.z == albedo.z);
    float3 refl = normalize(reflect(rayDir, n));
    CHECK_NEAR(s.wi.x, refl.x, 1e-5);
    CHECK_NEAR(s.wi.y, refl.y, 1e-5);
    CHECK_NEAR(s.wi.z, refl.z, 1e-5);
  }

  // parity metal with fuzz at grazing: fuzzed direction may dip below the
  // surface -> invalid (cxxrt kills the path); valid ones satisfy wi.n > 0
  {
    float3 albedo = f3(0.9f, 0.9f, 0.9f);
    MaterialDesc m = makeMat(MT_METAL, albedo, 1.0f);
    float3 wo = normalize(n * 0.05f + normalize(cross(n, f3(0, 0, 1))));
    float3 rayDir = -wo;
    CHECK(dot(wo, n) > 0.0f);
    int invalid = 0, valid = 0;
    for (int i = 0; i < 10000; i++) {
      BsdfSample s = bsdfSample(m, albedo, rayDir, n, true, 1, rng);
      if (!s.valid) { invalid++; continue; }
      valid++;
      CHECK_MSG(dot(s.wi, n) > 0.0f, "parity metal returned wi below surface");
      CHECK(s.weight.x == albedo.x);  // parity fresnel = albedo
      CHECK(s.isDelta);
    }
    CHECK_MSG(invalid > 100, "expected fuzzed-below-surface rejections, got %d", invalid);
    CHECK_MSG(valid > 100, "expected some valid fuzzy samples, got %d", valid);
  }

  // parity dielectric: weight == 1 always
  {
    MaterialDesc m = makeMat(MT_DIELECTRIC, f3(1, 1, 1), 0.0f, 1.5f);
    for (int i = 0; i < 2000; i++) {
      float3 wo = uniformHemi(n, rng);
      if (dot(wo, n) < 1e-3f) continue;
      BsdfSample s = bsdfSample(m, m.color, -wo, n, true, 1, rng);
      CHECK(s.valid && s.isDelta);
      CHECK(s.weight.x == 1.0f && s.weight.y == 1.0f && s.weight.z == 1.0f);
      CHECK_NEAR(length(s.wi), 1.0, 1e-4);
    }
  }

  // emissive never scatters
  {
    MaterialDesc m = makeMat(MT_EMISSIVE, f3(1, 1, 1));
    BsdfSample s = bsdfSample(m, m.color, -n, n, true, 0, rng);
    CHECK(!s.valid);
  }
}

// Regression: exiting the denser medium must use the transmitted-side cosine
// for Schlick. At 40 deg internal incidence (ior 1.5) the reflect probability
// is ~0.244; the old incident-cosine bug gave ~0.041.
static void testDielectricExitFresnel() {
  MaterialDesc m = makeMat(MT_DIELECTRIC, f3(1, 1, 1), 0.0f, 1.5f);
  float3 n = f3(0.0f, 0.0f, 1.0f);  // toward the incident (glass) side
  float s40 = sinf(40.0f * SD_PI / 180.0f), c40 = cosf(40.0f * SD_PI / 180.0f);
  float3 rayDir = normalize(f3(s40, 0.0f, -c40));
  Pcg32 rng = Pcg32::init(7, 7);
  int reflected = 0;
  const int N = 200000;
  for (int i = 0; i < N; i++) {
    BsdfSample s = bsdfSample(m, m.color, rayDir, n, /*frontface=*/false, 0, rng);
    CHECK(s.valid);
    if (s.wi.z > 0.0f) reflected++;  // bounced back into the glass
  }
  double frac = (double)reflected / N;
  CHECK_NEAR(frac, 0.2444, 0.01);  // schlick(cos_t=0.266, f0=0.04)

  // Past the critical angle (41.8 deg): total internal reflection, always.
  float s45 = sinf(45.0f * SD_PI / 180.0f);
  rayDir = normalize(f3(s45, 0.0f, -s45));
  for (int i = 0; i < 1000; i++) {
    BsdfSample s = bsdfSample(m, m.color, rayDir, n, false, 0, rng);
    CHECK(s.valid && s.wi.z > 0.0f);
  }
}

int main() {
  testLambert();
  testGgx();
  testDielectric();
  testDielectricExitFresnel();
  testParity();
  TEST_DONE("test_bsdf");
}

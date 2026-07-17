// sundog: the single OptiX module. Raygen owns the iterative path loop
// (NEE + MIS); closest-hit only packs hit info into payload registers;
// anyhit implements two-sided pass-through faces and alpha cutouts for both
// radiance and shadow rays.
#include <optix.h>

#include "math.cuh"
#include "params.h"
#include "rng.cuh"
#include "intersect.cuh"
#include "texture_eval.cuh"
#include "bsdf.cuh"
#include "light_sample.cuh"
#include "env_light.cuh"
#include "noise.cuh"
#include "volume.cuh"

using namespace sd;

extern "C" __constant__ LaunchParams params;

enum RayType { RAY_RADIANCE = 0, RAY_SHADOW = 1, RAY_TYPE_COUNT = 2 };

// ---------------- payload ----------------
// Radiance rays (8 registers):
// p0: 0 = miss; else bit0 = hit, bit1 = frontface
// p1: t (float bits)
// p2-4: miss -> background color; hit -> world shading normal (flipped to wo side)
// p5-6: u, v
// p7: matId (low 16) | (lightId + 1) << 16
//
// Shadow rays (5 registers):
// p0: 0 = occluded (default); __miss__shadow sets 1 = reached the light
// p1: product of per-interface Fresnel transmission (1-F)  (float bits)
// p2-4: signed per-channel optical depth: over transparent crossings, sum of
//       (backface ? +1 : -1) * sigma * hitT. Order-independent, so unsorted
//       anyhit invocations may accumulate it safely.

struct HitInfo {
  bool hit;
  bool frontface;
  float t;
  float3 nOrBg;
  float u, v;
  int matId;
  int lightId;  // -1 if none
};

static __forceinline__ __device__ HitInfo traceRadiance(float3 o, float3 d) {
  unsigned p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0;
  optixTrace(params.handle, o, d, 0.0f, 1e16f, 0.0f, 0xFF,
             OPTIX_RAY_FLAG_NONE, RAY_RADIANCE, RAY_TYPE_COUNT, RAY_RADIANCE,
             p0, p1, p2, p3, p4, p5, p6, p7);
  HitInfo h;
  h.hit = (p0 & 1u) != 0;
  h.frontface = (p0 & 2u) != 0;
  h.t = __uint_as_float(p1);
  h.nOrBg = f3(__uint_as_float(p2), __uint_as_float(p3), __uint_as_float(p4));
  h.u = __uint_as_float(p5);
  h.v = __uint_as_float(p6);
  h.matId = (int)(p7 & 0xFFFFu);
  h.lightId = (int)(p7 >> 16) - 1;
  return h;
}

// Straight-line transmittance toward the light: f3(0) = occluded, f3(1) =
// clear, fractional through glass/water (Fresnel per interface, Beer-Lambert
// per medium segment; the ray is not bent — see report ch16). Flame volumes
// are not in the BVH — the caller folds in flameTransmittance separately.
static __forceinline__ __device__ float3 traceShadow(float3 o, float3 d, float tmax) {
  unsigned p0 = 0;
  unsigned p1 = __float_as_uint(1.0f);
  unsigned p2 = 0, p3 = 0, p4 = 0;  // 0u == __float_as_uint(0.0f), on purpose
  optixTrace(params.handle, o, d, 0.0f, tmax, 0.0f, 0xFF,
             OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT | OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT,
             RAY_SHADOW, RAY_TYPE_COUNT, RAY_SHADOW, p0, p1, p2, p3, p4);
  if (!p0) return f3(0.0f);
  float3 tau = f3(fmaxf(0.0f, __uint_as_float(p2)),
                  fmaxf(0.0f, __uint_as_float(p3)),
                  fmaxf(0.0f, __uint_as_float(p4)));
  float3 tr = f3(__uint_as_float(p1));
  // Guard keeps fully-opaque scenes on the exact tr == 1.0 path (no exp3).
  if (maxComp(tau) > 0.0f) tr = tr * exp3(-tau);
  return tr;
}

// ---------------- background ----------------

static __forceinline__ __device__ float3 evalBackground(float3 dir) {
  if (params.bg.kind == BG_ENVMAP) return envEval(params.env, dir);
  if (params.bg.kind == BG_GRADIENT) {
    float t = 0.5f * (dir.y + 1.0f);
    return lerp3(params.bg.a, params.bg.b, t);
  }
  return params.bg.a;
}

// ---------------- miss ----------------

extern "C" __global__ void __miss__radiance() {
  float3 c = evalBackground(optixGetWorldRayDirection());
  optixSetPayload_0(0u);
  optixSetPayload_2(__float_as_uint(c.x));
  optixSetPayload_3(__float_as_uint(c.y));
  optixSetPayload_4(__float_as_uint(c.z));
}

extern "C" __global__ void __miss__shadow() { optixSetPayload_0(1u); }

// ---------------- quadric intersection ----------------

extern "C" __global__ void __intersection__quadric() {
  const HitRecordData* rec = (const HitRecordData*)optixGetSbtDataPointer();
  float3 o = optixGetObjectRayOrigin();
  float3 d = optixGetObjectRayDirection();
  QuadricHits hits = intersectQuadric(rec->geomKind, o, d,
                                      optixGetRayTmin(), optixGetRayTmax());
  for (int i = 0; i < hits.count; i++) {
    const QuadricHit& h = hits.hits[i];
    optixReportIntersection(h.t, 0,
                            __float_as_uint(h.n.x), __float_as_uint(h.n.y),
                            __float_as_uint(h.n.z), __float_as_uint(h.u),
                            __float_as_uint(h.v));
  }
}

// ---------------- hit-side helpers ----------------

struct ShadePoint {
  float3 nFront;   // world unit normal of the canonical front side
  float u, v;
  bool frontface;  // ray arrived on the front side
};

static __forceinline__ __device__ ShadePoint quadricShadePoint() {
  float3 nObj = f3(__uint_as_float(optixGetAttribute_0()),
                   __uint_as_float(optixGetAttribute_1()),
                   __uint_as_float(optixGetAttribute_2()));
  ShadePoint sp;
  sp.nFront = normalize(optixTransformNormalFromObjectToWorldSpace(nObj));
  sp.u = __uint_as_float(optixGetAttribute_3());
  sp.v = __uint_as_float(optixGetAttribute_4());
  sp.frontface = dot(sp.nFront, optixGetWorldRayDirection()) < 0.0f;
  return sp;
}

static __forceinline__ __device__ ShadePoint triShadePoint(const HitRecordData* rec) {
  unsigned prim = optixGetPrimitiveIndex();
  float2 bc = optixGetTriangleBarycentrics();
  uint3 tri = rec->indices[prim];
  float3 p0 = rec->positions[tri.x];
  float3 p1 = rec->positions[tri.y];
  float3 p2 = rec->positions[tri.z];
  float3 ngObj = cross(p1 - p0, p2 - p0);
  float3 ng = normalize(optixTransformNormalFromObjectToWorldSpace(ngObj));
  float3 ns = ng;
  if (rec->normals) {
    float3 n = rec->normals[tri.x] * (1.0f - bc.x - bc.y) +
               rec->normals[tri.y] * bc.x + rec->normals[tri.z] * bc.y;
    ns = normalize(optixTransformNormalFromObjectToWorldSpace(n));
    if (dot(ns, ng) < 0.0f) ns = -ns;  // keep shading on the geometric side
  }
  ShadePoint sp;
  sp.nFront = ns;
  if (rec->uvs) {
    float2 t = rec->uvs[tri.x] * (1.0f - bc.x - bc.y) +
               rec->uvs[tri.y] * bc.x + rec->uvs[tri.z] * bc.y;
    sp.u = t.x;
    sp.v = t.y;
  } else {
    sp.u = bc.x;
    sp.v = bc.y;
  }
  sp.frontface = dot(ng, optixGetWorldRayDirection()) < 0.0f;
  // MIS consistency for mesh area lights: NEE samples them with the sampled
  // triangle's GEOMETRIC normal in the pdf, so an emitter hit must measure
  // its pdf with the geometric normal too — pack ng instead of the smooth
  // normal. Safe because emissive hits terminate the path (raygen breaks;
  // the normal is never used for scattering there). Gated on the RESOLVED
  // side being emissive: a lambert back face of a one-sided mesh light keeps
  // smooth shading and continues bouncing. Known cost: a mesh light as first
  // hit writes the faceted normal into the normal AOV (all 8 payload
  // registers are in use — no room to carry both normals).
  if (rec->lightId >= 0) {
    int sideMat = sp.frontface ? rec->matFront : rec->matBack;
    if (sideMat != (int)MAT_NONE && params.materials[sideMat].kind == MT_EMISSIVE)
      sp.nFront = ng;
  }
  return sp;
}

static __forceinline__ __device__ void packHit(const ShadePoint& sp,
                                               const HitRecordData* rec) {
  int matId = sp.frontface ? rec->matFront : rec->matBack;
  float3 ns = sp.frontface ? sp.nFront : -sp.nFront;  // toward incident side
  optixSetPayload_0(1u | (sp.frontface ? 2u : 0u));
  optixSetPayload_1(__float_as_uint(optixGetRayTmax()));
  optixSetPayload_2(__float_as_uint(ns.x));
  optixSetPayload_3(__float_as_uint(ns.y));
  optixSetPayload_4(__float_as_uint(ns.z));
  optixSetPayload_5(__float_as_uint(sp.u));
  optixSetPayload_6(__float_as_uint(sp.v));
  optixSetPayload_7((unsigned)(matId & 0xFFFF) |
                    ((unsigned)(rec->lightId + 1) << 16));
}

extern "C" __global__ void __closesthit__radiance() {
  const HitRecordData* rec = (const HitRecordData*)optixGetSbtDataPointer();
  packHit(quadricShadePoint(), rec);
}

extern "C" __global__ void __closesthit__radiance_tri() {
  const HitRecordData* rec = (const HitRecordData*)optixGetSbtDataPointer();
  packHit(triShadePoint(rec), rec);
}

// Shared mask logic: pass-through faces without a material; alpha cutout.
static __forceinline__ __device__ void maskAnyhit(const ShadePoint& sp,
                                                  const HitRecordData* rec) {
  int matId = sp.frontface ? rec->matFront : rec->matBack;
  if (matId == (int)MAT_NONE) {
    optixIgnoreIntersection();
    return;
  }
  if (rec->cutoutTexId >= 0) {
    float4 c = evalTexture(params.textures[rec->cutoutTexId], sp.u, sp.v);
    if (c.w < 0.5f) optixIgnoreIntersection();
  }
}

extern "C" __global__ void __anyhit__mask() {
  const HitRecordData* rec = (const HitRecordData*)optixGetSbtDataPointer();
  maskAnyhit(quadricShadePoint(), rec);
}

extern "C" __global__ void __anyhit__mask_tri() {
  const HitRecordData* rec = (const HitRecordData*)optixGetSbtDataPointer();
  maskAnyhit(triShadePoint(rec), rec);
}

// Shadow anyhit: pass-through faces and alpha cutouts (same rules as
// maskAnyhit), plus flag-gated transparent shadows for glass/water. A plain
// return accepts the hit; with TERMINATE_ON_FIRST_HIT that means "fully
// occluded". Correctness of the payload accumulation relies on
// REQUIRE_SINGLE_ANYHIT_CALL (accel.cpp): each crossing must contribute
// exactly once.
static __forceinline__ __device__ void shadowAnyhit(const ShadePoint& sp,
                                                    const HitRecordData* rec) {
  int matId = sp.frontface ? rec->matFront : rec->matBack;
  if (matId == (int)MAT_NONE) {
    optixIgnoreIntersection();
    return;
  }
  if (rec->cutoutTexId >= 0) {
    float4 c = evalTexture(params.textures[rec->cutoutTexId], sp.u, sp.v);
    if (c.w < 0.5f) {
      optixIgnoreIntersection();
      return;
    }
  }
  const MaterialDesc& mat = params.materials[matId];
  if (mat.kind != MT_DIELECTRIC && mat.kind != MT_WATER) return;  // occluder
  if (!params.transparentShadows) return;  // legacy binary occlusion mode

  // Per-interface Fresnel with the same eta/f0/cosine conventions as
  // bsdfSample (device/bsdf.cuh). Documented approximations: the interface is
  // taken against vacuum (no medium context on an unordered shadow ray), the
  // flat front normal is used (no waterNormal perturbation), the ray
  // continues in a straight line, and a rough dielectric is treated as
  // smooth (roughness never blurs a shadow — brightness-only, see ch16).
  float3 d = optixGetWorldRayDirection();           // unit (== ls.wi)
  float3 n = sp.frontface ? sp.nFront : -sp.nFront;  // toward incident side
  float eta = sp.frontface ? 1.0f / mat.ior : mat.ior;
  float3 refr;
  if (!refract(d, n, eta, refr)) return;  // TIR: outside Snell's window
  float f0 = (1.0f - eta) / (1.0f + eta);
  f0 = f0 * f0;
  float cosine = sp.frontface ? -dot(d, n) : -dot(refr, n);
  float F = schlick(cosine, f0);
  optixSetPayload_1(__float_as_uint(
      __uint_as_float(optixGetPayload_1()) * (1.0f - F)));

  // Signed Beer-Lambert bookkeeping: exits add sigma*t, entries subtract, so
  // the unsorted sum equals sigma times the in-medium path length. In AH,
  // optixGetRayTmax() is the candidate hit t (world distance: d is unit).
  float t = optixGetRayTmax();
  float s = sp.frontface ? -t : t;
  optixSetPayload_2(__float_as_uint(__uint_as_float(optixGetPayload_2()) + mat.absorb.x * s));
  optixSetPayload_3(__float_as_uint(__uint_as_float(optixGetPayload_3()) + mat.absorb.y * s));
  optixSetPayload_4(__float_as_uint(__uint_as_float(optixGetPayload_4()) + mat.absorb.z * s));
  optixIgnoreIntersection();
}

extern "C" __global__ void __anyhit__shadow() {
  const HitRecordData* rec = (const HitRecordData*)optixGetSbtDataPointer();
  shadowAnyhit(quadricShadePoint(), rec);
}

extern "C" __global__ void __anyhit__shadow_tri() {
  const HitRecordData* rec = (const HitRecordData*)optixGetSbtDataPointer();
  shadowAnyhit(triShadePoint(rec), rec);
}

// ---------------- raygen ----------------

static __forceinline__ __device__ float3 sanitize(float3 c) {
  if (!isfinite(c.x) || !isfinite(c.y) || !isfinite(c.z)) return f3(0.0f);
  return c;
}

extern "C" __global__ void __raygen__render() {
  uint3 idx = optixGetLaunchIndex();
  int px = (int)idx.x, py = (int)idx.y;
  int pixel = py * params.width + px;

  const CameraData& cam = params.cam;
  int nStrata = (int)floorf(sqrtf((float)params.sppTotal) + 0.5f);
  if (nStrata * nStrata > params.sppTotal) nStrata--;
  if (nStrata < 1) nStrata = 1;

  unsigned long long raysTraced = 0;

  float4 accum = params.accum[pixel];
  float4 aovAlbedo = params.aovAlbedo[pixel];
  float4 aovNormal = params.aovNormal[pixel];

  for (int si = 0; si < params.sppThisLaunch; si++) {
    int s = params.sampleOffset + si;
    Pcg32 rng = Pcg32::init(((unsigned long long)pixel << 32) ^ (unsigned long long)s,
                            (unsigned long long)params.seed);

    // Stratified jitter over the whole spp budget.
    float jx, jy;
    if (s < nStrata * nStrata) {
      jx = ((s % nStrata) + rng.rnd()) / nStrata;
      jy = ((s / nStrata) + rng.rnd()) / nStrata;
    } else {
      jx = rng.rnd();
      jy = rng.rnd();
    }
    float u = (px + jx) / params.width;
    float v = ((params.height - 1 - py) + jy) / params.height;

    float3 org = cam.origin;
    if (cam.lensRadius > 0.0f) {
      float2 rd = concentricDisk(rng.rnd2()) * cam.lensRadius;
      org = org + cam.u * rd.x + cam.v * rd.y;
    }
    float3 dir = normalize(cam.lowerLeft + u * cam.horizontal +
                           v * cam.vertical - org);

    float3 L = f3(0.0f);
    float3 beta = f3(1.0f);
    float prevPdf = 0.0f;
    bool specularBounce = true;
    float3 prevX = org;
    float3 o = org, d = dir;
    bool aovDone = false;
    // Medium stack for nested dielectrics/water (LIFO; well-formed nesting
    // assumed — geometry must be properly contained, interpenetration is not
    // arbitrated). Logical depth may exceed the cap: writes above MED_MAX are
    // dropped but the counter keeps counting, so a deep excursion unwinds
    // without corrupting the tracked levels.
    constexpr int MED_MAX = 4;
    float3 medSigma[MED_MAX];
    float medIor[MED_MAX];
    int medDepth = 0;  // 0 = vacuum
    // NEE strategy set: the explicit lights plus (with an envmap) the
    // environment light. One uniform pick per vertex; the 1/nStrat selection
    // probability folds into both sides of every MIS pair below.
    const bool hasEnv = params.bg.kind == BG_ENVMAP;
    const int nStrat = params.numLights + (hasEnv ? 1 : 0);

    for (int depth = 0; depth < params.maxDepth; depth++) {
      HitInfo hit = traceRadiance(o, d);
      raysTraced++;

      // ---- absorbing medium (inside water): Beer-Lambert over the segment ----
      if (medDepth > 0) {
        float3 sigma = medSigma[min(medDepth, MED_MAX) - 1];
        if (maxComp(sigma) > 0.0f)
          beta *= exp3(-sigma * (hit.hit ? hit.t : 1e4f));
      }

      // ---- emissive media (procedural flames) ----
      // March before the surface/background contribution: emission along the
      // segment reaches the eye first, and the segment's transmittance
      // attenuates everything behind it via beta.
      if (params.numFlames > 0) {
        float3 trans = f3(1.0f);
        float3 Lv = marchFlames(params.flames, params.numFlames, o, d,
                                hit.hit ? hit.t : 1e16f, rng, trans);
        float3 cv = beta * Lv;
        if (depth >= 1 && params.clampVal > 0.0f) cv = min3(cv, f3(params.clampVal));
        L += cv;
        beta *= trans;
      }

      if (!hit.hit) {
        // With an envmap the escaped BSDF ray "hit" the environment light,
        // which env-NEE also samples: weight the two like the emissive-hit
        // branch below. solid/gradient backgrounds are not NEE targets (w=1).
        float w = 1.0f;
        if (hasEnv && !specularBounce) {
          float pdfE = envPdfSolidAngle(params.env, d) / nStrat;
          w = prevPdf / (prevPdf + pdfE);
        }
        float3 c = beta * hit.nOrBg * w;
        if (depth >= 1 && params.clampVal > 0.0f) c = min3(c, f3(params.clampVal));
        L += c;
        if (!aovDone) {
          aovAlbedo = make_float4(
              aovAlbedo.x + (hit.nOrBg.x - aovAlbedo.x) / (s + 1),
              aovAlbedo.y + (hit.nOrBg.y - aovAlbedo.y) / (s + 1),
              aovAlbedo.z + (hit.nOrBg.z - aovAlbedo.z) / (s + 1), 1.0f);
          aovDone = true;
        }
        break;
      }

      float3 x = o + hit.t * d;
      float3 ns = hit.nOrBg;  // shading normal, toward incident side
      const MaterialDesc& mat = params.materials[hit.matId];
      float3 albedo = materialAlbedo(mat, params.textures, hit.u, hit.v);
      if (mat.kind == MT_WATER && mat.waveAmp > 0.0f)
        ns = waterNormal(x, ns, -d, mat.waveAmp, mat.waveFreq);

      if (!aovDone) {
        float3 nc = f3(dot(ns, cam.u), dot(ns, cam.v), dot(ns, cam.w));
        aovAlbedo = make_float4(aovAlbedo.x + (albedo.x - aovAlbedo.x) / (s + 1),
                                aovAlbedo.y + (albedo.y - aovAlbedo.y) / (s + 1),
                                aovAlbedo.z + (albedo.z - aovAlbedo.z) / (s + 1), 1.0f);
        aovNormal = make_float4(aovNormal.x + (nc.x - aovNormal.x) / (s + 1),
                                aovNormal.y + (nc.y - aovNormal.y) / (s + 1),
                                aovNormal.z + (nc.z - aovNormal.z) / (s + 1), 1.0f);
        aovDone = true;
      }

      if (mat.kind == MT_EMISSIVE) {
        bool visible = hit.frontface || mat.twoSided;
        if (visible) {
          float3 Le = albedo * mat.intensity;
          float w = 1.0f;
          if (!specularBounce && hit.lightId >= 0 &&
              params.numLights > 0) {
            float pdfL = lightPdfSolidAngle(params.lights[hit.lightId], prevX,
                                            x, ns) /
                         nStrat;
            w = prevPdf / (prevPdf + pdfL);
          }
          float3 c = beta * Le * w;
          if (depth >= 1 && params.clampVal > 0.0f) c = min3(c, f3(params.clampVal));
          L += c;
        }
        break;
      }

      // ---- relative IOR at this interface (used by NEE eval/pdf and the
      // BSDF sample below) ----
      // Entering compares against the medium we are currently inside (stack
      // top); exiting compares the medium being left against the level
      // beneath it. Vacuum (1.0) when the stack has no such level — which is
      // every scene without nested media. Pure arithmetic, zero draws.
      float etaExt = 1.0f;
      if (mat.kind == MT_DIELECTRIC || mat.kind == MT_WATER) {
        int outer = min(hit.frontface ? medDepth : medDepth - 1, MED_MAX);
        if (outer > 0) etaExt = medIor[outer - 1];
      }
      float etaRel = hit.frontface ? etaExt / mat.ior : mat.ior / etaExt;

      // ---- NEE ----
      if (nStrat > 0 && !bsdfIsDelta(mat)) {
        int k = min((int)(rng.rnd() * nStrat), nStrat - 1);
        LightSample ls = (k < params.numLights)
            ? sampleLight(params.lights[k], x, rng, params.textures)
            : sampleEnv(params.env, rng);
        float cosS = ls.valid ? dot(ls.wi, ns) : 0.0f;
        // A rough dielectric also connects to lights BEHIND the interface:
        // its transmission lobe makes cosS < 0 a real contribution (frosted
        // glass lit from the far side). Every other non-delta material keeps
        // the front-hemisphere-only gate.
        bool transmitNee = cosS < 0.0f && mat.kind == MT_DIELECTRIC;
        if (ls.valid && (cosS > 0.0f || transmitNee)) {
          raysTraced++;
          float3 so = offsetRay(x, transmitNee ? -ns : ns);
          float tmaxS = ls.dist * 0.999f;
          float3 tr = traceShadow(so, ls.wi, tmaxS);
          if (maxComp(tr) > 0.0f) {
            // Flame volumes attenuate NEE like glass/water do (ch16), via a
            // transmittance-only march over the same segment (ch13). Placed
            // after the occlusion early-out: an occluded ray contributes
            // nothing regardless, and whether ANY accepting hit exists on a
            // fixed segment is deterministic (independent of anyhit order),
            // so the conditional rnd() draws stay a pure function of
            // scene + segment — re-renders remain bit-identical. The owning
            // flame of a flame-embedded light is exempt (LightDesc.flameId).
            if (params.numFlames > 0 && params.transparentShadows) {
              int skipFl = (k < params.numLights) ? params.lights[k].flameId : -1;
              tr = tr * flameTransmittance(params.flames, params.numFlames,
                                           so, ls.wi, tmaxS, rng, skipFl);
            }
            float pdfLe = (ls.isDelta ? 1.0f : ls.pdf) / nStrat;
            float3 f = bsdfEval(mat, albedo, -d, ls.wi, ns, etaRel);
            float w = 1.0f;
            if (!ls.isDelta) {
              float pdfB = bsdfPdf(mat, -d, ls.wi, ns, etaRel);
              w = pdfLe / (pdfLe + pdfB);
            }
            float3 c = beta * f * fabsf(cosS) * ls.Li * w / pdfLe;
            c = c * tr;  // separate statement: stays an exact *1.0 when clear
            if (depth >= 1 && params.clampVal > 0.0f) c = min3(c, f3(params.clampVal));
            L += sanitize(c);
          }
        }
      }

      // ---- BSDF sample ---- (etaExt resolved above the NEE block)
      BsdfSample bs = bsdfSample(mat, albedo, d, ns, hit.frontface, etaExt, rng);
      if (!bs.valid) break;
      // Transmission crosses the interface: push the entered medium on the
      // front side, pop on exit (TIR is a reflection — no toggle).
      if ((mat.kind == MT_WATER || mat.kind == MT_DIELECTRIC) &&
          dot(bs.wi, ns) < 0.0f) {
        if (hit.frontface) {
          if (medDepth < MED_MAX) {
            medSigma[medDepth] = mat.absorb;
            medIor[medDepth] = mat.ior;
          }
          medDepth++;
        } else if (medDepth > 0) {
          medDepth--;
        }
      }
      beta *= bs.weight;
      specularBounce = bs.isDelta;
      prevPdf = bs.isDelta ? 0.0f : bs.pdf;
      prevX = x;
      float3 offN = dot(bs.wi, ns) >= 0.0f ? ns : -ns;
      o = offsetRay(x, offN);
      d = bs.wi;

      // ---- Russian roulette ----
      if (depth >= 4) {
        float q = clampf(maxComp(beta), 0.05f, 0.95f);
        if (rng.rnd() >= q) break;
        beta *= 1.0f / q;
      }
      if (maxComp(beta) <= 0.0f) break;
    }

    L = sanitize(L);
    accum = make_float4(accum.x + (L.x - accum.x) / (s + 1),
                        accum.y + (L.y - accum.y) / (s + 1),
                        accum.z + (L.z - accum.z) / (s + 1), 1.0f);
  }

  params.accum[pixel] = accum;
  params.aovAlbedo[pixel] = aovAlbedo;
  params.aovNormal[pixel] = aovNormal;
  if (params.countRays && params.rayCounter) {
    atomicAdd(params.rayCounter, raysTraced);
  }
}

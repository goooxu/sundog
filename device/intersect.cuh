// sundog: analytic intersection math for the five quadric primitives, in
// canonical object space. The host uses only quadricAabb() (GAS builds).
//
// Canonical shapes:
//   sphere    r=1 at origin
//   rect      XZ square [-1,1]^2 at y=0, front = +Y
//   disk      XZ disk r<=1 at y=0, front = +Y
//   cylinder  x^2+z^2=1, y in [-1,1], open ended, front = outward
//   parabola  y = 0.5*(x^2+z^2), x^2+z^2 <= 1, front = convex (outside) side
#ifndef SUNDOG_INTERSECT_CUH
#define SUNDOG_INTERSECT_CUH

#include "math.cuh"
#include "params.h"

namespace sd {

struct QuadricHit {
  float t;
  float3 n;   // object-space unit normal pointing to the canonical front side
  float u, v;
};

// Each intersector may produce up to two hits (entry/exit); both must be
// reported to OptiX so that anyhit pass-through can fall through to the
// farther one. `count` is the number of valid entries in hits[].
struct QuadricHits {
  QuadricHit hits[2];
  int count;
};

SD_HD void sphereUv(float3 n, float& u, float& v) {
  u = (atan2f(n.z, n.x) + SD_PI) / (2.0f * SD_PI);
  v = (asinf(clampf(n.y, -1.0f, 1.0f)) + SD_PI / 2.0f) / SD_PI;
}

SD_HD QuadricHits intersectSphere(float3 o, float3 d, float tmin, float tmax) {
  QuadricHits r; r.count = 0;
  float a = dot(d, d);
  float b = dot(o, d);
  float c = dot(o, o) - 1.0f;
  float disc = b * b - a * c;
  if (disc <= 0.0f) return r;
  float s = sqrtf(disc);
  const float roots[2] = {(-b - s) / a, (-b + s) / a};
  for (int i = 0; i < 2; i++) {
    float t = roots[i];
    if (t > tmin && t < tmax) {
      QuadricHit& h = r.hits[r.count++];
      float3 p = o + t * d;
      h.t = t;
      h.n = normalize(p);
      sphereUv(h.n, h.u, h.v);
    }
  }
  return r;
}

SD_HD QuadricHits intersectRect(float3 o, float3 d, float tmin, float tmax) {
  QuadricHits r; r.count = 0;
  if (d.y == 0.0f) return r;
  float t = -o.y / d.y;
  if (t <= tmin || t >= tmax) return r;
  float x = o.x + t * d.x, z = o.z + t * d.z;
  if (fabsf(x) > 1.0f || fabsf(z) > 1.0f) return r;
  QuadricHit& h = r.hits[r.count++];
  h.t = t;
  h.n = f3(0.0f, 1.0f, 0.0f);
  h.u = (x + 1.0f) * 0.5f;
  h.v = (z + 1.0f) * 0.5f;
  return r;
}

SD_HD QuadricHits intersectDisk(float3 o, float3 d, float tmin, float tmax) {
  QuadricHits r; r.count = 0;
  if (d.y == 0.0f) return r;
  float t = -o.y / d.y;
  if (t <= tmin || t >= tmax) return r;
  float x = o.x + t * d.x, z = o.z + t * d.z;
  float rad = sqrtf(x * x + z * z);
  if (rad > 1.0f) return r;
  QuadricHit& h = r.hits[r.count++];
  h.t = t;
  h.n = f3(0.0f, 1.0f, 0.0f);
  h.u = (atan2f(z, x) + SD_PI) / (2.0f * SD_PI);
  h.v = rad;
  return r;
}

SD_HD QuadricHits intersectCylinder(float3 o, float3 d, float tmin, float tmax) {
  QuadricHits r; r.count = 0;
  float a = d.x * d.x + d.z * d.z;
  if (a == 0.0f) return r;
  float b = o.x * d.x + o.z * d.z;
  float c = o.x * o.x + o.z * o.z - 1.0f;
  float disc = b * b - a * c;
  if (disc <= 0.0f) return r;
  float s = sqrtf(disc);
  const float roots[2] = {(-b - s) / a, (-b + s) / a};
  for (int i = 0; i < 2; i++) {
    float t = roots[i];
    if (t > tmin && t < tmax) {
      float3 p = o + t * d;
      if (fabsf(p.y) <= 1.0f) {
        QuadricHit& h = r.hits[r.count++];
        h.t = t;
        h.n = normalize(f3(p.x, 0.0f, p.z));
        h.u = (atan2f(p.z, p.x) + SD_PI) / (2.0f * SD_PI);
        h.v = (p.y + 1.0f) * 0.5f;
      }
    }
  }
  return r;
}

SD_HD QuadricHits intersectParabola(float3 o, float3 d, float tmin, float tmax) {
  QuadricHits r; r.count = 0;
  // 0.5*((ox+t*dx)^2 + (oz+t*dz)^2) - (oy+t*dy) = 0
  float A = 0.5f * (d.x * d.x + d.z * d.z);
  float B = o.x * d.x + o.z * d.z - d.y;
  float C = 0.5f * (o.x * o.x + o.z * o.z) - o.y;
  float t0, t1;
  int n = 0;
  if (fabsf(A) < 1e-12f) {
    if (B == 0.0f) return r;
    t0 = -C / B;
    n = 1;
  } else {
    float disc = B * B - 4.0f * A * C;
    if (disc <= 0.0f) return r;
    float s = sqrtf(disc);
    t0 = (-B - s) / (2.0f * A);
    t1 = (-B + s) / (2.0f * A);
    n = 2;
  }
  for (int i = 0; i < n; i++) {
    float t = (i == 0) ? t0 : t1;
    if (t > tmin && t < tmax) {
      float3 p = o + t * d;
      if (p.x * p.x + p.z * p.z <= 1.0f) {
        QuadricHit& h = r.hits[r.count++];
        h.t = t;
        // grad(0.5*(x^2+z^2) - y) = (x, -1, z): convex-side ("front") normal
        h.n = normalize(f3(p.x, -1.0f, p.z));
        h.u = (atan2f(p.z, p.x) + SD_PI) / (2.0f * SD_PI);
        h.v = sqrtf(p.x * p.x + p.z * p.z);
      }
    }
  }
  return r;
}

SD_HD QuadricHits intersectQuadric(int kind, float3 o, float3 d, float tmin, float tmax) {
  switch (kind) {
    case GK_SPHERE:   return intersectSphere(o, d, tmin, tmax);
    case GK_RECT:     return intersectRect(o, d, tmin, tmax);
    case GK_DISK:     return intersectDisk(o, d, tmin, tmax);
    case GK_CYLINDER: return intersectCylinder(o, d, tmin, tmax);
    case GK_PARABOLA: return intersectParabola(o, d, tmin, tmax);
    default: { QuadricHits r; r.count = 0; return r; }
  }
}

// Object-space AABBs for GAS builds (host uses these too).
SD_HD void quadricAabb(int kind, float* lo, float* hi) {
  const float e = 1e-4f;
  switch (kind) {
    case GK_RECT:
    case GK_DISK:
      lo[0] = -1; lo[1] = -e; lo[2] = -1;
      hi[0] = 1; hi[1] = e; hi[2] = 1;
      break;
    case GK_PARABOLA:
      lo[0] = -1; lo[1] = -e; lo[2] = -1;
      hi[0] = 1; hi[1] = 0.5f + e; hi[2] = 1;
      break;
    default:  // sphere, cylinder
      lo[0] = -1; lo[1] = -1; lo[2] = -1;
      hi[0] = 1; hi[1] = 1; hi[2] = 1;
      break;
  }
}

}  // namespace sd

#endif

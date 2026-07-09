// sundog: vector/matrix math shared by OptiX device code and host code.
// Compiles three ways:
//   - nvcc (device):        __CUDACC__ defined, CUDA vector types available
//   - g++ with CUDA (host): SD_HAVE_CUDA defined, include <cuda_runtime.h> first
//   - plain g++ (tests):    neither -> local vector-type shim below
#ifndef SUNDOG_MATH_CUH
#define SUNDOG_MATH_CUH

#include <math.h>

#if defined(__CUDACC__)
#  define SD_HD __host__ __device__ __forceinline__
#else
#  define SD_HD inline
#endif

#if defined(SD_HAVE_CUDA) && !defined(__CUDACC__)
#include <cuda_runtime.h>
#endif

#if !defined(__CUDACC__) && !defined(SD_HAVE_CUDA)
struct float2 { float x, y; };
struct float3 { float x, y, z; };
struct float4 { float x, y, z, w; };
struct uint3  { unsigned x, y, z; };
typedef unsigned long long cudaTextureObject_t;
SD_HD float2 make_float2(float x, float y) { return {x, y}; }
SD_HD float3 make_float3(float x, float y, float z) { return {x, y, z}; }
SD_HD float4 make_float4(float x, float y, float z, float w) { return {x, y, z, w}; }
#endif

namespace sd {

SD_HD float3 f3(float v) { return make_float3(v, v, v); }
SD_HD float3 f3(float x, float y, float z) { return make_float3(x, y, z); }

SD_HD float3 operator+(float3 a, float3 b) { return f3(a.x + b.x, a.y + b.y, a.z + b.z); }
SD_HD float3 operator-(float3 a, float3 b) { return f3(a.x - b.x, a.y - b.y, a.z - b.z); }
SD_HD float3 operator-(float3 a) { return f3(-a.x, -a.y, -a.z); }
SD_HD float3 operator*(float3 a, float3 b) { return f3(a.x * b.x, a.y * b.y, a.z * b.z); }
SD_HD float3 operator*(float3 a, float s) { return f3(a.x * s, a.y * s, a.z * s); }
SD_HD float3 operator*(float s, float3 a) { return a * s; }
SD_HD float3 operator/(float3 a, float s) { return a * (1.0f / s); }
SD_HD float3 operator/(float3 a, float3 b) { return f3(a.x / b.x, a.y / b.y, a.z / b.z); }
SD_HD float3& operator+=(float3& a, float3 b) { a = a + b; return a; }
SD_HD float3& operator*=(float3& a, float3 b) { a = a * b; return a; }
SD_HD float3& operator*=(float3& a, float s) { a = a * s; return a; }

SD_HD float2 operator+(float2 a, float2 b) { return make_float2(a.x + b.x, a.y + b.y); }
SD_HD float2 operator*(float2 a, float s) { return make_float2(a.x * s, a.y * s); }

SD_HD float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
SD_HD float3 cross(float3 a, float3 b) {
  return f3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
SD_HD float length2(float3 a) { return dot(a, a); }
SD_HD float length(float3 a) { return sqrtf(dot(a, a)); }
SD_HD float3 normalize(float3 a) { return a * (1.0f / sqrtf(dot(a, a))); }

SD_HD float clampf(float x, float lo, float hi) { return fminf(hi, fmaxf(lo, x)); }
SD_HD float3 clamp3(float3 v, float lo, float hi) {
  return f3(clampf(v.x, lo, hi), clampf(v.y, lo, hi), clampf(v.z, lo, hi));
}
SD_HD float3 min3(float3 a, float3 b) { return f3(fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z)); }
SD_HD float maxComp(float3 v) { return fmaxf(v.x, fmaxf(v.y, v.z)); }
SD_HD float3 lerp3(float3 a, float3 b, float t) { return a + (b - a) * t; }
SD_HD float3 reflect(float3 v, float3 n) { return v - 2.0f * dot(v, n) * n; }

// Snell refraction. v: unit incident (toward surface), n: unit normal against v.
SD_HD bool refract(float3 v, float3 n, float eta, float3& out) {
  float cosi = -dot(v, n);
  float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
  if (k < 0.0f) return false;
  out = eta * v + (eta * cosi - sqrtf(k)) * n;
  return true;
}

SD_HD float schlick(float cosine, float f0) {
  float m = clampf(1.0f - cosine, 0.0f, 1.0f);
  float m2 = m * m;
  return f0 + (1.0f - f0) * m2 * m2 * m;
}
SD_HD float3 schlick3(float cosine, float3 f0) {
  float m = clampf(1.0f - cosine, 0.0f, 1.0f);
  float m2 = m * m;
  return f0 + (f3(1.0f) - f0) * (m2 * m2 * m);
}

// Branchless orthonormal basis from unit normal (Duff et al. 2017).
struct Onb {
  float3 t, b, n;
  SD_HD explicit Onb(float3 nn) : n(nn) {
    float s = copysignf(1.0f, nn.z);
    float a = -1.0f / (s + nn.z);
    float c = nn.x * nn.y * a;
    t = f3(1.0f + s * nn.x * nn.x * a, s * c, -s * nn.x);
    b = f3(c, s + nn.y * nn.y * a, -nn.y);
  }
  SD_HD float3 toWorld(float3 v) const { return v.x * t + v.y * b + v.z * n; }
  SD_HD float3 toLocal(float3 v) const { return f3(dot(v, t), dot(v, b), dot(v, n)); }
};

// Row-major 3x4 affine transform (rows match OptixInstance.transform).
struct Affine {
  float m[12];
  SD_HD static Affine identity() {
    return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
  }
  SD_HD float3 applyPoint(float3 p) const {
    return f3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3],
              m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
              m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
  }
  SD_HD float3 applyVector(float3 v) const {
    return f3(m[0] * v.x + m[1] * v.y + m[2] * v.z,
              m[4] * v.x + m[5] * v.y + m[6] * v.z,
              m[8] * v.x + m[9] * v.y + m[10] * v.z);
  }
};

SD_HD Affine mul(const Affine& a, const Affine& b) {  // a ∘ b (b applied first)
  Affine r{};
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      r.m[i * 4 + j] = a.m[i * 4 + 0] * b.m[0 * 4 + j] +
                       a.m[i * 4 + 1] * b.m[1 * 4 + j] +
                       a.m[i * 4 + 2] * b.m[2 * 4 + j];
    }
    r.m[i * 4 + 3] = a.m[i * 4 + 0] * b.m[0 * 4 + 3] +
                     a.m[i * 4 + 1] * b.m[1 * 4 + 3] +
                     a.m[i * 4 + 2] * b.m[2 * 4 + 3] + a.m[i * 4 + 3];
  }
  return r;
}

SD_HD Affine affineScale(float3 s) {
  return {{s.x, 0, 0, 0, 0, s.y, 0, 0, 0, 0, s.z, 0}};
}
SD_HD Affine affineTranslate(float3 t) {
  return {{1, 0, 0, t.x, 0, 1, 0, t.y, 0, 0, 1, t.z}};
}
SD_HD Affine affineRotateX(float rad) {
  float c = cosf(rad), s = sinf(rad);
  return {{1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0}};
}
SD_HD Affine affineRotateY(float rad) {
  float c = cosf(rad), s = sinf(rad);
  return {{c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0}};
}
SD_HD Affine affineRotateZ(float rad) {
  float c = cosf(rad), s = sinf(rad);
  return {{c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0}};
}

constexpr float SD_PI = 3.14159265358979323846f;
constexpr float SD_INV_PI = 0.31830988618379067154f;

// Self-intersection-safe ray origin: offset along n (which must face the
// outgoing hemisphere), scaled with distance from origin.
SD_HD float3 offsetRay(float3 p, float3 n) {
  float s = 1e-4f * (1.0f + fmaxf(fabsf(p.x), fmaxf(fabsf(p.y), fabsf(p.z))));
  return p + n * s;
}

}  // namespace sd

#endif

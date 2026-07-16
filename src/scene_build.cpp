// sundog: scene derivation (see scene_build.h). Every function body in here
// was moved verbatim from the old JSON loader — do not "improve" the float
// math, the golden images pin it bit-for-bit.
#include "scene_build.h"

#include <cmath>
#include <stdexcept>

namespace sd {

void sceneFail(const std::string& msg) { throw std::runtime_error("scene: " + msg); }

CameraData makeCamera(const CameraSettings& cs, int width, int height) {
  CameraData c{};
  float aspect = (float)width / (float)height;
  float theta = cs.vfov * SD_PI / 180.0f;
  float halfH = tanf(theta / 2.0f);
  float halfW = aspect * halfH;
  float focus = cs.focusDist > 0.0f ? cs.focusDist : length(cs.lookfrom - cs.lookat);
  c.w = normalize(cs.lookfrom - cs.lookat);
  c.u = normalize(cross(cs.up, c.w));
  c.v = cross(c.w, c.u);
  c.origin = cs.lookfrom;
  c.lowerLeft = c.origin - halfW * focus * c.u - halfH * focus * c.v - focus * c.w;
  c.horizontal = 2.0f * halfW * focus * c.u;
  c.vertical = 2.0f * halfH * focus * c.v;
  c.lensRadius = cs.aperture / 2.0f;
  return c;
}

Affine composeTransform(const XformStep* steps, int n) {
  Affine m = Affine::identity();
  const float d2r = SD_PI / 180.0f;
  for (int i = 0; i < n; i++) {
    const XformStep& st = steps[i];
    Affine step;
    switch (st.kind) {
      case XF_SCALE:     step = affineScale(f3(st.a, st.b, st.c)); break;
      case XF_TRANSLATE: step = affineTranslate(f3(st.a, st.b, st.c)); break;
      case XF_ROTATE_X:  step = affineRotateX(st.a * d2r); break;
      case XF_ROTATE_Y:  step = affineRotateY(st.a * d2r); break;
      case XF_ROTATE_Z:  step = affineRotateZ(st.a * d2r); break;
      default: sceneFail("unknown transform step kind");
    }
    m = mul(step, m);  // top-down: later steps wrap earlier ones
  }
  return m;
}

static const char* geomKindName(int k) {
  switch (k) {
    case GK_SPHERE: return "sphere";
    case GK_RECT: return "rect";
    case GK_DISK: return "disk";
    case GK_CYLINDER: return "cylinder";
    case GK_PARABOLA: return "parabola";
    default: return "mesh";
  }
}

void addObjectDerived(Scene& s, SceneObject& so) {
  if (so.matFront == MAT_NONE && so.matBack == MAT_NONE)
    sceneFail("object has no material on either face");

  if (so.physics.enabled) {
    if (!s.physics.enabled)
      sceneFail("physics: object opts in but the scene has no top-level physics block");
    if (so.geomKind == GK_DISK || so.geomKind == GK_CYLINDER || so.geomKind == GK_PARABOLA)
      sceneFail(std::string("physics: shape '") + geomKindName(so.geomKind) +
                "' is not supported as a collider (use sphere, rect, or mesh)");
    if (so.geomKind == GK_RECT && so.physics.dynamic)
      sceneFail("physics: rect colliders are static-only (zero-thickness plates tunnel)");
    if (so.physics.dynamic && so.physics.density <= 0.0f)
      sceneFail("physics: dynamic body needs positive density");
    if (so.physics.thickness <= 0.0f)
      sceneFail("physics: thickness must be positive");
  }

  // Auto-register emissive rect/disk/sphere objects as NEE area lights.
  uint16_t emId = MAT_NONE;
  if (so.matFront != MAT_NONE && s.materials[so.matFront].kind == MT_EMISSIVE)
    emId = so.matFront;
  bool registersLight =
      emId != MAT_NONE && so.nee &&
      (so.geomKind == GK_RECT || so.geomKind == GK_DISK || so.geomKind == GK_SPHERE);
  // Checked before the light is pushed so a rejected add leaves no orphan
  // light behind (the old all-or-nothing loader could afford to check after).
  if (so.physics.dynamic && registersLight)
    sceneFail("physics: a dynamic object cannot be an NEE area light (the light "
              "frame is baked at parse time); set nee:false or keep it static");
  if (registersLight) {
    const MaterialDesc& em = s.materials[emId];
    LightDesc ld{};
    ld.texId = -1;
    ld.p = so.xform.applyPoint(f3(0, 0, 0));
    float3 ux = so.xform.applyVector(f3(1, 0, 0));
    float3 uy = so.xform.applyVector(f3(0, 1, 0));
    float3 uz = so.xform.applyVector(f3(0, 0, 1));
    // Sign of det(M): cross(uz,ux) picks up the determinant sign while the
    // device normal (inverse-transpose) does not — flip to stay consistent
    // under mirroring transforms.
    float detSign = dot(uy, cross(uz, ux)) < 0.0f ? -1.0f : 1.0f;
    ld.L = em.color * em.intensity;
    ld.twoSided = em.twoSided;
    if (em.texId >= 0) {
      if (so.geomKind == GK_SPHERE)
        sceneFail("textured emissive sphere cannot be an NEE light (uv frame is "
                  "object-space); use a solid color or set nee:false");
      ld.texId = em.texId;
      ld.L = f3(em.intensity);  // Li = tex(uv) * intensity, matching emitter hits
    }
    if (so.geomKind == GK_RECT) {
      ld.kind = LT_RECT;
      ld.u = ux;
      ld.v = uz;
      ld.n = normalize(cross(uz, ux)) * detSign;
      ld.area = 4.0f * length(cross(ux, uz));
    } else if (so.geomKind == GK_DISK) {
      float ru = length(ux), rz = length(uz);
      if (fabsf(ru - rz) > 1e-3f * fmaxf(ru, rz))
        sceneFail("disk NEE light requires uniform XZ scale (pdf would be wrong); set nee:false");
      ld.kind = LT_DISK;
      ld.u = ux;
      ld.v = uz;
      ld.n = normalize(cross(uz, ux)) * detSign;
      ld.area = SD_PI * ru * rz;
    } else {
      float rx = length(ux), rz = length(uz);
      float ry = length(so.xform.applyVector(f3(0, 1, 0)));
      if (fabsf(rx - ry) > 1e-3f * rx || fabsf(rx - rz) > 1e-3f * rx)
        sceneFail("sphere NEE light requires uniform scale; set nee:false");
      ld.kind = LT_SPHERE;
      ld.radius = rx;
    }
    so.lightId = (int)s.lights.size();
    s.lights.push_back(ld);
  }
  s.objects.push_back(so);
}

void addFlameDerived(Scene& s, const FlameDesc& fd, float lightIntensity) {
  if (fd.height <= 0.0f || fd.radius <= 0.0f)
    sceneFail("flame: height and radius must be positive");
  if (fd.intensity < 0.0f || fd.sigma < 0.0f)
    sceneFail("flame: intensity and sigma must be >= 0");
  // The volume emission makes the flame visible; two embedded warm point
  // lights (soft shadows via radius) make it illuminate the scene through
  // the regular NEE machinery. Documented approximation: BSDF paths that
  // happen to cross the volume add a small extra on top of these lights.
  const float frac[2] = {0.65f, 0.35f};
  const float hfac[2] = {0.35f, 0.70f};
  const float3 warm[2] = {f3(1.0f, 0.50f, 0.15f), f3(1.0f, 0.62f, 0.25f)};
  for (int k = 0; k < 2; k++) {
    LightDesc ld{};
    ld.texId = -1;
    ld.kind = LT_POINT;
    ld.p = fd.base + f3(0.0f, hfac[k] * fd.height, 0.0f);
    ld.radius = 0.3f * fd.radius;
    ld.L = warm[k] * (lightIntensity * frac[k]);
    s.lights.push_back(ld);
  }
  s.flames.push_back(fd);
}

}  // namespace sd

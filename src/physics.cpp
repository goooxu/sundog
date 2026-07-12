// sundog: PhysX GPU rigid-body settling — the only TU that touches PhysX.
//
// Determinism: fixed timestep, actors created in scene order, fixed solver
// iteration counts, single CPU worker for residual host tasks, and the whole
// simulation on one GPU. Run-to-run reproducibility on the same GPU/driver is
// validated empirically (double-render sha256 in the verification flow);
// cross-machine reproducibility is not promised.
#include "physics.h"

#include <PxPhysicsAPI.h>
#include <gpu/PxGpu.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

using namespace physx;

namespace sd {

namespace {

[[noreturn]] void pfail(const std::string& msg) {
  throw std::runtime_error("physics: " + msg);
}

// T·R·S decomposition of a composed affine: S from column norms, R from the
// normalized columns. Shear and mirroring have no rigid-body counterpart and
// are rejected; dynamic bodies additionally need uniform scale so mass
// properties and PxMeshScale stay exact under rotation.
struct Trs {
  PxTransform pose;
  PxVec3 scale;
};

Trs decompose(const Affine& a, size_t objIndex, bool dynamic) {
  PxVec3 c0(a.m[0], a.m[4], a.m[8]);
  PxVec3 c1(a.m[1], a.m[5], a.m[9]);
  PxVec3 c2(a.m[2], a.m[6], a.m[10]);
  PxVec3 s(c0.magnitude(), c1.magnitude(), c2.magnitude());
  const std::string tag = "object " + std::to_string(objIndex);
  if (s.x < 1e-8f || s.y < 1e-8f || s.z < 1e-8f) pfail(tag + ": degenerate scale");
  PxVec3 r0 = c0 / s.x, r1 = c1 / s.y, r2 = c2 / s.z;
  float shear = fmaxf(fabsf(r0.dot(r1)), fmaxf(fabsf(r1.dot(r2)), fabsf(r0.dot(r2))));
  if (shear > 1e-3f) pfail(tag + ": sheared transform not supported");
  if (r0.cross(r1).dot(r2) < 0.0f) pfail(tag + ": mirrored transform not supported");
  if (dynamic && (fabsf(s.x - s.y) > 1e-3f * s.x || fabsf(s.x - s.z) > 1e-3f * s.x))
    pfail(tag + ": dynamic body needs uniform scale");
  PxQuat q(PxMat33(r0, r1, r2));
  q.normalize();
  return {PxTransform(PxVec3(a.m[3], a.m[7], a.m[11]), q), s};
}

// xform = T(p) · R(q) · S_orig, row-major 3x4.
void bakePose(Affine& a, const PxTransform& t, const PxVec3& s) {
  PxMat33 r(t.q);
  a.m[0] = r.column0.x * s.x; a.m[1] = r.column1.x * s.y; a.m[2]  = r.column2.x * s.z; a.m[3]  = t.p.x;
  a.m[4] = r.column0.y * s.x; a.m[5] = r.column1.y * s.y; a.m[6]  = r.column2.y * s.z; a.m[7]  = t.p.y;
  a.m[8] = r.column0.z * s.x; a.m[9] = r.column1.z * s.y; a.m[10] = r.column2.z * s.z; a.m[11] = t.p.z;
}

}  // namespace

PhysicsStats runPhysics(Scene& scene, const std::vector<GpuMesh>& meshes) {
  const auto t0 = std::chrono::steady_clock::now();
  const PhysicsSettings& cfg = scene.physics;
  PhysicsStats st;

  static PxDefaultAllocator alloc;
  static PxDefaultErrorCallback errCb;
  PxFoundation* foundation = PxCreateFoundation(PX_PHYSICS_VERSION, alloc, errCb);
  if (!foundation) pfail("PxCreateFoundation failed");
  PxPhysics* physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale());
  if (!physics) pfail("PxCreatePhysics failed");

  // GPU simulation is mandatory: rendering already requires an NVIDIA GPU,
  // so an unusable CUDA context here is a configuration error, not a case
  // for a silent CPU fallback.
  PxCudaContextManagerDesc cudaDesc;
  PxCudaContextManager* cudaCtx = PxCreateCudaContextManager(*foundation, cudaDesc);
  if (!cudaCtx || !cudaCtx->contextIsValid())
    pfail("PhysX GPU unavailable: CUDA context manager creation failed "
          "(is libPhysXGpu_64.so on LD_LIBRARY_PATH / next to the binary?)");

  PxDefaultCpuDispatcher* dispatcher = PxDefaultCpuDispatcherCreate(1);
  PxSceneDesc sceneDesc(physics->getTolerancesScale());
  sceneDesc.gravity = PxVec3(cfg.gravity.x, cfg.gravity.y, cfg.gravity.z);
  sceneDesc.cpuDispatcher = dispatcher;
  sceneDesc.cudaContextManager = cudaCtx;
  sceneDesc.filterShader = PxDefaultSimulationFilterShader;
  sceneDesc.flags |= PxSceneFlag::eENABLE_GPU_DYNAMICS | PxSceneFlag::eENABLE_PCM;
  sceneDesc.broadPhaseType = PxBroadPhaseType::eGPU;
  PxScene* px = physics->createScene(sceneDesc);
  if (!px) pfail("createScene failed (GPU dynamics unsupported on this device?)");

  PxCookingParams cookParams(physics->getTolerancesScale());
  std::vector<PxConvexMesh*> convexes(meshes.size(), nullptr);
  auto convexOf = [&](int meshId) {
    if (!convexes[meshId]) {
      const GpuMesh& gm = meshes[meshId];
      PxConvexMeshDesc cd;
      cd.points.count = (PxU32)gm.hostPositions.size();
      cd.points.stride = sizeof(float3);
      cd.points.data = gm.hostPositions.data();
      cd.flags = PxConvexFlag::eCOMPUTE_CONVEX;
      cd.vertexLimit = 64;
      convexes[meshId] =
          PxCreateConvexMesh(cookParams, cd, physics->getPhysicsInsertionCallback());
      if (!convexes[meshId])
        pfail("convex cooking failed for mesh " + std::to_string(meshId));
    }
    return convexes[meshId];
  };

  std::map<std::pair<float, float>, PxMaterial*> matCache;
  auto matOf = [&](float friction, float restitution) {
    if (friction < 0.0f) friction = cfg.friction;
    if (restitution < 0.0f) restitution = cfg.restitution;
    PxMaterial*& m = matCache[{friction, restitution}];
    if (!m) m = physics->createMaterial(friction, friction, restitution);
    return m;
  };

  struct DynBody {
    PxRigidDynamic* actor;
    size_t obj;
    PxVec3 scale;
  };
  std::vector<DynBody> dyn;
  for (size_t i = 0; i < scene.objects.size(); i++) {
    SceneObject& o = scene.objects[i];
    if (!o.physics.enabled) continue;
    const PhysicsObject& po = o.physics;
    Trs trs = decompose(o.xform, i, po.dynamic);
    PxMaterial* mat = matOf(po.friction, po.restitution);

    if (o.geomKind == GK_RECT) {
      // Canonical rect: XZ [-1,1]^2 at y=0, front = +Y. Collide as a solid
      // slab of depth `thickness` behind the face so nothing tunnels the
      // zero-thickness render plane.
      PxRigidStatic* a = physics->createRigidStatic(trs.pose);
      PxShape* sh = PxRigidActorExt::createExclusiveShape(
          *a, PxBoxGeometry(trs.scale.x, po.thickness * 0.5f, trs.scale.z), *mat);
      sh->setLocalPose(PxTransform(PxVec3(0.0f, -po.thickness * 0.5f, 0.0f)));
      px->addActor(*a);
      st.statics++;
      continue;
    }

    PxGeometryHolder geom;
    if (o.geomKind == GK_SPHERE) {
      geom.storeAny(PxSphereGeometry(trs.scale.x));
    } else {  // GK_MESH (other shapes rejected at parse)
      geom.storeAny(PxConvexMeshGeometry(convexOf(o.meshId), PxMeshScale(trs.scale)));
    }
    if (po.dynamic) {
      PxRigidDynamic* a = physics->createRigidDynamic(trs.pose);
      PxRigidActorExt::createExclusiveShape(*a, geom.any(), *mat);
      PxRigidBodyExt::updateMassAndInertia(*a, po.density);
      a->setLinearVelocity(PxVec3(po.velocity.x, po.velocity.y, po.velocity.z));
      a->setAngularVelocity(
          PxVec3(po.angularVelocity.x, po.angularVelocity.y, po.angularVelocity.z));
      a->setSolverIterationCounts(cfg.posIters, cfg.velIters);
      // GPU dynamics has no sweep-based CCD; speculative contacts cover the
      // remaining fast-motion cases together with the thick static slabs.
      a->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD, true);
      if (cfg.sleepThreshold >= 0.0f) a->setSleepThreshold(cfg.sleepThreshold);
      px->addActor(*a);
      dyn.push_back({a, i, trs.scale});
    } else {
      PxRigidStatic* a = physics->createRigidStatic(trs.pose);
      PxRigidActorExt::createExclusiveShape(*a, geom.any(), *mat);
      px->addActor(*a);
      st.statics++;
    }
  }
  if (dyn.empty()) pfail("physics block present but no dynamic bodies");

  auto allAsleep = [&] {
    for (const DynBody& b : dyn)
      if (!b.actor->isSleeping()) return false;
    return true;
  };
  if (cfg.stopTime > 0.0f) {
    // Freeze-frame: run exactly to the requested instant and bake mid-motion
    // poses. No sleep checks, no timeout warning — stopping here is the plan.
    const int steps = (int)ceilf(cfg.stopTime / cfg.timestep);
    while (st.steps < steps) {
      px->simulate(cfg.timestep);
      px->fetchResults(true);
      st.steps++;
    }
    st.settled = true;
  } else {
    const int maxSteps = (int)ceilf(cfg.maxTime / cfg.timestep);
    while (st.steps < maxSteps) {
      px->simulate(cfg.timestep);
      px->fetchResults(true);
      st.steps++;
      if (st.steps % 30 == 0 && allAsleep()) {
        st.settled = true;
        break;
      }
    }
    if (!st.settled) st.settled = allAsleep();
    if (!st.settled)
      fprintf(stderr, "physics: WARNING: not settled after %.1f s sim time; baking anyway\n",
              cfg.maxTime);
  }

  for (const DynBody& b : dyn)
    bakePose(scene.objects[b.obj].xform, b.actor->getGlobalPose(), b.scale);

  st.bodies = (int)dyn.size();
  st.simSeconds = (double)st.steps * cfg.timestep;

  px->release();
  dispatcher->release();
  for (PxConvexMesh* c : convexes)
    if (c) c->release();
  physics->release();
  cudaCtx->release();
  foundation->release();

  st.wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                  .count();
  return st;
}

}  // namespace sd

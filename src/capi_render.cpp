// sundog: C API — probe and render. The render orchestration is the old
// main.cpp moved verbatim: scene -> upload -> physics -> accel -> pipeline ->
// chunked launch loop -> optional denoise -> PQ AVIF + stats. Differences from
// the executable era: overrides come from sundog_render_options instead of
// the CLI, progress goes to stderr (stdout belongs to the hosting Python
// program), and the OptiX context is destroyed on the way out (a library
// cannot lean on process teardown).
#include "accel.h"
#include "capi_internal.h"
#include "context.h"
#include "cuda_check.h"
#include "denoise.h"
#include "env_light.h"
#include "film.h"
#include "mesh_obj.h"
#include "physics.h"
#include "pipeline.h"
#include "scene_build.h"
#include "stats.h"
#include "textures.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>

using namespace sd;
using namespace sd_capi;
using Clock = std::chrono::steady_clock;

static double msSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

extern "C" int sundog_probe(sundog_device_info* out) {
  SUNDOG_TRY
  if (!out) throw std::runtime_error("probe: null output");
  OptixDeviceContext ctx = createContext(false);
  DeviceInfo di = queryDeviceInfo(ctx);
  *out = sundog_device_info{};
  std::snprintf(out->name, sizeof(out->name), "%s", di.name.c_str());
  out->cc_major = di.ccMajor;
  out->cc_minor = di.ccMinor;
  out->driver_version = di.driverVersion;
  out->runtime_version = di.runtimeVersion;
  out->total_mem_mb = di.totalMemMB;
  out->optix_header_version = di.optixHeaderVersion;
  out->rtcore_version = di.rtcoreVersion;
  OPTIX_CHECK(optixDeviceContextDestroy(ctx));
  return SUNDOG_OK;
  SUNDOG_CATCH(SUNDOG_ERROR)
}

extern "C" int sundog_render(sundog_scene* h, const sundog_render_options* opt) {
  SUNDOG_TRY
  if (!opt || !opt->out_path || !*opt->out_path)
    throw std::runtime_error("render: out_path is required");
  if (sundog_scene_validate(h) != SUNDOG_OK)
    throw std::runtime_error(sundog_last_error());
  Scene& scene = h->scene;
  const char* sceneName = opt->scene_name ? opt->scene_name : "(scene)";
  bool quiet = opt->quiet != 0;

  OptixDeviceContext ctx = createContext(false);
  {
    DeviceInfo device = queryDeviceInfo(ctx);

    auto tTotal = Clock::now();
    size_t freeBefore = 0, totalMem = 0;
    CUDA_CHECK(cudaMemGetInfo(&freeBefore, &totalMem));

    // ---- overrides (the old CLI layer, same sentinel semantics) ----
    auto tLoad = Clock::now();
    if (opt->spp > 0) scene.render.spp = opt->spp;
    if (opt->width > 0) { scene.render.width = opt->width; scene.render.height = opt->height; }
    if (opt->seed >= 0) scene.render.seed = (unsigned)opt->seed;
    if (opt->denoise >= 0) scene.render.denoise = opt->denoise == 1;
    if (opt->transparent_shadows >= 0)
      scene.render.transparentShadows = opt->transparent_shadows == 1;
    if (isset(opt->clamp) && opt->clamp >= 0.0) scene.render.clampVal = (float)opt->clamp;
    const RenderSettings& rs = scene.render;

    TextureSet textureSet;
    std::vector<TextureDesc> texDescs = textureSet.upload(scene);
    EnvMap envMap;  // owns the env texture + CDFs for the render's lifetime

    std::vector<GpuMesh> meshes;
    size_t meshTriangles = 0;
    for (const SceneMesh& sm : scene.meshes) {
      std::string path = sm.objFile;
      if (!path.empty() && path[0] != '/') path = scene.baseDir + "/" + path;
      meshes.push_back(loadObjMesh(path, sm.smoothNormals, sm.usemtl));
      meshTriangles += meshes.back().numTris;
    }
    double sceneLoadMs = msSince(tLoad);

    // ---- physics settling / freeze-frame (bakes poses into object xforms) ----
    double physicsMs = 0;
    float physicsTime = isset(opt->physics_time) ? (float)opt->physics_time : -1.0f;
    if (physicsTime >= 0.0f && !scene.physics.enabled)
      throw std::runtime_error("--physics-time: scene has no physics block");
    if (scene.physics.enabled) {
      if (physicsTime >= 0.0f) scene.physics.stopTime = physicsTime;
      bool freeze = scene.physics.stopTime > 0.0f;
      auto tPhys = Clock::now();
      PhysicsStats phys = runPhysics(scene, meshes);
      physicsMs = msSince(tPhys);
      if (!quiet) {
        fprintf(stderr,
                "physics(GPU): %d bodies, %d statics, %d steps, %s at %.2f s sim time (%.0f ms wall)\n",
                phys.bodies, phys.statics, phys.steps,
                freeze ? "captured" : (phys.settled ? "settled" : "TIMEOUT"),
                phys.simSeconds, physicsMs);
      }
    }

    // ---- mesh area lights: world-space triangle CDFs ----
    // Parse time only registered placeholders (the OBJ was not loaded yet);
    // patch the triangle geometry into a render-local COPY of the light list
    // — the handle's scene.lights stays pure parse state, so no render-scoped
    // device pointer can dangle into a later render. Transforms are final
    // here: dynamic NEE mesh lights are rejected at parse time
    // (scene_build.cpp) and physics never moves statics.
    struct MeshLightData {
      CudaBuffer worldPos, cdf;
    };
    std::vector<MeshLightData> meshLightBufs;  // alive for the render scope
    std::vector<LightDesc> lights = scene.lights;
    for (const SceneObject& o : scene.objects) {
      if (o.geomKind != GK_MESH || o.lightId < 0) continue;
      const GpuMesh& m = meshes[o.meshId];
      std::vector<float3> wp(m.hostPositions.size());
      for (size_t i = 0; i < wp.size(); i++)
        wp[i] = o.xform.applyPoint(m.hostPositions[i]);
      // double accumulation: many small triangles, keep the prefix exact.
      // Known numeric edge: a triangle whose area is below ~1 ULP of the
      // running total rounds to an empty CDF interval and is never
      // NEE-selected, while the hit-side pdf still folds it into the total
      // area — a negative bias bounded by the sliver's own ~1e-7 relative
      // emission. Accepted; not worth per-triangle pdf plumbing.
      std::vector<float> cdf(m.numTris + 1, 0.0f);
      double total = 0.0;
      for (size_t t = 0; t < m.numTris; t++) {
        const uint3& tri = m.hostIndices[t];
        total += 0.5 * (double)length(cross(wp[tri.y] - wp[tri.x],
                                            wp[tri.z] - wp[tri.x]));
        cdf[t + 1] = (float)total;
      }
      if (!(total > 0.0))
        throw std::runtime_error("mesh NEE light has zero surface area: " +
                                 scene.meshes[o.meshId].objFile);
      for (size_t t = 1; t < m.numTris; t++) cdf[t] = (float)(cdf[t] / total);
      cdf[m.numTris] = 1.0f;  // exact upper bound for the binary search
      // Same sign convention as addObjectDerived's light frames (math.cuh).
      float ngSign = detSign3(o.xform.applyVector(f3(1, 0, 0)),
                              o.xform.applyVector(f3(0, 1, 0)),
                              o.xform.applyVector(f3(0, 0, 1)));
      MeshLightData mld;
      mld.worldPos.upload(wp);
      mld.cdf.upload(cdf);
      LightDesc& ld = lights[o.lightId];
      ld.mPositions = mld.worldPos.as<float3>();
      ld.mIndices = m.indices.as<uint3>();
      ld.mUvs = m.uvs.ptr ? m.uvs.as<float2>() : nullptr;
      ld.mCdf = mld.cdf.as<float>();
      ld.mNumTris = (int)m.numTris;
      ld.area = (float)total;
      ld.mNgSign = ngSign;
      meshLightBufs.push_back(std::move(mld));
    }

    // ---- acceleration structures ----
    auto tGas = Clock::now();
    std::vector<Gas> quadricGas(GK_MESH);
    bool kindUsed[GK_MESH] = {};
    for (const SceneObject& o : scene.objects)
      if (o.geomKind != GK_MESH) kindUsed[o.geomKind] = true;
    for (int k = 0; k < GK_MESH; k++)
      if (kindUsed[k]) quadricGas[k] = buildQuadricGas(ctx, k);
    std::vector<Gas> meshGas;
    for (const GpuMesh& m : meshes) meshGas.push_back(buildTriangleGas(ctx, m));
    Gas ias = buildIas(ctx, scene, quadricGas, meshGas);
    double gasBuildMs = msSince(tGas);

    // ---- pipeline & film ----
    Pipeline pipeline(ctx, false);
    pipeline.buildSbt(scene, meshes);
    Film film(rs.width, rs.height);

    CudaBuffer texBuf, matBuf, lightBuf, flameBuf, rayCounter;
    texBuf.upload(texDescs);
    matBuf.upload(scene.materials);
    if (!lights.empty()) lightBuf.upload(lights);
    if (!scene.flames.empty()) flameBuf.upload(scene.flames);
    rayCounter.allocZero(sizeof(unsigned long long));

    LaunchParams params{};
    params.width = rs.width;
    params.height = rs.height;
    params.sppTotal = rs.spp;
    params.maxDepth = rs.maxDepth;
    params.clampVal = rs.clampVal;
    params.seed = rs.seed;
    params.countRays = opt->stats_path ? 1 : 0;
    params.transparentShadows = rs.transparentShadows ? 1 : 0;
    params.accum = (float4*)film.accum();
    params.aovAlbedo = (float4*)film.aovAlbedo();
    params.aovNormal = (float4*)film.aovNormal();
    params.rayCounter = rayCounter.as<unsigned long long>();
    params.cam = makeCamera(scene.camera, rs.width, rs.height);
    params.bg = scene.bg;
    if (scene.bg.kind == BG_ENVMAP) params.env = envMap.upload(scene);
    params.textures = texBuf.as<TextureDesc>();
    params.materials = matBuf.as<MaterialDesc>();
    params.lights = lightBuf.ptr ? lightBuf.as<LightDesc>() : nullptr;
    params.numLights = (int)lights.size();
    params.flames = flameBuf.ptr ? flameBuf.as<FlameDesc>() : nullptr;
    params.numFlames = (int)scene.flames.size();
    params.handle = ias.handle;

    // ---- render loop ----
    if (!quiet) {
      fprintf(stderr, "sundog: %s %dx%d, %d spp, depth %d%s on %s\n", sceneName,
              rs.width, rs.height, rs.spp, rs.maxDepth,
              rs.denoise ? " [denoise]" : "",
              device.name.c_str());
    }
    auto tRender = Clock::now();
    int done = 0;
    while (done < rs.spp) {
      int chunk = std::min(rs.chunk, rs.spp - done);
      params.sampleOffset = done;
      params.sppThisLaunch = chunk;
      pipeline.launch(params, rs.width, rs.height);
      CUDA_CHECK(cudaDeviceSynchronize());
      done += chunk;
      if (!quiet) {
        fprintf(stderr, "\r%d/%d spp (%.1f%%)", done, rs.spp, 100.0 * done / rs.spp);
        fflush(stderr);
      }
    }
    double renderMs = msSince(tRender);
    if (!quiet) fprintf(stderr, "\n");

    // ---- denoise ----
    double denoiseMs = 0;
    CUdeviceptr outputBuf = film.accum();
    if (rs.denoise) {
      auto tDen = Clock::now();
      Denoiser denoiser(ctx, rs.width, rs.height);
      denoiser.run(film.accum(), film.aovAlbedo(), film.aovNormal(), film.denoised());
      denoiseMs = msSince(tDen);
      outputBuf = film.denoised();
    }

    // ---- output ----
    film.writeAvif(outputBuf, opt->out_path, rs.exposure);
    if (opt->aov_albedo_path) film.writeAovAvif(film.aovAlbedo(), opt->aov_albedo_path, false);
    if (opt->aov_normal_path) film.writeAovAvif(film.aovNormal(), opt->aov_normal_path, true);

    size_t freeAfter = 0;
    CUDA_CHECK(cudaMemGetInfo(&freeAfter, &totalMem));

    unsigned long long rays = 0;
    CUDA_CHECK(cudaMemcpy(&rays, (void*)rayCounter.ptr, sizeof(rays),
                          cudaMemcpyDeviceToHost));
    double totalMs = msSince(tTotal);
    if (!quiet) {
      fprintf(stderr, "Rendering elapsed time: %f seconds", renderMs / 1000.0);
      if (rays) fprintf(stderr, " (%.0f Mrays/s)", rays / renderMs / 1000.0);
      fprintf(stderr, "\nwrote %s\n", opt->out_path);
    }

    if (opt->stats_path) {
      RenderStats st;
      st.scene = sceneName;
      st.width = rs.width;
      st.height = rs.height;
      st.spp = rs.spp;
      st.maxDepth = rs.maxDepth;
      st.seed = rs.seed;
      st.denoised = rs.denoise;
      st.sceneLoadMs = sceneLoadMs;
      st.physicsMs = physicsMs;
      st.gasBuildMs = gasBuildMs;
      st.renderMs = renderMs;
      st.denoiseMs = denoiseMs;
      st.totalMs = totalMs;
      st.raysTraced = rays;
      st.mraysPerSec = rays / renderMs / 1000.0;
      // cudaMemGetInfo can report MORE free memory at exit than at start
      // (driver pool churn); clamp instead of underflowing the unsigned diff.
      st.peakVramMB = freeBefore > freeAfter ? (freeBefore - freeAfter) >> 20 : 0;
      st.numObjects = (int)scene.objects.size();
      st.numLights = (int)scene.lights.size();
      st.numMeshes = (int)scene.meshes.size();
      st.meshTriangles = meshTriangles;
      st.device = device;
      writeStats(st, opt->stats_path);
    }
  }  // all OptiX/CUDA RAII objects die before the context does
  OPTIX_CHECK(optixDeviceContextDestroy(ctx));
  return SUNDOG_OK;
  SUNDOG_CATCH(SUNDOG_ERROR)
}

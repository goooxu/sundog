// sundog: entry point. CLI -> scene -> upload -> accel -> pipeline -> render
// loop (chunked optixLaunch) -> optional denoise -> PNG + stats.
#include "accel.h"
#include "cli.h"
#include "context.h"
#include "cuda_check.h"
#include "denoise.h"
#include "film.h"
#include "mesh_obj.h"
#include "physics.h"
#include "pipeline.h"
#include "scene.h"
#include "stats.h"
#include "textures.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>

using namespace sd;
using Clock = std::chrono::steady_clock;

static double msSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main(int argc, char** argv) {
  try {
    CliOptions cli = parseCli(argc, argv);

    OptixDeviceContext ctx = createContext(false);
    DeviceInfo device = queryDeviceInfo(ctx);
    if (cli.probe) {
      printProbe(device);
      return 0;
    }

    auto tTotal = Clock::now();
    size_t freeBefore = 0, totalMem = 0;
    CUDA_CHECK(cudaMemGetInfo(&freeBefore, &totalMem));

    // ---- scene ----
    auto tLoad = Clock::now();
    Scene scene = loadScene(cli.scene);
    if (cli.spp > 0) scene.render.spp = cli.spp;
    if (cli.width > 0) { scene.render.width = cli.width; scene.render.height = cli.height; }
    if (cli.seed >= 0) scene.render.seed = (unsigned)cli.seed;
    if (cli.denoise >= 0) scene.render.denoise = cli.denoise == 1;
    if (cli.clampVal >= 0.0f) scene.render.clampVal = cli.clampVal;
    if (cli.gamma > 0.0f) scene.render.gamma = cli.gamma;
    const RenderSettings& rs = scene.render;

    TextureSet textureSet;
    std::vector<TextureDesc> texDescs = textureSet.upload(scene);

    std::vector<GpuMesh> meshes;
    size_t meshTriangles = 0;
    for (const SceneMesh& sm : scene.meshes) {
      std::string path = sm.objFile;
      if (!path.empty() && path[0] != '/') path = scene.baseDir + "/" + path;
      meshes.push_back(loadObjMesh(path, sm.smoothNormals));
      meshTriangles += meshes.back().numTris;
    }
    double sceneLoadMs = msSince(tLoad);

    // ---- physics settling / freeze-frame (bakes poses into object xforms) ----
    double physicsMs = 0;
    if (cli.physicsTime >= 0.0f && !scene.physics.enabled)
      throw std::runtime_error("--physics-time: scene has no physics block");
    if (scene.physics.enabled) {
      if (cli.physicsTime >= 0.0f) scene.physics.stopTime = cli.physicsTime;
      bool freeze = scene.physics.stopTime > 0.0f;
      auto tPhys = Clock::now();
      PhysicsStats phys = runPhysics(scene, meshes);
      physicsMs = msSince(tPhys);
      if (!cli.quiet) {
        printf("physics(GPU): %d bodies, %d statics, %d steps, %s at %.2f s sim time (%.0f ms wall)\n",
               phys.bodies, phys.statics, phys.steps,
               freeze ? "captured" : (phys.settled ? "settled" : "TIMEOUT"),
               phys.simSeconds, physicsMs);
      }
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
    if (!scene.lights.empty()) lightBuf.upload(scene.lights);
    if (!scene.flames.empty()) flameBuf.upload(scene.flames);
    rayCounter.allocZero(sizeof(unsigned long long));

    LaunchParams params{};
    params.width = rs.width;
    params.height = rs.height;
    params.sppTotal = rs.spp;
    params.maxDepth = rs.maxDepth;
    params.clampVal = rs.clampVal;
    params.seed = rs.seed;
    params.countRays = cli.statsPath.empty() ? 0 : 1;
    params.accum = (float4*)film.accum();
    params.aovAlbedo = (float4*)film.aovAlbedo();
    params.aovNormal = (float4*)film.aovNormal();
    params.rayCounter = rayCounter.as<unsigned long long>();
    params.cam = makeCamera(scene.camera, rs.width, rs.height);
    params.bg = scene.bg;
    params.textures = texBuf.as<TextureDesc>();
    params.materials = matBuf.as<MaterialDesc>();
    params.lights = lightBuf.ptr ? lightBuf.as<LightDesc>() : nullptr;
    params.numLights = (int)scene.lights.size();
    params.flames = flameBuf.ptr ? flameBuf.as<FlameDesc>() : nullptr;
    params.numFlames = (int)scene.flames.size();
    params.handle = ias.handle;

    // ---- render loop ----
    if (!cli.quiet) {
      printf("sundog: %s %dx%d, %d spp, depth %d%s on %s\n", cli.scene.c_str(),
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
      if (!cli.quiet) {
        printf("\r%d/%d spp (%.1f%%)", done, rs.spp, 100.0 * done / rs.spp);
        fflush(stdout);
      }
    }
    double renderMs = msSince(tRender);
    if (!cli.quiet) printf("\n");

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
    film.writePng(outputBuf, cli.out, rs.exposure, rs.gamma);
    if (!cli.aovAlbedoPath.empty()) film.writeAovPng(film.aovAlbedo(), cli.aovAlbedoPath, false);
    if (!cli.aovNormalPath.empty()) film.writeAovPng(film.aovNormal(), cli.aovNormalPath, true);

    size_t freeAfter = 0;
    CUDA_CHECK(cudaMemGetInfo(&freeAfter, &totalMem));

    unsigned long long rays = 0;
    CUDA_CHECK(cudaMemcpy(&rays, (void*)rayCounter.ptr, sizeof(rays),
                          cudaMemcpyDeviceToHost));
    double totalMs = msSince(tTotal);
    if (!cli.quiet) {
      printf("Rendering elapsed time: %f seconds", renderMs / 1000.0);
      if (rays) printf(" (%.0f Mrays/s)", rays / renderMs / 1000.0);
      printf("\nwrote %s\n", cli.out.c_str());
    }

    if (!cli.statsPath.empty()) {
      RenderStats st;
      st.scene = cli.scene;
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
      st.peakVramMB = (freeBefore - freeAfter) >> 20;
      st.numObjects = (int)scene.objects.size();
      st.numLights = (int)scene.lights.size();
      st.numMeshes = (int)scene.meshes.size();
      st.meshTriangles = meshTriangles;
      st.device = device;
      writeStats(st, cli.statsPath);
    }
    return 0;
  } catch (const std::exception& e) {
    fprintf(stderr, "sundog: fatal: %s\n", e.what());
    return 1;
  }
}

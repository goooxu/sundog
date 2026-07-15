#include "pipeline.h"

#include <optix_stack_size.h>
#include <optix_stubs.h>

#include <cstring>

extern "C" const unsigned char g_sundog_module[];
extern "C" const unsigned long g_sundog_module_size;

namespace sd {

enum HitGroupKind {
  // XPR shadow variants carry the unified shadow anyhit: pass-through +
  // cutout masking plus transparent-shadow accumulation for glass/water.
  HG_Q_RAD_OPQ = 0, HG_Q_RAD_MSK, HG_Q_SHD_OPQ, HG_Q_SHD_XPR,
  HG_T_RAD_OPQ, HG_T_RAD_MSK, HG_T_SHD_OPQ, HG_T_SHD_XPR,
  HG_COUNT
};

// groups_ layout: [raygen, miss_radiance, miss_shadow, HG_COUNT hitgroups]
static constexpr int GROUP_RAYGEN = 0;
static constexpr int GROUP_MISS0 = 1;
static constexpr int GROUP_MISS1 = 2;
static constexpr int GROUP_HG0 = 3;

template <typename T>
struct SbtRecord {
  __attribute__((aligned(OPTIX_SBT_RECORD_ALIGNMENT)))
  char header[OPTIX_SBT_RECORD_HEADER_SIZE];
  T data;
};
struct Empty {};
using RaygenRecord = SbtRecord<Empty>;
using MissRecord = SbtRecord<Empty>;
using HitRecord = SbtRecord<HitRecordData>;

Pipeline::Pipeline(OptixDeviceContext ctx, bool debug) : ctx_(ctx) {
  char log[4096];
  size_t logSize = sizeof(log);

  OptixModuleCompileOptions mopts{};
  mopts.optLevel = debug ? OPTIX_COMPILE_OPTIMIZATION_LEVEL_0
                         : OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
  mopts.debugLevel = debug ? OPTIX_COMPILE_DEBUG_LEVEL_FULL
                           : OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;

  OptixPipelineCompileOptions popts{};
  popts.usesMotionBlur = 0;
  popts.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
  popts.numPayloadValues = 8;
  popts.numAttributeValues = 5;
  popts.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
  popts.pipelineLaunchParamsVariableName = "params";
  popts.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_CUSTOM |
                                 OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;

  OPTIX_CHECK_LOG(optixModuleCreate(ctx_, &mopts, &popts,
                                    (const char*)g_sundog_module,
                                    g_sundog_module_size, log, &logSize, &module_),
                  log, logSize);

  auto pgDesc = [&](OptixProgramGroupKind kind) {
    OptixProgramGroupDesc d{};
    d.kind = kind;
    return d;
  };

  std::vector<OptixProgramGroupDesc> descs;
  {
    OptixProgramGroupDesc d = pgDesc(OPTIX_PROGRAM_GROUP_KIND_RAYGEN);
    d.raygen.module = module_;
    d.raygen.entryFunctionName = "__raygen__render";
    descs.push_back(d);
  }
  {
    OptixProgramGroupDesc d = pgDesc(OPTIX_PROGRAM_GROUP_KIND_MISS);
    d.miss.module = module_;
    d.miss.entryFunctionName = "__miss__radiance";
    descs.push_back(d);
  }
  {
    OptixProgramGroupDesc d = pgDesc(OPTIX_PROGRAM_GROUP_KIND_MISS);
    d.miss.module = module_;
    d.miss.entryFunctionName = "__miss__shadow";
    descs.push_back(d);
  }
  // Hitgroup variants: {quadric, tri} x {radiance, shadow} x {opaque, masked}
  auto hg = [&](const char* is, const char* ch, const char* ah) {
    OptixProgramGroupDesc d = pgDesc(OPTIX_PROGRAM_GROUP_KIND_HITGROUP);
    if (is) { d.hitgroup.moduleIS = module_; d.hitgroup.entryFunctionNameIS = is; }
    if (ch) { d.hitgroup.moduleCH = module_; d.hitgroup.entryFunctionNameCH = ch; }
    if (ah) { d.hitgroup.moduleAH = module_; d.hitgroup.entryFunctionNameAH = ah; }
    descs.push_back(d);
  };
  hg("__intersection__quadric", "__closesthit__radiance", nullptr);
  hg("__intersection__quadric", "__closesthit__radiance", "__anyhit__mask");
  hg("__intersection__quadric", nullptr, nullptr);
  hg("__intersection__quadric", nullptr, "__anyhit__shadow");
  hg(nullptr, "__closesthit__radiance_tri", nullptr);
  hg(nullptr, "__closesthit__radiance_tri", "__anyhit__mask_tri");
  hg(nullptr, nullptr, nullptr);
  hg(nullptr, nullptr, "__anyhit__shadow_tri");

  groups_.resize(descs.size());
  OptixProgramGroupOptions pgOpts{};
  logSize = sizeof(log);
  OPTIX_CHECK_LOG(optixProgramGroupCreate(ctx_, descs.data(), (unsigned)descs.size(),
                                          &pgOpts, log, &logSize, groups_.data()),
                  log, logSize);

  OptixPipelineLinkOptions lopts{};
  lopts.maxTraceDepth = 1;
  logSize = sizeof(log);
  OPTIX_CHECK_LOG(optixPipelineCreate(ctx_, &popts, &lopts, groups_.data(),
                                      (unsigned)groups_.size(), log, &logSize,
                                      &pipeline_),
                  log, logSize);

  OptixStackSizes stackSizes{};
  for (OptixProgramGroup g : groups_) {
    OPTIX_CHECK(optixUtilAccumulateStackSizes(g, &stackSizes, pipeline_));
  }
  unsigned dssTrav = 0, dssState = 0, css = 0;
  OPTIX_CHECK(optixUtilComputeStackSizes(&stackSizes, 1, 0, 0, &dssTrav,
                                         &dssState, &css));
  OPTIX_CHECK(optixPipelineSetStackSize(pipeline_, dssTrav, dssState, css, 2));

  paramsBuf_.alloc(sizeof(LaunchParams));
}

void Pipeline::buildSbt(const Scene& scene, const std::vector<GpuMesh>& meshes) {
  RaygenRecord rg{};
  OPTIX_CHECK(optixSbtRecordPackHeader(groups_[GROUP_RAYGEN], &rg));
  raygenRec_.upload(std::vector<RaygenRecord>{rg});

  std::vector<MissRecord> miss(2);
  OPTIX_CHECK(optixSbtRecordPackHeader(groups_[GROUP_MISS0], &miss[0]));
  OPTIX_CHECK(optixSbtRecordPackHeader(groups_[GROUP_MISS1], &miss[1]));
  missRec_.upload(miss);

  std::vector<HitRecord> hits(scene.objects.size() * 2);
  for (size_t i = 0; i < scene.objects.size(); i++) {
    const SceneObject& o = scene.objects[i];
    bool tri = o.geomKind == GK_MESH;
    bool masked = o.matFront == MAT_NONE || o.matBack == MAT_NONE || o.cutoutTexId >= 0;
    // Shadow rays additionally need anyhit on glass/water (transmittance
    // accumulation); radiance rays keep the opaque fast path for those.
    bool shadowAH = masked || objectTransmissive(scene, o);

    HitRecordData data{};
    data.geomKind = o.geomKind;
    data.matFront = o.matFront;
    data.matBack = o.matBack;
    data.lightId = o.lightId;
    data.cutoutTexId = o.cutoutTexId;
    if (tri) {
      const GpuMesh& m = meshes[o.meshId];
      data.positions = m.positions.as<float3>();
      data.indices = m.indices.as<uint3>();
      data.normals = m.normals.ptr ? m.normals.as<float3>() : nullptr;
      data.uvs = m.uvs.ptr ? m.uvs.as<float2>() : nullptr;
    }

    int rad = tri ? (masked ? HG_T_RAD_MSK : HG_T_RAD_OPQ)
                  : (masked ? HG_Q_RAD_MSK : HG_Q_RAD_OPQ);
    int shd = tri ? (shadowAH ? HG_T_SHD_XPR : HG_T_SHD_OPQ)
                  : (shadowAH ? HG_Q_SHD_XPR : HG_Q_SHD_OPQ);
    hits[2 * i].data = data;
    hits[2 * i + 1].data = data;
    OPTIX_CHECK(optixSbtRecordPackHeader(groups_[GROUP_HG0 + rad], &hits[2 * i]));
    OPTIX_CHECK(optixSbtRecordPackHeader(groups_[GROUP_HG0 + shd], &hits[2 * i + 1]));
  }
  if (hits.empty()) {
    // OptiX requires a non-null hitgroup SBT even for empty scenes.
    HitRecord dummy{};
    OPTIX_CHECK(optixSbtRecordPackHeader(groups_[GROUP_HG0 + HG_Q_RAD_OPQ], &dummy));
    hits.push_back(dummy);
  }
  hitRec_.upload(hits);

  sbt_ = OptixShaderBindingTable{};
  sbt_.raygenRecord = raygenRec_.ptr;
  sbt_.missRecordBase = missRec_.ptr;
  sbt_.missRecordStrideInBytes = sizeof(MissRecord);
  sbt_.missRecordCount = 2;
  sbt_.hitgroupRecordBase = hitRec_.ptr;
  sbt_.hitgroupRecordStrideInBytes = sizeof(HitRecord);
  sbt_.hitgroupRecordCount = (unsigned)hits.size();
}

void Pipeline::launch(const LaunchParams& params, int width, int height) {
  CUDA_CHECK(cudaMemcpy((void*)paramsBuf_.ptr, &params, sizeof(LaunchParams),
                        cudaMemcpyHostToDevice));
  OPTIX_CHECK(optixLaunch(pipeline_, 0, paramsBuf_.ptr, sizeof(LaunchParams),
                          &sbt_, width, height, 1));
}

Pipeline::~Pipeline() {
  if (pipeline_) optixPipelineDestroy(pipeline_);
  for (OptixProgramGroup g : groups_) optixProgramGroupDestroy(g);
  if (module_) optixModuleDestroy(module_);
}

}  // namespace sd

# sundog 架构

自底向上：一个 OptiX module、一条 pipeline、一次 launch 渲染一段 spp。
本文描述实际实现（`src/` + `device/`），与代码不符时以代码为准。

## 总体数据流

```
scene JSON ──> host 解析 ──> (可选) PhysX GPU 刚体沉降，烘焙 xform
                                │
                 上传 ──> GAS(5 种 quadric + 每网格一个) ──> IAS
                                                │
CLI ──> LaunchParams ──> optixLaunch(分块 spp) ─┴─> accum/AOV float4 缓冲
                                                      │
                                       (可选) OptiX denoiser ──> PNG + stats
```

## Megakernel raygen：路径循环 + trace depth 1

整个积分器在 `__raygen__render` 里迭代完成（`device/programs.cu`）。
`optixPipelineLinkOptions::maxTraceDepth = 1`：只有 raygen 调 `optixTrace`，
closesthit / anyhit / miss 一律不递归。closesthit 只负责把命中信息打包进
payload 寄存器；BSDF 求值、NEE、MIS、俄罗斯轮盘（深度 ≥4 起）
都在 raygen 的 for 循环里。好处：栈开销最小（`optixUtilComputeStackSizes`
按 depth 1 计算），控制流集中，寄存器压力可控。

spp 按 `--chunk`（默认 16）分多次 `optixLaunch`，累积用 running mean
（`accum += (L - accum)/(s+1)`），因此任意分块方式结果一致，长渲染可中途看进度。

## 两种 ray type

`RAY_RADIANCE = 0`、`RAY_SHADOW = 1`。SBT 每实例 2 条 hitgroup 记录（stride 2）。
阴影光线用 `TERMINATE_ON_FIRST_HIT | DISABLE_CLOSESTHIT`，靠
`__miss__shadow` 置 payload=1 表示可见——即"命中任何不可忽略的表面就挡住"。
穿透面与 cutout 的 anyhit 对两种 ray type 同样生效，所以阴影也正确穿透。

## 8 寄存器 payload

radiance 光线固定 8 个 32 位 payload 寄存器（`numPayloadValues = 8`）：

| 寄存器 | 内容 |
|---|---|
| p0 | 0 = miss；否则 bit0 = hit，bit1 = frontface |
| p1 | 命中距离 t（float bits） |
| p2–p4 | miss：背景色；hit：世界空间 shading normal（已翻到入射侧） |
| p5–p6 | u, v |
| p7 | matId（低 16 位）\| (lightId + 1) << 16 |

背景色复用法线寄存器：miss 时 raygen 直接拿 p2–p4 当辐射亮度，省一次分支后的
二次求值。matId/lightId 打包让 raygen 无需读 SBT。

## 几何：5 种 quadric 自定义求交 + 硬件三角形

`GeomKind`：sphere / rect / disk / cylinder / parabola / mesh。前五种是
**canonical 单位形状**（定义见 `docs/SCENES.md`），`__intersection__quadric`
在物体空间解析求交（`device/intersect.cuh`），经 5 个 attribute
寄存器交出法线(3) + uv(2)。每种 quadric 只建 **一个** 单 AABB 的 GAS，全场景
共享；网格每个 OBJ 一个三角形 GAS（RT Core 硬件求交），法线可选平滑，
`__closesthit__radiance_tri` 用重心插值并把 shading normal 压回几何面一侧。
GAS 全部 `PREFER_FAST_TRACE + ALLOW_COMPACTION` 并做 compaction。

## 双面 / 穿透 / 镂空：anyhit 设计

双面语义：每个面有独立的正/背材质，`null` 面是**穿透**（光线与阴影都直接穿过）。
实现上 hitgroup 分 8 个变体：{quadric, tri} x {radiance, shadow} x {opaque, masked}。

- 无穿透面且无 cutout 的对象走 **opaque** 变体，实例加
  `OPTIX_INSTANCE_FLAG_DISABLE_ANYHIT`，零 anyhit 开销；
- 否则走 **masked** 变体：`__anyhit__mask(_tri)` 判断命中侧的 matId，
  `MAT_NONE` 即 `optixIgnoreIntersection()`；有 cutout 纹理时采样 alpha，
  < 0.5 同样忽略。GAS 建时给 `REQUIRE_SINGLE_ANYHIT_CALL` 保证 anyhit
  恰好调用一次（cutout 采样有副作用语义时安全）。

closesthit 按 frontface 选 matFront/matBack，法线翻向入射侧后交给 raygen。

## IAS：单层实例化 + 非均匀缩放

`ALLOW_SINGLE_LEVEL_INSTANCING`，每个场景对象一个 `OptixInstance`，
3x4 行主变换直接来自场景 JSON 的 transform 链（scale → rotate → translate
自上而下复合）。非均匀缩放没问题：法线用
`optixTransformNormalFromObjectToWorldSpace`（逆转置）变换后归一化。
实例化的规模能力见 `scenes/05-spot-swarm.json`：32768 个 Spot 实例共享
同一份 5,856 三角形 GAS（≈1.9 亿等效三角形）。

## PhysX GPU 刚体装载

带顶层 `physics` 块的场景（格式见 `docs/SCENES.md`）在网格加载之后、GAS 构建
之前跑一遍 PhysX 5 刚体模拟（`src/physics.cpp`，工程里唯一接触 PhysX 头的
TU——`scene_json.cpp` 只解析纯结构体描述，host 单测因此无需 PhysX）。模拟
**必须在 GPU 上执行**（`eENABLE_GPU_DYNAMICS` + `PxBroadPhaseType::eGPU`，
CUDA context manager 建不起来直接报错，不回退 CPU）；渲染与物理共用同一块
GPU，先模拟后渲染串行执行。碰撞体映射：rect → 沿背面挤出的实体薄盒（仅静态）、
sphere → 精确球、mesh → 顶点上限 64 的凸包（每网格烹制一次，实例共享 +
`PxMeshScale`）。全部刚体休眠（或超时告警）后，`getGlobalPose()` 与原缩放
重组为 T·R·S 烘焙回对象 xform，下游对物理一无所知。`stop_time` /
`--physics-time` 切换到锐利定格模式：模拟恰好推进到指定时刻即烘焙，
凌空姿态原样入画（画廊 06 主图 = 同一场景 t=1.0 s 的定格）。GPU dynamics 不支持
sweep CCD，改用每刚体 speculative CCD + 厚碰撞板防穿透。固定步长/迭代数/
actor 顺序下同机跑间可复现；耗时计入 stats 的 `timings_ms.physics`。
示例：`scenes/06-spot-cascade.json`（512 只 Spot 倾泻沉降）。

## 体积火焰（发射型参与介质）

带顶层 `flames` 的场景启用 sundog 唯一的体积路径（device/volume.cuh）：
raygen 每次 `optixTrace` 返回后、处理命中/背景之前，对每团火焰与其竖直
包围圆柱**解析求交**（火焰不进 GAS/SBT），在 [ε, hit.t] 段内 32 步抖动
起点行进：`L += β·T·ε(p)·dt`，`T *= exp(-σ(p)·dt)`，最后 `β *= T`——
其后的表面/背景贡献自然被介质衰减。发射与密度场是位置的纯函数
（PCG 派生 hash 的三线性值噪声 fbm 塑形泪滴轮廓），决定性与渲染同口径。
照明用内嵌点光近似（见 docs/SCENES.md 的 Flames 节，含能量重复的如实
声明）；阴影线不穿越体积衰减（光学薄近似）。

## 水面材质与介质吸收状态

`water`（device/bsdf.cuh 与 dielectric 共享光滑界面采样）在 raygen 里多两笔账：
命中水面时先用 fbm 高度场的中心差分梯度扰动着色法线（`waterNormal()`
（device/noise.cuh），噪声栈与体积火焰共用；掠射角下扰动法线背对视线时回退
平面法线防黑边）；透射穿过界面时切换路径的**介质状态**——`mediumAbsorb`
是路径循环里的一个局部量，进水置为材质的 `absorb`、出水清零（TIR 是反射，
不切换），此后每段光线按 Beer–Lambert `β *= exp(−σ·t)` 衰减，介质内 miss
按大程长衰减到 ≈0。嵌套介质不支持；水下 NEE 被界面遮挡的折衷见
docs/SCENES.md。

## NEE + MIS（balance heuristic）

每个着色点均匀选一盏灯（`lights` 数组 = 显式 point/distant + 自动收集的
emissive rect/disk/sphere 区域光），采样后发阴影光线：

- 面积光按立体角换算 pdf，与 BSDF pdf 做 balance heuristic：
  `w = pdfL / (pdfL + pdfB)`；
- BSDF 采样命中发光体时用对偶权重 `w = pdfB / (pdfB + pdfL)`（`prevPdf`
  经 payload 的 lightId 找回对应灯）；
- delta 灯（point/distant）与 delta BSDF（镜面/玻璃）不做 MIS——前者只走
  NEE，后者只走 BSDF 路径（`specularBounce` 时发光体全额计入）。

`--clamp` 只作用于间接（depth ≥ 1）贡献，控 firefly。
点光源单位约定：`Li = intensity / d²`（intensity 是辐射强度）。
dielectric 按命中面选 η，Schlick 近似用低折射率侧的余弦（含全内反射 TIR）。

## PCG32 决定性

`device/rng.cuh` 的 PCG32。每个
(pixel, sample) 由 `(pixel << 32) ^ sample` 加全局 seed 独立初始化——流之间
无相关性，且与 chunk 划分、launch 顺序无关：固定 `--seed` 在同一
GPU/驱动上逐位可复现（`scripts/run-golden.sh` 的 sha256 决定性子测试依赖此）。
前 `⌊√spp⌋²` 个样本在像素内做分层抖动，其余纯随机。

## 降噪管线

raygen 顺带累积两张 guide AOV：first-hit albedo 与 camera 空间法线
（miss 时 albedo 记背景色）。`--denoise` 时用 OptiX HDR denoiser
（`OPTIX_DENOISER_MODEL_KIND_HDR` + albedo/normal guide），先
`optixDenoiserComputeIntensity` 再 invoke，输出替换 beauty 后再做
exposure/gamma 写 PNG。AOV 可用 `--aov-albedo/--aov-normal` 单独导出。
效果量化见 `docs/BENCHMARKS.md` 降噪层（16 spp + 降噪 vs 4096 spp 参考的 PSNR）。

## PTX vs OptiX-IR：已知问题

`nvcc 13.0` 的 `--optix-ir` 输出会被 R610 驱动（610.47.04）的 OptiX loader
拒收——`optixModuleCreate` 返回空日志的 `COMPILE ERROR`。因此 Makefile 默认
`IR=0`：设备代码编成 PTX（`-arch=compute_120`）嵌入二进制，运行时 JIT。
对比过 JIT 后的最终 SASS 与 OptiX-IR 路径应有的一致，性能无差别；代价只是
首次 module 创建慢一点。驱动修复后置 `IR=1` 即可切回。

## 统计

`--stats` 时 raygen 里每线程计数 trace 调用（radiance + shadow），launch 末
`atomicAdd` 汇总；host 侧记录 scene_load / gas_build / render / denoise / total
分段计时与 `cudaMemGetInfo` 差值（显存峰值估计），写成 JSON
（schema 见 `src/stats.cpp`）。不开 `--stats` 时计数关闭，零开销。

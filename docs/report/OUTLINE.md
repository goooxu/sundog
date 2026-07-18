# 技术报告章节规范（冻结版——写作/产图/审查的唯一依据）

每章 2000-4000 字。图文件名在此冻结，写作与产图两侧按名对接。
写作前必读 STYLE.md。"对账"列出该章公式必须与之逐一核对的代码。

---

## index.md（集成阶段编写）

阅读指南（四条路径：全读 / 只看原理 01-05+特性选读 / 只看工程 01→08-18 / 只看陷阱附录）、章节导航表、
致谢与延伸阅读（PBRT、Ray Tracing in One Weekend、OptiX Programming Guide）。

---

## 01-images-and-rays.md 成像与光线

**回答**：数字图像是什么？渲染在计算什么？为什么沿"反方向"追光线？

小节：
1. 像素、RGB 与线性空间——显示器 gamma、为什么渲染在线性空间累积、编码留到出口（输入 sRGB 解码 / 输出 PQ）
2. HDR 输出：把无界的辐亮度原样交给显示器——SDR 色调映射的结构性代价（v0.18 起退役）；PQ（SMPTE ST 2084）公式与锚点走读（100nits→0.508、203→0.581、10000→1.0）；曝光→709→2020→203nit 锚→PQ→12bit 的管线顺序；无损 AVIF 与决定性（对账 `pqOetf()`（src/transfer.h）与 `Film::writeAvif()`（src/film.cpp））
3. 针孔相机模型——成像平面、视场角 vfov、从像素 (i,j) 构造光线 o+t·d（对账 `makeCamera()`（src/scene_build.cpp）：lowerLeft/horizontal/vertical 的几何含义）
4. 薄透镜与景深——光圈半径、对焦距离（对账 raygen 中 lensRadius 采样）
5. 光的可逆性：从眼睛出发反向追踪的合理性（Helmholtz 互易性直觉版）；一张图给出"渲染一帧 = 对每个像素解一个积分"的全书路线图
6. sundog 的一帧流程速览（伪代码 10 行：循环 spp→构造光线→追踪→累积→PQ 编码）

图：
- `figures/ch01-pinhole-camera.svg`：针孔相机+成像平面+像素网格+一根光线
- `figures/ch01-thin-lens.svg`：薄透镜、光圈、焦平面、弥散圈
- `figures/ch01-spp-convergence.avif`：同场景 1/4/16/64/256 spp 五联横条
- `figures/ch01-pq-curve.avif`：数据图——PQ 转移函数与 SDR 区间带（gen-report-charts.py）

---

## 02-rendering-equation.md 光的度量与渲染方程

**回答**：如何用数学精确描述"光有多亮"和"表面如何反光"？

小节：
1. 立体角——定义 dω = dA/r²，半球 2π，微分立体角 sinθ dθ dφ
2. 辐亮度 L——单位面积、单位立体角的功率；为什么选它：沿光线不变、相机像素测的就是它
3. BRDF——定义 f_r = dL_o/dE_i，物理约束（非负、互易、能量守恒 ∫f_r cosθ dω ≤ 1）；漫反射 f_r = albedo/π 的 π 从哪来（推导）
4. 渲染方程——L_o = L_e + ∫ f_r L_i cosθ dω 逐项含义；cosθ 的投影意义；它为什么难解（递归、无穷维）
5. sundog 中的对应：材质=f_r（第 5 章）、灯=L_e（发光体+delta 灯）、求积分=MC（第 3 章）

图：
- `figures/ch02-solid-angle.svg`：球面上的面积块与立体角
- `figures/ch02-radiance.svg`：面元 dA、方向 ω、立体角锥——辐亮度的三要素
- `figures/ch02-rendering-equation.svg`：表面点半球上多方向入射→单方向出射，逐项标注 L_e/f_r/L_i/cosθ

---

## 03-monte-carlo.md 蒙特卡洛积分

**回答**：怎么用随机采样算积分？为什么图像有噪点、加 spp 就变干净？

小节：
1. MC 估计量 F̂ = (1/N)Σ f(X_k)/p(X_k)——无偏性证明（两行期望运算）
2. 方差与收敛速率——Var ∝ 1/N，误差 O(1/√N)；"spp 翻 4 倍噪声减半"；与图 ch03-mc-convergence 对照（PSNR 每 4×spp 约 +6dB）
3. 重要性采样——p 越像 f 方差越小；极端例：p ∝ f 时零方差
4. pdf 的换元——立体角 pdf ↔ 面积 pdf：p_ω = p_A · r²/cosθ'（第 4 章 NEE 直接用，对账 `sampleLight()`（device/light_sample.cuh）rect 分支 `pdf = d2/(cosL*area)`）
5. 半球上的常用采样：均匀球面、cosine 加权（给出 (u1,u2)→方向的构造与 pdf=cosθ/π 验证，对账 `cosineHemisphere()`（device/rng.cuh））

图：
- `figures/ch03-mc-convergence.avif`：数据图——PSNR(dB) vs spp（对数轴），标注 +6dB/4× 参考斜率
- `figures/ch03-importance-sampling.svg`：同一被积函数下均匀采样 vs 重要性采样的样本分布
- 复用 ch01-spp-convergence 交叉引用

---

## 04-path-tracing.md 路径追踪算法

**回答**：把 MC 用到渲染方程上，完整算法长什么样？sundog 的 raygen 循环逐行在干什么？

小节：
1. 递归展开：L = L_e + ∫f L → 路径积分观点；吞吐量 β 的迭代维护 β ← β·f·cosθ/p（对账 raygen 主循环 `beta *= bs.weight`（device/programs.cu））
2. 为什么纯 BSDF 采样打不中小灯——NEE：单独采样光源、按立体角 pdf 加权、shadow ray 验可见性（对账 NEE 段）；delta 灯（点光/平行光）为什么只能 NEE；子节·三角网格光——发光网格缺席=萤火虫案例回收；两级采样（世界空间面积前缀 CDF 二分 + 重心 √ 变换，double 累加防尾部失真）；p_ω = d²/(cosθ_g·A_total) 换元、cosθ_g 恒用几何法线；两侧同口径的换法线技巧（发光命中终止路径 → closest-hit 对已登记发光网格打包几何法线；按命中面材质门控；法线 AOV 代价如实入账；mNgSign 镜像翻正与 detSign 同源）；纹理 Li/单双面老规矩新对象（同 buffer 同权重逐位一致；球灯纹理限制不适用于网格）（对账 `sampleLight()` LT_MESH / `lightPdfSolidAngle()`（device/light_sample.cuh）、`triShadePoint()`（device/programs.cu）、CDF 构建段（src/capi_render.cpp）、`addObjectDerived()`（src/scene_build.cpp））
3. 双重计数问题与 MIS——同一贡献两条获取路径；balance heuristic w = p_a/(p_a+p_b) 的直觉与推导（证明加权后无偏）；对账：NEE 侧 `w = pdfLe/(pdfLe+pdfB)`、发光体命中侧 `w = prevPdf/(prevPdf+pdfL)`，两式互补
4. 俄罗斯轮盘——以概率 q 存活、除以 q 补偿，期望不变的两行证明；sundog depth≥4 启动、q=clamp(maxComp(β))（对账 RR 段）
5. firefly 与 clamp——为什么间接光会出孤立亮点；clamp 引入可控偏差换方差（对账 `clampVal` 仅 depth≥1）
6. 完整伪代码（与 programs.cu 一一对应，≤30 行）

图：
- `figures/ch04-path-trace.svg`：相机→漫反射点(带 NEE shadow ray)→玻璃→灯 的完整路径
- `figures/ch04-mis-weights.svg`：小亮灯场景中 BSDF 采样 pdf 与光源采样 pdf 的互补覆盖
- `figures/ch04-nee.avif`：cornell 类场景 64spp，NEE 开 vs 关（发光体 nee:false）双联
- `figures/ch04-clamp.avif`：低 spp 下 --clamp 0 vs 默认 双联（fireflies 可见）

---

## 05-materials.md 材质与 BSDF

**回答**：漫反射/金属/玻璃在数学上是什么？粗糙度如何进入方程？

小节：
1. Lambertian——f_r=albedo/π；cosine 采样推导（含 (u1,u2) 映射与 pdf 验证）；f·cos/p = albedo 的"权重恰好是反照率"（对账 `bsdfSample()` MT_LAMBERT 分支）
2. 微表面理论——宏观粗糙=微镜面统计；D（GGX：α²/(π((n·h)²(α²-1)+1)²)）、G/G1（Smith，含 Λ 式）、完整 f_r = DGF/(4cosθ_i cosθ_o)（对账 `ggxD/ggxLambda/ggxG`）
3. VNDF 采样——为什么按可见法线采样、pdf = G1·D/(4cosθ_o)、权重化简为 F·G/G1（对账 `ggxSampleVndf/ggxPdf`，含 roughness<1e-3 退化 delta 镜面）
4. 菲涅尔——电介质精确式与 Schlick 近似 F0+(1-F0)(1-cosθ)⁵；金属的"F0 即颜色"；曲线图对照
5. 折射——Snell 定律、η 按面且按介质栈选取（etaExt，第 15 章）、TIR 条件、出射侧余弦（对账 `refract()`（device/math.cuh）与 MT_DIELECTRIC 分支，特别是"低折射率侧余弦"的选择——错用密介质侧余弦正是附录陷阱清单的第一条）
6. 双面材质与穿透面语义（front/back、MAT_NONE）

图：
- `figures/ch05-microfacet.svg`：宏观面上的微镜面、半程向量 h、遮蔽/阴影
- `figures/ch05-snell.svg`：折射几何+临界角+TIR
- `figures/ch05-fresnel-curves.avif`：数据图——η=1.5 精确 Fresnel vs Schlick，0-90°
- `figures/ch05-roughness-ladder.avif`：5 个金属球 roughness 0/0.1/0.25/0.45/0.7

---

## 06-geometry.md 几何求交

**回答**：光线怎么"打中"一个球/圆柱/抛物面/三角形？法线和 UV 从哪来？

小节：
1. 射线参数化 r(t)=o+t·d 与"最近正根"原则、tmin/tmax 区间
2. 隐式面求交范式——代入→关于 t 的方程；球（二次式推导全过程）；平面/矩形/圆盘（一次式+范围裁剪）；圆柱（xz 平面二次式+高度裁剪）；抛物面 y=(x²+z²)/2（含"竖直光线退化为一次式"的处理）（对账 intersect.cuh 各函数，含"两根都上报"对穿透面的意义）
3. 法线（梯度 ∇F）与 UV 参数化（球面经纬、圆柱/抛物面的 (θ, ·)）
4. 三角形求交与重心坐标——插值法线/UV；几何法线 vs 平滑法线（对账 `triShadePoint()`（device/programs.cu）；OBJ 按 (v,vt) 重索引一句带过指向 mesh_obj.cpp）
5. 抛物面的聚焦性质——焦点 (0,1/2,0) 推导，正是 04 场景光束的原理

图：
- `figures/ch06-ray-quadric.svg`：射线穿球的两根 t、判别式几何含义
- `figures/ch06-barycentric.svg`：三角形重心坐标与属性插值
- `figures/ch06-parabola-focus.svg`：抛物面反射平行光过焦点（与 04 场景互证）
- `figures/ch06-primitives.avif`：features.py 渲染（5 种原语同框）

---

## 07-transforms.md 变换与实例化

**回答**：为什么只需要"单位球"？一万个物体为什么不用一万份几何？

小节：
1. 仿射变换 3×4 矩阵、复合顺序（scale→rotate→translate 的列表语义，对账 `parseTransform()` 的 `M = e·M`）
2. 光线的逆变换——世界光线→物体空间求交→命中变回世界（OptiX 实例机制承担）
3. 法线为什么用逆转置——切向量保持相切的推导 (M⁻¹)ᵀ；非均匀缩放反例（对账 `optixTransformNormalFromObjectToWorldSpace` 的使用+renormalize）
4. 实例化——一份 GAS + N 个带变换的实例；32768 奶牛只有 5856 个三角形一份；面光源的面积/pdf 要用世界空间量（对账 scene_build 灯注册 `area = 4|u×v|`、行列式符号翻转法线）
5. 变换下的 pdf/面积修正（为什么非均匀缩放的球面光被解析期拒绝）

图：
- `figures/ch07-instancing.svg`：一份单位几何 GAS ← 三个实例矩阵 → 世界中三个不同摆放
- `figures/ch07-normal-transform.svg`：非均匀缩放下"直接变换法线"出错 vs 逆转置正确
- 复用 `../gallery/05-spot-swarm.avif` 交叉引用

---

## 08-acceleration.md 加速结构与 RT Core

**回答**：近 2 亿条光线 × 1.9 亿三角形为什么能在几十毫秒内算完？

小节：
1. 朴素求交 O(N)——一帧的乘法次数量级估算（具体数字算给读者看）
2. BVH——包围盒剪枝、树高 O(logN)；AABB 与光线的 slab 相交测试（数学，配 ch08-slab 图）
3. RT Core——硬件化的是什么（BVH 遍历 + 三角形/AABB 测试）、自定义图元走 IS 程序的分工
4. 两级结构 GAS/IAS——实例化如何天然融入；压缩（compaction）（对账 `buildAndCompact()`（src/accel.cpp））
5. 实测规模感受：05 场景 stats（GAS 构建耗时/显存/Mrays/s，引 BENCHMARKS.md）

图：
- `figures/ch08-bvh.svg`：场景包围盒层级 + 对应二叉树 + 一根光线的剪枝路径
- `figures/ch08-slab.svg`：AABB slab 法的三对平板区间求交
- `figures/ch08-two-level.svg`：IAS→GAS 两级引用关系（含 SBT offset 线索，呼应第 9 章）

---

## 09-optix-pipeline.md OptiX 工程实现

**回答**：一个 OptiX 应用从头到尾长什么样？一次 optixTrace 背后发生什么？渲染器代码怎么组织到 GPU 上？

小节：
1. 一个 OptiX 应用的完整生命周期——教科书七步（①CUDA 数据准备 ②GAS/IAS ③程序编译 ④Pipeline+SBT ⑤optixLaunch ⑥遍历/求交/着色 ⑦Denoiser）逐步走读 + 步骤→API→sundog 文件→深入章节的映射表；点明两处偏差（着色在 raygen；PTX 而非 OptiX-IR）；补齐步骤① CUDA 数据准备的综述（CudaBuffer/网格上传/纹理对象/描述符数组）
2. OptiX 程序模型——raygen/IS/AH/CH/miss 五种程序各管什么、一次 trace 的调用时序
3. sundog 的 megakernel 决策——路径循环放 raygen、trace depth=1、CH 只打包命中信息（8 个 payload 寄存器布局表，对账 programs.cu 顶部注释与 `packHit()`）
4. SBT——records、`sbtOffset = 2×instanceId`、radiance/shadow 两套 hitgroup、8 个 PG 变体矩阵（对账 `Pipeline::buildSbt()`（src/pipeline.cpp））
5. anyhit 的四件事——穿透面（MAT_NONE ignore）、alpha 镂空、阴影线复用、透明阴影透射率累积（第 15 章）；DISABLE_ANYHIT 快速路径的三态判据（对账 `maskAnyhit()`/`shadowAnyhit()` 与 accel.cpp 实例 flags）
6. 主机侧一帧的编排——上传/建 AS/launch 分块/回读（对账 src/capi_render.cpp 渲染循环）；PTX/OptiX-IR 的工程坑一段带过

图：
- `figures/ch09-app-flow.svg`：七步生命周期泳道图（主机一次性准备 → 渲染循环 ⑤↔⑥ → 降噪落盘，框内标源文件，硬件/软件分色）
- `figures/ch09-optix-pipeline.svg`：一次 optixTrace 的程序调用流程图（含 AH ignore 回路）
- `figures/ch09-sbt.svg`：SBT 内存布局：raygen|miss×2|每实例两条 hitgroup 记录
- `figures/ch09-aov.avif`：beauty / albedo AOV / normal AOV 三联

---

## 10-sampling-denoising.md 随机数、纹理与 AI 降噪

**回答**：GPU 上万条并行光线怎么"随机"得又快又可复现？低 spp 图像怎么变干净？

小节：
1. PCG32——为什么不用 curand；按 (pixel,sample) 播种的独立流设计→逐位决定性（同 seed 同驱动图像比特级一致，正是 golden 测试基础）（对账 `Pcg32::init()` 播种式）
2. 分层采样——把 [0,1)² 切格子降方差；spp 的 ⌊√N⌋² 分层（对账 raygen 抖动段）
3. 纹理——UV→纹素、双线性过滤、sRGB 硬件解码、alpha 镂空（对账 textures.cpp 与 `evalTexture()`）
4. AI 降噪——为什么 MC 噪声适合学习法去除；albedo/normal 引导层的作用（"告诉网络哪些边是真的"）；HDR 域降噪在输出编码前（对账 denoise.cpp 流程）；局限（偏差、非决定性输出不入 golden）

图：
- `figures/ch10-stratified.svg`：64 个纯随机点 vs 8×8 分层点的空隙/成团对比
- 复用 `../gallery/09-ember-shore-spp16-raw.avif` 与 `-denoised.avif` 双联交叉引用（体积火焰+水面的重噪声场景）
- `figures/ch09-aov.avif` 交叉引用（引导层）

---

## 11-validation.md 验证方法学与性能

**回答**：怎么证明渲染器"算得对"？性能怎么度量、怎么解读？

小节：
1. 正确性验证金字塔——场景解析单测（渲染数学为 GPU 专属，由第 2–7 章逐公式对账 + golden/决定性钉住）、golden 图 PSNR≥45、sha256 决定性、compute-sanitizer
2. 两层基准设计——特性层（九个画廊场景的渲染时间/吞吐/显存）与降噪层（低 spp + 降噪 vs 高 spp 参考的 PSNR）；计时口径（只计渲染循环，场景解析、加速结构构建与物理沉降不计入）
3. 结果解读——吞吐从哪来（并行度、RT core、内存层次），引用 BENCHMARKS.md 实测表格；Mrays/s 的含义与用它还原平均路径结构
4. 降噪的等效加速——16spp+降噪 ≈ 数千 spp 视觉质量（PSNR 表引 BENCHMARKS.md 降噪层）

图：
- 无专属图；表格引用 docs/BENCHMARKS.md 数字（不虚构）

---

## 12-volumes.md 体积渲染：程序化火焰

**回答**：光穿过"会发光的空气"时发生了什么？没有解析解的积分怎么算？一团火的形状从哪来？

小节：
1. 从表面到介质——"表面之间免费旅行"假设的失效；参与介质三种事件（吸收/发射/散射）；sundog 取发射+吸收舍散射（光学薄近似），边界如实（烟雾那样散射主导的介质不在此列；阴影线的体积透射见第 6 节）（对账 device/volume.cuh 头注释）
2. 辐射传输——dL/dt = −σL + ε 逐项推导；Beer–Lambert 透射率；完整解与路径循环的两笔账拼接（对账 raygen 的 marchFlames 调用段（device/programs.cu））
3. 光线行进——32 步固定求积、抖动起点化带状伪影为噪声（PCG 流→决定性）、包围圆柱解析剪枝"不穿火不付费"（对账 `clipFlameBounds()`）；07 实测引 BENCHMARKS、golden 全 inf 零扰动
4. 程序化火焰场——hash→值噪声→fbm 三层构造；泪滴轮廓极值/归一化推导；y 压缩噪声扰边成火舌；heat 的 1/2/3 次幂发射梯度（对账 `hashU()/vnoise()/fbm3()/flameField()`）
5. 照明与工程记账——体积发射无面积 pdf 不可 NEE；内嵌双点光近似（0.35H/0.70H、0.3R 软阴影、65/35 分摊）；能量轻微重复的如实定量讨论；light_intensity = 逃逸后发射的语义推论（铺垫第 6 节宿主豁免）；决定性口径不破
6. 阴影线的体积透射——12 号烟柱不投影的物理缺口；NEE 可见性因子 T = T_surf（第 15 章）× T_vol 的乘积推广；ε=0 的透射专用行进复用 clipFlameBounds、与 marchFlames 透射轨道逐位同离散化（test_volume.cpp 克隆流交叉钉死）；抖动的决定性两层账（裁剪早退后才抽 → 无火焰场景流照旧；遮挡早退内行进 → 条件抽取仍是纯函数）；宿主火焰豁免 = 拒绝双重记账（LightDesc.flameId 回链）；--opaque-shadows 同关两套透射保持旧口径对照（对账 `flameTransmittance()`（device/volume.cuh）、raygen NEE 分支（device/programs.cu）、`addFlameDerived()`（src/scene_build.cpp））

图：
- `figures/ch12-radiative-transfer.svg`：介质微元账目 + 透射率衰减曲线
- `figures/ch12-flame-field.svg`：泪滴轮廓/fbm 扰边/发射梯度三层解剖
- `figures/ch12-noise-anatomy.avif`：火焰特写三联（noise_scale 0/1.5/3）
- `figures/ch12-flame-shadow.avif`：12 号场景对比双联（--opaque-shadows vs 默认，烟柱投影）
- 复用 `../gallery/07-campfire.avif` 交叉引用（正文未内嵌，画廊链接）

---

## 13-water.md 水面：界面、波纹与水色

**回答**：渲染里的"水"为什么没有任何新数学——它如何由第 5/6/12 章的三件事组合而成？波光路径从哪来？

小节：
1. 水面的三层拆解——界面 = ior 1.33 电介质（第 5 章"站在湖边"直觉闭环；对账 bsdfSample() 的 MT_WATER/MT_DIELECTRIC 共享分支（device/bsdf.cuh））、波纹 = 法线扰动、水体 = 介质吸收；三层独立可开关（配 anatomy 三联）
2. 波纹：高度场与法线扰动——y=H(x,z) 梯度法线推导（回链第 6 章）、fbm 高度场中心差分（对账 waterNormal()（device/noise.cuh））、掠射角防黑边回退；波光路径成因（法线锥 → 沿视线拉长的镜面亮带，配示意图）
3. 水色：介质吸收——Beer–Lambert 回链第 12 章、σ_r>σ_g>σ_b 波长偏心（红光半衰深度 ln2/0.45≈1.5 单位算给读者）；raygen 介质状态记账（透射切换/TIR 不切换/介质内 miss 大程长衰减，对账 mediumAbsorb 段（device/programs.cu））
4. 工程记账与边界——初稿两条限制（嵌套介质、水下 NEE 死区）均被第 15 章解除，仅保留决定性声明；实测引 BENCHMARKS（08：0.037 s / 6159 Mrays/s，全画廊最快的原因）；golden 101 dB 事件 = 第 11 章 45 dB 阈值设计的现场兑现
5. 小结——三层组合的自然涌现；链式交棒第 14 章（环境光照）

图：
- `figures/ch13-water-layers.svg`：纵剖面三层解剖（Fresnel 分光/法线锥/深度色衰减）
- `figures/ch13-glitter.svg`：波光路径成因（法线抖动锥 → 亮带）
- `figures/ch13-anatomy.avif`：三联（wave_amp 0 / 默认 / absorb 0）
- 复用 `../gallery/08-lakeside.avif` 交叉引用（正文未内嵌，画廊链接）

---

## 14-envlight.md 环境光照：HDR 环境贴图与重要性采样

**回答**：一张天空照片怎么变成可采样的光源？小而亮的太阳为什么是均匀采样的灾难、重要性采样的主场？

小节：
1. 从"背景"到"光源"——solid/gradient 只是 miss 时的着色，NEE 采不到它；无穷远球面光源的形式化：L_env 只依赖方向、渲染方程的边界项（对账 evalBackground() 的 BG_ENVMAP 分支（device/programs.cu））
2. HDR 与 equirect 映射——太阳与天空差 4-5 个数量级、8-bit + sRGB 装不下；Radiance RGBE 共享指数格式一段（stbi_loadf 解码）；(u,v)↔(θ,φ) 双向映射、rotate = 方位角平移、两极拉伸畸变与面积元 sinθ dθdφ（对账 envDirToUv() / envEval()（device/env_light.cuh）与 EnvMap::upload()（src/env_light.cpp）的 float4 纹理路径）
3. 把亮度变成概率——目标 pdf ∝ 亮度×sinθ（畸变补偿的由来）；行边缘 CDF + 行内条件 CDF 的两级构造；逆变换采样两次二分（回链第 3 章）；立体角 pdf 换元 p(ω) = p(u,v)/(2π² sinθ) 完整推导与白图自检 1/4π（对账 CDF 构建循环（src/env_light.cpp）与 sampleEnv() / envPdfSolidAngle()（device/env_light.cuh））
4. 与 NEE/MIS 的接驳——环境光是一盏"方向域光源"：策略集 nStrat = numLights + hasEnv 均匀选一、选择概率折进两侧 pdf；BSDF 光线逃逸即"命中"环境光，miss 分支权重与第 4 章两式逐字互补；importance:false 的均匀球面对照（pdf=1/4π）；玻璃后的直射透光由第 15 章透明阴影修复、聚焦焦散仍靠 BSDF 路径（对账 raygen 的 nStrat/miss 权重/NEE 分支（device/programs.cu））
5. 工程记账与实测——CDF 构建耗时与显存增量（float4 4k 贴图 + 两级 CDF，stats 实测）；均匀 vs 重要性等 spp 对比的定量 PSNR；白炉测试（纯白环境+白球=常数）验证 MIS 无重复计数；决定性口径不破（无 env 场景 RNG 消耗逐位不变的设计约束）；限制如实声明（引 BENCHMARKS.md 10 行）

图：
- `figures/ch14-equirect-mapping.svg`：球面方向域↔矩形 (u,v)、两极拉伸、sinθ 因子
- `figures/ch14-env-cdf.svg`：亮度图→行边缘 CDF→条件 CDF→两次逆变换查找（太阳行的尖峰画出来）
- `figures/ch14-uniform-vs-importance.avif`：四联条带（均匀/重要性 × 16/256 spp）
- `figures/ch14-env-luminance.avif`：数据图——envmap 缩略图 + 逐行边缘亮度曲线（log），能量集中在极小立体角的直观证据
- 复用 `../gallery/10-suncatcher.avif` 交叉引用（正文未内嵌，画廊链接）

---

## 15-transparent-media.md 透明阴影与嵌套介质

**回答**：玻璃为什么曾投出实心黑影、水下为什么曾一片死寂？把"布尔遮挡"升级成"直线透射率"要付出什么、买回什么？水里的玻璃怎么算对？

小节：
1. 布尔阴影的两宗罪——玻璃黑影（回链附录陷阱 3 的旧折衷）与水下 NEE 死区（回链 13 章旧边界）；根因三件套：DISABLE_ANYHIT 实例旗标 + TERMINATE_ON_FIRST_HIT + 只放行穿透面的 anyhit
2. 直线透射近似——逐界面菲涅尔（eta/f0/余弦约定回链第 5 章；backface TIR 判全遮挡 = Snell 窗口自然涌现，半角 asin(1/1.33)≈48.8°）；符号距离法 Beer–Lambert：Σ(出射 σt) − Σ(入射 σt) 的序无关性证明与"起点在介质内"的自然覆盖（对账 shadowAnyhit()（device/programs.cu））
3. OptiX 落地——shadow payload 从 1 bit 到 5 寄存器（p0 可见性 + p1 菲涅尔连乘 + p2-4 逐通道符号光学深度）；统一 shadow anyhit 与实例三态分流（opaque/masked/transmissive）；REQUIRE_SINGLE_ANYHIT_CALL 从性能项升级为正确性前提（对账 traceShadow()（device/programs.cu）、buildSbt()（src/pipeline.cpp）、buildIas()（src/accel.cpp））
4. 嵌套介质栈与相对 IOR——单变量为什么算错水中玻璃；LIFO 栈的进出与溢出退化；η = n_外/n_内 的按栈选取、Schlick 余弦泛化为"低折射率侧"；玻璃球⊃水球⊃气泡的走读（对账 raygen 介质栈段与 bsdfSample() 的 etaExt（device/bsdf.cuh））
5. 偏差与记账——四项文档化近似（不折弯：焦散仍靠 BSDF；界面对真空；平面法线；嵌套区间双重衰减）；实测：05 号 4096 玻璃牛的 anyhit 代价（约 11% 吞吐）、smoke/02/04 三场景逐字节不变、tier B 降噪 PSNR 逐位不变；两套透射机制在阴影线上会师（anyhit 表面透射 × 火焰体积行进，回链 12 章 §12.6；--opaque-shadows 同关两者）；决定性（表面项零随机数消耗；体积项抽取仅发生在穿柱段）

图：
- `figures/ch15-shadow-transmit.svg`：阴影线穿介质的符号距离记账解剖（入/出成对、起点在水中的不成对情形）
- `figures/ch15-medium-stack.svg`：介质栈进出与相对 IOR（玻璃⊃水⊃气泡三层走读）
- `figures/ch15-shadow-compare.avif`：11-glasswork 透明阴影开/关双联（--opaque-shadows）
- `figures/ch15-snell-window.avif`：水下仰视临时场景——Snell 窗口内是天空、窗外是全内反射的水底
- 复用 `../gallery/11-glasswork.avif` 交叉引用（正文未内嵌，画廊链接）

---

## 16-rough-dielectric.md 粗糙电介质：微表面透射与磨砂玻璃

**回答**：磨砂玻璃为什么"透光不透形"？第 5 章的 GGX 微表面怎么从反射搬进透射？粗糙度归零时如何逐位退回旧玻璃？

小节：
1. 两个旧答案拼不出的新材质——金属有粗糙度但只反射（5.2-5.3）、玻璃能透射但只有光滑（5.4-5.5）；磨砂玻璃的直觉：微镜面各自光滑折射、宏观方向被法线抖动糊开（13 号场景对读：屏后火苗 vs 屏前光斑）
2. 透射半程向量——反射 h ∝ ω_o+ω_i 的推广 h ∝ η·ω_o+ω_i（Walter 2007，相对 η 形式；canonical 取 ω_o 侧）；逐微镜面 Snell 的方向意义；TIR 在微镜面级发生、确定性反射（对账透射半程向量构造与 `fresnelDielectric()`（device/bsdf.cuh））
3. Walter BTDF 与雅可比推导——f_t 全式逐因子来历，教程式推导 |dω_h/dω_i| = |ω_i·h|/(η(ω_o·h)+(ω_i·h))² 的透射雅可比（反射侧 1/(4|ω_o·h|) 的镜像）；η² 辐亮度因子为什么不可省略：NEE 消费 eval 做不成对的单界面连接，缺 η² 每界面偏 η²，沿完整 BSDF 路径进出才相消（对账 `bsdfEval()` 透射分支与 `ggxPdfTransmit()`（device/bsdf.cuh））
4. VNDF 采样、瓣选择与权重化简——ggxSampleVndf 采 h 照旧、菲涅尔 F(ω_o·h) 掷硬币选瓣（抽取序：VNDF 在前、瓣选在后——F 依赖所采微面）；两瓣 f/p 同时化简为 G/G₁（透射再乘 η²）；组合 pdf = F·pdf_refl / (1−F)·pdf_trans 两侧一致（对账 `bsdfSample()`/`bsdfPdf()`（device/bsdf.cuh））
5. delta 退化、决定性与一次数值现场——roughness<1e-3 走原 delta 分支、抽取契约逐字节（golden 七场景全 inf 红线；水面固定光滑极限）；ggxD 紧凑式 k=c²(α²-1)+1 在极小 α 下 float 舍入到 k=0、D 爆 inf 的现场复盘与稳定式 k=α²c²+(1-c²)（对账 `ggxD()`（device/bsdf.cuh））；delta 极限验证：roughness 2e-3 vs 0 各 4096 spp 收敛一致
6. NEE/MIS 接驳与阴影近似第五项——粗糙电介质不再 isDelta：透射 NEE（光在玻璃后的主动连线，13 号的方差控制）与 MIS 组合 pdf 记账（对账 raygen NEE 分支（device/programs.cu））；阴影线仍按光滑菲涅尔直线透射（第 15 章近似清单第五项）——13 号屏前光斑逐扇同锐 vs 屏后光晕逐扇变糊即其可视化；实测引 BENCHMARKS（13 号渲染吞吐与 11 号对照）

图：
- `figures/ch16-halfvec-refraction.svg`：反射/透射半程向量几何（微镜面上的 Snell、η 加权和、TIR 情形）
- `figures/ch16-frosted-ladder.avif`：磨砂玻璃阶梯（5 球 roughness 0/0.05/0.15/0.3/0.6，HDR 天空）
- 复用 `../gallery/13-frosted-veil.avif` 交叉引用（正文未内嵌，画廊链接）

---

## 17-plastic.md 塑料：涂层直觉与首个双瓣混合 BSDF

**回答**：有色底 + 白高光的日常材质为什么天生两层？两个同半球波瓣怎么共享一套 sample/eval/pdf 而互不欠账？第 16 章的 G/G₁ 权重化简为什么到这里恰好失效？

小节：
1. 清单上的空档——lambert 无高光、金属高光即本色、玻璃无底色；涂层物理直觉（电介质罩漫反射底，4% 白高光浮在颜色上面）；模型 = 第 16 章反射瓣 + 第 5 章朗伯相加；F₀ 恒从 m.ior（不透明、不入介质栈、正反同式、eta 参数有意忽略的文档化近似）；14 号场景对读（对账 `plasticTerms()`（device/bsdf.cuh））
2. 菲涅尔耦合四候选——不耦合/高光补/常数补/双向 (1-F(cos_o))(1-F(cos_i)) 的能量与互易对照表；(d) 的守恒机制（掠射处漫反射自动归零）；半球 Schlick 均值 F̄ = F₀+(1-F₀)/21 的积分推导与"白色比朗伯暗约 10%"的如实记账（对账 `plasticTerms()` 的 fDiff 与 `plastic()` docstring（scenes/scenelib.py））
3. 混合 pdf 记账——常数 0.5 瓣选硬币的三重理由（权重硬上界/bsdfPdf 无 albedo 签名/两侧不可能失配）；抽取序与 16 章刻意相反（硬币先、方向后，恒 3 抽分支无关的强决定性契约）；混合边际 pdf 的全概率直觉与三消费点一致（bs.pdf→发光命中 MIS、bsdfPdf→NEE pdfB、eval 两瓣和）；plasticTerms 单一助手的结构性封死（对账 `bsdfSample()`/`bsdfPdf()`/`bsdfEval()` 的 MT_PLASTIC 分支（device/bsdf.cuh））
4. 捷径失效与上界 2——16 章两瓣异半球→pdf 逐向退化单项→F 约掉→G/G₁；塑料同半球→pdf 恒两项→无消去→全式权重；mediant 上界证明 w ≤ 2；涂层 1e-3 钳制永不 delta 的决策（delta+漫反射混合需要半 delta 旗标穿透四处记账，不值得；bsdfIsDelta 恒 false、NEE 永远可连）
5. 数值审计与加法性——主机端 6 组 × 20 万样本 weight/pdf 三方零失配、权重峰值 1.84 < 2、全角度能量 ≤ 1（垂直入射 E≈0.908 对上理论 0.87a+0.04；高粗糙度掠射的 G/G₁ 单次散射损失照旧）；枚举尾部追加 + raygen 白名单零改动 → 既有 7 golden 逐字节不变的机器证明；14 号入列第 8 张 golden 三连跑

图：
- `figures/ch17-toy-ladder.avif`：玩具工厂涂层阶梯（14 号场景，粗糙度 0.03→0.6 的灯管反射条渐糊）
- `figures/ch17-coupling-energy.avif`：三种耦合的半球反照率随入射角曲线（唯 (d) 全程 ≤ 1）
- 复用 `../gallery/14-toy-factory.avif` 交叉引用（正文未内嵌，画廊链接）

---

## 18-physics.md 物理装载：PhysX GPU 刚体模拟

**回答**：512 只堆叠奶牛的位姿从哪来？刚体模拟怎么与渲染共用一块 GPU，又怎么保持逐位可复现？

小节：
1. 为什么渲染器要长出物理——自然堆叠"摆不出来"；场景只声明初始条件，模拟生成最终位姿；管线位置在第 9 章生命周期 ①② 之间（对账 `runPhysics()` 调用处（src/capi_render.cpp）与 scenes/06-spot-cascade.py 的 physics 声明）
2. 刚体动力学速成——状态 (p,q,v,ω)、四元数姿态、牛顿–欧拉方程、半隐式欧拉与固定步长 dt=1/240（对账 `updateMassAndInertia` 调用）
3. 碰撞：从形状到接触——凸包 ≤64 顶点实例共享（对账 `convexOf()` 与 PxMeshScale）、rect 薄盒挤出、宽相/窄相（呼应第 8 章）、迭代约束求解 8/2、restitution/friction、speculative CCD、休眠=沉降
4. GPU 上的物理与位姿烘焙——eENABLE_GPU_DYNAMICS+GPU 宽相、共卡串行、唯一 PhysX TU 模块边界；T·R·S 列范数分解与烘焙（对账 `decompose()`/`bakePose()`（src/physics.cpp）），实测 2100 步/8.75 s/约 2.5 s 墙钟（stats physics 分段）
5. 两种停机：沉降与定格——stop_time/--physics-time、画廊 06 主图 t=1.0 s、决定性四要素与双渲 sha256 实测；06 不进 golden

图：
- `figures/ch18-physics-pipeline.svg`：装载管线与 PhysX 模块边界
- `figures/ch18-trs-bake.svg`：T·R·S 分解/烘焙示意
- `figures/ch18-freeze-sequence.avif`：t=0.3/0.7/1.0/1.4 + 沉降态五联
- 复用 `../gallery/06-spot-cascade.avif` 交叉引用（正文未内嵌，画廊链接）

---

## appendix-pitfalls.md 附录：路径追踪常见实现陷阱

**回答**：写一个路径追踪器最容易在哪些地方悄悄算错？（案例式清单，每条=症状/数学分析/sundog 的做法）

条目：
1. 折射率之比用错方向——恒用 η=1/ior 时判别式恒正、玻璃永远不发生 TIR 的证明；Schlick 用密介质侧余弦会严重低估反射（临界角处 F 应连续趋于 1）；sundog 按面选 η、用低折射率侧余弦（第 5 章）
2. 采样分布与权重不匹配——"n + 球内均匀点"≠ cosine 分布，却按完美重要性采样加权→系统性偏差；sundog 用 Malley 方法精确余弦采样
3. 阴影线把透射材质当不透明——玻璃背后影子全黑；朴素实现的常见近似，sundog 以直线透射率修复直接光一半（第 15 章），聚焦焦散仍靠 BSDF 路径
4. 低质量 BVH 切分——随机选轴的中位数切分：兄弟盒重叠大、遍历质量差且不可复现
5. 求交时现算变换三角函数——逐轴旋转每条光线现算 sin/cos，本可在加载时预乘成 3×4 仿射一次到位；sundog 的 Affine 复合 + OptiX 实例变换
6. 终止与随机数的三个坑——无 RR 的深递归纯浪费且硬截断有偏；全屏共享分层抖动使像素间噪声相关；random_device 播种毁掉"同输入同输出"的回归能力

图：
- `figures/appendix-lambert-bias.svg`：n+单位球内点 vs 余弦采样的方向分布对比

---

# 产图任务清单（图文件名冻结）

## 渲染图（scripts/render-report-figures.sh 产出，测试机执行）

| 文件 | 内容 | 生成要点 |
|---|---|---|
| ch01-spp-convergence.avif | 五联横条 1/4/16/64/256 spp | 02-cornell-lume --size 480x270，PIL 横拼+每格左上角标 spp |
| ch04-nee.avif | NEE 开关双联 | 02-cornell-lume 64spp 原样 vs sed 生成 nee:false 变体场景 |
| ch04-clamp.avif | firefly 对比双联 | 04-parabolica --size 640x360 --spp 24，--clamp 0 vs 默认 |
| ch05-roughness-ladder.avif | 金属球粗糙度阶梯 | 新建临时小场景（5 球 roughness 0/0.1/0.25/0.45/0.7，一个面光）512spp |
| ch06-primitives.avif | 5 原语同框 | features.py 256spp |
| ch09-aov.avif | beauty/albedo/normal 三联 | 03-spot-atrium --spp 64 --aov-albedo --aov-normal，PIL 三拼 |
| ch18-freeze-sequence.avif | 倾泻时序五联（4 个定格 + 沉降态） | 06-spot-cascade --size 480x270 --spp 24，--physics-time 0.3/0.7/1.0/1.4 与无覆盖，PIL 横拼 |
| ch12-noise-anatomy.avif | 火焰特写三联（noise_scale 0/1.5/3） | 内联 python 生成火焰特写 temp 场景（480x640 / 48 spp），PIL 横拼 |
| ch12-flame-shadow.avif | 烟柱投影对比双联 | 12-molten-oracle 960x540/96spp，--opaque-shadows vs 默认，PIL 横拼 |
| ch13-anatomy.avif | 水面三联（wave_amp 0 / 默认 / absorb 0） | 内联 python 生成水面特写 temp 场景（640x360 / 64 spp），PIL 横拼 |
| ch14-uniform-vs-importance.avif | 均匀 vs 重要性四联（16/256 spp × 2） | 10-suncatcher --size 480x270，内联 python 生成 importance:false 变体场景，PIL 横拼 |
| ch15-shadow-compare.avif | 透明阴影开/关双联 | 11-glasswork 960x540/96spp，--opaque-shadows vs 默认，PIL 横拼 |
| ch16-frosted-ladder.avif | 磨砂玻璃阶梯（5 球 roughness 0→0.6） | 新建 figures/src/frosted-ladder.py（仿 roughness-ladder，HDR 天空 + 棋盘地面），256spp，compose ladder |
| ch17-toy-ladder.avif | 玩具工厂涂层阶梯 | 14-toy-factory --spp 384（场景原生 1920x1080），compose ladder 单图配注 |
| ch15-snell-window.avif | 水下仰视 Snell 窗口 | 内联 python 生成水下相机临时场景（960x540/128spp） |

全部经 PIL 无损压缩入 docs/report/figures/；标注文字用 PIL 默认字体白底黑字角标即可。

## 数据图（matplotlib，测试机采数据）

| 文件 | 内容 | 数据来源 |
|---|---|---|
| ch03-mc-convergence.avif | PSNR vs spp（1,2,4,...,1024），log-x | 02-cornell-lume 各 spp vs 4096spp 参考，img_compare 采 |
| ch05-fresnel-curves.avif | η=1.5 精确 Fresnel vs Schlick | 纯计算 |
| ch14-env-luminance.avif | envmap 亮度分布解剖（缩略图 + 逐行边缘亮度 log 曲线） | gen-report-charts.py 内置 RGBE 解码读 assets/*.hdr |
| ch17-coupling-energy.avif | 塑料三种耦合的半球反照率 E(θ) 曲线 | 纯计算：gen-report-charts.py 复算 GGX+Schlick 半球积分（albedo=1，roughness 0.15） |

风格：白底、单一强调色系（蓝 #2563EB 主色、灰网格线）、中文标签（若字体缺中文则英文标签）、300 dpi 导出后≤1600px 宽。

## SVG 示意图（手写，浅色自足背景）

ch01-pinhole-camera, ch01-thin-lens, ch02-solid-angle, ch02-radiance,
ch02-rendering-equation, ch03-importance-sampling, ch04-path-trace,
ch04-mis-weights, ch05-microfacet, ch05-snell, ch06-ray-quadric,
ch06-barycentric, ch06-parabola-focus, ch07-instancing,
ch07-normal-transform, ch08-bvh, ch08-slab, ch08-two-level,
ch09-app-flow, ch09-optix-pipeline, ch09-sbt, ch10-stratified,
ch18-physics-pipeline, ch18-trs-bake, ch12-radiative-transfer,
ch12-flame-field, ch13-water-layers, ch13-glitter,
ch14-equirect-mapping, ch14-env-cdf,
ch15-shadow-transmit, ch15-medium-stack, ch16-halfvec-refraction,
appendix-lambert-bias（共 34 张）

要求：`<svg>` 根元素带白色背景 rect；宽 720-960；字号≥16；中文标注；
配色统一（线条 #334155、强调 #2563EB、光线 #F59E0B、法线 #16A34A、面/体填充 10-15% 透明度）；
箭头用 marker；不用外部字体/图片。

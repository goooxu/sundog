# 技术报告章节规范（冻结版——写作/产图/审查的唯一依据）

每章 2000-4000 字。图文件名在此冻结，写作与产图两侧按名对接。
写作前必读 STYLE.md。"对账"列出该章公式必须与之逐一核对的代码。

---

## index.md（集成阶段编写）

阅读指南（三条路径：只看原理 01-05 / 只看工程 08-11 / 全读）、章节导航表、
致谢与延伸阅读（PBRT、Ray Tracing in One Weekend、OptiX Programming Guide）。

---

## 01-images-and-rays.md 成像与光线

**回答**：数字图像是什么？渲染在计算什么？为什么沿"反方向"追光线？

小节：
1. 像素、RGB 与线性空间——显示器 gamma、为什么渲染在线性空间累积最后才 `pow(1/γ)`（对账 `Film::writePng()`（src/film.cpp）：exposure→clamp→gamma 顺序）
2. 针孔相机模型——成像平面、视场角 vfov、从像素 (i,j) 构造光线 o+t·d（对账 `makeCamera()`（src/scene_json.cpp）：lowerLeft/horizontal/vertical 的几何含义）
3. 薄透镜与景深——光圈半径、对焦距离（对账 raygen 中 lensRadius 采样）
4. 光的可逆性：从眼睛出发反向追踪的合理性（Helmholtz 互易性直觉版）；一张图给出"渲染一帧 = 对每个像素解一个积分"的全书路线图
5. sundog 的一帧流程速览（伪代码 10 行：循环 spp→构造光线→追踪→累积→tonemap）

图：
- `figures/ch01-pinhole-camera.svg`：针孔相机+成像平面+像素网格+一根光线
- `figures/ch01-thin-lens.svg`：薄透镜、光圈、焦平面、弥散圈
- `figures/ch01-spp-convergence.png`：同场景 1/4/16/64/256 spp 五联横条
- `figures/ch01-gamma.png`：gamma 1.0 vs 2.2 双联

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
- `figures/ch03-mc-convergence.png`：数据图——PSNR(dB) vs spp（对数轴），标注 +6dB/4× 参考斜率
- `figures/ch03-importance-sampling.svg`：同一被积函数下均匀采样 vs 重要性采样的样本分布
- 复用 ch01-spp-convergence 交叉引用

---

## 04-path-tracing.md 路径追踪算法

**回答**：把 MC 用到渲染方程上，完整算法长什么样？sundog 的 raygen 循环逐行在干什么？

小节：
1. 递归展开：L = L_e + ∫f L → 路径积分观点；吞吐量 β 的迭代维护 β ← β·f·cosθ/p（对账 raygen 主循环 `beta *= bs.weight`（device/programs.cu））
2. 为什么纯 BSDF 采样打不中小灯——NEE：单独采样光源、按立体角 pdf 加权、shadow ray 验可见性（对账 NEE 段）；delta 灯（点光/平行光）为什么只能 NEE
3. 双重计数问题与 MIS——同一贡献两条获取路径；balance heuristic w = p_a/(p_a+p_b) 的直觉与推导（证明加权后无偏）；对账：NEE 侧 `w = pdfLe/(pdfLe+pdfB)`、发光体命中侧 `w = prevPdf/(prevPdf+pdfL)`，两式互补
4. 俄罗斯轮盘——以概率 q 存活、除以 q 补偿，期望不变的两行证明；sundog depth≥4 启动、q=clamp(maxComp(β))（对账 RR 段）
5. firefly 与 clamp——为什么间接光会出孤立亮点；clamp 引入可控偏差换方差（对账 `clampVal` 仅 depth≥1）
6. 完整伪代码（与 programs.cu 一一对应，≤30 行）

图：
- `figures/ch04-path-trace.svg`：相机→漫反射点(带 NEE shadow ray)→玻璃→灯 的完整路径
- `figures/ch04-mis-weights.svg`：小亮灯场景中 BSDF 采样 pdf 与光源采样 pdf 的互补覆盖
- `figures/ch04-nee.png`：cornell 类场景 64spp，NEE 开 vs 关（发光体 nee:false）双联
- `figures/ch04-clamp.png`：低 spp 下 --clamp 0 vs 默认 双联（fireflies 可见）

---

## 05-materials.md 材质与 BSDF

**回答**：漫反射/金属/玻璃在数学上是什么？粗糙度如何进入方程？

小节：
1. Lambertian——f_r=albedo/π；cosine 采样推导（含 (u1,u2) 映射与 pdf 验证）；f·cos/p = albedo 的"权重恰好是反照率"（对账 `bsdfSample()` MT_LAMBERT 分支）
2. 微表面理论——宏观粗糙=微镜面统计；D（GGX：α²/(π((n·h)²(α²-1)+1)²)）、G/G1（Smith，含 Λ 式）、完整 f_r = DGF/(4cosθ_i cosθ_o)（对账 `ggxD/ggxLambda/ggxG`）
3. VNDF 采样——为什么按可见法线采样、pdf = G1·D/(4cosθ_o)、权重化简为 F·G/G1（对账 `ggxSampleVndf/ggxPdf`，含 roughness<1e-3 退化 delta 镜面）
4. 菲涅尔——电介质精确式与 Schlick 近似 F0+(1-F0)(1-cosθ)⁵；金属的"F0 即颜色"；曲线图对照
5. 折射——Snell 定律、η 按面选取、TIR 条件、出射侧余弦（对账 `refract()`（device/math.cuh）与 MT_DIELECTRIC 分支，特别是 `cosine = frontface ? -dot(rayDir,n) : -dot(refr,n)` 的原因——错用密介质侧余弦正是附录陷阱清单的第一条）
6. 双面材质与穿透面语义（front/back、MAT_NONE）

图：
- `figures/ch05-microfacet.svg`：宏观面上的微镜面、半程向量 h、遮蔽/阴影
- `figures/ch05-snell.svg`：折射几何+临界角+TIR
- `figures/ch05-fresnel-curves.png`：数据图——η=1.5 精确 Fresnel vs Schlick，0-90°
- `figures/ch05-roughness-ladder.png`：5 个金属球 roughness 0/0.1/0.25/0.45/0.7

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
- `figures/ch06-primitives.png`：features.json 渲染（5 种原语同框）

---

## 07-transforms.md 变换与实例化

**回答**：为什么只需要"单位球"？一万个物体为什么不用一万份几何？

小节：
1. 仿射变换 3×4 矩阵、复合顺序（scale→rotate→translate 的列表语义，对账 `parseTransform()` 的 `M = e·M`）
2. 光线的逆变换——世界光线→物体空间求交→命中变回世界（OptiX 实例机制承担）
3. 法线为什么用逆转置——切向量保持相切的推导 (M⁻¹)ᵀ；非均匀缩放反例（对账 `optixTransformNormalFromObjectToWorldSpace` 的使用+renormalize）
4. 实例化——一份 GAS + N 个带变换的实例；32768 奶牛只有 5856 个三角形一份；面光源的面积/pdf 要用世界空间量（对账 scene_json 灯注册 `area = 4|u×v|`、行列式符号翻转法线）
5. 变换下的 pdf/面积修正（为什么非均匀缩放的球面光被解析期拒绝）

图：
- `figures/ch07-instancing.svg`：一份单位几何 GAS ← 三个实例矩阵 → 世界中三个不同摆放
- `figures/ch07-normal-transform.svg`：非均匀缩放下"直接变换法线"出错 vs 逆转置正确
- 复用 `../gallery/05-spot-swarm.png` 交叉引用

---

## 08-acceleration.md 加速结构与 RT Core

**回答**：3 千万条光线 × 1.9 亿三角形为什么能在几十毫秒内算完？

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
5. anyhit 的三件事——穿透面（MAT_NONE ignore）、alpha 镂空、阴影线复用同一逻辑；DISABLE_ANYHIT 快速路径（对账 `maskAnyhit()` 与 accel.cpp 实例 flags）
6. 主机侧一帧的编排——上传/建 AS/launch 分块/回读（对账 main.cpp 渲染循环）；PTX/OptiX-IR 的工程坑一段带过（引 ARCHITECTURE.md）

图：
- `figures/ch09-app-flow.svg`：七步生命周期泳道图（主机一次性准备 → 渲染循环 ⑤↔⑥ → 降噪落盘，框内标源文件，硬件/软件分色）
- `figures/ch09-optix-pipeline.svg`：一次 optixTrace 的程序调用流程图（含 AH ignore 回路）
- `figures/ch09-sbt.svg`：SBT 内存布局：raygen|miss×2|每实例两条 hitgroup 记录
- `figures/ch09-aov.png`：beauty / albedo AOV / normal AOV 三联

---

## 10-sampling-denoising.md 随机数、纹理与 AI 降噪

**回答**：GPU 上万条并行光线怎么"随机"得又快又可复现？低 spp 图像怎么变干净？

小节：
1. PCG32——为什么不用 curand；按 (pixel,sample) 播种的独立流设计→逐位决定性（同 seed 同驱动图像比特级一致，正是 golden 测试基础）（对账 `Pcg32::init()` 播种式）
2. 分层采样——把 [0,1)² 切格子降方差；spp 的 ⌊√N⌋² 分层（对账 raygen 抖动段）
3. 纹理——UV→纹素、双线性过滤、sRGB 硬件解码、alpha 镂空（对账 textures.cpp 与 `evalTexture()`）
4. AI 降噪——为什么 MC 噪声适合学习法去除；albedo/normal 引导层的作用（"告诉网络哪些边是真的"）；HDR 域降噪在 tonemap 前（对账 denoise.cpp 流程）；局限（偏差、非决定性输出不入 golden）

图：
- `figures/ch10-stratified.svg`：64 个纯随机点 vs 8×8 分层点的空隙/成团对比
- 复用 `../gallery/03-spot-atrium-spp32-raw.png` 与 `-denoised.png` 双联交叉引用
- `figures/ch09-aov.png` 交叉引用（引导层）

---

## 11-validation.md 验证方法学与性能

**回答**：怎么证明渲染器"算得对"？性能怎么度量、怎么解读？

小节：
1. 正确性验证金字塔——150 万断言的 host 单测（与 GPU 共享同一份头文件的设计）、白炉测试、golden 图 PSNR≥45、sha256 决定性、compute-sanitizer
2. 两层基准设计——特性层（五个画廊场景的渲染时间/吞吐/显存）与降噪层（低 spp + 降噪 vs 高 spp 参考的 PSNR）；计时口径（只计渲染循环，场景解析与加速结构构建不计入）
3. 结果解读——吞吐从哪来（并行度、RT core、内存层次），引用 BENCHMARKS.md 实测表格；Mrays/s 的含义与用它还原平均路径结构
4. 降噪的等效加速——16spp+降噪 ≈ 数千 spp 视觉质量（PSNR 表引 BENCHMARKS.md 降噪层）

图：
- 无专属图；表格引用 docs/BENCHMARKS.md 数字（不虚构）

---

## appendix-pitfalls.md 附录：路径追踪常见实现陷阱

**回答**：写一个路径追踪器最容易在哪些地方悄悄算错？（案例式清单，每条=症状/数学分析/sundog 的做法）

条目：
1. 折射率之比用错方向——恒用 η=1/ior 时判别式恒正、玻璃永远不发生 TIR 的证明；Schlick 用密介质侧余弦会严重低估反射（临界角处 F 应连续趋于 1）；sundog 按面选 η、用低折射率侧余弦（第 5 章）
2. 采样分布与权重不匹配——"n + 球内均匀点"≠ cosine 分布，却按完美重要性采样加权→系统性偏差；sundog 用 Malley 方法精确余弦采样
3. 阴影线把透射材质当不透明——玻璃背后影子全黑；常见工程折衷的取舍讨论：sundog 同样如此，靠面光 + BSDF 折射路径 + MIS 兜底玻璃后方与焦散
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
| ch01-spp-convergence.png | 五联横条 1/4/16/64/256 spp | 02-cornell-lume --size 480x270，PIL 横拼+每格左上角标 spp |
| ch01-gamma.png | gamma 1.0 vs 2.2 双联 | smoke.json --gamma 1.0/2.2，PIL 竖缝拼接+标注 |
| ch04-nee.png | NEE 开关双联 | 02-cornell-lume 64spp 原样 vs sed 生成 nee:false 变体场景 |
| ch04-clamp.png | firefly 对比双联 | 04-parabolica --size 640x360 --spp 24，--clamp 0 vs 默认 |
| ch05-roughness-ladder.png | 金属球粗糙度阶梯 | 新建临时小场景（5 球 roughness 0/0.1/0.25/0.45/0.7，一个面光）512spp |
| ch06-primitives.png | 5 原语同框 | features.json 256spp |
| ch09-aov.png | beauty/albedo/normal 三联 | 03-spot-atrium --spp 64 --aov-albedo --aov-normal，PIL 三拼 |

全部经 PIL 无损压缩入 docs/report/figures/；标注文字用 PIL 默认字体白底黑字角标即可。

## 数据图（matplotlib，测试机采数据）

| 文件 | 内容 | 数据来源 |
|---|---|---|
| ch03-mc-convergence.png | PSNR vs spp（1,2,4,...,1024），log-x | 02-cornell-lume 各 spp vs 4096spp 参考，img_compare 采 |
| ch05-fresnel-curves.png | η=1.5 精确 Fresnel vs Schlick | 纯计算 |

风格：白底、单一强调色系（蓝 #2563EB 主色、灰网格线）、中文标签（若字体缺中文则英文标签）、300 dpi 导出后≤1600px 宽。

## SVG 示意图（手写，浅色自足背景）

ch01-pinhole-camera, ch01-thin-lens, ch02-solid-angle, ch02-radiance,
ch02-rendering-equation, ch03-importance-sampling, ch04-path-trace,
ch04-mis-weights, ch05-microfacet, ch05-snell, ch06-ray-quadric,
ch06-barycentric, ch06-parabola-focus, ch07-instancing,
ch07-normal-transform, ch08-bvh, ch08-slab, ch08-two-level,
ch09-app-flow, ch09-optix-pipeline, ch09-sbt, ch10-stratified,
appendix-lambert-bias（共 23 张）

要求：`<svg>` 根元素带白色背景 rect；宽 720-960；字号≥16；中文标注；
配色统一（线条 #334155、强调 #2563EB、光线 #F59E0B、法线 #16A34A、面/体填充 10-15% 透明度）；
箭头用 marker；不用外部字体/图片。

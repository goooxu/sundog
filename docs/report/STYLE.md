# 技术报告风格规范（写作与审查共同遵守）

## 读者假设

- 没有任何计算机图形学背景；不知道"光线追踪/BRDF/BVH"是什么
- 具备工程师数学水平：微积分、线性代数、概率论（期望/方差/pdf）可直接使用
- 每个图形学概念：先给直觉（一两句白话+图），再给严格定义与推导，最后指认代码

## 术语规范

首次出现时写"中文（English/缩写）"，之后统一用中文；全书用词以下表为准：

| 中文 | English | 说明 |
|---|---|---|
| 光线追踪 | ray tracing | |
| 路径追踪 | path tracing | |
| 光线 | ray | o + t·d |
| 图元 | primitive | |
| 求交 | intersection | |
| 辐亮度 | radiance | L，W·sr⁻¹·m⁻² |
| 辐照度 | irradiance | E |
| 立体角 | solid angle | ω，单位 sr |
| 双向反射分布函数 | BRDF | f_r；本书在含透射时用 BSDF |
| 渲染方程 | rendering equation | |
| 蒙特卡洛 | Monte Carlo / MC | |
| 概率密度函数 | probability density function / pdf | 全书小写 pdf |
| 重要性采样 | importance sampling | |
| 下一事件估计 | next event estimation / NEE | 即"直接光采样" |
| 多重重要性采样 | multiple importance sampling / MIS | |
| 平衡启发式 | balance heuristic | |
| 俄罗斯轮盘 | Russian roulette / RR | |
| 吞吐量 | throughput | β，路径累积衰减 |
| 萤火虫噪点 | firefly | 孤立超亮像素 |
| 朗伯（漫反射） | Lambertian | |
| 微表面 | microfacet | |
| 法线分布函数 | normal distribution function / NDF | GGX 的 D |
| 几何遮蔽项 | masking-shadowing | G / G1 |
| 可见法线分布采样 | VNDF sampling | |
| 菲涅尔 | Fresnel | |
| 全内反射 | total internal reflection / TIR | |
| 折射率 | index of refraction / IOR | η 或 ior |
| 粗糙度 | roughness | α = roughness² |
| 包围盒 | bounding box / AABB | |
| 波瓣 | lobe | 方向分布集中的区域；表面越光滑反射波瓣越窄 |
| 层次包围盒 | bounding volume hierarchy / BVH | |
| 加速结构 | acceleration structure / AS | |
| 几何加速结构 | GAS | OptiX 术语 |
| 实例加速结构 | IAS | OptiX 术语 |
| 实例化 | instancing | |
| 着色器绑定表 | shader binding table / SBT | |
| 光线负载 | payload | |
| 任意命中 | any-hit / AH | OptiX 程序类型，同理 raygen/miss/closest-hit(CH)/intersection(IS) |
| 每像素采样数 | samples per pixel / spp | |
| 命中组 | hitgroup | 某类图元的 IS/AH/CH 程序打包成的一组，SBT 记录的指向单位 |
| 环境光遮蔽 | —— | 本书不涉及，勿引入 |
| 刚体 | rigid body | 不发生形变的理想物体 |
| 位姿 | pose | 位置 + 姿态（平移 + 旋转） |
| 四元数 | quaternion | q，单位四元数表示旋转 |
| 角速度 | angular velocity | ω |
| 惯量张量 | inertia tensor | I，旋转世界的"质量" |
| 凸包 | convex hull | 包住全部顶点的最小凸多面体 |
| 宽相 / 窄相 | broad / narrow phase | 碰撞检测的两级筛选 |
| 恢复系数 | restitution | 碰撞后保留的法向速度比例 |
| 休眠 | sleeping | 动能长期低于阈值的刚体被冻结 |
| 沉降 | settling | 模拟到全体刚体休眠 |
| 定格 | freeze-frame | 模拟推进到 stop_time 即烘焙 |
| 参与介质 | participating medium | 吸收/发射/散射光的介质 |
| 辐射传输 | radiative transfer | 介质中辐亮度的演化方程 |
| 吸收 / 发射 / 散射 | absorption / emission / scattering | 介质对光的三种作用 |
| 透射率 | transmittance | T，穿过介质存活的比例 |
| 比尔–朗伯定律 | Beer–Lambert | T = exp(−∫σ) |
| 光线行进 | ray marching | 沿光线的固定步长数值求积 |
| 值噪声 | value noise | 格点伪随机数的三线性插值 |
| 分形布朗运动 | fractal Brownian motion / fBm | 多八度噪声叠加 |
| 着色法线 | shading normal | 用于着色、可与几何法线不同 |
| 高度场 | height field | y = H(x,z) 的隐函数曲面 |
| 法线扰动 | normal perturbation | 只骗着色、不动几何的波纹手法 |
| 波光路径 | sun glitter | 波面法线抖动拉出的镜面亮带 |
| 环境贴图 | environment map / envmap | 无穷远处包住场景的球面光源图像 |
| 基于图像的照明 | image-based lighting / IBL | 用环境贴图充当光源 |
| 等距柱状投影 | equirectangular | 经纬直展的方向↔像素映射 |
| 高动态范围 | high dynamic range / HDR | 浮点辐亮度；Radiance .hdr 格式 |
| 累积分布函数 | cumulative distribution function / CDF | 逆变换采样的查找表 |
| 逆变换采样 | inverse transform sampling | 均匀随机数经 CDF⁻¹ 得目标分布 |
| 边缘 / 条件分布 | marginal / conditional distribution | 2D 采样"先选行再选列"的分解 |
| 透明阴影 | transparent shadows | 阴影线沿直线累积透射率而非布尔遮挡 |
| 斯涅尔窗口 | Snell's window | 水下仰望时天空被压进的临界角圆锥 |
| 介质栈 | medium stack | 嵌套介质的 LIFO 记账（进 push 出 pop） |
| 光学深度 | optical depth | τ = ∫σ dt，透射率 e^−τ 的指数 |
| 粗糙电介质 | rough dielectric | roughness>0 的玻璃：微表面反射+透射 |
| 磨砂玻璃 | frosted glass | 粗糙电介质的日常名 |
| 微表面透射 | microfacet transmission | Walter 2007，透射半程向量 |
| 双向透射分布函数 | BTDF | f_t；与 BRDF 合称 BSDF |
| 降噪器 | denoiser | |
| 引导层 | guide layer | albedo/normal AOV |
| 色调映射 | tone mapping | 出口映射：ACES Hill 拟合（RRT+ODT），第 1 章 |
| ACES | Academy Color Encoding System | 电影工业色彩编码标准 |
| 脚趾 / 肩部 | toe / shoulder | S 曲线的暗部轻压段 / 高光滚降段 |
| 伽马校正 | gamma correction | |
| 峰值信噪比 | PSNR | dB |

## 数学符号约定（与代码变量对应）

| 符号 | 含义 | 代码对应 |
|---|---|---|
| $`x`$ | 表面点 | `x` (programs.cu) |
| $`\omega_i, \omega_o`$ | 入射/出射方向（都从 $`x`$ 指出，单位向量） | `wi`, `wo` |
| $`n`$ | 着色法线（朝入射侧） | `ns` |
| $`L(x,\omega)`$ | 辐亮度 | — |
| $`L_e`$ | 自发光辐亮度 | `Le` |
| $`f_r`$ | BRDF/BSDF | `bsdfEval` |
| $`\theta`$ | 与法线夹角，$`\cos\theta = n\cdot\omega`$ | `cosS` 等 |
| $`p(\omega)`$ | 立体角 pdf | `pdf` |
| $`\beta`$ | 路径吞吐量 | `beta` |
| $`\alpha`$ | GGX 粗糙度参数（= roughness²） | `alpha` |
| $`\eta`$ | 相对折射率 | `eta` |
| $`F_0`$ | 垂直入射反射率 | `f0` |
| $`N`$ | 样本数（spp） | `sppTotal` |

- 向量默认列向量、单位化时明说；点乘写 $`n\cdot\omega`$
- 期望 $`\mathbb{E}[\cdot]`$，方差 $`\mathrm{Var}[\cdot]`$

## Markdown 约定

- 数学：行内用 ``$`...`$``（美元+反引号），独立公式用 ```` ```math ```` 围栏块——两种写法 GitHub 与 GitLab 都原生渲染；**不要用裸 `$...$` / `$$...$$`**（GitHub 对紧邻中文全角字符的 `$` 解析会失效，GitLab 则完全不支持）。重要公式前空行独立成段；不用公式编号，引用时用文字（"上式""渲染方程"）
- 源码引用：`` `bsdfSample()`（device/bsdf.cuh）``格式——函数名+文件路径，不写行号（会漂移）
- 图：`![说明](figures/chNN-name.png)`，图后紧跟一行斜体图注 `*图：……*`；所有图放 `docs/report/figures/`
- 交叉引用其他章：`[第 3 章·蒙特卡洛积分](03-monte-carlo.md)`
- 每章结构：开头 2-3 句"本章回答什么问题、承接哪一章"；结尾"小结 + 下一章去哪"；标题层级从 `#`（章名）到 `###` 为止
- 代码片段：只贴关键 10 行以内并注明出处，不整段复制源码
- 语言：中文技术写作，短句优先；第一人称复数（"我们"）用于推导过程

## 质量红线（审查依据）

1. 报告中出现的每个公式，若代码有对应实现，形式必须与实现一致（含常数因子、符号约定）；对账清单见 OUTLINE.md 各章"代码对账"
2. 任何概念不得未定义先用（读者零图形学基础）；允许前向引用但须注明"见第 N 章"
3. 图必须真实存在且内容与图注相符；渲染图须可由 `scripts/render-report-figures.sh` 再生
4. 不虚构性能数字——一律引用 docs/BENCHMARKS.md 或 stats.json 实测值

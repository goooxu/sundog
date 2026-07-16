# sundog 更新日志

本文件记录 sundog 每个主要版本的改动，格式参照
[Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循
[语义化版本](https://semver.org/lang/zh-CN/)。项目尚无正式发布流程：版本为按
功能里程碑追溯编号，日期取该里程碑收尾提交之日；各版本末尾括注主要提交号，
便于在无标签的历史中定位。

## [Unreleased]

### 修复

- **火焰体积阴影**：NEE 阴影线按火焰透射率衰减——火焰投影、纯吸收烟柱遮光，
  修复"灯光无衰减穿过体积介质"的物理缺口（报告第 13 章 §13.6）。透射专用
  行进 `flameTransmittance` 与 radiance 行进共享同一离散化（32 步抖动 +
  包围柱剪枝，tests/host/test_volume.cpp 以克隆 RNG 流逐位交叉钉死）；
  内嵌点光对**宿主火焰**豁免（`LightDesc.flameId` 回链——`light_intensity`
  校准的是逃逸后发射，自吸收不得记两次账），穿越其他火焰照常衰减；随
  `transparent_shadows` 门控，`--opaque-shadows` 保持旧口径对照完整
- **发光网格 NEE 登记**：emissive `mesh:NAME` 对象自动注册为区域光（此前
  只有 rect/disk/sphere），终结"发光网格只能被 BSDF 撞中"的萤火虫来源
  （报告第 4 章网格光子节）。两级采样 = 世界空间三角形面积前缀 CDF 二分 +
  重心 √ 变换；`pdf = d²/(cosθ_g·A_total)` 两侧同用几何法线（closest-hit
  对已登记发光网格改打包几何法线，发光命中即终止路径故安全）；纹理化
  发光网格合法（uv 两侧同 buffer 同权重逐位一致）；CDF 于渲染期在
  加载后的网格上构建（OBJ 延迟加载），动态刚体不得兼任网格灯（与其他
  灯型同规）

### 变更

- `--opaque-shadows` 语义扩展：除表面透射外同时禁用火焰体积阴影衰减
  （完整还原旧口径基线）
- **行为变化**：动态刚体 + 发光网格 + 默认 `nee` 由"仅 BSDF 可见"变为
  硬错误（需显式 `nee=False`）——与 rect/disk/sphere 的既有约束对齐；
  经查无现有场景触雷
- **行为变化**：alpha 镂空对象不得兼任 NEE 区域光（任意形状）——NEE 按
  完整几何面积采样会把镂空孔洞当作发光点，与穿孔的 BSDF 侧对不上账
  （正偏差）；此为审查中发现的既有隐患，随网格灯一并堵上，需 `nee=False`；
  经查无现有场景触雷

## [0.12.1] - 2026-07-16 — 清理

### 移除（零引用死面，三路审计交叉验证）

- `scripts/gen_cover_textures.py`（封面纹理一次性生成器；gear/runes.png
  转为静态资产，出处记入 assets/LICENSES.md，生成器见 git 历史）
- 10 个场景文件的转换器模板剩余 import（AST 逐名核对）与 scenelib 的
  未用 `import math`
- 旧 CLI 残骸 `printProbe()`（探针输出早已由 Python 侧格式化）、
  零调用设备 helper `fractf()`、生产侧零消费者的 `tonemapFromString()`
  （tonemap 走 C ABI int）及其单测用例、Makefile 失效旋钮 `ARCH`
  （设备架构硬编码 compute_120）

### 修正（v0.11 库化迁移漏更的陈旧引用）

- 技术报告 8 处 `src/main.cpp` 对账点与 ch09 流程图 SVG 改指
  `src/capi_render.cpp`；"八个设备程序"改十个（透明阴影后实际入口数）、
  "嵌入可执行文件/二进制"改嵌入渲染库；golden 场景数 5→6；
  07 章"JSON 中的角度"改场景口径；OUTLINE 残留 .json 提法
- README 目录结构（src/ 的 CLI/场景解析提法、assets/ 举例）；
  report/index.md 摘掉 OUTLINE/STYLE 的读者链接（写作脚手架保留，
  不再暴露在报告门面）

（主要提交见本版单提交）

## [0.12.0] - 2026-07-16 — Sparky 机器人与多材质组网格

### 新增

- **网格 usemtl 组过滤**：`s.mesh(name, obj, usemtl="组名")` 只加载 OBJ 的
  一个材质组——多材质模型按组拆成同变换子网格，各组映射独立的 sundog
  材质（tinyobj 逐面 material_ids 过滤；组缺失/空组报错）
- **Sparky**：AI 生成的卡通机器人模型入库（assets/sparky.obj/.mtl +
  屏幕图集纹理，7,284 三角形、十个材质组，无外部下载源故全部入库，
  provenance 见 assets/LICENSES.md）

### 变更

- `03-spot-atrium` 改造为「Sparky 会奶牛」：机器人十组子网格
  （dielectric 玻璃头罩、emissive 像素屏与核心灯、metal 关节、
  lambert 塑料壳——材质按 .mtl 名字意图演绎）与三头 Spot 同框；
  画廊/基准/README/报告 ch09 AOV 图同步

（主要提交 016f6b9 与 Sparky 入库提交）

## [0.11.0] - 2026-07-16 — 零中间表示：libsundog.so + ctypes 直灌

### 新增

- **C 场景构建 ABI**（`src/sundog_api.h` + `capi_scene/capi_render.cpp`）：
  渲染器编译为 `libsundog.so`，场景数据经类型化 grouped-call 逐调用直灌
  （标量 double 进边界立即窄化 float，与旧 JSON 解析同一 IEEE 操作；
  可选性 = NaN/NULL/-1 哨兵，渲染器缺省只活在 C++；相位状态机把
  光序契约升格为 API 不变量；异常不过 ABI——错误码 + thread-local
  last_error）；`scene_build.cpp` 承载装载器无关的派生逻辑
  （变换合成/NEE 面光注册/火焰双灯），自旧装载器逐字搬运
- scenelib：`_program()` 纯函数展平调用流（纹理/材质/网格 id 按
  sorted(name) 分配——与 nlohmann std::map 字典序一致，golden 逐位
  稳定的前提）、ctypes 后端（RTLD_GLOBAL 供 PhysX 运行时解析 CUDA
  符号）、argparse 全旗面、`--probe` 经库、公开 `validate()`、
  `doc` 升格官方逃生舱（图版变体经改 doc 后进程内渲染）

### 变更

- **场景的 JSON 中间表示自代码中彻底删除**：渲染路径零序列化、零临时
  文件、零子进程——`python3 scenes/xx.py` 在进程内完成构建与渲染
- **独立可执行文件 sundog 删除**（Makefile 只产 .so）；`--scene`/
  `--emit-json`/`to_json()`/`save()` 随之退场；`extern/json.hpp`
  （24765 行）移除，`--stats` 输出改约 40 行手写序列化
- run-sanitizer 直包 python 进程；报告图版的变体场景全部改为进程内
  scenelib 构建（含入库 roughness-ladder 场景迁 .py）；
  `test_scene_json.cpp` → `test_scene_build.cpp`（断言面重定向到
  C API，新增光序/相位/detSign/材质上限契约测试）
- 取代 0.10.0 的三点表述：JSON 临时中间表示、--emit-json 逃生舱、
  host-tests 经 --emit-json 装载——本版全部不复存在
- 已知行为变化：ctypes 调用期间 Ctrl-C 响应有延迟（渲染在 python
  进程内，信号处理排队至调用返回）

### 修复

- 报告图版 ch09 一直把 .py 场景喂给 `--scene`（场景即程序切换后即坏），
  改为场景直执行带 AOV 旗子
- C API 原子性：动态刚体+NEE 面光的拒绝原发生在光注册之后，失败的
  add 会留下孤儿光——检查前移，失败调用零副作用

红线记录：golden 6 场景不重生成 PSNR 全 inf；smoke 决定性 sha 与
JSON 时代基准逐位相同（2de55ced…）；报告图版 18 张重渲逐字节不变。

（主要提交 9e7a18a · de347d5 · 9a1a1b9 · 0274f37）

## [0.10.0] - 2026-07-15 — 场景即程序：Python 场景定义

### 新增

- **scenelib**（`scenes/scenelib.py`）：Python 场景定义库——场景是可执行
  程序，直接运行即渲染（`python3 scenes/07-campfire.py`），输出名由代码内
  `s.run(out=…)` 指定，命令行参数原样透传覆盖场景内建设置；
  `--emit-json PATH|-` 只发中间表示不渲染（测试/工具逃生舱）
- OMIT 哨兵透传（可选参数不设置就不落键）、任意序 transform 构造器、
  镜像装载器全部硬校验并报场景文件行号、决定性契约（库内零随机零时间戳）
- `tests/test_scenelib.py`：57 项 Python 侧单测挂进 make check

### 变更

- **全部 14 个场景（12 画廊 + smoke + features）迁为自执行 .py**，等价性
  逐字段验证（对象级 deep-diff），golden 6 场景不重生成 PSNR 全 inf、
  smoke 决定性 sha 与历史逐位一致——渲染零扰动；JSON 降级为渲染时落在
  场景同目录的临时中间表示，不再入库、不再是用户格式（C++ 后端零改动）
- 三个场景生成器（gen_swarm/gen_drop/gen_oracle）被同名场景 .py 吸收；
  测试与画廊/基准/报告图版脚本全部改为直接执行场景文件；
  [SCENES.md](docs/SCENES.md) 重写为 scenelib API 参考
- host-tests 运行新增 python3 依赖（场景经 --emit-json 装载）

### 修复

- 06 号场景的 ACES 曝光补偿（exposure 0.25）当年只手术进了 JSON、
  生成器未同步——迁移 parity 闸门抓出并对齐

（主要提交 f2e6e0b · 44f1a63 · 9d7c564 · c6e23fb）

## [0.9.0] - 2026-07-15 — 封面场景

### 新增

- 封面场景 `12-molten-oracle`「熔岩圣殿的机械先知」：机械奶牛在祭坛烈焰上被
  击碎的瞬间——PhysX 于 0.20 s 定格 49 个刚体的爆裂、穹顶破口的 envmap 神光
  与地面碎影、零发射黑烟柱、极坐标镂空齿轮、纹理化符文发光体、下沉水池，
  单场景汇演全部渲染机制（生成器 `scripts/gen_oracle.py` /
  `scripts/gen_cover_textures.py` 与产物同入库）
- 原生 4K / 1024 spp 成品 `docs/cover.png`，作为 README 首图

### 变更

- 封面修正：下沉水池按屏幕投影反推移入右前景，倾斜池底做清浅→幽蓝渐变，
  西缘接住烈焰倒影
- 画廊与基准纳入 12 号场景（压轴）

（主要提交 e6a83d0 · 49ac698 · 0e0c287）

## [0.8.0] - 2026-07-15 — 透明阴影与嵌套介质

### 新增

- **透明阴影**：阴影线沿直线透射玻璃/水，逐界面菲涅尔 + 介质段
  Beer–Lambert——有色玻璃投有色亮影、水下点收到直接光、Snell 窗口自然涌现；
  `transparent_shadows` 场景键与 `--opaque-shadows` 对照开关
- **嵌套介质**：介质栈 + 相对折射率——水中玻璃按 η=1.5/1.33 折射、玻璃中
  气泡按 1.33/1.0；`dielectric` 支持 `absorb` 体吸收
- 画廊场景 `11-glasswork`「琉璃静物」（玻璃⊃水⊃气泡，牛立气泡中）
- [报告第 16 章](docs/report/16-transparent-media.md)：透明阴影与嵌套介质

### 变更

- golden 集扩至 6 场景并按透明阴影口径重生成；画廊与基准全量重跑；
  全书折衷叙述翻新

（主要提交 f4179ec · 1719b89 · 5c3dc13）

## [0.7.0] - 2026-07-14 — ACES 色调映射

### 新增

- ACES 输出管线（Hill 拟合 RRT+ODT）：曝光 → ACES → gamma → 量化，默认对
  全部输出生效——高光沿肩部渐进滚降而非硬截断为纯白；`render.tonemap` 场景键
  与 `--tonemap`（`clamp` 保留线性退路供数值实验）
- 报告第 1 章新增色调映射小节

### 变更

- 场景曝光按 ACES 响应逐场景微调；golden、画廊、基准与报告插图全部按
  ACES 口径重生成

（主要提交 57c7568 · c23d535 · ceac6ca）

## [0.6.0] - 2026-07-14 — HDR 环境光照

### 新增

- `background` 支持 equirect HDR 环境贴图（`envmap`）：按亮度 × sinθ 预构建
  两级 CDF 做**环境光重要性采样**，与 NEE/MIS 全接驳——一张带太阳的天空图
  即可点亮整个场景；`importance:false` 切均匀球面采样对照
- 画廊场景 `10-suncatcher`「晴空捕日」（纯 IBL、全场零显式灯），并纳入 golden
- [报告第 15 章](docs/report/15-envlight.md)：环境光照

### 变更

- 聚焦清理：删除重复文档与无消费者的配置面
- golden manifest 按当期测试机口径重钉

### 修复

- 渲染统计的显存峰值无符号下溢

（主要提交 1cbc0e6 · 043b81c · ba6b12a）

## [0.5.0] - 2026-07-12 — 水面材质

### 新增

- `water` 材质三件套：ior 1.33 电介质界面 + fbm 波纹法线 + Beer–Lambert
  水体吸收（介质内路径按长度衰减，深水偏蓝绿）
- 画廊场景 `08-lakeside`「黄昏湖畔」
- [报告第 14 章](docs/report/14-water.md)：水面——界面、波纹与水色

### 变更

- 新增 `09-ember-shore`「余烬湖岸」并迁移降噪对比载体（体积火焰 + 水面的
  重噪声组合）
- 首图两轮重设计，定稿 `01-marble-run`「晨光弹珠乐园」

（主要提交 ffd6e3c · 7679389 · 2e9b14c）

## [0.4.0] - 2026-07-12 — 体积火焰

### 新增

- 体积火焰光源：程序化发射 + 吸收参与介质（值噪声 fbm 泪滴场，解析界定 +
  光线行进积分），火焰内嵌软阴影点光经 NEE 照亮场景；`flames` 场景键
- 画廊场景 `07-campfire`「篝火夜景」（火焰为全场唯一主光源）
- [报告第 13 章](docs/report/13-volumes.md)：体积渲染——程序化火焰

（主要提交 6687b12 · b89d529）

## [0.3.0] - 2026-07-12 — PhysX GPU 物理装载

### 新增

- PhysX 5.8 GPU 刚体运行时集成：场景 JSON 声明刚体初始条件（`physics` 块 +
  逐对象 opt-in），加载时 GPU 模拟沉降到静止后烘焙变换，再构建加速结构
- 锐利定格模式：`stop_time` 场景键 / `--physics-time`，把刚体冻结在运动中的
  任一瞬间
- 画廊场景 `06-spot-cascade`「奶牛倾泻堆」（512 刚体 GPU 沉降，主图为倾泻
  第 1.0 秒定格）
- [报告第 12 章](docs/report/12-physics.md)：物理装载——PhysX GPU 刚体模拟

（主要提交 0fe7187 · 1126711 · 08db9ff）

## [0.2.0] - 2026-07-11 — 网格纹理、技术报告与项目独立化

### 新增

- OBJ `vt` 纹理坐标导入，网格 UV 纹理接通；画廊网格改用 Spot 卡通奶牛（CC0）
- 渲染技术报告初版：12 章 + 附录、34 图，面向无图形学背景读者
  （[docs/report/](docs/report/index.md)）

### 变更

- 项目独立化：移除对旧 CPU 渲染器的兼容采样模式与对比基线，下架全部 CPU
  渲染代码，定位纯 GPU 渲染器
- 示例场景与基准统一 1920×1080

（主要提交 4d15f49 · dcc8cfc · 0e8beed）

## [0.1.0] - 2026-07-08 — 初始版本

### 新增

- Megakernel 路径追踪：raygen 内迭代 path loop（trace depth 1）、
  NEE + MIS（balance heuristic）、深度 ≥4 起俄罗斯轮盘
- 几何：5 种 quadric（sphere / rect / disk / cylinder / parabola）自定义求交 +
  OBJ 硬件三角形网格；单层 IAS 实例化，支持非均匀缩放
- 材质：lambert、GGX metal、dielectric、emissive（区域光自动 NEE）；
  双面材质、`null` 穿透面、alpha cutout 镂空
- 纹理：solid / checker / grid / PNG 图像（sRGB）；灯光：point（带半径软阴影）、
  distant 与 emissive 区域光
- OptiX AI 降噪（HDR + albedo/normal 引导 AOV）
- 决定性：PCG32，固定种子时同 GPU/驱动上逐位一致，golden 测试依赖此性质
- `--stats` JSON 渲染统计（分段计时、光线数、Mrays/s、显存峰值）
- 测试机引导脚本（用户态安装 CUDA/OptiX 到 /tmp）；初版画廊入库

（主要提交 9c3df67 · 67a2d9e）

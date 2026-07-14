# sundog scene format

Scenes are JSON with eight sections: `render`, `camera`, `background`,
`textures`, `materials`, `meshes`, `objects`, `lights` — plus an optional
ninth, `physics` (PhysX GPU rigid-body settling, see below).

## Canonical shapes (object space)

| shape | definition | front face | UV |
|---|---|---|---|
| `sphere` | r=1 at origin | outside | spherical (u: azimuth, v: latitude) |
| `rect` | XZ square [-1,1]^2 at y=0 | +Y side | u=(x+1)/2, v=(z+1)/2 |
| `disk` | XZ disk r<=1 at y=0 | +Y side | u: azimuth, v: radius |
| `cylinder` | x^2+z^2=1, y in [-1,1], **open ends** | outside | u: azimuth, v: height |
| `parabola` | y = (x^2+z^2)/2, r<=1 (focus at (0,0.5,0)) | **convex underside** | u: azimuth, v: radius |
| `mesh:NAME` | triangles from `meshes.NAME` | winding side | OBJ `vt` per-vertex; barycentric fallback without `vt` |

To make a *concave* parabola reflector (dish), put the mirror on
`material_back` (the bowl side) and set `material` to `null`… or rotate the
bowl to aim its convex side away. Remember: `"material"` is the front face.

## Objects

```json
{ "shape": "rect", "material": "gold", "material_back": null,
  "cutout": "spotTex", "nee": true,
  "transform": [ { "scale": [2,1,2] }, { "rotate_x": 90 }, { "translate": [0,2,0] } ] }
```

- `material` (front face). `material_back`: omitted = same as front;
  `null` = **pass-through** (both rays and shadows go straight through when
  hitting that side). `material` may itself be `null`
  when `material_back` is set (back-only surface).
- `cutout`: texture name; alpha < 0.5 makes holes (both faces).
- `transform`: list applied top-down in object space — `[scale, rotate, translate]`
  means translate(rotate(scale(p))). Angles in degrees. Steps: `scale` (number
  or [x,y,z]), `rotate_x/y/z`, `translate`.
  Non-uniform scale is fine (normals handled correctly).
- `nee`: set `false` to keep an emissive object out of explicit light sampling
  (it then only contributes when a path hits it).

## Materials

- `lambert`: `color` or `texture`
- `metal`: GGX, `color` (F0) or `texture`, `roughness` (0 = mirror)
- `dielectric`: `ior` (smooth glass)
- `water`: 光滑电介质界面（`ior` 默认 **1.33**）+ 两件水特有的事——
  **fbm 波纹法线**（`wave_amp` 默认 0.05、`wave_freq` 默认 2.0；只扰动着色
  法线不位移；波场定义在世界 xz，水面按水平放置约定）与 **Beer–Lambert
  水体吸收**（`absorb` 默认 `[0.45, 0.08, 0.035]`，红先被吃 → 深水偏蓝绿；
  折射入水后按介质内路径长度衰减，直到再次穿出水面）。限制：嵌套介质不
  支持（水里再放玻璃按真空算）；水下着色点的 NEE 阴影线会被折射界面遮挡
  （与"透射材质挡阴影线"同一工程折衷，见附录陷阱 3）——水下照明只来自
  穿出水面的 BSDF 路径，深水底部会偏暗。
- `emissive`: `color`/`texture`, `intensity`, `two_sided` (default false —
  only the front face emits). Emissive `rect`/`disk`/`sphere` objects are
  automatically sampled as area lights (unless `nee: false`). Sphere/disk
  area lights require uniform scale. Textured emissives are supported as NEE
  lights for `rect`/`disk`; a textured emissive `sphere` must use
  `nee: false` (its uv frame is object-space and cannot be light-sampled).

## Textures

- `solid`: `color`
- `checker`: `a`, `b`, `scale: [su, sv]`
- `grid`: `a` (cell), `b` (line), `scale`, `width` (line width, cell fraction)
- `image`: `file` (PNG etc., relative to the scene file), `srgb` (default true), bilinear filtering

## Lights (explicit delta lights)

```json
{ "type": "point", "position": [x,y,z], "radius": 0.3, "intensity": [r,g,b] }
{ "type": "distant", "direction": [x,y,z], "radiance": [r,g,b] }
```

`point` has a radius for soft shadows; `intensity` is radiant intensity
(falls off with 1/d^2). `distant` is a parallel light.
Area lights are *not* declared here — make an emissive object instead.

## Background

`{ "type": "solid", "color": [r,g,b] }`,
`{ "type": "gradient", "horizon": [r,g,b], "zenith": [r,g,b] }` (lerp on ray y), or

```json
{ "type": "envmap", "file": "../assets/sky_4k.hdr",
  "rotate": 0, "intensity": 1.0, "importance": true }
```

`envmap` turns an equirectangular Radiance `.hdr` panorama into an
infinitely-distant light: missed rays look it up by direction, *and* it joins
NEE+MIS as a first-class light — at load time a 2D CDF over
luminance × sin θ is built so light sampling hits the brightest texels
directly (a small sun stops being a noise disaster). `file` resolves relative
to the scene file (gallery scenes use `../assets/*.hdr`, downloaded by
`scripts/fetch-assets.sh`); `rotate` spins the map around +y (degrees);
`intensity` is a scalar radiance gain; `importance: false` degrades NEE to
uniform sphere sampling (comparison experiments only — leave it on).

## Render settings

`width height spp max_depth seed clamp gamma exposure` — `width/height`,
`spp`, `seed`, `clamp`, `gamma` also overridable from the CLI. `clamp` limits indirect per-sample contributions (firefly control,
0 = off). Fixed `seed` gives bit-identical images on the same GPU/driver.

## Flames（体积火焰光源）

顶层 `flames` 数组声明程序化火焰——sundog 的第一类**参与介质**（发射 +
吸收，无散射）：raygen 在每段光线上与火焰的竖直包围圆柱解析求交，行进积分
噪声塑形的发射场（黑→深红→橙→黄白梯度），并按透射率衰减其后的一切贡献。
火焰因此"看得见"（含反射/折射里的像），也会遮挡（烟雾感吸收）。

```json
"flames": [
  { "base": [0, 0.12, 0], "height": 1.7, "radius": 0.5,
    "intensity": 22, "sigma": 4.5, "noise_scale": 3.0, "seed": 3,
    "light_intensity": 40 }
]
```

- `base`/`height`/`radius`（必填）：火根位置、火高、包围圆柱半径（轴恒为 +y）
- `intensity`（默认 20）发射亮度；`sigma`（默认 4）吸收密度；
  `noise_scale`（默认 3）火舌噪声频率；`seed`（默认 0）多团火去相关
- `light_intensity`（默认 12）：**照明近似**——每团火自动注册 2 个带半径的
  暖色点光（轴上 0.35H/0.70H 处，半径 0.3R 软阴影），经常规 NEE 照亮场景。
  体积发射本身被 BSDF 路径偶然穿过时也会记入，与点光有轻微能量重复——
  火焰立体角小、σ 小，量级可忽略，此为如实声明的工程近似
- 限制：阴影线不穿越体积衰减（火焰是光学薄介质）；火焰不写 AOV 引导层

## Physics（PhysX GPU 刚体沉降）

带顶层 `physics` 块的场景在加载时先跑一遍 **PhysX 5 GPU** 刚体模拟
（`eENABLE_GPU_DYNAMICS` + GPU 宽相，无 CPU 回退）：JSON 里的 `transform`
是**初始位姿**，模拟到全部刚体休眠（或 `max_time` 超时）后，最终位姿被烘焙回
每个对象的变换，之后照常构建加速结构与渲染。堆叠、倚靠、翻倒的形态因此不需要
手工摆放——`scenes/06-spot-cascade.json`（由 `scripts/gen_drop.py` 生成）就是
512 只 Spot 倾泻进房间的例子。

```json
"physics": { "gravity": [0,-9.81,0], "timestep": 0.0041667, "max_time": 15.0,
             "friction": 0.6, "restitution": 0.1,
             "solver_iterations": [8, 2], "sleep_threshold": 0.05,
             "stop_time": 0 }
```

`stop_time > 0` 是**锐利定格**模式：模拟恰好推进到该时刻（秒）就停下烘焙，
凌空翻滚的刚体原样冻结进画面；`0`/缺省 = 模拟到全体休眠（或 `max_time`
超时）。CLI `--physics-time F` 可覆盖场景值（`0` 强制回沉降模式）——画廊
主图 06 就是同一场景在 `--physics-time 1.0` 下的定格，对照图则是沉降态。

对象通过自己的 `physics` 键 **显式 opt-in**（没有该键的对象不参与碰撞）：

```json
{ "shape": "rect",     "material": "floor",
  "physics": { "thickness": 0.5, "friction": 0.8 }, ... }
{ "shape": "mesh:spot", "material": "spot_skin",
  "physics": { "dynamic": true, "density": 250,
               "velocity": [0,-2,0], "angular_velocity": [1,0,-1] }, ... }
```

- `dynamic`（默认 false）：false = 静态碰撞体，true = 刚体。
  `density`、`velocity`、`angular_velocity` 仅对刚体有意义；
  `friction`/`restitution` 缺省继承全局值。
- **形状支持**：`sphere`（静/动）、`mesh:*`（静/动，凸包近似——顶点上限 64 的
  convex hull，共享网格只烹制一次，实例用 `PxMeshScale` 缩放）、`rect`
  （**仅静态**：按正面 +Y 往背面挤出 `thickness`（默认 0.2）厚的实体板，
  零厚度平面才不会被穿透）。`disk`/`cylinder`/`parabola` 不支持，解析期报错。
- **变换约束**：参与对象的变换必须可分解为 平移·旋转·缩放（无剪切、无镜像）；
  刚体还要求**均匀缩放**。
- **限制**：`dynamic: true` 不能用在自动注册为 NEE 面光源的发光对象上
  （光源采样框架在解析期烘焙）——要么保持静态，要么 `nee: false`。
- **决定性**：固定步长、固定迭代数、按场景顺序建 actor；同一 GPU/驱动上
  跑间可复现（与渲染的决定性同一口径），跨机器不承诺。模拟耗时计入
  `--stats` 的 `timings_ms.physics`，不影响 `render` 口径。

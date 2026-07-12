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
  or [x,y,z]), `rotate_x/y/z`, `translate`, `matrix` (12 floats, row-major 3x4).
  Non-uniform scale is fine (normals handled correctly).
- `nee`: set `false` to keep an emissive object out of explicit light sampling
  (it then only contributes when a path hits it).

## Materials

- `lambert`: `color` or `texture`
- `metal`: GGX, `color` (F0) or `texture`, `roughness` (0 = mirror)
- `dielectric`: `ior` (smooth glass)
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
- `image`: `file` (PNG etc., relative to the scene file), `srgb` (default true)

## Lights (explicit delta lights)

```json
{ "type": "point", "position": [x,y,z], "radius": 0.3, "intensity": [r,g,b] }
{ "type": "distant", "direction": [x,y,z], "radiance": [r,g,b] }
```

`point` has a radius for soft shadows; `intensity` is radiant intensity
(falls off with 1/d^2). `distant` is a parallel light.
Area lights are *not* declared here — make an emissive object instead.

## Background

`{ "type": "solid", "color": [r,g,b] }` or
`{ "type": "gradient", "horizon": [r,g,b], "zenith": [r,g,b] }` (lerp on ray y).

## Render settings

`width height spp max_depth seed clamp gamma exposure` — all overridable from
the CLI. `clamp` limits indirect per-sample contributions (firefly control,
0 = off). Fixed `seed` gives bit-identical images on the same GPU/driver.

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

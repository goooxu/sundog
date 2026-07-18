# sundog scene format

**A sundog scene is an executable Python program.** Every scene — the
gallery scenes in `scenes/` and any scene you write — is a `.py` file that
defines its content through the `scenelib` API and renders itself when run:

```console
$ python3 scenes/07-campfire.py                  # renders 07-campfire.avif
$ python3 scenes/07-campfire.py --spp 16 --size 640x360 --out /tmp/quick.avif
```

There is no separate scene *file format* to learn and no main program to
drive: the scene chooses its own render settings and output name in code,
and any backend flag given on the command line overrides it
(`--spp/--size/--seed/--out/--denoise/--stats/--physics-time/...`).
Under the hood `Scene.run()` feeds the scene through the C ABI of the
renderer library (`$SUNDOG_BUILD/libsundog.so`) via ctypes — call by call,
with **no intermediate representation of any kind** — then renders in-process
(see [ABI notes](#appendix-the-c-abi-internal)).

## Quick start

`scenes/smoke.py`, the minimal end-to-end scene:

```python
#!/usr/bin/env python3
"""sundog scene smoke — minimal sanity-check scene."""

from scenelib import Scene, scale, translate

s = Scene()
s.render(width=256, height=256, spp=16, max_depth=8, seed=7)
s.camera(lookfrom=[0, 1.5, 5], lookat=[0, 0.7, 0], vfov=35)
s.background_gradient(horizon=[1.0, 1.0, 1.0], zenith=[0.4, 0.6, 1.0])

s.texture('floor', 'checker', a=[0.85, 0.85, 0.85], b=[0.15, 0.15, 0.15],
          scale=[8, 8])
s.lambert('ground', texture='floor')
s.lambert('ball', color=[0.7, 0.3, 0.3])
s.point_light(position=[3, 4, 2], intensity=[40, 40, 40], radius=0.4)

s.add('rect', 'ground', scale(4))
s.add('sphere', 'ball', scale(0.7), translate(0, 0.7, 0))

if __name__ == "__main__":
    s.run(out="smoke.avif")
```

Inspect a scene's assembled state without rendering:
`python3 -c "import runpy,pprint,sys; sys.path.insert(0,'scenes'); pprint.pprint(runpy.run_path('scenes/smoke.py')['s'].doc)"`
(`Scene.doc` is a live view — report-figure scripts mutate it to render
variants of a committed scene).

Scene files import `scenelib` from their own directory (Python puts the
script's directory on `sys.path`), so keep scenes inside `scenes/` — they
run from any cwd.

## Scene API

Every optional argument defaults to the sentinel `OMIT`, meaning "don't
write the key": the renderer's own default applies. Values you pass are
emitted verbatim — scenelib never rewrites numbers. `None` is a real value
only for `material`/`material_back` (pass-through, see below).

### `s.render(...)`

`width` (1280), `height` (720), `spp` (64), `max_depth` (16), `clamp` (50.0,
indirect-light firefly clamp, 0 = off), `seed` (7), `gamma` (2.2),
`exposure` (0.0, EV — HDR-domain scale before PQ encoding), `transparent_shadows` (true).

Most are overridable from the command line at run time; fixed `seed` gives
bit-identical images on the same GPU/driver.

Output is always HDR AVIF: linear radiance is PQ-encoded (SMPTE ST 2084, BT.2020 container, 12-bit, lossless) with linear 1.0 anchored at 203 cd/m² — there is no SDR tonemap path (v0.18).

`transparent_shadows` (default true) lets shadow rays transmit through
glass/water with Fresnel + Beer–Lambert attenuation, and march flame volumes
for their transmittance (flames cast shadows; a light's owning flame is
exempt); `false` restores the legacy binary occlusion with shadow-blind
flames (`--opaque-shadows`, comparison figures only).

### `s.camera(lookfrom, lookat, up, vfov, aperture, focus_dist)`

`lookfrom`/`lookat` are required 3-vectors. `up` ([0,1,0]), `vfov` (40°,
vertical), `aperture` (0 = pinhole; thin-lens otherwise), `focus_dist`
(0 → distance to `lookat`).

### Backgrounds

- `s.background_solid(color)`
- `s.background_gradient(horizon, zenith)` — lerp on ray y
- `s.background_envmap(file, rotate, intensity, importance)`

`background_envmap` turns an equirectangular Radiance `.hdr` panorama into
an infinitely-distant light: missed rays look it up by direction, *and* it
joins NEE+MIS as a first-class light — at load time a 2D CDF over
luminance × sin θ is built so light sampling hits the brightest texels
directly (a small sun stops being a noise disaster). `file` resolves
relative to the scene file (gallery scenes use `../assets/*.hdr`, downloaded
by `scripts/fetch-assets.sh`); `rotate` spins the map around +y (degrees);
`intensity` is a scalar radiance gain; `importance=False` degrades NEE to
uniform sphere sampling (comparison experiments only — leave it on).

### Textures — `s.texture(name, type, **fields)`

- `"solid"`: `color`
- `"checker"`: `a`, `b`, `scale=[su, sv]` (default [8,8])
- `"grid"`: `a` (cell), `b` (line), `scale`, `width` (line width, cell fraction)
- `"image"`: `file` (lossless sRGB AVIF, relative to the scene file), `srgb` (default
  true), bilinear filtering

Returns the name, so `tex = s.texture("skin", "image", file=...)` and
`s.lambert("cow", texture=tex)` compose.

### Materials

Typed helpers (each returns the material name):

- `s.lambert(name, color | texture)`
- `s.metal(name, color | texture, roughness)` — GGX, `color` is F0,
  roughness 0 = mirror
- `s.dielectric(name, ior, absorb, roughness)` — glass; optional `absorb`
  [r,g,b] is Beer–Lambert absorption inside the glass (tinted interiors and
  tinted transparent shadows). `roughness` (default 0) turns it into frosted
  glass: GGX microfacet reflection + transmission (VNDF-sampled Walter BTDF);
  below 1e-3 it degenerates bit-exactly to the smooth delta interface. Shadow
  rays always treat the interface as smooth — roughness blurs what you see
  through the glass, never the shadow behind it (report ch16)
- `s.water(name, ior, absorb, wave_amp, wave_freq)` — 光滑电介质界面
  （`ior` 默认 **1.33**）+ 两件水特有的事——**fbm 波纹法线**（`wave_amp`
  默认 0.05、`wave_freq` 默认 2.0；只扰动着色法线不位移；波场定义在世界
  xz，水面按水平放置约定）与 **Beer–Lambert 水体吸收**（`absorb` 默认
  `[0.45, 0.08, 0.035]`，红先被吃 → 深水偏蓝绿；折射入水后按介质内路径
  长度衰减，直到再次穿出水面）。嵌套介质经介质栈与相对折射率支持（水中
  玻璃、玻璃中气泡；几何须良构嵌套——真包含、不穿插）；水下着色点的 NEE
  阴影线经菲涅尔 + Beer–Lambert 衰减穿过水面（超出 Snell 窗口的方向被
  全内反射挡住），水下能收到直接光。
- `s.plastic(name, color | texture, roughness)` — opaque diffuse base under
  a glossy dielectric coat (fixed IOR 1.5, F0 = 0.04); `color`/`texture`
  drives the base, `roughness` (default 0.15) shapes the coat highlight and
  is floored at 0.001 on the device — the coat is never a mirror (use
  metal/dielectric for that). Whites render ~10% below lambert: the
  bidirectional Fresnel coupling sends that energy into the highlight
  (report ch17)
- `s.emissive(name, color | texture, intensity, two_sided)` — `two_sided`
  defaults to false (only the front face emits). Emissive `rect`/`disk`/
  `sphere`/`mesh:NAME` objects are automatically sampled as area lights
  (unless `nee=False`). Sphere/disk area lights require uniform scale; mesh
  area lights sample triangles proportional to world-space area and accept
  any transform, non-uniform scale included. Textured emissives are
  supported as NEE lights for `rect`/`disk`/`mesh:NAME` (mesh uvs are
  per-vertex and interpolate identically on both MIS sides); a textured
  emissive `sphere` must use `nee=False` (its uv frame is object-space and
  cannot be light-sampled).

The generic `s.material(name, type, **fields)` accepts any documented field
and is what the typed helpers delegate to.

### Meshes — `s.mesh(name, obj, normals, usemtl)`

Loads an OBJ (path relative to the scene file; `vt` texture coordinates are
honored, smooth area-weighted normals by default — pass anything other than
`"smooth"` for flat geometric normals). Returns `"mesh:name"`, ready to use
as a shape.

`usemtl="GroupName"` loads only that material group of a multi-material OBJ
(the `.mtl` next to the OBJ supplies the group names; a missing or empty
group is an error). One sundog material per object still holds — split a
multi-material model into per-group sub-meshes sharing one transform:

```python
POSE = [scale(1.0), rotate_y(18), translate(0, 0, -0.6)]
for grp, mat in [("GlassHead", "roboGlass"), ("MetalGrey", "roboMetal")]:
    s.add(s.mesh("robot_" + grp, "../assets/sparky.obj", usemtl=grp),
          mat, *POSE)
```

(`scenes/03-spot-atrium.py` splits Sparky's ten groups this way.)

### Objects — `s.add(shape, material, *steps, ...)`

```python
s.add("rect", "gold", scale(2, 1, 2), rotate_x(90), translate(0, 2, 0),
      material_back=None, cutout="gearTex", nee=False,
      physics=rigid_body(density=250, velocity=[0, -2, 0]))
```

Canonical shapes (object space):

| shape | definition | front face | UV |
|---|---|---|---|
| `sphere` | r=1 at origin | outside | spherical (u: azimuth, v: latitude) |
| `rect` | XZ square [-1,1]^2 at y=0 | +Y side | u=(x+1)/2, v=(z+1)/2 |
| `disk` | XZ disk r<=1 at y=0 | +Y side | u: azimuth, v: radius |
| `cylinder` | x^2+z^2=1, y in [-1,1], **open ends** | outside | u: azimuth, v: height |
| `parabola` | y = (x^2+z^2)/2, r<=1 (focus at (0,0.5,0)) | **convex underside** | u: azimuth, v: radius |
| `mesh:NAME` | triangles from `s.mesh(NAME, …)` | winding side | OBJ `vt` per-vertex; barycentric fallback without `vt` |

An emissive mesh auto-registers as an NEE area light (see `s.emissive`
above); one-sided emission follows the winding-side front face — set
`two_sided=True` on the material to emit from both. A dynamic rigid body
cannot also be an NEE light (same rule as the other shapes): set
`nee=False` or keep it static.

To make a *concave* parabola reflector (dish), put the mirror on
`material_back` (the bowl side) and set `material=None`… or rotate the
bowl to aim its convex side away. Remember: `material` is the front face.

- `material` (front face). `material_back`: omitted = same as front;
  `None` = **pass-through** (both rays and shadows go straight through when
  hitting that side). `material` may itself be `None` when `material_back`
  is set (back-only surface).
- `*steps` — transform constructors, composed **top-down in object space**:
  `scale(s)` / `scale(x, y, z)`, `rotate_x/y/z(degrees)`,
  `translate(x, y, z)`. `scale, rotate, translate` order means
  translate(rotate(scale(p))). Any sequence and repetition is allowed;
  non-uniform scale is fine (normals handled correctly). No steps =
  identity.
- `cutout`: texture name; alpha < 0.5 makes holes (both faces).
- `nee=False` keeps an emissive object out of explicit light sampling
  (it then only contributes when a path hits it).
- `physics`: see below.

### Lights (explicit delta lights)

```python
s.point_light(position=[x, y, z], intensity=[r, g, b], radius=0.3)
s.distant_light(direction=[x, y, z], radiance=[r, g, b])
```

`point` has a radius for soft shadows; `intensity` is radiant intensity
(falls off with 1/d^2). `distant` is a parallel light. Area lights are
*not* declared here — make an emissive object instead.

### `s.flame(...)`（体积火焰光源）

程序化火焰——sundog 的第一类**参与介质**（发射 + 吸收，无散射）：raygen 在
每段光线上与火焰的竖直包围圆柱解析求交，行进积分噪声塑形的发射场（黑→深红
→橙→黄白梯度），并按透射率衰减其后的一切贡献。火焰因此"看得见"（含反射/
折射里的像），也会遮挡（烟雾感吸收）。

```python
s.flame(base=[0, 0.12, 0], height=1.7, radius=0.5,
        intensity=22, sigma=4.5, noise_scale=3.0, seed=3,
        light_intensity=40)
```

- `base`/`height`/`radius`（必填）：火根位置、火高、包围圆柱半径（轴恒为 +y）
- `intensity`（默认 20）发射亮度；`sigma`（默认 4）吸收密度；
  `noise_scale`（默认 3）火舌噪声频率；`seed`（默认 0）多团火去相关
- `light_intensity`（默认 12）：**照明近似**——每团火自动注册 2 个带半径的
  暖色点光（轴上 0.35H/0.70H 处，半径 0.3R 软阴影），经常规 NEE 照亮场景。
  体积发射本身被 BSDF 路径偶然穿过时也会记入，与点光有轻微能量重复——
  火焰立体角小、σ 小，量级可忽略，此为如实声明的工程近似
- **体积阴影**：NEE 阴影线按火焰透射率衰减——火焰投影、纯吸收烟柱
  （`intensity=0`）遮光；内嵌点光对**自己宿主**的火焰豁免（其
  `light_intensity` 已按逃逸后的发射校准，见报告第 12 章 §12.6），穿越
  其他火焰照常衰减。随 `transparent_shadows` 门控（`--opaque-shadows`
  同时关闭表面透射与体积衰减）
- 限制：无散射（烟雾类介质渲不了）；火焰不写 AOV 引导层

### `s.physics(...)` + per-object opt-in（PhysX GPU 刚体沉降）

调用了 `s.physics(...)` 的场景在加载时先跑一遍 **PhysX 5 GPU** 刚体模拟
（`eENABLE_GPU_DYNAMICS` + GPU 宽相，无 CPU 回退）：对象的 transform 是
**初始位姿**，模拟到全部刚体休眠（或 `max_time` 超时）后，最终位姿被烘焙回
每个对象的变换，之后照常构建加速结构与渲染。堆叠、倚靠、翻倒的形态因此不
需要手工摆放——`scenes/06-spot-cascade.py` 就是 512 只 Spot 倾泻进房间的
例子。

```python
s.physics(gravity=[0, -9.81, 0], timestep=0.0041667, max_time=15.0,
          friction=0.6, restitution=0.1, solver_iterations=[8, 2],
          sleep_threshold=0.05, stop_time=0)
```

`stop_time > 0` 是**锐利定格**模式：模拟恰好推进到该时刻（秒）就停下烘焙，
凌空翻滚的刚体原样冻结进画面；`0`/缺省 = 模拟到全体休眠（或 `max_time`
超时）。CLI `--physics-time F` 可覆盖场景值（`0` 强制回沉降模式）——画廊
主图 06 就是同一场景在 `--physics-time 1.0` 下的定格，对照图则是沉降态。

对象通过 `physics=` 关键字 **显式 opt-in**（没有的对象不参与碰撞）：

```python
s.add("rect", "floor", scale(12, 1, 12),
      physics=static_body(thickness=0.5, friction=0.8))
s.add("mesh:spot", "spot_skin", scale(0.5), translate(0, 3, 0),
      physics=rigid_body(density=250, velocity=[0, -2, 0],
                         angular_velocity=[1, 0, -1]))
```

- `static_body(thickness, friction, restitution)`：静态碰撞体。
  `rigid_body(density, velocity, angular_velocity, friction, restitution)`：
  刚体。`friction`/`restitution` 缺省继承全局值。
- **形状支持**：`sphere`（静/动）、`mesh:*`（静/动，凸包近似——顶点上限 64 的
  convex hull，共享网格只烹制一次，实例用 `PxMeshScale` 缩放）、`rect`
  （**仅静态**：按正面 +Y 往背面挤出 `thickness`（默认 0.2）厚的实体板，
  零厚度平面才不会被穿透）。`disk`/`cylinder`/`parabola` 不支持，emit 期
  报错。
- **变换约束**：参与对象的变换必须可分解为 平移·旋转·缩放（无剪切、无镜像）；
  刚体还要求**均匀缩放**。
- **限制**：`rigid_body` 不能用在自动注册为 NEE 面光源的发光对象上
  （光源采样框架在解析期烘焙）——要么保持静态，要么 `nee=False`。
- **决定性**：固定步长、固定迭代数、按 `s.add` 顺序建 actor；同一 GPU/驱动
  上跑间可复现（与渲染的决定性同一口径），跨机器不承诺。模拟耗时计入
  `--stats` 的 `timings_ms.physics`，不影响 `render` 口径。

## Validation & errors

`scenelib` mirrors every hard constraint of the renderer's loader and
reports it at emit time with the scene-file line that registered the
offending item:

```
SceneError: objects[17] (12-molten-oracle.py:196): disk area light requires uniform XZ scale
```

Checked: shape/texture/material enums, unknown field names (typo
guard), references to unregistered materials/textures/meshes, double-null
faces, area-light rules (sphere/disk uniform scale; the textured-emissive-
sphere `nee=False` rule; a dynamic body cannot be an NEE light — emissive
meshes included; an alpha-cutout object cannot be an NEE light — NEE would
sample emission inside the holes), physics shape/dynamic constraints, and
numeric domains (flame/envmap/absorb/wave parameters).

## Determinism & reproducibility

- scenelib itself never draws randomness or timestamps; the backend call
  program is a pure function of the API calls.
- Scenes that use random layout own their seed: call `random.seed(N)` before
  the first draw (see `scenes/05-spot-swarm.py`). This layout seed is
  independent of `render(seed=…)`, the renderer's sampling seed.
- Object order is `s.add` call order — it is the scene order the renderer
  and PhysX see, so keep it stable.

## Asset paths

`texture(file=…)`, `background_envmap(file=…)` and `mesh(obj=…)` paths
resolve relative to **the scene file's own directory** (not the cwd) —
gallery scenes use `textures/…` and `../assets/…`. Absolute paths pass
through unchanged.

## Appendix: the C ABI (internal)

`Scene.run()` drives `src/sundog_api.h` — a C scene-construction API
(`sundog_add_material_*`/`sundog_add_object`/…): scalars cross as doubles
and are narrowed to float once at the boundary, omitted options cross as
NaN/NULL/-1 sentinels so renderer defaults stay renderer-side, and the call
order (config → objects → flames → lights) is enforced by the library. It is
an internal contract between scenelib and libsundog.so — not a user-facing
API, and it may change with the renderer. Tooling that needs scene data
works on `Scene.doc` in-process instead.

# Asset licenses / provenance

## spot.obj — Spot（卡通奶牛）

- **Origin:** "Spot", a cartoon cow model by Keenan Crane, from his 3D Model
  Repository: <https://www.cs.cmu.edu/~kmcrane/Projects/ModelRepository/>
  (archive `spot.zip`; this copy is `spot_triangulated.obj`, 5,856 triangles,
  with UV texture coordinates).
- **Texture:** `scenes/textures/spot_texture.png`（同一压缩包内的纹理图集，入库）。
- **Terms:** **CC0 1.0 Universal** — dedicated to the public domain; no
  attribution required ("acknowledgement is appreciated").
- **获取方式:** 入库（`assets/spot.obj` + `scenes/textures/spot_texture.png`；
  CC0 允许再分发，`fetch-assets.sh` 已不再下载网格）。
- **Use in this project:** rendering demo/benchmark scenes for the sundog
  path tracer.

## sparky.obj / sparky.mtl — Sparky（卡通机器人）

- **Origin:** AI 生成模型（项目作者提供，由生成式 3D 工具产出；7,284 三角形，
  带 UV，十个 usemtl 材质组）。
- **Texture:** `scenes/textures/sparky_albedo.png`（屏幕面板图集，入库）。
- **Terms:** 项目自有资产，随仓库分发。
- **获取方式:** 无外部下载源——OBJ/MTL/纹理全部入库
  （`assets/sparky.obj`、`assets/sparky.mtl`）。
- **Use in this project:** `scenes/03-spot-atrium.py` 的多材质网格演示
  （usemtl 组过滤：玻璃头罩/发光屏幕/金属关节/塑料壳）。

## capsule_mascot.obj / capsule_mascot.mtl — 胶囊吉祥物

- **Origin:** AI 生成模型（项目作者提供，由生成式 3D 工具产出，与
  sparky 同源做法；5,816 三角形、15 个 usemtl 材质组；无 vt/vn——UV
  回退重心坐标、法线由加载器按需生成，MTL 的 Kd 仅作各组配色意图
  参考，渲染器不读取）。
- **Terms:** 项目自有资产，随仓库分发。
- **获取方式:** 无外部下载源——OBJ/MTL 全部入库。
- **Use in this project:** `scenes/15-assembly-hall.py` 旗舰场景的黄色督工
  吉祥物（v0.16 起，塑料混搭材质映射）。

## box.obj / brick.obj — 程序化盒体网格

- **Origin:** 项目自制程序化网格（各 12 三角形、逐面 uv、无法线——由
  加载器按需生成）。box 为单位立方（y 0..1），brick 预拉伸为
  1.8×0.8×0.9 砖比例：动态刚体要求均匀缩放，长宽比只能内嵌在网格里；
  渲染器无 box 图元且 rect 碰撞体仅限静态，PhysX 落体的积木必须走
  mesh 凸包。
- **Terms:** 项目自有资产，随仓库分发。
- **Use in this project:** `scenes/16-atelier.py` 落体积木。

## gear.png / runes.png — 封面场景程序化纹理

- **Origin:** 项目自制程序化纹理（12 号场景的齿轮 alpha 镂空与符文
  发光图集），静态资产入库于 `scenes/textures/`；一次性生成器已随
  v0.12.1 清理移除，可在 git 历史找 `scripts/gen_cover_textures.py`。
- **Terms:** 项目自有资产，随仓库分发。

## kloofendal_48d_partly_cloudy_puresky_4k.hdr — 晴日天空 HDRI

- **Origin:** "Kloofendal 48d Partly Cloudy (Pure Sky)" by Greg Zaal,
  Poly Haven: <https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky>
  （4k equirectangular Radiance HDR，4096×2048，太阳未削波）。
- **Terms:** **CC0 1.0 Universal** — public domain; no attribution required.
- **获取方式:** `scripts/fetch-assets.sh`（HDR 不入库，按需重新下载；直链
  `https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/kloofendal_48d_partly_cloudy_puresky_4k.hdr`）。
- **Use in this project:** 场景 10/11/12/15 的环境光照（IBL），技术报告
  第 14 章的重要性采样演示。

# Asset licenses / provenance

## spot.obj — Spot（卡通奶牛）

- **Origin:** "Spot", a cartoon cow model by Keenan Crane, from his 3D Model
  Repository: <https://www.cs.cmu.edu/~kmcrane/Projects/ModelRepository/>
  (archive `spot.zip`; this copy is `spot_triangulated.obj`, 5,856 triangles,
  with UV texture coordinates).
- **Texture:** `scenes/textures/spot_texture.png`（同一压缩包内的纹理图集，入库）。
- **Terms:** **CC0 1.0 Universal** — dedicated to the public domain; no
  attribution required ("acknowledgement is appreciated").
- **获取方式:** `scripts/fetch-assets.sh`（OBJ 不入库，按需重新下载；纹理入库）。
- **Use in this project:** rendering demo/benchmark scenes for the sundog
  path tracer.

## kloofendal_48d_partly_cloudy_puresky_4k.hdr — 晴日天空 HDRI

- **Origin:** "Kloofendal 48d Partly Cloudy (Pure Sky)" by Greg Zaal,
  Poly Haven: <https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky>
  （4k equirectangular Radiance HDR，4096×2048，太阳未削波）。
- **Terms:** **CC0 1.0 Universal** — public domain; no attribution required.
- **获取方式:** `scripts/fetch-assets.sh`（HDR 不入库，按需重新下载；直链
  `https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/kloofendal_48d_partly_cloudy_puresky_4k.hdr`）。
- **Use in this project:** `scenes/10-suncatcher.json` 的环境光照（IBL），
  技术报告第 15 章的重要性采样演示。

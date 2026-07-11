# sundog 画廊

由 `scripts/render-gallery.sh` 生成于 2026-07-11。
正式图入库于 `docs/gallery/`（无损重压缩的 1080p PNG）；渲染原件在 `out/gallery/`（不入库）。

## 01-prism-court

![01-prism-court](gallery/01-prism-court.png)

黄昏渐变天空下的棱镜庭院：玻璃立方、抛光镜面与四档粗糙度的金属球，考验折射、多次镜面反弹与 GGX 高光。

## 02-cornell-lume

![02-cornell-lume](gallery/02-cornell-lume.png)

Cornell 盒变体：暖色小面积主灯加冷色低强度月光球，四档粗糙度钢球，NEE+MIS 在小光源下的收敛能力一目了然。

## 03-spot-atrium

![03-spot-atrium](gallery/03-spot-atrium.png)

网格地板中庭里的三只 Spot 卡通奶牛（原生纹理 / 金 / 玻璃，各 5,856 三角形），硬件三角形求交、OBJ UV 纹理与平滑法线。

## 03-spot-atrium-spp32-denoised

![03-spot-atrium-spp32-denoised](gallery/03-spot-atrium-spp32-denoised.png)

同一场景仅 32 spp + OptiX AI 降噪（albedo/normal 引导）——低采样即可得到干净画面。

## 03-spot-atrium-spp32-raw

![03-spot-atrium-spp32-raw](gallery/03-spot-atrium-spp32-raw.png)

对照组：同样 32 spp、不降噪的原始蒙特卡洛噪点。

## 04-parabolica

![04-parabolica](gallery/04-parabolica.png)

夜景抛物面聚光：金色抛物碟（背面材质成像）把发光灯珠聚成一道光束扫过暗色地面，展示 parabola 自定义求交与双面材质语义。

## 05-spot-swarm

![05-spot-swarm](gallery/05-spot-swarm.png)

32768 个实例化 Spot 卡通奶牛的阵列（约 1.9 亿等效三角形）——同一份三角形 GAS 通过 IAS 实例复用，展示单层实例化的规模能力。

## 渲染统计

| 图像 | 分辨率 | spp | 降噪 | 渲染时间 (s) | Mrays/s | 峰值显存 (MB) |
|---|---|---|---|---|---|---|
| 01-prism-court | 1920x1080 | 512 | 否 | 0.70 | 5197 | 690 |
| 02-cornell-lume | 1920x1080 | 512 | 否 | 1.25 | 5361 | 690 |
| 03-spot-atrium | 1920x1080 | 256 | 否 | 0.21 | 6785 | 694 |
| 03-spot-atrium-spp32-denoised | 1920x1080 | 32 | 是 | 0.03 | 6799 | 696 |
| 03-spot-atrium-spp32-raw | 1920x1080 | 32 | 否 | 0.03 | 6813 | 694 |
| 04-parabolica | 1920x1080 | 512 | 否 | 0.38 | 6619 | 694 |
| 05-spot-swarm | 1920x1080 | 128 | 否 | 0.18 | 4136 | 708 |

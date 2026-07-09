# sundog 基准

由 `scripts/run-benchmark.sh` 生成于 2026-07-09T01:21:52+00:00。GPU：NVIDIA GeForce RTX 5090；
CPU 基线：cxxrt（`/tmp/cxxrt-baseline`，OMP_NUM_THREADS=16，-O3 -march=native）。

## A. compat 层 — cxxrt CPU vs sundog GPU

同一场景（cxxrt example 的 1:1 JSON 移植），GPU 端 `--parity --gamma 2.0
--clamp 0 --max-depth 50`，1024x1024。两边计时都只含渲染循环（CPU 取其
"Rendering elapsed time" 输出，GPU 取 stats 的 `timings_ms.render`）。

| 场景 | spp | CPU (s) | GPU (s) | 加速比 | GPU Mrays/s |
|---|---|---|---|---|---|
| compat-01 | 16 | 0.75 | 0.007 | 101.0 | 4955 |
| compat-01 | 256 | 11.91 | 0.082 | 144.4 | 7153 |
| compat-03 | 16 | 0.46 | 0.004 | 101.2 | 8936 |
| compat-03 | 256 | 7.28 | 0.071 | 101.9 | 8996 |

## B. 特性层 — 画廊场景（960x540 / 64 spp / 不降噪）

| 场景 | 物体 | 三角形 | 灯 | 渲染 (s) | Mrays/s | 峰值显存 (MB) |
|---|---|---|---|---|---|---|
| 01-prism-court | 22 | 0 | 1 | 0.027 | 4230 | 594 |
| 02-cornell-lume | 11 | 0 | 2 | 0.045 | 4667 | 594 |
| 03-bunny-atrium | 4 | 144046 | 2 | 0.014 | 6636 | 600 |
| 04-parabolica | 6 | 0 | 3 | 0.012 | 6660 | 598 |
| 05-bunny-swarm | 4098 | 144046 | 2 | 0.021 | 3869 | 602 |

## C. 降噪层 — 02-cornell-lume（960x540，参考 4096 spp）

| 图像 | spp | 降噪 | PSNR vs 参考 (dB) |
|---|---|---|---|
| 原始蒙特卡洛 | 16 | 否 | 26.13 |
| OptiX AI 降噪 | 16 | 是 | 42.32 |


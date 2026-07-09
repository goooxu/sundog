# 第三方组件

## 编入 sundog 的库（`extern/`）

| 组件 | 用途 | 许可 |
|---|---|---|
| [nlohmann/json](https://github.com/nlohmann/json)（`json.hpp`） | 场景 JSON、stats 输出 | MIT |
| [stb](https://github.com/nothings/stb)（`stb_image.h`、`stb_image_write.h`） | 纹理读取、PNG 输出、img_compare | public domain（Unlicense）/ MIT 双许可 |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader)（`tiny_obj_loader.h`） | OBJ 网格加载 | MIT |

## 仅 CPU 基线使用（不进入 sundog 二进制）

| 组件 | 用途 | 许可 |
|---|---|---|
| [lodepng](https://github.com/lvandeve/lodepng) | cxxrt CPU 基线的 PNG 编码（`scripts/build-cpu-baseline.sh` 克隆到 `/tmp/cxxrt-baseline/3rd-party/lodepng`） | zlib |

## 资产

- **Stanford Bunny**（`assets/bunny.obj`）：Stanford University Computer
  Graphics Laboratory 扫描重建（Turk & Levoy, 1994），OBJ 打包取自 Morgan
  McGuire 的 Computer Graphics Archive。仅用于研究/开发场景渲染，须署名，
  详见 [assets/LICENSES.md](assets/LICENSES.md)。

## 运行时依赖（不随仓库分发）

- NVIDIA CUDA Toolkit 13.0（`scripts/setup-testbox.sh` 用户态安装）
- NVIDIA OptiX SDK 9.1.0（仅头文件；运行时实现在显卡驱动内）

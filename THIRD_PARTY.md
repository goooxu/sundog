# 第三方组件

## 编入 sundog 的库（`extern/`）

| 组件 | 用途 | 许可 |
|---|---|---|
| [nlohmann/json](https://github.com/nlohmann/json)（`json.hpp`） | 场景 JSON、stats 输出 | MIT |
| [stb](https://github.com/nothings/stb)（`stb_image.h`、`stb_image_write.h`） | 纹理读取、PNG 输出、img_compare | public domain（Unlicense）/ MIT 双许可 |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader)（`tiny_obj_loader.h`） | OBJ 网格加载 | MIT |

## 资产

- **Spot**（`assets/spot.obj` + `scenes/textures/spot_texture.png`）：
  Keenan Crane 的卡通奶牛模型，CC0 1.0（公有领域）。OBJ 不入库，由
  `scripts/fetch-assets.sh` 下载，详见 [assets/LICENSES.md](assets/LICENSES.md)。

## 运行时依赖（不随仓库分发）

- NVIDIA CUDA Toolkit 13.0（`scripts/setup-testbox.sh` 用户态安装）
- NVIDIA OptiX SDK 9.1.0（仅头文件；运行时实现在显卡驱动内）
- NVIDIA PhysX 5.8.0（BSD-3-Clause，GPU 内核开源；`setup-testbox.sh` 从
  NFS 源码包构建静态库 + `libPhysXGpu_64.so`，产物 tarball 缓存回 NFS）

# 第三方组件

## 编入 sundog 的库（`extern/`）

| 组件 | 用途 | 许可 |
|---|---|---|
| [stb](https://github.com/nothings/stb)（`stb_image.h`） | Radiance .hdr 环境贴图解码、img2avif 的 SDR 图读取 | public domain（Unlicense）/ MIT 双许可 |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader)（`tiny_obj_loader.h`） | OBJ 网格加载 | MIT |

## 资产

- **Spot**（`assets/spot.obj` + `scenes/textures/spot_texture.avif`）：
  Keenan Crane 的卡通奶牛模型，CC0 1.0（公有领域）。OBJ 不入库，由
  `scripts/fetch-assets.sh` 下载，详见 [assets/LICENSES.md](assets/LICENSES.md)。

## BakingLab — ACES 色调映射拟合

- **来源：** Stephen Hill 的 ACES RRT+ODT 拟合（"ACESFitted"），常数取自
  MJP 的 BakingLab（<https://github.com/TheRealMJP/BakingLab>，`ACES.hlsl`）。
- **许可：** MIT。
- **用途：** `src/tonemap.h` 的输出色调映射（输入/输出 3×3 矩阵与有理拟合
  常数照录；实现为独立 C++ 改写）。

## 构建时静态链接（不随仓库分发源码）

- [libavif](https://github.com/AOMediaCodec/libavif) 1.1.1（BSD-2-Clause）——
  HDR AVIF 编码/解码（渲染输出、纹理输入、img_compare/img2avif）；
- [libaom](https://aomedia.googlesource.com/aom/) 3.9.1（BSD-2-Clause +
  AOM 专利授权）——AV1 编解码器后端。
  两者由 `scripts/setup-testbox.sh` 按架构从源码静态构建，产物 tarball
  缓存回 NFS（同 PhysX 模式）。

## 运行时依赖（不随仓库分发）

- NVIDIA CUDA Toolkit 13.0（`scripts/setup-testbox.sh` 用户态安装）
- NVIDIA OptiX SDK 9.1.0（仅头文件；运行时实现在显卡驱动内）
- NVIDIA PhysX 5.8.0（BSD-3-Clause，GPU 内核开源；`setup-testbox.sh` 从
  NFS 源码包构建静态库 + `libPhysXGpu_64.so`，产物 tarball 缓存回 NFS）

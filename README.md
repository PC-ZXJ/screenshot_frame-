# 截图套壳 Screenshot Framer（源码）

自动为系统截图（PNG/JPG）套上设备外壳的 root 模块（KernelSU / APatch / Magisk）。

## 目录

- `module/`：模块本体（脚本、`webroot/` 界面、`module.prop`、`defaults/`）。`bin/frame` 由构建生成，不在源码里。
- `src/`：核心程序 `frame` 的 C 源码（`frame.c`，依赖 `lodepng`，JPG 用 stb 单头文件，`build.sh` 会自动下载）。
- `build_module.sh`：一键编译并打包模块 zip。

## 构建（Termux）

```
pkg install clang curl zip
sh build_module.sh
```

生成 `screenshot_frame_<版本>.zip`，可直接在管理器里刷入。版本号改 `module/module.prop`。

## 第三方组件

- LodePNG（zlib 许可）
- stb_image、stb_image_write（公有领域）

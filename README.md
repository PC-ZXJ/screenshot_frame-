# 截图套壳 Screenshot Framer

自动为系统截图（PNG/JPG）套上设备外壳的 root 模块（KernelSU / APatch / Magisk）。
支持的管理器：KernelSU 及其衍生版（如 KernelSU Next、SukiSU Ultra）、APatch，可直接在管理器中打开本界面；Magisk 自身没有模块界面，需借助 MMRL 等支持 WebUI 的工具打开。目前仅在 KernelSU 上测试，其他管理器未逐一验证。

## 主要功能

自动监听截图文件夹，截图后数秒内完成套壳并原位替换。
截图文件夹可自动检测或自定义，适配不同品牌的系统，修改后立即生效。
PNG 截图无损输出：截图像素 1:1 保留，不重新压缩；也可在设置中改为按壳图尺寸输出。
自动识别壳图的屏幕区域（含圆角与挖孔），更换机型无需手动测量。
JPG 截图套壳后以最高画质重新保存（JPG 本身有损），壳图外的透明区域填白。
仅处理全屏截图；区域截图、长截图等尺寸不符的会自动跳过。
支持横屏截图，自动旋转壳图。
支持多套壳图，按截图分辨率自动匹配。
可视化校准，支持 1 像素级偏移微调。
壳图在界面内导入，模块自动保存，无需手动创建文件夹。

## 构建（Termux）

```
pkg install clang curl zip
sh build_module.sh
```



## 第三方组件

- LodePNG（zlib 许可）
- stb_image、stb_image_write（公有领域）

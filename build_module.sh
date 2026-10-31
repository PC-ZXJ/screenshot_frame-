#!/bin/sh
# Termux 一键构建：编译核心程序 frame，打包视频处理组件 ffmpeg，并生成可安装的模块 zip。
#   用法：sh build_module.sh
# 需要：clang、curl、zip（pkg install clang curl zip）；首次会自动 pkg install ffmpeg binutils
cd "$(dirname "$0")" || exit 1
sh src/build.sh || exit 1
mkdir -p module/bin
cp -f src/frame module/bin/frame || exit 1
if [ ! -x module/bin/ffmpeg/ffmpeg ]; then
  sh src/bundle_ffmpeg.sh || exit 1
fi
ver=$(grep '^version=' module/module.prop | cut -d= -f2 | tr -d '\r' | tr . _)
out="screenshot_frame_${ver}.zip"
rm -f "$out"
(cd module && zip -qr "../$out" . -x 'bin/.keep') || exit 1
ls -l "$out"

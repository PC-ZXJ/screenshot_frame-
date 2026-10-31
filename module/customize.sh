# Installer script (KernelSU / Magisk / APatch). Runs after the files are extracted to $MODPATH.
DATA=/data/adb/screenshot_frame

ui_print "- Screenshot Framer 截图套壳"
[ "$ARCH" = "arm64" ] || abort "! 仅支持 arm64 设备（当前：$ARCH）"

mkdir -p "$DATA/profiles" "$DATA/bin" "$DATA/frames"
# keep the user's settings, frames and profiles on upgrade
[ -f "$DATA/config" ] || cp "$MODPATH/defaults/config" "$DATA/config"

if [ -f "$MODPATH/bin/frame" ]; then
  cp -f "$MODPATH/bin/frame" "$DATA/bin/frame"
  chmod 755 "$DATA/bin/frame"
  ui_print "- 核心程序已就绪"
else
  ui_print "! 安装包缺少核心程序 bin/frame"
fi

if [ -d "$MODPATH/bin/ffmpeg" ]; then
  rm -rf "$DATA/bin/ffmpeg"
  cp -rf "$MODPATH/bin/ffmpeg" "$DATA/bin/ffmpeg"
  chmod -R 755 "$DATA/bin/ffmpeg"
  if LD_LIBRARY_PATH="$DATA/bin/ffmpeg/lib" "$DATA/bin/ffmpeg/ffmpeg" -version >/dev/null 2>&1; then
    ui_print "- 视频处理组件已就绪（录屏套壳）"
  else
    ui_print "! 视频处理组件无法运行，录屏套壳不可用（截图套壳不受影响）"
  fi
else
  ui_print "! 安装包缺少视频处理组件，录屏套壳不可用（截图套壳不受影响）"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
for f in "$MODPATH"/*.sh; do set_perm "$f" 0 0 0755; done
if [ -d "$MODPATH/bin/ffmpeg" ]; then
  set_perm_recursive "$MODPATH/bin/ffmpeg" 0 0 0755 0755
fi
[ -f "$MODPATH/bin/frame" ] && set_perm "$MODPATH/bin/frame" 0 0 0755
ui_print "- 安装完成，重启后生效；之后在管理器中打开本模块的界面添加壳图"

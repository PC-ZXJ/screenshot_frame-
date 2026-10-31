#!/system/bin/sh
# Prints the folders that hold screen recordings, one per line.
#   recdirs.sh         the configured folder (rec_dir); if empty, every folder found automatically
#   recdirs.sh --scan  ignore the setting, list every folder found automatically
DATA=/data/adb/screenshot_frame
. "$DATA/config" 2>/dev/null
if [ "$1" != "--scan" ] && [ -n "$rec_dir" ]; then
  echo "$rec_dir"
  exit 0
fi
{
  for c in "/sdcard/DCIM/Screen recordings" "/sdcard/Pictures/Screen recordings" "/sdcard/Movies/Screen recordings" \
           "/sdcard/DCIM/ScreenRecorder" "/sdcard/Movies/ScreenRecorder" "/sdcard/Movies/Screenrecords" \
           "/sdcard/Pictures/Screenrecords" "/sdcard/Movies/screenrecorder" "/sdcard/DCIM/Screenrecorder"; do
    [ -d "$c" ] && echo "$c"
  done
  find /sdcard -maxdepth 3 -path /sdcard/Android -prune -o -type d \
    \( -iname '*screen*record*' -o -name '*录屏*' -o -name '*屏幕录制*' \) -print 2>/dev/null
} | sort -u | head -n 6

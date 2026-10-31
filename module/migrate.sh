#!/system/bin/sh
# One-time move of frames from the old shared folder into the module's own folder.
DATA=${SF_DATA:-/data/adb/screenshot_frame}
OLD=${SF_OLD_FRAMES:-/sdcard/Pictures/ScreenshotFrames}
NEW="$DATA/frames"
mkdir -p "$NEW"
. "$DATA/config" 2>/dev/null
[ "$frames_dir" = "$OLD" ] || exit 0
for f in "$OLD"/*.png "$OLD"/*.PNG; do
  [ -f "$f" ] && [ ! -e "$NEW/${f##*/}" ] && cp "$f" "$NEW/"
done
sed -i "s#^frames_dir=.*#frames_dir='$NEW'#" "$DATA/config"

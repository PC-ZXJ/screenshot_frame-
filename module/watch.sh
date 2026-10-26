#!/system/bin/sh
# (Re)starts the screenshot-folder listener from config. Usage: watch.sh [--fg]
#   --fg  run the listener in this process (used by service.sh at boot)
#   none  start it in the background (used by the settings page after the folder changed)
MODDIR=${0%/*}
FG=0; [ "$1" = "--fg" ] && FG=1
DATA=/data/adb/screenshot_frame
. "$DATA/config" 2>/dev/null
[ -n "$watch_dir" ] || watch_dir=/sdcard/Pictures/Screenshots
[ -n "$frames_dir" ] || frames_dir="$DATA/frames"
mkdir -p "$watch_dir" "$frames_dir" 2>/dev/null
rm -f "$watch_dir"/.frame_tmp_* 2>/dev/null

# keep a single listener: stop older ones started from this module
for pid in $(pidof inotifyd); do
  tr '\0' ' ' < /proc/$pid/cmdline 2>/dev/null | grep -q "$MODDIR/handler.sh" && kill "$pid" 2>/dev/null
done

# The system writes files through the lower storage path; events there are reliable,
# events on /sdcard are not. Watch both, duplicates are harmless.
set -- "$MODDIR/handler.sh"
case "$watch_dir" in /sdcard/*) l="/data/media/0/${watch_dir#/sdcard/}" ;; *) l= ;; esac
[ -n "$l" ] && [ -d "$l" ] && set -- "$@" "$l:wn"
set -- "$@" "$watch_dir:wn"
echo "$(date '+%F %T') service started, watching: $*" >> "$DATA/log.txt"

if [ "$FG" = "1" ]; then
  exec inotifyd "$@"
fi
if command -v setsid >/dev/null 2>&1; then
  ( setsid nohup inotifyd "$@" </dev/null >/dev/null 2>&1 & )
else
  ( nohup inotifyd "$@" </dev/null >/dev/null 2>&1 & )
fi
exit 0

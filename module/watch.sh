#!/system/bin/sh
# (Re)starts the screenshot-folder listener from config. Usage: watch.sh [--fg]
#   --fg  run the listener in this process (used by service.sh at boot)
#   none  start it in the background (used by the settings page after the folder changed)
MODDIR=${0%/*}
FG=0; [ "$1" = "--fg" ] && FG=1
DATA=/data/adb/screenshot_frame

# When started from the settings page this shell is a child of the manager app, and the
# system freezes the app's whole process group once it goes to the background (the
# listeners then stop reacting until the app is opened again). Move out of that group first;
# everything started below inherits the root group.
cg_before=$(head -n 1 /proc/$$/cgroup 2>/dev/null)
for cg in /sys/fs/cgroup/cgroup.procs /dev/cg2_bpf/cgroup.procs; do
  [ -w "$cg" ] && echo $$ > "$cg" 2>/dev/null && break
done
cg_after=$(head -n 1 /proc/$$/cgroup 2>/dev/null)
. "$DATA/config" 2>/dev/null
[ -n "$watch_dir" ] || watch_dir=/sdcard/Pictures/Screenshots
[ -n "$frames_dir" ] || frames_dir="$DATA/frames"
mkdir -p "$watch_dir" "$frames_dir" 2>/dev/null
rm -f "$watch_dir"/.frame_tmp_* 2>/dev/null

echo "$(date '+%F %T') cgroup: ${cg_before:-?} -> ${cg_after:-?}" >> "$DATA/log.txt"

# keep a single listener: stop older ones started from this module
for pid in $(pidof inotifyd); do
  tr '\0' ' ' < /proc/$pid/cmdline 2>/dev/null | grep -Eq "$MODDIR/(v?handler|rescan).sh" && kill "$pid" 2>/dev/null
done

# screen recordings: watch the configured folder, else every folder found (see recdirs.sh)
start_bg() {
  if command -v setsid >/dev/null 2>&1; then
    ( setsid nohup inotifyd "$@" </dev/null >/dev/null 2>&1 & )
  else
    ( nohup inotifyd "$@" </dev/null >/dev/null 2>&1 & )
  fi
}
rec_listener() {
  dirs=$(sh "$MODDIR/recdirs.sh")
  set -- "$MODDIR/vhandler.sh"
  n=0
  while IFS= read -r d; do
    [ -n "$d" ] || continue
    mkdir -p "$d" 2>/dev/null
    rm -f "$d"/.frame_tmp_* 2>/dev/null
    case "$d" in /sdcard/*) l="/data/media/0/${d#/sdcard/}" ;; *) l= ;; esac
    [ -n "$l" ] && [ -d "$l" ] && set -- "$@" "$l:wy"
    set -- "$@" "$d:wy"
    n=$((n + 1))
  done <<EOT
$dirs
EOT
  if [ $n -gt 0 ]; then
    echo "$(date '+%F %T') recording listener started, watching: $dirs" | tr '\n' ' ' >> "$DATA/log.txt"; echo >> "$DATA/log.txt"
    start_bg "$@"
  else
    # no recording folder yet: wait for the system to create one
    echo "$(date '+%F %T') no recording folder found yet, waiting for the first recording" >> "$DATA/log.txt"
    set -- "$MODDIR/rescan.sh"
    for p in /sdcard/DCIM /sdcard/Pictures /sdcard/Movies; do
      [ -d "$p" ] && set -- "$@" "$p:n"
    done
    start_bg "$@"
  fi
}
rec_listener

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

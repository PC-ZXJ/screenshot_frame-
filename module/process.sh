#!/system/bin/sh
# Frames one screenshot in place. Usage: process.sh /path/to/Screenshot.png
MODDIR=${0%/*}
DATA=${SF_DATA:-/data/adb/screenshot_frame}
PROFILES="$DATA/profiles"
LOG="$DATA/log.txt"
DONE="$DATA/done.txt"
BIN="$DATA/bin/frame"
[ -x "$BIN" ] || BIN="$MODDIR/bin/frame"

. "$MODDIR/lib.sh"

# png_size FILE -> sets W H from the PNG header
png_size() {
  set -- $(od -An -tu1 -j16 -N8 "$1" 2>/dev/null)
  [ $# -eq 8 ] || return 1
  W=$(( $1 * 16777216 + $2 * 65536 + $3 * 256 + $4 ))
  H=$(( $5 * 16777216 + $6 * 65536 + $7 * 256 + $8 ))
}

# img_size FILE -> sets W H (PNG header read in shell; JPEG and anything else via the core program)
img_size() {
  case "$1" in
    *.[pP][nN][gG]) png_size "$1" ;;
    *)
      set -- $("$BIN" size "$1" 2>/dev/null)
      [ $# -eq 2 ] || return 1
      W=$1; H=$2
      ;;
  esac
}

# size_match W H: true when W x H equals the screenshot size (either orientation)
size_match() {
  [ -n "$1" ] && [ -n "$2" ] || return 1
  { [ "$1" = "$SW" ] && [ "$2" = "$SH" ]; } || { [ "$1" = "$SH" ] && [ "$2" = "$SW" ]; }
}

CLAIM=
HAVE_LOCK=
LOCK="$DATA/lock"
cleanup() {
  [ -n "$HAVE_LOCK" ] && rmdir "$LOCK" 2>/dev/null
  [ -n "$CLAIM" ] && rmdir "$CLAIM" 2>/dev/null
}
trap cleanup EXIT

mkdir -p "$DATA"
. "$DATA/config" 2>/dev/null
# per-channel settings (screenshots); empty = use the general value
[ -n "$shot_res" ] && res=$shot_res
[ -n "$shot_mode" ] && mode=$shot_mode
[ -n "$shot_rot" ] && rot=$shot_rot
[ "$enabled" = "1" ] || exit 0
[ -x "$BIN" ] || { log "frame binary missing: $BIN"; exit 1; }

if [ "$1" = "--wait" ]; then
  # event-driven mode: the final file may not exist yet (.pending rename), so wait for it.
  # Only one waiter per file name.
  shift
  f="$1"
  mkdir -p "$DATA/claims"
  c="$DATA/claims/$(echo "${f##*/}" | cksum | cut -d' ' -f1)"
  mkdir "$c" 2>/dev/null || exit 0
  CLAIM=$c
  i=0
  while [ ! -f "$f" ] && [ $i -lt 120 ]; do i=$((i + 1)); sleep 0.5; done
else
  f="$1"
fi
[ -f "$f" ] || exit 0

# one screenshot at a time (events can arrive twice)
n=0
while ! mkdir "$LOCK" 2>/dev/null; do
  n=$((n + 1))
  if [ $n -gt 100 ]; then rm -rf "$LOCK"; fi   # stale lock after ~30s
  sleep 0.3
done
HAVE_LOCK=1

[ -f "$f" ] || exit 0

# wait until the file stops growing
prev=-1
for i in 1 2 3 4 5 6 7 8 9 10; do
  cur=$(stat -c %s "$f" 2>/dev/null || echo 0)
  [ "$cur" = "$prev" ] && [ "$cur" -gt 0 ] && break
  prev=$cur
  sleep 0.3
done

t_start=$(date +%s)
f_mtime=$(stat -c %Y "$f" 2>/dev/null || echo "$t_start")
img_size "$f" || { log "not a readable image: $f"; exit 0; }
SW=$W; SH=$H

# already processed by us? (same name and same byte size as right after framing)
key="${f##*/}|$(stat -c %s "$f" 2>/dev/null)"
grep -Fxq -- "$key" "$DONE" 2>/dev/null && exit 0

# Device resolution: a full-screen screenshot has exactly this size.
dm=$(wm size 2>/dev/null | tail -n 1 | sed 's/.*: *//')
dev_w=${dm%x*}; dev_h=${dm#*x}
case "$dev_w$dev_h" in *[!0-9]*|"") dev_w=; dev_h= ;; esac
# A manual screen resolution from the settings (screen_res=WxH) replaces the device's own.
case "$screen_res" in
  [0-9]*x[0-9]*)
    sr_w=${screen_res%x*}; sr_h=${screen_res#*x}
    case "$sr_w$sr_h" in *[!0-9]*|"") ;; *) dev_w=$sr_w; dev_h=$sr_h ;; esac ;;
esac

# Pick a profile for a full-screen screenshot (the screen resolution is one global setting, shared by
# all frames). Several candidates: the active one wins.
sel=; first=; have_profiles=
for p in "$PROFILES"/*.conf; do
  [ -f "$p" ] || continue
  have_profiles=1
  # a frame chosen for screenshots is the only candidate
  if [ -n "$shot_frame" ] && [ -f "$PROFILES/$shot_frame.conf" ] && [ "${p##*/}" != "$shot_frame.conf" ]; then continue; fi
  unset frame dx dy scale ref_w ref_h quad orient
  . "$p"
  if [ -z "$dev_w" ] || size_match "$dev_w" "$dev_h"; then
    [ -z "$first" ] && first=$p
    if [ -n "$active" ] && [ "${p##*/}" = "$active.conf" ]; then sel=$p; break; fi
  fi
done
[ -z "$sel" ] && sel=$first

unset frame dx dy scale ref_w ref_h quad orient
if [ -n "$sel" ]; then
  . "$sel"
elif [ -n "$have_profiles" ]; then
  # profiles exist but none matches: not a full-screen screenshot (region, scroll capture, ...)
  log "skipped ${f##*/} (${SW}x${SH}): not a full-screen size of any profile"
  record_done "$key"
  exit 0
elif [ -n "$dev_w" ] && ! size_match "$dev_w" "$dev_h"; then
  log "skipped ${f##*/} (${SW}x${SH}): not full-screen (device ${dev_w}x${dev_h})"
  record_done "$key"
  exit 0
fi
if [ -z "$frame" ]; then
  for c in "$frames_dir"/*.png "$frames_dir"/*.PNG; do
    [ -f "$c" ] && { frame=$c; break; }
  done
fi
[ -n "$frame" ] || { log "no frame image found in $frames_dir"; exit 1; }
case "$frame" in /*) ;; *) frame="$frames_dir/$frame" ;; esac
[ -f "$frame" ] || { log "frame image not found: $frame"; exit 1; }

# already framed (our own rename comes back as an event)?
if png_size "$frame"; then
  if { [ "$W" = "$SW" ] && [ "$H" = "$SH" ]; } || { [ "$W" = "$SH" ] && [ "$H" = "$SW" ]; }; then
    exit 0
  fi
fi

log "working ${f##*/} (${SW}x${SH}): 正在套壳"
dir=${f%/*}
ext=${f##*.}
tmp="$dir/.frame_tmp_$$.$ext"   # the output format follows this extension (png / jpg)
"$BIN" compose "$frame" "$f" "$tmp" --dx "${dx:-0}" --dy "${dy:-0}" --scale "${scale:-1}" \
  --mode "${mode:-cover}" --rot "${rot:-ccw}" --res "${res:-native}" \
  --orient "${orient:-auto}" ${quad:+--quad "$quad"} 2>>"$LOG"
rc=$?
if [ $rc -eq 0 ] && [ -s "$tmp" ]; then
  if mv -f "$tmp" "$f"; then
    record_done "${f##*/}|$(stat -c %s "$f" 2>/dev/null)"
    log "framed ${f##*/} (${SW}x${SH}) with ${frame##*/}，截图后 $(( $(date +%s) - f_mtime )) 秒完成"
    refresh_media "$f"
  fi
else
  rm -f "$tmp"
  [ $rc -eq 3 ] || log "failed ${f##*/}: 套壳失败 rc=$rc"
fi
exit 0

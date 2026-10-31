#!/system/bin/sh
# Frames one screen recording in place. Usage: process_video.sh [--wait] /path/to/Recording.mp4
# The video is re-encoded: it is placed inside the frame's real screen shape (mask taken from
# the frame's transparent area), so nothing spills over rounded corners; audio is copied as is.
MODDIR=${0%/*}
DATA=${SF_DATA:-/data/adb/screenshot_frame}
PROFILES="$DATA/profiles"
LOG="$DATA/log.txt"
DONE="$DATA/done.txt"
BIN="$DATA/bin/frame"
[ -x "$BIN" ] || BIN="$MODDIR/bin/frame"

. "$MODDIR/lib.sh"

CLAIM=; HAVE_LOCK=; WORK=; TMP=
LOCK="$DATA/vlock"
cleanup() {
  [ -n "$TMP" ] && rm -f "$TMP"
  [ -n "$WORK" ] && rm -rf "$WORK"
  [ -n "$HAVE_LOCK" ] && rmdir "$LOCK" 2>/dev/null
  [ -n "$CLAIM" ] && rmdir "$CLAIM" 2>/dev/null
}
trap cleanup EXIT

mkdir -p "$DATA"
. "$DATA/config" 2>/dev/null
# per-channel settings (recordings); empty = use the general value
[ -n "$rec_res" ] && res=$rec_res
[ -n "$rec_mode" ] && mode=$rec_mode
[ -n "$rec_rot" ] && rot=$rec_rot
[ "$rec_enabled" = "1" ] || exit 0
[ -x "$BIN" ] || { log "frame binary missing: $BIN"; exit 1; }
for last; do :; done
find_ffmpeg || { log "rec failed ${last##*/}: 缺少视频处理组件，请重新安装模块"; exit 1; }

if [ "$1" = "--wait" ]; then
  shift
  f="$1"
  mkdir -p "$DATA/vclaims"
  c="$DATA/vclaims/$(echo "${f##*/}" | cksum | cut -d' ' -f1)"
  mkdir "$c" 2>/dev/null || exit 0
  CLAIM=$c
  i=0
  while [ ! -f "$f" ] && [ $i -lt 120 ]; do i=$((i + 1)); sleep 0.5; done
else
  f="$1"
fi
[ -f "$f" ] || exit 0

# probe_video FILE -> sets VW VH (after rotation); fails while the file is still being recorded
probe_video() {
  VW=; VH=; ROT=0
  out=$(ffp -v error -select_streams v:0 -show_entries stream=width,height:stream_tags=rotate:stream_side_data=rotation -of default=noprint_wrappers=1 "$1" 2>/dev/null) || return 1
  for line in $out; do
    k=${line%%=*}; v=${line#*=}
    case "$k" in
      width) VW=$v ;;
      height) VH=$v ;;
      rotation|TAG:rotate) ROT=${v%.*} ;;
    esac
  done
  case "$VW$VH" in ""|*[!0-9]*) return 1 ;; esac
  [ "$VW" -gt 0 ] && [ "$VH" -gt 0 ] || return 1
  case "$ROT" in 90|-90|270|-270) t=$VW; VW=$VH; VH=$t ;; esac
  return 0
}

# wait until the recorder has finished: size stops changing and the file is readable
prev=-1; same=0; tries=0; ready=
while [ $tries -lt 300 ]; do
  [ -f "$f" ] || exit 0
  cur=$(stat -c %s "$f" 2>/dev/null || echo 0)
  if [ "$cur" = "$prev" ] && [ "$cur" -gt 0 ]; then same=$((same + 1)); else same=0; fi
  prev=$cur
  if [ $same -ge 2 ] && probe_video "$f"; then ready=1; break; fi
  tries=$((tries + 1))
  sleep 2
done
[ -n "$ready" ] || { log "rec failed ${f##*/}: 视频一直无法读取，已跳过"; exit 0; }

# one recording at a time
n=0
while ! mkdir "$LOCK" 2>/dev/null; do
  n=$((n + 1))
  if [ $n -gt 1800 ]; then rm -rf "$LOCK"; fi   # stale lock after ~1h
  sleep 2
done
HAVE_LOCK=1
[ -f "$f" ] || exit 0

key="${f##*/}|$(stat -c %s "$f" 2>/dev/null)"
grep -Fxq -- "$key" "$DONE" 2>/dev/null && exit 0

# Device screen size: a full-screen recording has this aspect ratio (any resolution).
dm=$(wm size 2>/dev/null | tail -n 1 | sed 's/.*: *//')
dev_w=${dm%x*}; dev_h=${dm#*x}
case "$dev_w$dev_h" in *[!0-9]*|"") dev_w=; dev_h= ;; esac
# A manual screen resolution from the settings (screen_res=WxH) replaces the device's own.
case "$screen_res" in
  [0-9]*x[0-9]*)
    sr_w=${screen_res%x*}; sr_h=${screen_res#*x}
    case "$sr_w$sr_h" in *[!0-9]*|"") ;; *) dev_w=$sr_w; dev_h=$sr_h ;; esac ;;
esac

# aspect_match RW RH: true when VW x VH has the aspect ratio of RW x RH (either orientation, 2%)
aspect_match() {
  [ -n "$1" ] && [ -n "$2" ] || return 1
  a=$((VW * $2 - VH * $1)); [ $a -lt 0 ] && a=$((0 - a))
  [ $((a * 50)) -le $((VH * $1)) ] && return 0
  a=$((VW * $1 - VH * $2)); [ $a -lt 0 ] && a=$((0 - a))
  [ $((a * 50)) -le $((VH * $2)) ]
}

sel=; first=; have_profiles=
for p in "$PROFILES"/*.conf; do
  [ -f "$p" ] || continue
  have_profiles=1
  # a frame chosen for recordings is the only candidate
  if [ -n "$rec_frame" ] && [ -f "$PROFILES/$rec_frame.conf" ] && [ "${p##*/}" != "$rec_frame.conf" ]; then continue; fi
  unset frame dx dy scale ref_w ref_h quad orient
  . "$p"
  if [ -z "$dev_w" ] || aspect_match "$dev_w" "$dev_h"; then
    [ -z "$first" ] && first=$p
    if [ -n "$active" ] && [ "${p##*/}" = "$active.conf" ]; then sel=$p; break; fi
  fi
done
[ -z "$sel" ] && sel=$first

unset frame dx dy scale ref_w ref_h quad orient
if [ -n "$sel" ]; then
  . "$sel"
elif [ -n "$have_profiles" ]; then
  log "rec skipped ${f##*/} (${VW}x${VH}): 宽高比与所有壳图的匹配规则都不符"
  record_done "$key"; exit 0
elif [ -n "$dev_w" ] && ! aspect_match "$dev_w" "$dev_h"; then
  log "rec skipped ${f##*/} (${VW}x${VH}): 宽高比与屏幕不符，不是全屏录屏"
  record_done "$key"; exit 0
fi
if [ -z "$frame" ]; then
  for c in "$frames_dir"/*.png "$frames_dir"/*.PNG; do
    [ -f "$c" ] && { frame=$c; break; }
  done
fi
[ -n "$frame" ] || { log "rec failed ${f##*/}: 没有找到壳图"; exit 1; }
case "$frame" in /*) ;; *) frame="$frames_dir/$frame" ;; esac
[ -f "$frame" ] || { log "rec failed ${f##*/}: 壳图文件不存在"; exit 1; }

WORK="$DATA/vwork_$$"
rm -rf "$WORK"; mkdir -p "$WORK"
"$BIN" vprep "$frame" "$VW" "$VH" "$WORK" --dx "${dx:-0}" --dy "${dy:-0}" --scale "${scale:-1}" \
  --mode "${mode:-cover}" --rot "${rot:-ccw}" --res "${res:-native}" \
  --orient "${orient:-auto}" ${quad:+--quad "$quad"} > "$WORK/geo" 2>>"$LOG"
rc=$?
[ $rc -eq 0 ] || { log "rec failed ${f##*/}: 排版计算失败 rc=$rc"; exit 1; }
unset persp out_w out_h scale_w scale_h crop_x crop_y crop_w crop_h place_x place_y vf_rot bx by bw bh p0x p0y p1x p1y p2x p2y p3x p3y
. "$WORK/geo"
# already framed (our own output comes back as an event)?
if [ "$VW" = "$out_w" ] && [ "$VH" = "$out_h" ]; then exit 0; fi

dir=${f%/*}
TMP="$dir/.frame_tmp_$$.mp4"
in_w=$VW; in_h=$VH
t0=$(date +%s)
NICE=; command -v nice >/dev/null 2>&1 && NICE="nice -n 10"
log "rec working ${f##*/} (${in_w}x${in_h}): 正在处理，输出 ${out_w}x${out_h}"
if [ "$persp" = "1" ]; then
  # tilted / 3D frame: warp the whole video into the four screen corners
  FC="[0:v]${vf_rot:+$vf_rot,}scale=${bw}:${bh}:flags=lanczos,perspective=x0=${p0x}:y0=${p0y}:x1=${p1x}:y1=${p1y}:x2=${p2x}:y2=${p2y}:x3=${p3x}:y3=${p3y}:interpolation=linear:sense=destination,pad=${out_w}:${out_h}:${bx}:${by}:color=black,format=gbrp[p]"
else
  FC="[0:v]scale=${scale_w}:${scale_h}:flags=lanczos,crop=${crop_w}:${crop_h}:${crop_x}:${crop_y},pad=${out_w}:${out_h}:${place_x}:${place_y}:color=black,format=gbrp[p]"
fi
FC="${FC};[2:v]format=gbrp[m];[p][m]blend=all_mode=multiply:shortest=1[vm];[vm][1:v]overlay=shortest=1:format=auto,scale=out_color_matrix=bt709:out_range=tv,format=yuv420p[o]"
LD_LIBRARY_PATH="$FFLIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" $NICE "$FFBIN" -nostdin -hide_banner -loglevel error -y \
  -i "$f" -loop 1 -i "$WORK/overlay.png" -loop 1 -i "$WORK/mask.png" \
  -filter_complex "$FC" \
  -map "[o]" -map "0:a?" -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p -colorspace bt709 -color_primaries bt709 -color_trc bt709 -c:a copy \
  -movflags +faststart -f mp4 "$TMP" 2>>"$LOG"
rc=$?
if [ $rc -eq 0 ] && [ -s "$TMP" ] && probe_video "$TMP" && [ "$VW" = "$out_w" ]; then
  touch -r "$f" "$TMP" 2>/dev/null
  if mv -f "$TMP" "$f"; then
    TMP=
    record_done "${f##*/}|$(stat -c %s "$f" 2>/dev/null)"
    log "rec framed ${f##*/} (${in_w}x${in_h}) with ${frame##*/}，耗时 $(( $(date +%s) - t0 )) 秒"
    refresh_media "$f"
  fi
else
  log "rec failed ${f##*/}: 编码失败 rc=$rc，已保留原视频"
fi
exit 0

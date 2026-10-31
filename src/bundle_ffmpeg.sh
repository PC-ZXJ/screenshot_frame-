#!/bin/sh
# Termux: copy ffmpeg/ffprobe and every library they need into module/bin/ffmpeg/
# (so the module runs without Termux). Usage: sh src/bundle_ffmpeg.sh
cd "$(dirname "$0")/.." || exit 1
P=${PREFIX:-/data/data/com.termux/files/usr}
command -v ffmpeg >/dev/null 2>&1 || pkg install -y ffmpeg || exit 1
command -v readelf >/dev/null 2>&1 || pkg install -y binutils || exit 1
out=module/bin/ffmpeg
rm -rf "$out"; mkdir -p "$out/lib"
cp -L "$P/bin/ffmpeg" "$P/bin/ffprobe" "$out/" || exit 1
queue="$out/ffmpeg $out/ffprobe"
seen=" "
while [ -n "$queue" ]; do
  set -- $queue; cur=$1; shift; queue="$*"
  for so in $(readelf -d "$cur" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p'); do
    case "$seen" in *" $so "*) continue ;; esac
    seen="$seen$so "
    if [ -e "$P/lib/$so" ]; then
      cp -L "$P/lib/$so" "$out/lib/$so" && queue="$queue $out/lib/$so"
    fi
  done
done
strip "$out/ffmpeg" "$out/ffprobe" 2>/dev/null
echo "ffmpeg bundle: $(ls "$out/lib" | wc -l) libraries, $(du -sh "$out" | cut -f1)"
LD_LIBRARY_PATH="$out/lib" "$out/ffmpeg" -version | head -n 1 || { echo "bundled ffmpeg does not run"; exit 1; }
LD_LIBRARY_PATH="$out/lib" "$out/ffmpeg" -hide_banner -encoders 2>/dev/null | grep -q libx264 || { echo "this ffmpeg has no libx264 encoder"; exit 1; }

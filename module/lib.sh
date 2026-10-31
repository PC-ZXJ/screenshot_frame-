#!/system/bin/sh
# Shared helpers. The caller defines DATA, LOG and DONE before sourcing this file.

log() {
  echo "$(date '+%F %T') $*" >> "$LOG"
  if [ "$(stat -c %s "$LOG" 2>/dev/null || echo 0)" -gt 204800 ]; then
    tail -n 200 "$LOG" > "$LOG.tmp" && mv -f "$LOG.tmp" "$LOG"
  fi
}

# record_done KEY: remember "name|size" so duplicate events are ignored
record_done() {
  echo "$1" >> "$DONE"
  if [ "$(wc -l < "$DONE")" -gt 500 ]; then
    tail -n 300 "$DONE" > "$DONE.tmp" && mv -f "$DONE.tmp" "$DONE"
  fi
}

# media_field ROW NAME: value of NAME=... in a "content query" row
media_field() {
  echo "$1" | sed -n "s/.*[ ,]$2=\([^,]*\).*/\1/p" | head -n 1
}

# refresh_media FILE: make MediaStore (and so the Gallery) pick up the replaced file.
# The Gallery caches thumbnails by the row's modification time, so besides the new size
# and dimensions the row must also get the file's new modification time.
refresh_media() {
  rp="$1"
  case "$rp" in /sdcard/*) rp="/storage/emulated/0/${rp#/sdcard/}" ;; esac
  touch "$1" 2>/dev/null
  mt=$(stat -c %Y "$1" 2>/dev/null)
  out=$(content call --uri content://media --method scan_file --arg "$rp" 2>&1 | head -c 120)
  am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d "file://$rp" >/dev/null 2>&1
  row=$(content query --uri content://media/external/file --projection _id:date_modified:width:height:_size \
    --where "_data='$rp'" 2>&1 | head -n 1)
  id=$(media_field "$row" _id)
  dm=$(media_field "$row" date_modified)
  fixed=
  if [ -n "$id" ] && [ -n "$mt" ] && [ "${dm:-0}" -lt "$mt" ] 2>/dev/null; then
    content update --uri "content://media/external/file/$id" --bind "date_modified:l:$mt" >/dev/null 2>&1
    row=$(content query --uri content://media/external/file --projection _id:date_modified:width:height:_size \
      --where "_data='$rp'" 2>&1 | head -n 1)
    fixed=" (date_modified was ${dm}, set to ${mt})"
  fi
  log "media: scan=${out:-none} row=${row:-none}${fixed}"
}

# find_ffmpeg: sets FFBIN, FFPROBE, FFLIB. Prefers the copy shipped with the module,
# falls back to Termux's ffmpeg when it is installed.
find_ffmpeg() {
  FFBIN=; FFPROBE=; FFLIB=
  for d in "$DATA/bin/ffmpeg" "$MODDIR/bin/ffmpeg"; do
    if [ -x "$d/ffmpeg" ] && [ -x "$d/ffprobe" ]; then
      FFBIN="$d/ffmpeg"; FFPROBE="$d/ffprobe"; FFLIB="$d/lib"; return 0
    fi
  done
  t=/data/data/com.termux/files/usr
  if [ -x "$t/bin/ffmpeg" ] && [ -x "$t/bin/ffprobe" ]; then
    FFBIN="$t/bin/ffmpeg"; FFPROBE="$t/bin/ffprobe"; FFLIB="$t/lib"; return 0
  fi
  return 1
}
ff()  { LD_LIBRARY_PATH="$FFLIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$FFBIN" "$@"; }
ffp() { LD_LIBRARY_PATH="$FFLIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$FFPROBE" "$@"; }

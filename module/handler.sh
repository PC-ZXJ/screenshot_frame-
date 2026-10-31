#!/system/bin/sh
# inotifyd calls: handler.sh EVENT DIR NAME
# DIR may be the lower storage path (/data/media/0/...); work on the /sdcard view instead.
LOWER=${SF_LOWER:-/data/media/0}
UPPER=${SF_UPPER:-/sdcard}
dir=$2
name=$3
case "$dir" in
  "$LOWER"/*) dir="$UPPER/${dir#"$LOWER"/}" ;;
esac
case "$name" in
  .pending-*) name=${name#.pending-}; name=${name#*-} ;;   # .pending-<time>-<final name>
  .*) exit 0 ;;
esac
case "$name" in
  *.[pP][nN][gG]|*.[jJ][pP][gG]|*.[jJ][pP][eE][gG]) ;;
  *) exit 0 ;;
esac
MODDIR=${0%/*}
( sh "$MODDIR/process.sh" --wait "$dir/$name" >/dev/null 2>&1 & )

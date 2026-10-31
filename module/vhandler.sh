#!/system/bin/sh
# inotifyd calls: vhandler.sh EVENT DIR NAME   (screen recordings folder)
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
  *.[mM][pP]4) ;;
  *) exit 0 ;;
esac
MODDIR=${0%/*}
( sh "$MODDIR/process_video.sh" --wait "$dir/$name" >/dev/null 2>&1 & )

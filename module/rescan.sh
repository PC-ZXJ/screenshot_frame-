#!/system/bin/sh
# inotifyd handler used only while no recording folder exists yet:
# when the system creates one (first recording), restart the listeners so it gets watched.
MODDIR=${0%/*}
case "$3" in
  *[Ss][Cc][Rr][Ee][Ee][Nn]*[Rr][Ee][Cc][Oo][Rr][Dd]*|*录屏*|*屏幕录制*)
    sleep 2
    ( sh "$MODDIR/watch.sh" >/dev/null 2>&1 & )
    ;;
esac

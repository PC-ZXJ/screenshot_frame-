#!/system/bin/sh
# Boot entry: prepares data folders, then runs the listener (watch.sh --fg)
MODDIR=${0%/*}
DATA=/data/adb/screenshot_frame
mkdir -p "$DATA/profiles" "$DATA/bin"
[ -f "$DATA/config" ] || cp "$MODDIR/defaults/config" "$DATA/config"
chmod 755 "$MODDIR"/*.sh
rm -rf "$DATA/claims" "$DATA/lock"
# keep the working copy of the core program in sync with the one shipped in the module
if [ -f "$MODDIR/bin/frame" ] && ! cmp -s "$MODDIR/bin/frame" "$DATA/bin/frame"; then
  cp -f "$MODDIR/bin/frame" "$DATA/bin/frame" && chmod 755 "$DATA/bin/frame"
fi

while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 2; done
while [ ! -d /sdcard/Android ]; do sleep 3; done

sh "$MODDIR/migrate.sh"
exec sh "$MODDIR/watch.sh" --fg

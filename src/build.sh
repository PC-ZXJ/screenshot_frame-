#!/bin/sh
# Build the core program in Termux:  sh build.sh
cd "$(dirname "$0")" || exit 1
for h in stb_image.h stb_image_write.h; do
  [ -f "$h" ] || curl -fL -o "$h" "https://raw.githubusercontent.com/nothings/stb/master/$h" || exit 1
done
clang -O2 -ffunction-sections -Wl,--gc-sections -o frame frame.c lodepng.c -lm || exit 1
strip frame 2>/dev/null || llvm-strip frame 2>/dev/null
ls -l frame

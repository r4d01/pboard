#!/bin/sh
set -eu
cd "$(dirname "$0")"
: "${PBSDK:?Set PBSDK to the absolute SDK-A13 directory}"
sdk_sysroot="$PBSDK/usr/arm-obreey-linux-gnueabi/sysroot"
export LD_LIBRARY_PATH="$PBSDK/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$PBSDK/usr/bin/arm-obreey-linux-gnueabi-gcc" \
  --sysroot="$sdk_sysroot" -std=gnu99 -Os -Wall -Wextra \
  -march=armv7-a -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp \
  -I"$sdk_sysroot/usr/local/include" -I"$sdk_sysroot/usr/include" -I"$sdk_sysroot/usr/include/freetype2" \
  dashboard.c -o dashboard.app \
  -L"$sdk_sysroot/usr/local/lib" -L"$sdk_sysroot/usr/lib" \
  -Wl,-rpath-link,"$sdk_sysroot/usr/lib:$sdk_sysroot/lib:$sdk_sysroot/usr/local/lib" \
  -linkview -lcurl

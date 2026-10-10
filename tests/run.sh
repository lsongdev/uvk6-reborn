#!/bin/sh
# Run host regressions with hardware calls mocked; no device is accessed.
set -eu
cd "$(dirname "$0")/.."
test_binary=$(mktemp -t uvk6-regression.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
case "$(uname -s)" in
  Darwin) dead_strip=-Wl,-dead_strip ;;
  *) dead_strip=-Wl,--gc-sections ;;
esac
"${CC:-cc}" -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types -DPRINTF_INCLUDE_CONFIG_H \
  -Isrc/config -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/radio-regression.c tests/finput-regression.c tests/bands-regression.c \
  src/external/printf/printf.c "$dead_strip" -o "$test_binary"
"$test_binary"
echo 'Frequency input, receiver switching, volume initialization, and band names passed.'

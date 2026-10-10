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
"$test_binary" --bk1080
"${CC:-cc}" -std=c2x -Wall -Wextra -Werror tests/model-regression.c \
  src/radio-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types -DPRINTF_INCLUDE_CONFIG_H \
  -Isrc/config -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/vfo-regression.c src/radio-model.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types -DPRINTF_INCLUDE_CONFIG_H \
  -Isrc/config -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/si-regression.c "$dead_strip" -o "$test_binary"
"$test_binary"
echo 'SI sidebands/BFO, VFO keys, confirmation, unavailable receivers, frequency input, legacy storage, cached detection, receiver plans, precision, and band boundaries passed.'

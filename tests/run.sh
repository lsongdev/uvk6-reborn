#!/bin/sh
# Run host regressions with hardware calls mocked; no device is accessed.
set -eu
cd "$(dirname "$0")/.."
python3 tests/flash-regression.py
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror \
  tests/uart-protocol-regression.c src/hal/uart-protocol.c -o /tmp/uvk6-uart-protocol-test
/tmp/uvk6-uart-protocol-test
rm -f /tmp/uvk6-uart-protocol-test
test_binary=$(mktemp -t uvk6-regression.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
case "$(uname -s)" in
  Darwin) dead_strip=-Wl,-dead_strip ;;
  *) dead_strip=-Wl,--gc-sections ;;
esac
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types -DPRINTF_INCLUDE_CONFIG_H \
  -Isrc/config -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/radio-regression.c tests/finput-regression.c \
  src/core/radio-model.c src/external/printf/printf.c "$dead_strip" -o "$test_binary"
"$test_binary"
"$test_binary" --bk1080
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror \
  tests/seek-regression.c src/core/seek.c src/core/radio-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/model-regression.c \
  src/core/radio-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types -DPRINTF_INCLUDE_CONFIG_H \
  -Isrc/config -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/vfo-regression.c src/core/dcs.c src/external/printf/printf.c src/core/radio-model.c src/core/radio-state.c src/core/rx-runtime.c src/core/tx-model.c src/core/tx-runtime.c src/core/seek.c \
  src/core/rds.c src/core/rds-receiver.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types -DPRINTF_INCLUDE_CONFIG_H \
  -Isrc/config -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/si-regression.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/bk1080-regression.c "$dead_strip" -o "$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types -DPRINTF_INCLUDE_CONFIG_H \
  -Isrc/config -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/rx-device-regression.c src/core/dcs.c src/external/printf/printf.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/tx-regression.c \
  src/core/tx-model.c src/core/tx-runtime.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/rx-runtime-regression.c \
  src/core/rx-runtime.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/config-store-regression.c \
  src/core/radio-state.c src/core/radio-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types -DPRINTF_INCLUDE_CONFIG_H \
  -Isrc/config -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/band-editor-regression.c src/core/config-store.c src/core/radio-model.c src/core/radio-state.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types -DPRINTF_INCLUDE_CONFIG_H \
  -Isrc/config -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/vfo-component-regression.c src/ui/graphics.c src/ui/components/vfo.c \
  src/ui/components/signal.c src/ui/components/frequency.c src/core/radio-model.c src/external/printf/printf.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/rds-regression.c \
  src/core/rds.c src/core/rds-receiver.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -DPRINTF_INCLUDE_CONFIG_H -Isrc/config \
  tests/tone-regression.c src/drivers/bk4819/css.c src/core/dcs.c src/external/printf/printf.c -o "$test_binary"
"$test_binary"
echo 'Radio, VFO UI, storage, receiver plans, SI sidebands/BFO, RDS PS/RadioText, and RX/TX tone/split-frequency regressions passed.'

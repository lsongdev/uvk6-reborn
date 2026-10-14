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
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/receiver-regression.c tests/number-input-regression.c src/ui/number-input.c \
  src/core/radio-model.c src/core/text.c "$dead_strip" -o "$test_binary"
"$test_binary"
"$test_binary" --bk1080
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror \
  tests/seek-regression.c src/core/seek.c src/core/radio-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/model-regression.c \
  src/core/radio-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/radio-app-regression.c src/apps/channel-manager.c src/core/tuning.c src/ui/number-input.c src/core/dcs.c src/core/text.c src/core/radio-model.c src/core/radio-state.c src/core/rx-runtime.c src/core/watch.c src/core/tx-model.c src/core/tx-runtime.c src/core/seek.c src/core/capture.c \
  src/core/rds.c src/core/rds-receiver.c src/ui/components/frequency.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/si-regression.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/bk1080-regression.c "$dead_strip" -o "$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/rx-device-regression.c src/core/dcs.c src/core/text.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/tuning-regression.c \
  src/core/tuning.c src/core/radio-state.c src/core/radio-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/watch-regression.c \
  src/core/watch.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Wno-missing-field-initializers \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/tx-device-regression.c src/core/dcs.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Wno-missing-field-initializers \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/tx-disabled-regression.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -Wno-missing-field-initializers \
  tests/parallel-watch-regression.c src/core/watch.c -o "$test_binary"
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
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/band-editor-regression.c src/core/config-store.c src/core/radio-model.c src/core/radio-state.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/radio-card-regression.c src/ui/graphics.c src/ui/components/radio-card.c \
  src/ui/components/signal.c src/ui/components/frequency.c src/core/radio-model.c src/core/text.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/rds-regression.c \
  src/core/rds.c src/core/rds-receiver.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror \
  tests/tone-regression.c src/drivers/bk4819/css.c src/core/dcs.c src/core/text.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror \
  tests/text-regression.c src/core/dcs.c src/core/text.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/font-regression.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections tests/menu-regression.c src/ui/number-input.c src/core/text.c \
  src/ui/numnav.c src/core/measurements.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/settings-regression.c src/core/measurements.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  \
  -Wno-missing-field-initializers tests/settings-inline-regression.c src/ui/number-input.c src/ui/numnav.c \
  src/core/measurements.c src/core/radio-state.c src/core/radio-model.c src/ui/graphics.c src/core/text.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/device-config-regression.c \
  src/core/config-store.c src/core/radio-state.c src/core/radio-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/backlight-regression.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/channel-store-regression.c \
  src/core/radio-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/bk-rx-mode-regression.c \
  src/drivers/bk4819/rx.c -o "$test_binary"
"$test_binary"

"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/capture-regression.c src/core/capture.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/bk-capture-regression.c src/drivers/bk4819/capture.c -o "$test_binary"
"$test_binary"
echo 'Radio, multi-VFO UI, storage, receiver plans, FC capture, RDS, and RX/TX regressions passed.'

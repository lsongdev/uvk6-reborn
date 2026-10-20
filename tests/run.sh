#!/bin/sh
# Run host regressions with hardware calls mocked; no device is accessed.
set -eu
cd "$(dirname "$0")/.."
python3 tools/check-layers.py
python3 tests/flash-regression.py
python3 tests/resources-regression.py
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror \
  tests/uart-protocol-regression.c src/app/link/uart-protocol.c -o /tmp/uvk6-uart-protocol-test
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
  tests/radio-app-regression.c src/devices/keyboard.c src/core/tuning.c src/ui/number-input.c src/core/dcs.c src/core/text.c src/core/radio-model.c src/core/radio-state.c src/radio/rx-runtime.c src/core/watch.c src/core/tx-model.c src/radio/tx-runtime.c src/core/seek.c src/core/capture.c src/core/spectrum.c \
  src/core/rds.c src/core/rds-receiver.c src/ui/frequency.c "$dead_strip" -o "$test_binary"
"$test_binary"
# ENABLE_FC=0 must keep compiling: FC branches fold to a constant phase.
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include -DUVK6_ENABLE_FC=0 -fsyntax-only \
  src/radio/radio.c src/radio/capture.c src/radio/watch.c src/radio/seek.c src/radio/tx.c \
  src/radio/spectrum.c src/ui/screens/home.c src/ui/screens/slot-menu.c src/ui/screens/save-channel.c \
  src/ui/screens/spectrum.c src/ui/screens/tone-scan.c src/radio/tone-scan.c src/ui/spectrum.c
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include -DUVK6_ENABLE_SPECTRUM=0 -fsyntax-only \
  src/radio/radio.c src/radio/spectrum.c src/radio/watch.c src/ui/screens/slot-menu.c
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/si-regression.c src/store/resource.c src/core/crc32.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/bk1080-regression.c "$dead_strip" -o "$test_binary"
"$test_binary"
# The broadcast chip is a build choice: K6 SI4732, K1 BK1080.
for rx_board in uvk6:1 uvk1:0; do
  board=${rx_board%:*}
  "${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
    -Wno-incompatible-pointer-types -DUVK6_ENABLE_SI4732="${rx_board#*:}" \
    -Isrc/external/CMSIS_5/CMSIS/Core/Include \
    -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
    tests/rx-device-regression.c "src/boards/$board/calibration.c" \
    src/core/dcs.c src/core/text.c "$dead_strip" -o "$test_binary"
  "$test_binary"
done
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/tuning-regression.c \
  src/core/tuning.c src/core/radio-state.c src/core/tx-model.c src/core/radio-model.c -o "$test_binary"
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
  tests/bk4829-tx-regression.c src/boards/uvk1/calibration.c src/boards/uvk1/led.c \
  src/devices/led.c src/drivers/bk48x9/common.c src/drivers/bk48x9/bk4829.c \
  src/drivers/bk48x9/tx.c src/drivers/bk48x9/rx.c src/drivers/bk48x9/css.c \
  src/core/dcs.c src/core/measurements.c "$dead_strip" -o "$test_binary"
"$test_binary"
for led_board in uvk1 uvk6; do
  led_k1=0
  if [ "$led_board" = uvk1 ]; then led_k1=1; fi
  "${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -DTEST_UVK1="$led_k1" tests/led-regression.c src/devices/led.c \
    "src/boards/$led_board/led.c" src/drivers/bk48x9/common.c "$dead_strip" -o "$test_binary"
  "$test_binary"
done
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Wno-missing-field-initializers \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/tx-disabled-regression.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/tx-regression.c \
  src/core/tx-model.c src/radio/tx-runtime.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/rx-runtime-regression.c \
  src/radio/rx-runtime.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/config-store-regression.c src/core/crc32.c \
  src/core/radio-state.c src/core/tx-model.c src/core/radio-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/band-editor-regression.c src/store/config-store.c src/core/crc32.c src/core/radio-model.c src/core/radio-state.c src/core/tx-model.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/radio-card-regression.c src/ui/graphics.c src/ui/radio-card.c \
  src/ui/signal.c src/ui/frequency.c src/core/radio-model.c src/core/text.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/rds-regression.c \
  src/core/rds.c src/core/rds-receiver.c -o "$test_binary"
"$test_binary"
for tone_k1 in 0 1; do
  "${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -DTEST_BK4829="$tone_k1" \
    tests/tone-regression.c src/drivers/bk48x9/css.c src/core/dcs.c src/core/text.c -o "$test_binary"
  "$test_binary"
done
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror \
  tests/text-regression.c src/core/dcs.c src/core/text.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/font-regression.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/screen-regression.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections tests/menu-regression.c src/ui/number-input.c src/core/text.c \
  src/ui/numnav.c src/core/measurements.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -ffunction-sections -fdata-sections \
  -Wno-incompatible-pointer-types \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/settings-regression.c src/core/measurements.c src/core/crc32.c src/store/resource.c src/store/preferences.c src/core/text.c src/core/dcs.c "$dead_strip" -o "$test_binary"
"$test_binary"
for settings_si in 0 1; do
  "${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -DUVK6_ENABLE_SI4732="$settings_si" \
    -Wno-missing-field-initializers tests/settings-inline-regression.c src/core/crc32.c src/store/resource.c src/store/preferences.c src/ui/number-input.c src/ui/numnav.c \
    src/core/measurements.c src/core/radio-state.c src/core/tx-model.c src/core/radio-model.c src/ui/graphics.c src/core/text.c "$dead_strip" -o "$test_binary"
  "$test_binary"
done
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/device-config-regression.c src/store/resource.c \
  src/store/config-store.c src/core/crc32.c src/core/radio-state.c src/core/tx-model.c src/core/radio-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include \
  -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/backlight-regression.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Isrc/external/CMSIS_5/CMSIS/Core/Include -Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include \
  tests/channels-regression.c src/store/channel-store.c src/core/crc32.c src/store/resource.c src/core/radio-model.c src/core/tx-model.c \
  src/ui/graphics.c src/ui/frequency.c src/core/text.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/channel-store-regression.c src/core/crc32.c src/store/resource.c \
  src/core/radio-model.c src/core/tx-model.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/storage-regression.c -o "$test_binary"
"$test_binary"
# The PC link over two serial ports (the K1's UART and USB).
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections tests/link-regression.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/bk-rx-mode-regression.c \
  src/drivers/bk48x9/rx.c src/drivers/bk48x9/bk4819.c src/drivers/bk48x9/common.c \
  -ffunction-sections -fdata-sections "$dead_strip" -o "$test_binary"
"$test_binary"
# Each chip backend is linked alone, as the firmware selects it by RADIO_CHIP.
for chip in bk4819 bk4829; do
  chip_k1=0
  if [ "$chip" = bk4829 ]; then chip_k1=1; fi
  "${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -DTEST_BK4829="$chip_k1" tests/bk4829-rx-regression.c src/drivers/bk48x9/common.c \
    "src/drivers/bk48x9/$chip.c" src/drivers/bk48x9/rx.c src/drivers/bk48x9/css.c \
    src/core/measurements.c "$dead_strip" -o "$test_binary"
  "$test_binary"
done
for chip in bk4819:0 bk4829:1; do
  "${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -DTEST_BK4829="${chip#*:}" tests/tx-tail-driver-regression.c \
    src/drivers/bk48x9/common.c "src/drivers/bk48x9/${chip%:*}.c" \
    src/drivers/bk48x9/tx.c src/drivers/bk48x9/rx.c src/drivers/bk48x9/css.c \
    src/core/measurements.c src/core/dcs.c src/core/text.c "$dead_strip" -o "$test_binary"
  "$test_binary"
done
for chip in bk4819 bk4829; do
  "${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    tests/tone-scan-regression.c src/drivers/bk48x9/tone-scan.c \
    src/drivers/bk48x9/common.c "src/drivers/bk48x9/$chip.c" \
    src/drivers/bk48x9/rx.c src/drivers/bk48x9/css.c \
    src/core/measurements.c src/core/dcs.c src/core/text.c "$dead_strip" -o "$test_binary"
  "$test_binary"
done
# Register-write traces captured from the pre-split runtime-selected driver.
# BK4829 since then: user squelch delays reach REG_4E, and AM is configured.
for chip in bk4819:0:173:0x672c4eae bk4829:1:202:0x2740c8e2; do
  IFS=: read -r name k1 writes trace <<EOF_TRACE
$chip
EOF_TRACE
  "${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -DTEST_BK4829="$k1" -DEXPECTED_WRITES="$writes"u -DEXPECTED_TRACE="$trace"u \
    tests/bk48x9-trace-regression.c src/drivers/bk48x9/common.c "src/drivers/bk48x9/$name.c" \
    src/drivers/bk48x9/rx.c src/drivers/bk48x9/tx.c src/drivers/bk48x9/css.c \
    src/drivers/bk48x9/capture.c src/core/measurements.c "$dead_strip" -o "$test_binary"
  "$test_binary" >/dev/null
done

"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/spectrum-regression.c src/core/spectrum.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/capture-regression.c src/core/capture.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/bk-capture-regression.c src/drivers/bk48x9/capture.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  tests/uvk1-storage-regression.c src/drivers/py25q16/py25q16.c src/store/config-store.c \
  src/store/channel-store.c src/core/crc32.c src/core/radio-state.c src/core/tx-model.c \
  src/core/radio-model.c src/core/dcs.c src/core/text.c "$dead_strip" -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror tests/battery-regression.c \
  src/devices/battery.c -o "$test_binary"
"$test_binary"
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  tests/uvk6-crystal-regression.c src/boards/uvk6/calibration.c \
  src/drivers/bk48x9/common.c src/drivers/bk48x9/bk4819.c src/drivers/bk48x9/tx.c \
  src/drivers/bk48x9/rx.c src/drivers/bk48x9/css.c src/core/measurements.c \
  "$dead_strip" -o "$test_binary"
"$test_binary"
# Check actual board PWM registers, not a duplicate duty mapping in a mock.
for backlight_k1 in 0 1; do
  "${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror \
    -DTEST_UVK1="$backlight_k1" -DPY32F071xB \
    -Isrc/drivers/py32f071/vendor -Isrc/external/CMSIS_5/CMSIS/Core/Include \
    tests/backlight-board-regression.c -o "$test_binary"
  "$test_binary"
done
"${CC:-cc}" -Isrc -std=c2x -Wall -Wextra -Werror \
  tests/display-contrast-regression.c src/drivers/st7565/st7565.c -o "$test_binary"
"$test_binary"
echo 'Radio, multi-VFO UI, storage, receiver plans, FC capture, RDS, and RX/TX regressions passed.'

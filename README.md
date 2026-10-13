# UVK6 Reborn

Experimental fork of [Hawk5](https://github.com/fagci/hawk5) for the
DP32G030-based Quansheng UV-K6, including radios modified with SI4732.

The refactored receiver supports up to four VFO slots (two visible at once),
NFM/WFM/AM/SSB modes, band presets, dual watch, user channels, UVR2
persistence and receiver selection. **Production builds disable TX at compile
time.** The experimental PA path is tested with `UVK6_ENABLE_TX=1` in host
tests only. Do not enable RF transmission before verifying the board,
calibration, filtering, PA shutdown timing and applicable frequency limits. See the
[user manual](docs/user-manual.md) for current controls and the
[implementation status](docs/refactor-progress.md) before testing.

## Documentation

- [重构进度](docs/refactor-progress.md)
- [用户手册](docs/user-manual.md)
- [硬件事实](docs/hardware.md)
- [设计文档](docs/design.md)

## Build

Requires an ARM GNU toolchain (`arm-none-eabi-gcc`), Python 3 and `crcmod`.

```sh
git submodule update --init --recursive
python3 -m pip install crcmod
make
```

Output: `bin/firmware.bin` (raw) and `bin/firmware.packed.bin` (packed).
Choose the file required by your actual flashing tool and bootloader; they
are not interchangeable.

Flash through the stock DP32G030 UV-K5/K6 Bootloader on macOS / Linux:

```sh
make flash PORT=/dev/cu.usbserial-1420  # Linux: PORT=/dev/ttyUSB0
```

Hold PTT while powering on to enter Bootloader. This builds and sends the raw
image, waits for block acknowledgements and leaves EEPROM untouched. See
[flashing instructions](tools/README.md#刷机) for recovery and serial-port details.

Development scripts and the CHIRP driver are in [`tools/`](tools/README.md).
Source directory responsibilities are described in [`src/`](src/README.md).

## EEPROM and first boot

**Back up your entire EEPROM before flashing**. LOSEHU HF and Hawk5 use
different, incompatible settings/channel layouts. Firmware updates do not
automatically migrate EEPROM content.

- Unrecognized settings now enter the initialization screen **without
  writing an EEPROM marker or rebooting repeatedly**.
- EEPROM writes stay locked until a recognized UVK6 Reborn configuration is
  loaded, or the user explicitly initializes: press `0`, then `1` to
  confirm. **This writes the two UVR2 configuration slots at
  `0x0800–0x0FFF`, which may contain data from earlier firmware.**
  Legacy settings are not automatically migrated. User channels use a
  separate `0xB000–0xDFFF` region. Do not confirm
  before obtaining a backup and checking the EEPROM geometry.
- The 4-Mbit / 512-KiB addressing mode assumes eight 64-KiB address banks
  (`0xA0` through `0xAE`). Writes are limited to at most 32 bytes per chunk until hardware is verified;
  **verify the actual EEPROM part and mapping on your board**.
- The original Hawk5 SSB patch loader expects an 8840-byte patch at the end
  of the selected EEPROM. LOSEHU HF places its SSB patch differently, so
  **SSB will not work without additional patch migration and verification**.
  A valid-looking patch header is insufficient: production SSB is disabled
  until a trusted CRC32 of the entire installed image is supplied at build
  time using `-DUVK6_SSB_PATCH_CRC32=0x...` (only after independent image
  verification). A missing or mismatching patch is rejected.
- A successful compilation is not evidence of hardware compatibility. See
  the implementation status for outstanding hardware acceptance checks.

## First hardware smoke test

1. Save the LOSEHU firmware version, inspect the MCU/EEPROM part numbers,
   and make a verified full EEPROM backup.
2. Confirm the programmer/bootloader and the target MCU; do not flash a
   binary intended for the PY32F071 platform.
3. Boot and verify LCD, keypad and the initialization prompt. **Do not
   confirm initialization yet**; check the EEPROM contents remain intact.
4. After verifying storage mapping, initialize deliberately and test
   BK4819 and SI4732 reception separately.
5. Verify PTT restrictions, TX calibration, legal frequency limits and
   RF hardware before performing any transmissions.

Further work: validate actual EEPROM geometry, import the SSB patch to
an explicitly reserved region, then extend the current single VFO to multiple watch frequencies
over capability-driven receiver drivers.

# UVK6 Reborn

Experimental fork of [Hawk5](https://github.com/fagci/hawk5) for the
DP32G030-based Quansheng UV-K6, including radios modified with SI4732.

This initial branch fixes several startup and driver problems. It does **not**
yet implement the planned unified A/B radio interface.

## Documentation

- [用户手册](docs/user-manual.md)
- [硬件事实](docs/hardware-facts.md)

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

## EEPROM and first boot

**Back up your entire EEPROM before flashing**. LOSEHU HF and Hawk5 use
different, incompatible settings/channel layouts. Firmware updates do not
automatically migrate EEPROM content.

- Unrecognized settings now enter the initialization screen **without
  writing an EEPROM marker or rebooting repeatedly**.
- EEPROM writes stay locked until a recognized UVK6 Reborn configuration is
  loaded, or the user explicitly initializes: press `0`, then `1` to
  confirm. **This overwrites settings and channel data.** Do not confirm
  before obtaining a backup and checking the EEPROM geometry.
- The 4-Mbit / 512-KiB addressing mode assumes eight 64-KiB address banks
  (`0xA0` through `0xAE`). The nominal 128-byte write chunk is conservative;
  **verify the actual EEPROM part and mapping on your board**.
- The original Hawk5 SSB patch loader expects an 8840-byte patch at the end
  of the selected EEPROM. LOSEHU HF places its SSB patch differently, so
  **SSB will not work without additional patch migration and verification**.
  The driver now refuses a missing/invalid Hawk5 patch instead of sending
  arbitrary EEPROM data to the SI4732.
- A successful compilation is not evidence of hardware compatibility. No
  physical device was flashed or tested as part of these changes.

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
an explicitly reserved region, then implement two user-facing A/B frequency
slots over capability-driven receiver drivers.

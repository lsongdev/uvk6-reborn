# Resources

Data installed into the radio's EEPROM by `tools/resources.py`, never
compiled into the firmware. Each resource sits in the reserved tail at the
top of the EEPROM behind a 16-byte header (magic, length, CRC-32); see
`src/core/resource.h`.

| File | Resource | Bytes | CRC-32 |
| --- | --- | --- | --- |
| `si4735/patch_init.h` | SI4735/SI4732 SSB receive patch (`SSBP`) | 8840 | `0xf9c0f474` |

## si4735/patch_init.h

Copied unchanged from the PU2CLR SI4735 Arduino library,
[`src/patch_init.h`](https://github.com/pu2clr/SI4735/blob/123eccc730dc03c0ccc28526cbea6e305da7d2fc/src/patch_init.h)
at commit `123eccc`. It is the `amrx_6_0_1_ssbrx_patch_init_0xA902` patch
published by Vadim Afonkin; as the file's own header says, the patch is not
part of that library's licence and should not be used commercially. Keep the
file byte-identical so its source stays verifiable.

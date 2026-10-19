#ifndef CORE_CALIBRATION_H
#define CORE_CALIBRATION_H

// Factory calibration block, DualTachyon's UV-K5 layout, as offsets in
// STORAGE_CALIBRATION. The PA table is seven 16-byte band records, the
// first 9 bytes Low/Medium/High x three anchors.
#define CALIBRATION_BYTES 0x200u
// Six ascending 12-bit ADC points; point 3 is the 7.60 V reference.
#define CALIBRATION_BATTERY_OFFSET 0x140u
#define CALIBRATION_BATTERY_POINTS 6u
#define CALIBRATION_BATTERY_REFERENCE 3u

// Squelch thresholds: six 16-byte rows per range, byte n for level n.
#define CALIBRATION_SQUELCH_UHF_OFFSET 0x00u // 174 MHz and above.
#define CALIBRATION_SQUELCH_VHF_OFFSET 0x60u // Below 174 MHz.

// s16 crystal trim, two reserved u16s, u8 AF volume gain, u8 DAC gain.
#define CALIBRATION_RX_MISC_OFFSET 0x188u
#define CALIBRATION_RX_MISC_BYTES 8u

#define CALIBRATION_PA_OFFSET 0xD0u
#define CALIBRATION_PA_BANDS 7u
#define CALIBRATION_PA_STRIDE 16u
#define CALIBRATION_PA_BYTES 9u

#endif

#ifndef CORE_CALIBRATION_H
#define CORE_CALIBRATION_H

// Factory calibration block, DualTachyon's UV-K5 layout, as offsets in
// STORAGE_CALIBRATION. The PA table is seven 16-byte band records, the
// first 9 bytes Low/Medium/High x three anchors.
#define CALIBRATION_BYTES 0x200u
#define CALIBRATION_PA_OFFSET 0xD0u
#define CALIBRATION_PA_BANDS 7u
#define CALIBRATION_PA_STRIDE 16u
#define CALIBRATION_PA_BYTES 9u

#endif

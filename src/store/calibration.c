#include "store/calibration.h"
#include "hal/storage.h"

uint16_t CALIBRATION_BatteryReference(void) {
  uint8_t data[CALIBRATION_BATTERY_POINTS * 2u];
  if (!STORAGE_Read(STORAGE_CALIBRATION, CALIBRATION_BATTERY_OFFSET, data, sizeof(data)))
    return CALIBRATION_BATTERY_FALLBACK;
  uint16_t previous = 0, reference = 0;
  for (unsigned i = 0; i < CALIBRATION_BATTERY_POINTS; ++i) {
    uint16_t value = (uint16_t)data[i * 2u] | (uint16_t)((uint16_t)data[i * 2u + 1u] << 8);
    if (value <= previous || value > CALIBRATION_BATTERY_MAX)
      return CALIBRATION_BATTERY_FALLBACK;
    if (i == CALIBRATION_BATTERY_REFERENCE) reference = value;
    previous = value;
  }
  return reference;
}

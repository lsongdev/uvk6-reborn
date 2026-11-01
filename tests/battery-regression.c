#include "devices/battery.h"
#include "hal/board.h"
#include "hal/storage.h"
#include "store/calibration.h"
#include "store/device-config.h"
#include <assert.h>
#include <string.h>

DeviceConfig gDeviceConfig = {.preferences=CONFIG_DEFAULT_PREFERENCES};
bool gDeviceConfigReady;
static uint16_t points[6] = {1442, 2010, 2163, 2193, 2314, 2600};
static bool readable = true;
static uint16_t voltage = 2274;
static uint16_t chargingCurrent;

bool STORAGE_Read(StorageArea area, uint32_t offset, void *data, uint16_t size) {
  assert(area == STORAGE_CALIBRATION && offset == 0x140u && size == 12);
  if (!readable) return false;
  uint8_t *out = data;
  for (unsigned i = 0; i < 6; ++i) {
    out[i*2] = points[i]; out[i*2+1] = points[i] >> 8;
  }
  return true;
}
void BOARD_ADC_GetBatteryInfo(uint16_t *v, uint16_t *current) {
  *v=voltage; *current=chargingCurrent;
}

int main(void) {
  assert(CALIBRATION_BatteryReference() == 2193);
  gDeviceConfig.preferences.battery_calibration = CALIBRATION_BatteryReference();
  assert(gDeviceConfig.preferences.battery_calibration == 2193);
  BATTERY_UpdateBatteryInfo();
  assert(gBatteryVoltage == 788 && !gChargingWithTypeC);
  assert(BATTERY_GetPreciseVoltage(2193) == 78807);
  // Reproduce the reported 9.27 V display using the old default. The same
  // ADC reading is 8.45 V with this unit's factory reference, inside TX limits.
  voltage = 2440;
  chargingCurrent = 501; // Charging transition resets the voltage average.
  gDeviceConfig.preferences.battery_calibration = 2000;
  BATTERY_UpdateBatteryInfo();
  assert(gBatteryVoltage == 927);
  gDeviceConfig.preferences.battery_calibration = 2193;
  BATTERY_UpdateBatteryInfo();
  assert(gBatteryVoltage == 845);
  // The user's simultaneous meter reading needs a coefficient above 2154;
  // verify the actual calibration calculation and resulting displayed volts.
  uint16_t measured = BATTERY_GetCal(82900);
  assert(measured == 2236);
  gDeviceConfig.preferences.battery_calibration = measured;
  BATTERY_UpdateBatteryInfo();
  assert(gBatteryVoltage == 829);
  // Missing, erased, zero, out-of-order or out-of-range factory data returns
  // a nonzero fallback without mutating any existing user value.
  gDeviceConfig.preferences.battery_calibration = 2030;
  readable = false;
  assert(CALIBRATION_BatteryReference() == CALIBRATION_BATTERY_FALLBACK);
  readable = true;
  memset(points, 0xFF, sizeof(points));
  assert(CALIBRATION_BatteryReference() == CALIBRATION_BATTERY_FALLBACK);
  memset(points, 0, sizeof(points));
  assert(CALIBRATION_BatteryReference() == CALIBRATION_BATTERY_FALLBACK);
  uint16_t bad[6] = {1442,2010,2163,2163,2314,2600};
  memcpy(points, bad, sizeof(points));
  assert(CALIBRATION_BatteryReference() == CALIBRATION_BATTERY_FALLBACK);
  points[3] = 2193; points[5] = 4096;
  assert(CALIBRATION_BatteryReference() == CALIBRATION_BATTERY_FALLBACK);
  assert(gDeviceConfig.preferences.battery_calibration == 2030);
  return 0;
}

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

bool STORAGE_Read(StorageArea area, uint32_t offset, void *data, uint16_t size) {
  assert(area == STORAGE_CALIBRATION && offset == 0x140u && size == 12);
  if (!readable) return false;
  uint8_t *out = data;
  for (unsigned i = 0; i < 6; ++i) {
    out[i*2] = points[i]; out[i*2+1] = points[i] >> 8;
  }
  return true;
}
void BOARD_ADC_GetBatteryInfo(uint16_t *v, uint16_t *current) { *v=voltage; *current=0; }

int main(void) {
  gDeviceConfigReady = true;
  assert(!BATTERY_UseFactoryCalibration());
  assert(gDeviceConfig.preferences.battery_calibration == 2000);
  gDeviceConfigReady = false;
  assert(BATTERY_UseFactoryCalibration());
  assert(gDeviceConfig.preferences.battery_calibration == 2193);
  BATTERY_UpdateBatteryInfo();
  assert(gBatteryVoltage == 788 && !gChargingWithTypeC);
  assert(BATTERY_GetPreciseVoltage(2193) == 78807);
  // Missing, erased, zero, out-of-order or out-of-range calibration cannot
  // replace the existing default/user value or introduce division by zero.
  gDeviceConfig.preferences.battery_calibration = 2030;
  readable = false;
  assert(!BATTERY_UseFactoryCalibration());
  readable = true;
  memset(points, 0xFF, sizeof(points));
  assert(!BATTERY_UseFactoryCalibration());
  memset(points, 0, sizeof(points));
  assert(!BATTERY_UseFactoryCalibration());
  uint16_t bad[6] = {1442,2010,2163,2163,2314,2600};
  memcpy(points, bad, sizeof(points));
  assert(!BATTERY_UseFactoryCalibration());
  points[3] = 2193; points[5] = 4096;
  assert(!BATTERY_UseFactoryCalibration());
  assert(gDeviceConfig.preferences.battery_calibration == 2030);
  return 0;
}

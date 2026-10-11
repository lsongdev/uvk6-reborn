#include "device-config.h"
#include "driver/eeprom.h"
#include "scheduler.h"
#include "settings.h"

DeviceConfig gDeviceConfig;
bool gDeviceConfigReady;
static uint32_t committedSequence;
static uint32_t saveAt;

static bool readStorage(void *context, uint32_t address, void *data, uint16_t size) {
  (void)context;
  return EEPROM_ReadBuffer(address, data, size) == 0;
}
static bool writeStorage(void *context, uint32_t address, const void *data,
                         uint16_t size) {
  (void)context;
  return EEPROM_TryWriteBuffer(address, data, size);
}
static ConfigStore storage(void) {
  uint32_t size = EEPROM_DetectSize();
  return (ConfigStore){.read = readStorage, .write = writeStorage,
      .capacity = size, .reserved_tail = size >= 32768 ? PATCH_SIZE : 0};
}
static void exportPreferences(void) {
  ConfigPreferences *p = &gDeviceConfig.preferences;
  p->battery_calibration = gSettings.batteryCalibration;
  p->backlight = gSettings.backlight;
  p->contrast = gSettings.contrast;
  p->brightness = gSettings.brightness;
  p->battery_type = gSettings.batteryType;
  p->battery_style = gSettings.batteryStyle;
  p->backlight_on_squelch = gSettings.backlightOnSquelch;
  p->keylock = gSettings.keylock;
  p->ptt_lock = gSettings.pttLock;
  p->beep = gSettings.beep;
  p->si4732_power_off = gSettings.si4732PowerOff;
  p->mic = gSettings.mic;
  p->deviation = gSettings.deviation;
  p->bound_240_280 = gSettings.bound_240_280;
}
bool DEVICE_Load(void) {
  gDeviceConfigReady = false;
  CONFIG_Default(&gDeviceConfig);
  if (EEPROM_DetectType() == EEPROM_UNKNOWN)
    return false;
  ConfigStore io = storage();
  if (!CONFIG_Load(&io, &gDeviceConfig, &committedSequence))
    return false;
  gDeviceConfigReady = true;
  saveAt = 0;
  return true;
}
bool DEVICE_Initialize(void) {
  if (EEPROM_DetectType() == EEPROM_UNKNOWN)
    return false;
  CONFIG_Default(&gDeviceConfig);
  exportPreferences();
  ConfigStore io = storage();
  if (!CONFIG_Save(&io, &gDeviceConfig, &committedSequence))
    return false;
  gDeviceConfigReady = true;
  saveAt = 0;
  return true;
}
void DEVICE_RequestSave(void) {
  if (gDeviceConfigReady)
    saveAt = Now() + 1500u;
}
bool DEVICE_SaveNow(void) {
  if (!gDeviceConfigReady)
    return false;
  exportPreferences();
  ConfigStore io = storage();
  if (!CONFIG_Save(&io, &gDeviceConfig, &committedSequence))
    return false;
  saveAt = 0;
  return true;
}
void DEVICE_Update(void) {
  if (saveAt && (int32_t)(Now() - saveAt) >= 0 && !DEVICE_SaveNow())
    saveAt = Now() + 1500u;
}

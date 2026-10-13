#include "core/device-config.h"
#include "core/channel-store.h"
#include "hal/eeprom.h"
#include "hal/scheduler.h"
#include "core/settings.h"

// The boot entry point initializes defaults before touching peripherals.
// Keeping this in BSS avoids copying the full configuration into Flash.
DeviceConfig gDeviceConfig;
bool gDeviceConfigReady;
static uint32_t committedSequence;
static uint32_t saveAt;
static bool dirty, saveFailed;

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
bool DEVICE_Load(void) {
  gDeviceConfigReady = false;
  dirty = saveFailed = false;
  CONFIG_Default(&gDeviceConfig);
  if (EEPROM_DetectType() == EEPROM_UNKNOWN)
    return false;
  ConfigStore io = storage();
  if (!CONFIG_Load(&io, &gDeviceConfig, &committedSequence))
    return false;
  gDeviceConfigReady = true;
  (void)CHANNEL_STORE_Init();
  saveAt = 0;
  dirty = saveFailed = false;
  // Older format-8 preferences allowed an idle level above active brightness.
  // Normalize that relationship without rejecting an otherwise valid config.
  if (gDeviceConfig.preferences.brightness_low > gDeviceConfig.preferences.brightness) {
    gDeviceConfig.preferences.brightness_low = gDeviceConfig.preferences.brightness;
    DEVICE_RequestSave();
  }
  return true;
}
bool DEVICE_Initialize(void) {
  if (EEPROM_DetectType() == EEPROM_UNKNOWN)
    return false;
  CONFIG_Default(&gDeviceConfig);
  ConfigStore io = storage();
  if (!CONFIG_Save(&io, &gDeviceConfig, &committedSequence)) {
    dirty = saveFailed = true;
    saveAt = Now() + 5000u;
    return false;
  }
  gDeviceConfigReady = true;
  (void)CHANNEL_STORE_Init();
  saveAt = 0;
  dirty = saveFailed = false;
  return true;
}
void DEVICE_RequestSave(void) {
  if (gDeviceConfigReady) {
    dirty = true;
    saveAt = Now() + 1500u;
  }
}
bool DEVICE_SaveNow(void) {
  if (!gDeviceConfigReady)
    return false;
  ConfigStore io = storage();
  if (!CONFIG_Save(&io, &gDeviceConfig, &committedSequence)) {
    dirty = saveFailed = true;
    saveAt = Now() + 5000u;
    return false;
  }
  saveAt = 0;
  dirty = saveFailed = false;
  return true;
}
void DEVICE_Update(void) {
  if (gDeviceConfigReady && dirty && (int32_t)(Now() - saveAt) >= 0)
    (void)DEVICE_SaveNow();
}

DeviceSaveStatus DEVICE_GetSaveStatus(void) {
  if (!gDeviceConfigReady) return DEVICE_UNAVAILABLE;
  if (saveFailed) return DEVICE_SAVE_FAILED;
  return dirty ? DEVICE_PENDING : DEVICE_SAVED;
}
bool DEVICE_SavePreferences(const ConfigPreferences *preferences) {
  ConfigPreferences previous = gDeviceConfig.preferences;
  gDeviceConfig.preferences = *preferences;
  if (DEVICE_SaveNow()) return true;
  gDeviceConfig.preferences = previous;
  return false;
}

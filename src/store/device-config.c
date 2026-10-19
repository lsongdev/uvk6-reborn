#include "store/device-config.h"
#include "store/channel-store.h"
#include "hal/storage.h"
#include "hal/scheduler.h"
#include "store/preferences.h"

// The boot entry point initializes defaults before touching peripherals.
// Keeping this in BSS avoids copying the full configuration into Flash.
DeviceConfig gDeviceConfig;
bool gDeviceConfigReady;
static uint32_t committedSequence;
static uint32_t saveAt;
static bool dirty, saveFailed;

static StorageArea area(unsigned slot) { return slot ? STORAGE_CONFIG_B : STORAGE_CONFIG_A; }
static bool readStorage(void *context, unsigned slot, uint16_t offset, void *data,
                        uint16_t size) {
  (void)context;
  return STORAGE_Read(area(slot), offset, data, size);
}
static bool writeStorage(void *context, unsigned slot, const void *data, uint16_t size) {
  (void)context;
  return STORAGE_Write(area(slot), 0, data, size);
}
static ConfigStore storage(void) {
  return (ConfigStore){.read = readStorage, .write = writeStorage};
}
bool DEVICE_Load(void) {
  gDeviceConfigReady = false;
  dirty = saveFailed = false;
  CONFIG_Default(&gDeviceConfig);
  if (!STORAGE_Ready())
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
  if (!STORAGE_Ready())
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

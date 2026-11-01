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
static bool dirty, saveFailed, volatileConfig;

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
static void defaults(void) {
  CONFIG_Default(&gDeviceConfig);
  gDeviceConfig.preferences.battery_calibration = CALIBRATION_BatteryReference();
}
bool DEVICE_Load(void) {
  gDeviceConfigReady = false;
  dirty = saveFailed = false;
  defaults();
  if (!STORAGE_Ready())
    return false;
  uint16_t reference = gDeviceConfig.preferences.battery_calibration;
  ConfigStore io = storage();
  if (!CONFIG_Load(&io, &gDeviceConfig, &committedSequence))
    return false;
  gDeviceConfigReady = true;
  (void)CHANNEL_STORE_Init();
  saveAt = 0;
  dirty = saveFailed = false;
  // Older firmware persisted 2000 during initialization, discarding the
  // factory reference. Repair that legacy default; keep other user values.
  if (gDeviceConfig.preferences.battery_calibration == CALIBRATION_BATTERY_FALLBACK) {
    if (reference != CALIBRATION_BATTERY_FALLBACK) {
      gDeviceConfig.preferences.battery_calibration = reference;
      DEVICE_RequestSave();
    }
  }
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
  defaults();
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
bool DEVICE_UseVolatile(void) {
  if (!STORAGE_Ready() || STORAGE_Size(STORAGE_CONFIG_A) || STORAGE_Size(STORAGE_CONFIG_B))
    return false;
  defaults();
  gDeviceConfigReady = volatileConfig = true;
  saveAt = 0;
  dirty = saveFailed = false;
  return true;
}
bool DEVICE_SaveNow(void) {
  if (!gDeviceConfigReady)
    return false;
  if (volatileConfig) { // Accepted in RAM only.
    dirty = false;
    return true;
  }
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
  if (volatileConfig) return DEVICE_VOLATILE;
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

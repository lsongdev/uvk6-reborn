#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H
#include "store/config-store.h"
extern DeviceConfig gDeviceConfig;
extern bool gDeviceConfigReady;
bool DEVICE_Load(void);
bool DEVICE_Initialize(void); // Explicit user-confirmed format.
void DEVICE_RequestSave(void);
bool DEVICE_SaveNow(void);
void DEVICE_Update(void);
typedef enum { DEVICE_UNAVAILABLE, DEVICE_SAVED, DEVICE_PENDING,
               DEVICE_SAVE_FAILED } DeviceSaveStatus;
DeviceSaveStatus DEVICE_GetSaveStatus(void);
// Save a small preference draft before committing it to the running device.
bool DEVICE_SavePreferences(const ConfigPreferences *preferences);
#endif

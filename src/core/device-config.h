#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H
#include "core/config-store.h"
extern DeviceConfig gDeviceConfig;
extern bool gDeviceConfigReady;
bool DEVICE_Load(void);
bool DEVICE_Initialize(void); // Explicit user-confirmed format.
void DEVICE_RequestSave(void);
bool DEVICE_SaveNow(void);
void DEVICE_Update(void);
#endif

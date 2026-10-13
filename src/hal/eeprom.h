#ifndef DRIVER_EEPROM_H
#define DRIVER_EEPROM_H

#include "core/settings.h"
#include <stdbool.h>
#include <stdint.h>

extern bool gEepromWrite;

// Writes remain disabled until a valid config or explicit initialization.
void EEPROM_SetWritesEnabled(bool enabled);

int EEPROM_ReadBuffer(uint32_t Address, void *pBuffer, uint16_t Size);
bool EEPROM_TryWriteBuffer(uint32_t address, const uint8_t *data, uint16_t size);
bool EEPROM_Detect(uint8_t device_addr);
uint16_t EEPROM_GetPageSize(void);
uint32_t EEPROM_DetectSize(void);
void EEPROM_Init(void);
#if UVK6_SEED_PA_CAL
bool EEPROM_SeedPaCalibration(void);
#endif
EEPROMType EEPROM_DetectType(void);

#endif

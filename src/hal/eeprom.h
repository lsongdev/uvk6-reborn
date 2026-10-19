#ifndef HAL_EEPROM_H
#define HAL_EEPROM_H

#include <stdbool.h>
#include <stdint.h>

// The UV-K6 I2C EEPROM by absolute address. Only board code (storage.c, the
// UART maintenance commands) uses it; everything else goes through
// hal/storage.h.
typedef enum {
  EEPROM_BL24C64,   //
  EEPROM_BL24C128,  //
  EEPROM_BL24C256,  //
  EEPROM_BL24C512,  //
  EEPROM_BL24C1024, //
  EEPROM_M24M02,    // 256 KiB
  EEPROM_4MBIT,     // 512 KiB, eight I2C address banks
  EEPROM_UNKNOWN,
} EEPROMType;

extern const char *EEPROM_TYPE_NAMES[7];

// Writes remain disabled until a valid config or explicit initialization.
void EEPROM_SetWritesEnabled(bool enabled);

int EEPROM_ReadBuffer(uint32_t Address, void *pBuffer, uint16_t Size);
bool EEPROM_TryWriteBuffer(uint32_t address, const uint8_t *data, uint16_t size);
// Writes and reads back; only the resource tail and the calibration block
// (tools/resources.py over UART).
bool EEPROM_WriteMaintenance(uint32_t address, const uint8_t *data, uint16_t size);
bool EEPROM_Detect(uint8_t device_addr);
uint16_t EEPROM_GetPageSize(void);
uint32_t EEPROM_DetectSize(void);
void EEPROM_Init(void);
EEPROMType EEPROM_DetectType(void);

#endif

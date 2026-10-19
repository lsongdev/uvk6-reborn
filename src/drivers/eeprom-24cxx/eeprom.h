#ifndef HAL_EEPROM_H
#define HAL_EEPROM_H

#include <stdbool.h>
#include <stdint.h>

// 24Cxx-style I2C EEPROMs (BL24C64 ... 4 Mbit banked parts) by absolute
// address. Only board code uses it; everything else goes through
// hal/storage.h. The driver applies no layout policy: the board decides
// which addresses may be written.
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

// ACK address patterns can only identify a candidate memory mapping.
// They do not establish the physical part number, capacity, or page size.
static inline EEPROMType EEPROM_CandidateFromAckMask(uint8_t mask) {
  switch (mask) {
  case 0x01: return EEPROM_BL24C64;
  case 0x03: return EEPROM_BL24C128;
  case 0x0f: return EEPROM_M24M02;
  case 0xff: return EEPROM_4MBIT;
  default: return EEPROM_UNKNOWN;
  }
}

int EEPROM_ReadBuffer(uint32_t Address, void *pBuffer, uint16_t Size);
// Page-split write, at most 32 bytes per page until the part is verified.
bool EEPROM_WriteBuffer(uint32_t address, const uint8_t *data, uint16_t size);
bool EEPROM_Detect(uint8_t device_addr);
uint16_t EEPROM_GetPageSize(void);
uint32_t EEPROM_DetectSize(void);
void EEPROM_Init(void);
EEPROMType EEPROM_DetectType(void);

#endif

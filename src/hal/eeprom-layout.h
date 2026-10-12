#ifndef EEPROM_LAYOUT_H
#define EEPROM_LAYOUT_H
#include "core/config-store.h"
#include "core/settings.h"

// Only format7 slots at 0x0800..0x0fff are writable in normal firmware.
// Legacy 0x0000..0x03ff slots, RF calibration and SI patch stay read-only.
static inline bool EEPROM_WriteAllowed(uint32_t address, uint16_t size) {
  return size > 0 && address >= CONFIG_STORE_BASE &&
         address < CONFIG_STORE_END && size <= CONFIG_STORE_END-address;
}
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
#endif

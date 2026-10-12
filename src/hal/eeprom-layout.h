#ifndef EEPROM_LAYOUT_H
#define EEPROM_LAYOUT_H
#include "core/config-store.h"
#include "core/settings.h"

// Only the two UVR2 slots are writable in normal firmware. EEPROM contents
// beyond this range (especially RF calibration and SI SSB patch) are read-only.
static inline bool EEPROM_WriteAllowed(uint32_t address, uint16_t size) {
  const uint32_t limit = CONFIG_STORE_SLOT_SIZE*CONFIG_STORE_SLOTS;
  return size > 0 && address < limit && size <= limit-address;
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

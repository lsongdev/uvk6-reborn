#ifndef EEPROM_LAYOUT_H
#define EEPROM_LAYOUT_H
#include "core/config-store.h"
#include "core/channel-store.h"
#include "core/settings.h"
#include "core/resource.h"

// Only the active config pair and dedicated user-channel records are writable.
// Legacy channel records, factory calibration and the SI patch remain read-only.
static inline bool EEPROM_WriteAllowed(uint32_t address, uint16_t size) {
  if (!size) return false;
  bool config = address >= CONFIG_STORE_BASE && address < CONFIG_STORE_END &&
      size <= CONFIG_STORE_END-address;
  bool channels = address >= CHANNEL_STORE_BASE && address < CHANNEL_STORE_END &&
      size <= CHANNEL_STORE_END-address;
  return config || channels;
}
// The resource tail at the top is written only through EEPROM_WriteResource
// (the PC tool over UART), never by the configuration or channel code.
static inline bool EEPROM_ResourceWriteAllowed(uint32_t address, uint16_t size,
                                               uint32_t capacity) {
  return size && RESOURCE_SsbPatchAddress(capacity) &&
      address >= capacity - RESOURCE_TAIL_BYTES && address < capacity &&
      size <= capacity - address;
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

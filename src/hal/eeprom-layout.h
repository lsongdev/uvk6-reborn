#ifndef EEPROM_LAYOUT_H
#define EEPROM_LAYOUT_H
#include "core/config-store.h"

// Only the two UVR2 slots are writable in normal firmware. EEPROM contents
// beyond this range (especially RF calibration and SI SSB patch) are read-only.
static inline bool EEPROM_WriteAllowed(uint32_t address, uint16_t size) {
  const uint32_t limit = CONFIG_STORE_SLOT_SIZE*CONFIG_STORE_SLOTS;
  return size > 0 && address < limit && size <= limit-address;
}
#endif

#ifndef EEPROM_LAYOUT_H
#define EEPROM_LAYOUT_H
// Where the UV-K6 I2C EEPROM keeps each storage area. Writes need no erase,
// so a channel's two copies may sit side by side.
#include "core/calibration.h"
#include "core/config-store.h"
#include "core/channel-store.h"
#include "core/resource.h"
#include "hal/eeprom.h"

#define CONFIG_STORE_BASE 0x0800u
#define CONFIG_STORE_END (CONFIG_STORE_BASE + CONFIG_STORE_SLOT_SIZE*CONFIG_STORE_SLOTS)
// Channel n (one-based), copy c at BASE + ((n-1)*2 + c) * 64.
#define CHANNEL_STORE_BASE 0xB000u
#define CHANNEL_STORE_END \
  (CHANNEL_STORE_BASE + CHANNEL_STORE_COPY_COUNT * CHANNEL_STORE_AREA_BYTES)
#define CAL_BASE 0x1E00u
#define CAL_END (CAL_BASE + CALIBRATION_BYTES)

_Static_assert(CONFIG_STORE_END <= CAL_BASE, "Never overlap RF calibration");
_Static_assert(CHANNEL_STORE_BASE >= CAL_END,
               "User channel storage must not overlap factory calibration");

// The resource tail at the top of parts large enough for it, else 0.
static inline uint32_t EEPROM_ResourceBase(uint32_t capacity) {
  return capacity >= 0x20000u ? capacity - RESOURCE_TAIL_BYTES : 0;
}

// Only the active config pair and dedicated user-channel records are writable.
// Legacy channel records, the rest of calibration and the SI patch are read-only.
static inline bool EEPROM_WriteAllowed(uint32_t address, uint16_t size) {
  if (!size) return false;
  bool config = address >= CONFIG_STORE_BASE && address < CONFIG_STORE_END &&
      size <= CONFIG_STORE_END-address;
  bool channels = address >= CHANNEL_STORE_BASE && address < CHANNEL_STORE_END &&
      size <= CHANNEL_STORE_END-address;
  return config || channels;
}
// The resource tail and the calibration block are written only through
// EEPROM_WriteMaintenance (the PC tool over UART), never by the configuration
// or channel code.
static inline bool EEPROM_ResourceWriteAllowed(uint32_t address, uint16_t size,
                                               uint32_t capacity) {
  uint32_t base = EEPROM_ResourceBase(capacity);
  return size && base && address >= base && address < capacity &&
      size <= capacity - address;
}
// Within 0x1E00-0x1FFF, so a backed-up block can be restored whole.
static inline bool EEPROM_CalibrationWriteAllowed(uint32_t address, uint16_t size) {
  return size && address >= CAL_BASE && address < CAL_END && size <= CAL_END - address;
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

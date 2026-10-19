#ifndef HAL_STORAGE_H
#define HAL_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

// Persistent areas. Each board places them on its own medium, so callers
// address bytes within an area and never see absolute addresses.
//
// A write may, if power fails during it, disturb other bytes of the same
// area (a flash erase unit), but never another area. Keep the two copies of
// a record in different areas, and finish one copy before writing the other.
typedef enum {
  STORAGE_CONFIG_A,   // One configuration record (CONFIG_STORE_SLOT_SIZE).
  STORAGE_CONFIG_B,
  STORAGE_CHANNELS_A, // USER_CHANNEL_COUNT channel records, one copy each.
  STORAGE_CHANNELS_B,
  STORAGE_CALIBRATION, // Factory calibration, UV-K5 layout; read-only.
  STORAGE_RESOURCES,   // Resources installed by tools/resources.py; read-only.
  STORAGE_AREA_COUNT,
} StorageArea;

// Set by every completed write; the status line shows and clears it.
extern bool gStorageWrite;

void STORAGE_Init(void);
// False when the medium is missing or not recognized; nothing is writable.
bool STORAGE_Ready(void);
const char *STORAGE_Name(void);
uint32_t STORAGE_Capacity(void); // Bytes of the whole medium.
uint32_t STORAGE_Size(StorageArea area); // 0 when this medium lacks the area.

// Writes stay disabled until a valid config loads or the user initializes.
void STORAGE_SetWritesEnabled(bool enabled);

bool STORAGE_Read(StorageArea area, uint32_t offset, void *data, uint16_t size);
// Configuration and channel areas only.
bool STORAGE_Write(StorageArea area, uint32_t offset, const void *data, uint16_t size);

#endif

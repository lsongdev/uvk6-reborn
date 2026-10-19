#include "hal/storage.h"
#include "hal/eeprom.h"
#include "hal/eeprom-layout.h"

// UV-K6: every area is a window onto the I2C EEPROM.
bool gStorageWrite;

typedef struct {
  uint32_t base;
  uint32_t size;
} Window;

static Window windows[STORAGE_AREA_COUNT];

void STORAGE_Init(void) {
  EEPROM_Init();
  uint32_t capacity = EEPROM_DetectSize();
  if (capacity < CONFIG_STORE_END) return; // Nothing usable, not even config.
  windows[STORAGE_CONFIG_A] = (Window){CONFIG_STORE_BASE, CONFIG_STORE_SLOT_SIZE};
  windows[STORAGE_CONFIG_B] =
      (Window){CONFIG_STORE_BASE + CONFIG_STORE_SLOT_SIZE, CONFIG_STORE_SLOT_SIZE};
  windows[STORAGE_CALIBRATION] = (Window){CAL_BASE, CALIBRATION_BYTES};
  uint32_t resources = EEPROM_ResourceBase(capacity);
  if (resources >= CHANNEL_STORE_END) {
    // Channel copies interleave in the EEPROM; see channelAddress().
    windows[STORAGE_CHANNELS_A] = (Window){CHANNEL_STORE_BASE, CHANNEL_STORE_AREA_BYTES};
    windows[STORAGE_CHANNELS_B] = (Window){CHANNEL_STORE_BASE, CHANNEL_STORE_AREA_BYTES};
    windows[STORAGE_RESOURCES] = (Window){resources, RESOURCE_TAIL_BYTES};
  }
}

bool STORAGE_Ready(void) { return windows[STORAGE_CONFIG_A].size != 0; }

const char *STORAGE_Name(void) {
  EEPROMType type = EEPROM_DetectType();
  return type < EEPROM_UNKNOWN ? EEPROM_TYPE_NAMES[type] : "EEPROM unknown";
}

uint32_t STORAGE_Capacity(void) { return EEPROM_DetectSize(); }

uint32_t STORAGE_Size(StorageArea area) {
  return area < STORAGE_AREA_COUNT ? windows[area].size : 0;
}

void STORAGE_SetWritesEnabled(bool enabled) { EEPROM_SetWritesEnabled(enabled); }

// Channel n's copy c is the 64-byte record ((n-1)*2 + c) from the base; an
// access never crosses a record.
static bool channelAddress(StorageArea area, uint32_t offset, uint16_t size,
                           uint32_t *address) {
  uint32_t record = offset / CHANNEL_STORE_RECORD_BYTES, at = offset % CHANNEL_STORE_RECORD_BYTES;
  if (size > CHANNEL_STORE_RECORD_BYTES - at) return false;
  uint32_t copy = area == STORAGE_CHANNELS_B;
  *address = CHANNEL_STORE_BASE +
      (record * CHANNEL_STORE_COPY_COUNT + copy) * CHANNEL_STORE_RECORD_BYTES + at;
  return true;
}

static bool resolve(StorageArea area, uint32_t offset, uint16_t size, uint32_t *address) {
  if (area >= STORAGE_AREA_COUNT || !size) return false;
  const Window *w = &windows[area];
  if (offset >= w->size || size > w->size - offset) return false;
  if (area == STORAGE_CHANNELS_A || area == STORAGE_CHANNELS_B)
    return channelAddress(area, offset, size, address);
  *address = w->base + offset;
  return true;
}

bool STORAGE_Read(StorageArea area, uint32_t offset, void *data, uint16_t size) {
  uint32_t address;
  return resolve(area, offset, size, &address) && EEPROM_ReadBuffer(address, data, size) == 0;
}

bool STORAGE_Write(StorageArea area, uint32_t offset, const void *data, uint16_t size) {
  uint32_t address;
  // EEPROM_TryWriteBuffer applies the layout policy again.
  return area <= STORAGE_CHANNELS_B && resolve(area, offset, size, &address) &&
         EEPROM_TryWriteBuffer(address, data, size);
}

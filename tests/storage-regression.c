// UV-K6 storage areas must land on the EEPROM addresses earlier firmware
// used, so existing configuration and channels stay readable.
#include "../src/hal/storage.c"
#include <assert.h>
#include <string.h>

static uint32_t capacity = 524288u, lastRead, lastWrite;
static unsigned writes;
const char *EEPROM_TYPE_NAMES[7] = {"64 ?", "128", "256", "512", "1024", "M02?", "4Mb?"};
void EEPROM_Init(void) {}
void EEPROM_SetWritesEnabled(bool enabled) { (void)enabled; }
uint32_t EEPROM_DetectSize(void) { return capacity; }
EEPROMType EEPROM_DetectType(void) { return capacity ? EEPROM_4MBIT : EEPROM_UNKNOWN; }
int EEPROM_ReadBuffer(uint32_t address, void *data, uint16_t size) {
  lastRead = address; memset(data, 0, size); return 0;
}
bool EEPROM_TryWriteBuffer(uint32_t address, const uint8_t *data, uint16_t size) {
  (void)data;
  assert(EEPROM_WriteAllowed(address, size));
  lastWrite = address; ++writes; return true;
}

static uint32_t readAt(StorageArea area, uint32_t offset) {
  uint8_t b[2];
  lastRead = UINT32_MAX;
  assert(STORAGE_Read(area, offset, b, sizeof(b)));
  return lastRead;
}

int main(void) {
  uint8_t record[64] = {0};
  STORAGE_Init();
  assert(STORAGE_Ready() && STORAGE_Capacity() == 524288u);
  // Configuration slots 0x0800 and 0x0C00.
  assert(readAt(STORAGE_CONFIG_A, 0) == 0x0800 && readAt(STORAGE_CONFIG_B, 0) == 0x0C00);
  assert(readAt(STORAGE_CONFIG_B, 1022) == 0x0FFE);
  assert(!STORAGE_Read(STORAGE_CONFIG_A, 1023, record, 2));
  // Channel n copy c at 0xB000 + ((n-1)*2 + c) * 64.
  assert(readAt(STORAGE_CHANNELS_A, 0) == 0xB000 && readAt(STORAGE_CHANNELS_B, 0) == 0xB040);
  assert(readAt(STORAGE_CHANNELS_A, 11 * 64 + 2) == 0xB000 + 22 * 64 + 2);
  assert(readAt(STORAGE_CHANNELS_B, 199 * 64) == 0x11400 - 64);
  assert(!STORAGE_Read(STORAGE_CHANNELS_A, 63, record, 2)); // Never across records.
  assert(!STORAGE_Read(STORAGE_CHANNELS_A, 200 * 64, record, 1));
  // Calibration and the resource tail.
  assert(readAt(STORAGE_CALIBRATION, 0xF0) == 0x1EF0);
  assert(STORAGE_Size(STORAGE_RESOURCES) == RESOURCE_TAIL_BYTES);
  assert(readAt(STORAGE_RESOURCES, RESOURCE_SSB_PATCH_OFFSET) == 0x7DD78);
  // Writes reach configuration and channel areas only.
  assert(STORAGE_Write(STORAGE_CONFIG_B, 0, record, 32) && lastWrite == 0x0C00);
  assert(STORAGE_Write(STORAGE_CHANNELS_B, 64, record, 64) && lastWrite == 0xB0C0);
  writes = 0;
  assert(!STORAGE_Write(STORAGE_CALIBRATION, 0, record, 9));
  assert(!STORAGE_Write(STORAGE_RESOURCES, 0, record, 16));
  assert(!STORAGE_Write(STORAGE_CHANNELS_A, 32, record, 64) && !writes);
  // An 8 KiB part keeps the configuration but has no channels or resources.
  memset(windows, 0, sizeof(windows));
  capacity = 8192;
  STORAGE_Init();
  assert(STORAGE_Ready() && STORAGE_Size(STORAGE_CHANNELS_A) == 0 &&
         STORAGE_Size(STORAGE_RESOURCES) == 0);
  // No EEPROM at all: nothing is ready.
  memset(windows, 0, sizeof(windows));
  capacity = 0;
  STORAGE_Init();
  assert(!STORAGE_Ready() && !STORAGE_Read(STORAGE_CONFIG_A, 0, record, 1));
  return 0;
}

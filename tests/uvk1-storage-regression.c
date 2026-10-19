// Mock SPI registers; exercise the real board mapping and read-only driver.
#define UVK1_PINS_H
#include <stdint.h>
#include <assert.h>
#include <string.h>
#define UVK1_FLASH_CS 3u
#define SPI_SR_BSY 0x80u
static struct { uint32_t SR; } spi;
static struct { uint32_t BSRR; } gpio;
#define SPI2 (&spi)
#define GPIOA (&gpio)
#include "../src/boards/uvk1/storage.c"

static uint8_t sent[1024], received[1024];
static unsigned count;
void PY32_SPI2_Init(void) {}
uint8_t PY32_SPI2_Transfer(uint8_t value) {
  assert(count < sizeof(sent));
  sent[count] = value;
  return received[count++];
}
static void initId(uint32_t id) {
  count = 0;
  memset(received, 0, sizeof(received));
  received[1] = id >> 16; received[2] = id >> 8; received[3] = id;
  STORAGE_Init();
  assert(count == 4 && sent[0] == 0x9F);
  assert(gpio.BSRR == (1u << UVK1_FLASH_CS)); // CS released.
  count = 0;
}
int main(void) {
  uint8_t data[8];
  initId(0x856015);
  assert(STORAGE_Ready());
  initId(0xEB6015);
  assert(STORAGE_Ready() && BOARD_RawSize() == 2097152u);
  assert(!strcmp(STORAGE_Name(), "SPI EB6015 RO"));
  assert(STORAGE_Size(STORAGE_CONFIG_A) == 0);
  assert(STORAGE_Size(STORAGE_CALIBRATION) == 512);
  received[4] = 0x42;
  assert(STORAGE_Read(STORAGE_CALIBRATION, 0xC0, data, 1) && data[0] == 0x42);
  assert(count == 5 && !memcmp(sent, "\x03\x01\x00\xC0\xff", 5));
  count = 0;
  assert(!STORAGE_Read(STORAGE_CALIBRATION, 511, data, 2));
  assert(!STORAGE_Read(STORAGE_CONFIG_A, 0, data, 1));
  assert(!BOARD_RawRead(0x200000, data, 1));
  assert(!BOARD_RawRead(0x1FFFFF, data, 2));
  assert(!BOARD_RawRead(0xFFFFFFFF, data, 1));
  assert(!BOARD_RawRead(0, data, 0) && !BOARD_RawRead(0, NULL, 1));
  assert(!count);
  STORAGE_SetWritesEnabled(true);
  for (StorageArea area = 0; area < STORAGE_AREA_COUNT; ++area)
    assert(!STORAGE_Write(area, 0, data, 1));
  assert(!BOARD_MaintenanceWrite(0x010000, data, 1));
  assert(!BOARD_MaintenanceWrite(0, data, 1));
  assert(!count && !gStorageWrite); // No SPI traffic at all for rejected writes.
  assert(BOARD_RawRead(0x1FFFFF, data, 1));
  assert(!memcmp(sent, "\x03\x1f\xff\xff", 4));
  initId(0xFFFFFF);
  assert(!STORAGE_Ready() && !BOARD_RawRead(0, data, 1) && !count);
  initId(0x856017); // An unexpected capacity must not silently wrap at 2 MiB.
  assert(!STORAGE_Ready() && !BOARD_RawRead(0, data, 1) && !count);
  return 0;
}

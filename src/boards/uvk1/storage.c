#include "hal/storage.h"
#include "hal/board.h"
#include "boards/uvk1/pins.h"
#include "drivers/py32f071/spi.h"
#include "drivers/py25q16/py25q16.h"
#include "store/calibration.h"

#define CAL_PHYSICAL 0x010000u
#define FLASH_BYTES (2u * 1024u * 1024u)
static uint32_t capacity;
static uint32_t jedec;
static char name[] = "SPI 000000 RO";
bool gStorageWrite;

static void selectFlash(bool selected) {
  // Transfer waits for RXNE; wait for the last clock before deasserting CS.
  while (SPI2->SR & SPI_SR_BSY) continue;
  GPIOA->BSRR = 1u << (UVK1_FLASH_CS + (selected ? 16u : 0u));
}
static const Py25q16Bus bus = {selectFlash, PY32_SPI2_Transfer};

void STORAGE_Init(void) {
  capacity = 0;
  PY32_SPI2_Init();
  jedec = PY25Q16_ReadJedecId(&bus);
  for (unsigned i = 0; i < 6; ++i)
    name[4 + i] = "0123456789ABCDEF"[(jedec >> (20u - 4u * i)) & 15u];
  // 2 MiB NOR: EB/60/15 measured on this K1; also support PY25Q16.
  // Fail closed on other IDs; neither path enables any write opcode.
  if (jedec == 0xEB6015u || jedec == 0x856015u) capacity = FLASH_BYTES;
}
bool STORAGE_Ready(void) { return capacity != 0; }
const char *STORAGE_Name(void) { return name; }
uint32_t STORAGE_Capacity(void) { return capacity; }
uint32_t STORAGE_Size(StorageArea area) {
  return capacity && area == STORAGE_CALIBRATION ? CALIBRATION_BYTES : 0;
}
void STORAGE_SetWritesEnabled(bool enabled) { (void)enabled; }
bool STORAGE_Read(StorageArea area, uint32_t offset, void *data, uint16_t size) {
  return area == STORAGE_CALIBRATION && offset < CALIBRATION_BYTES &&
         size <= CALIBRATION_BYTES - offset && BOARD_RawRead(CAL_PHYSICAL + offset, data, size);
}
bool STORAGE_Write(StorageArea area, uint32_t offset, const void *data, uint16_t size) {
  (void)area; (void)offset; (void)data; (void)size;
  return false;
}
uint32_t BOARD_RawSize(void) { return capacity; }
bool BOARD_RawRead(uint32_t address, void *data, uint16_t size) {
  if (!data || !size || address >= capacity || size > capacity - address) return false;
  PY25Q16_Read(&bus, address, data, size);
  return true;
}
bool BOARD_MaintenanceWrite(uint32_t address, const uint8_t *data, uint16_t size) {
  (void)address; (void)data; (void)size;
  return false;
}

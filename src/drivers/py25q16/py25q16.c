#include "drivers/py25q16/py25q16.h"
#include "hal/system.h"

static void command(const Py25q16Bus *bus, uint8_t opcode, uint32_t address) {
  bus->transfer(opcode);
  bus->transfer(address >> 16);
  bus->transfer(address >> 8);
  bus->transfer(address);
}

uint32_t PY25Q16_ReadJedecId(const Py25q16Bus *bus) {
  bus->select(true);
  bus->transfer(0x9F);
  uint32_t id = 0;
  for (unsigned i = 0; i < 3; ++i) id = (id << 8) | bus->transfer(0xFF);
  bus->select(false);
  return id;
}

uint8_t PY25Q16_ReadStatus(const Py25q16Bus *bus) {
  bus->select(true);
  bus->transfer(0x05);
  uint8_t status = bus->transfer(0xFF);
  bus->select(false);
  return status;
}

void PY25Q16_Read(const Py25q16Bus *bus, uint32_t address, void *data, uint16_t size) {
  uint8_t *out = data;
  bus->select(true);
  command(bus, 0x03, address);
  for (uint16_t i = 0; i < size; ++i) out[i] = bus->transfer(0xFF);
  bus->select(false);
}

static bool writeEnable(const Py25q16Bus *bus) {
  bus->select(true);
  bus->transfer(0x06);
  bus->select(false);
  return PY25Q16_ReadStatus(bus) & PY25Q16_STATUS_WEL;
}

// Polls every millisecond; the chip clears WEL itself when it finishes.
static bool waitReady(const Py25q16Bus *bus, uint16_t limit_ms) {
  for (uint16_t ms = 0;; ++ms) {
    if (!(PY25Q16_ReadStatus(bus) & PY25Q16_STATUS_BUSY)) return true;
    if (ms >= limit_ms) return false;
    SYS_DelayMs(1);
  }
}

bool PY25Q16_EraseSector(const Py25q16Bus *bus, uint32_t address) {
  if (address % PY25Q16_SECTOR_BYTES || !writeEnable(bus)) return false;
  bus->select(true);
  command(bus, 0x20, address);
  bus->select(false);
  return waitReady(bus, 500); // Generous: 4 KiB NOR erases take tens of ms.
}

bool PY25Q16_Program(const Py25q16Bus *bus, uint32_t address, const void *data,
                     uint16_t size) {
  const uint8_t *in = data;
  while (size) {
    uint16_t n = (uint16_t)(PY25Q16_PAGE_BYTES - address % PY25Q16_PAGE_BYTES);
    if (n > size) n = size;
    if (!writeEnable(bus)) return false;
    bus->select(true);
    command(bus, 0x02, address);
    for (uint16_t i = 0; i < n; ++i) bus->transfer(in[i]);
    bus->select(false);
    if (!waitReady(bus, 10)) return false; // Generous: a page takes about 1 ms.
    address += n; in += n; size -= n;
  }
  return true;
}

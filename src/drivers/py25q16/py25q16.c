#include "drivers/py25q16/py25q16.h"

uint32_t PY25Q16_ReadJedecId(const Py25q16Bus *bus) {
  bus->select(true);
  bus->transfer(0x9F);
  uint32_t id = 0;
  for (unsigned i = 0; i < 3; ++i) id = (id << 8) | bus->transfer(0xFF);
  bus->select(false);
  return id;
}

void PY25Q16_Read(const Py25q16Bus *bus, uint32_t address, void *data, uint16_t size) {
  uint8_t *out = data;
  bus->select(true);
  bus->transfer(0x03);
  bus->transfer(address >> 16);
  bus->transfer(address >> 8);
  bus->transfer(address);
  for (uint16_t i = 0; i < size; ++i) out[i] = bus->transfer(0xFF);
  bus->select(false);
}

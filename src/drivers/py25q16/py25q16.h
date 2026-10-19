#ifndef PY25Q16_H
#define PY25Q16_H
#include <stdbool.h>
#include <stdint.h>

// Only read opcodes exist in this driver. The board owns the bus and CS.
typedef struct {
  void (*select)(bool selected);
  uint8_t (*transfer)(uint8_t value);
} Py25q16Bus;
uint32_t PY25Q16_ReadJedecId(const Py25q16Bus *bus);
void PY25Q16_Read(const Py25q16Bus *bus, uint32_t address, void *data, uint16_t size);
#endif

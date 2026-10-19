#ifndef PY25Q16_H
#define PY25Q16_H
#include <stdbool.h>
#include <stdint.h>

// SPI NOR commands only; the board owns the bus, CS and which addresses may
// ever be erased or programmed.
typedef struct {
  void (*select)(bool selected);
  uint8_t (*transfer)(uint8_t value);
} Py25q16Bus;

#define PY25Q16_SECTOR_BYTES 4096u
#define PY25Q16_PAGE_BYTES 256u
// Status register 1: busy, write enable latch, block protection BP0..BP4.
#define PY25Q16_STATUS_BUSY 0x01u
#define PY25Q16_STATUS_WEL 0x02u
#define PY25Q16_STATUS_PROTECT 0x7Cu

uint32_t PY25Q16_ReadJedecId(const Py25q16Bus *bus);
uint8_t PY25Q16_ReadStatus(const Py25q16Bus *bus);
void PY25Q16_Read(const Py25q16Bus *bus, uint32_t address, void *data, uint16_t size);
// Both wait for completion; false when the chip refused the write enable or
// stayed busy past a generous time limit. Neither verifies the data.
bool PY25Q16_EraseSector(const Py25q16Bus *bus, uint32_t address); // 4 KiB aligned.
// Split at page boundaries: a NOR page program wraps instead of crossing one.
bool PY25Q16_Program(const Py25q16Bus *bus, uint32_t address, const void *data,
                     uint16_t size);
#endif

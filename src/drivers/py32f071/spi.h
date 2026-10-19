#ifndef PY32F071_SPI_H
#define PY32F071_SPI_H

#include <stdint.h>

// SPI2, polled, mode 3: the board's serial flash bus. hal/spi.h is SPI1.
void PY32_SPI2_Init(void);
uint8_t PY32_SPI2_Transfer(uint8_t value);

#endif

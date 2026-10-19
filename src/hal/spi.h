#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stdbool.h>
#include <stdint.h>

// The display bus: write-only, mode 3, board-controlled chip select.
void SPI_Init(void);
void SPI_Select(bool selected);
void SPI_Write(uint8_t value); // Queues one byte; waits only for FIFO space.
void SPI_Flush(void);          // Waits until queued bytes have left.

#endif

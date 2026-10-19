#ifndef HAL_UART_H
#define HAL_UART_H

#include "hal/serial.h"

// The K-plug UART at 38400 baud, from the MCU driver.
#define UART_RX_RING_BYTES 256u
void UART_Init(void);
extern const SerialPort UART_PORT;

#endif

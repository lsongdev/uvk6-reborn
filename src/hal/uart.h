#ifndef DRIVER_UART_H
#define DRIVER_UART_H

#include <stdbool.h>
#include <stdint.h>


// Transport, from the MCU driver: the hardware fills gUartRxRing in a loop
// and UART_RxHead is the index it writes next.
#define UART_RX_RING_BYTES 256u
extern uint8_t gUartRxRing[UART_RX_RING_BYTES];
void UART_Init(void);
void UART_Send(const void *pBuffer, uint32_t Size);
uint16_t UART_RxHead(void);

// PC link protocol (hal/uart.c).

bool UART_IsCommandAvailable(void);
void UART_HandleCommand(void);

#endif

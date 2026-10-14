#ifndef DRIVER_UART_H
#define DRIVER_UART_H

#include <stdbool.h>
#include <stdint.h>


void UART_Init(void);
void UART_Send(const void *pBuffer, uint32_t Size);

bool UART_IsCommandAvailable(void);
void UART_HandleCommand(void);

#endif

#ifndef APP_LINK_UART_LINK_H
#define APP_LINK_UART_LINK_H

#include <stdbool.h>

// PC link protocol over hal/uart.h (tools/resources.py): framing,
// obfuscation and the command set.
bool UART_IsCommandAvailable(void);
void UART_HandleCommand(void);

#endif

#ifndef APP_LINK_UART_LINK_H
#define APP_LINK_UART_LINK_H

#include <stdbool.h>

// PC link protocol over the board's serial ports (hal/serial.h,
// tools/resources.py): framing, obfuscation and the command set.
bool LINK_IsCommandAvailable(void);
void LINK_HandleCommand(void);

#endif

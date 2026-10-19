#ifndef PY32F071_USB_H
#define PY32F071_USB_H

#include "hal/serial.h"

// USB full-speed CDC-ACM serial port on the MCU's own USB device. Received
// bytes land in a ring like the UART's; output waits for the host to open
// the port (DTR) and is dropped otherwise, so nothing ever blocks for long.
void USB_Init(void);
extern const SerialPort USB_PORT;

#endif

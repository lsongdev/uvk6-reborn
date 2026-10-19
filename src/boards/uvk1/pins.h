#ifndef UVK1_PINS_H
#define UVK1_PINS_H
#include "drivers/py32f071/port.h"
// Reference wiring: armel/uv-k1-k5v3-firmware-custom, commit 5fedc09.
// SWD PA13/PA14 remains available. There is no LCD reset or BK1080 enable.
#define UVK1_FLASH_CS 3u   // PA3, idle high
#define UVK1_BACKLIGHT 8u  // PF8, idle low
#define UVK1_I2C_SCL 5u    // PF5, open drain, idle high
#define UVK1_I2C_SDA 6u    // PF6, open drain, idle high
#endif

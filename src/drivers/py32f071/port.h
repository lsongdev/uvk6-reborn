#ifndef PY32F071_PORT_H
#define PY32F071_PORT_H

#include "py32f0xx.h"
#include <stdbool.h>
#include <stdint.h>

// Ports A, B, C and F; PY32_NO_LINE marks a hal/gpio.h line the board lacks
// (writes are ignored, reads return false).
enum { PY32_PORT_A, PY32_PORT_B, PY32_PORT_C, PY32_PORT_F, PY32_NO_LINE = 0xFF };

// One hal/gpio.h line. The board defines GPIO_LINES, indexed by GpioLine.
typedef struct {
  uint8_t port;
  uint8_t pin;
} Py32GpioLine;

#define PY32_GPIO_LINE(port, pin) {PY32_PORT_##port, pin}
#define PY32_GPIO_NONE {PY32_NO_LINE, 0}

extern const Py32GpioLine GPIO_LINES[];

typedef enum { PY32_INPUT, PY32_OUTPUT, PY32_ALTERNATE, PY32_ANALOG } Py32PinMode;
typedef enum { PY32_PULL_NONE, PY32_PULL_UP, PY32_PULL_DOWN } Py32Pull;

GPIO_TypeDef *PY32_Port(uint8_t port);
void PY32_PinMode(uint8_t port, uint8_t pin, Py32PinMode mode, Py32Pull pull);
// Alternate function af (0..15), push-pull, very high speed.
void PY32_PinAlternate(uint8_t port, uint8_t pin, uint8_t af, Py32Pull pull);

#endif

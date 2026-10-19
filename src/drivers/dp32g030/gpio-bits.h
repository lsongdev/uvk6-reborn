#ifndef DP32G030_GPIO_BITS_H
#define DP32G030_GPIO_BITS_H

#include "drivers/dp32g030/gpio.h"
#include "drivers/dp32g030/portcon.h"
#include <stdint.h>

void GPIO_ClearBit(volatile uint32_t *pReg, uint8_t Bit);
uint8_t GPIO_CheckBit(volatile const uint32_t *pReg, uint8_t Bit);
void GPIO_SetBit(volatile uint32_t *pReg, uint8_t Bit);

// One hal/gpio.h line: port (0 = A, 1 = B, 2 = C) and bit. The board defines
// GPIO_LINES, indexed by GpioLine. Port registers are evenly spaced, so the
// bank and its input-enable register follow from the port number.
typedef struct {
  uint8_t port;
  uint8_t bit;
} Dp32GpioLine;

#define DP32_GPIO_PORT_A 0u
#define DP32_GPIO_PORT_B 1u
#define DP32_GPIO_PORT_C 2u
#define DP32_GPIO_LINE(port, bit) {DP32_GPIO_PORT_##port, bit}

extern const Dp32GpioLine GPIO_LINES[];

#endif

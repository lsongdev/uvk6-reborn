#ifndef DP32G030_GPIO_BITS_H
#define DP32G030_GPIO_BITS_H

#include "drivers/dp32g030/gpio.h"
#include "drivers/dp32g030/portcon.h"
#include <stdint.h>

void GPIO_ClearBit(volatile uint32_t *pReg, uint8_t Bit);
uint8_t GPIO_CheckBit(volatile const uint32_t *pReg, uint8_t Bit);
void GPIO_SetBit(volatile uint32_t *pReg, uint8_t Bit);

// One hal/gpio.h line: its port and, for GPIO_SetInput, the port's input
// enable register. The board defines GPIO_LINES, indexed by GpioLine.
typedef struct {
  volatile GPIO_Bank_t *bank;
  volatile uint32_t *inputEnable; // PORTCON_PORTx_IE.
  uint8_t bit;
} Dp32GpioLine;

#define DP32_GPIO_LINE(port, bit) {GPIO##port, &PORTCON_PORT##port##_IE, bit}

extern const Dp32GpioLine GPIO_LINES[];

#endif

/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "drivers/dp32g030/gpio-bits.h"
#include "hal/gpio.h"

_Static_assert(GPIOB_BASE_ADDR - GPIOA_BASE_ADDR == 0x800u &&
               GPIOC_BASE_ADDR - GPIOB_BASE_ADDR == 0x800u, "GPIO bank stride");
_Static_assert(PORTCON_PORTB_IE_ADDR - PORTCON_PORTA_IE_ADDR == 4u &&
               PORTCON_PORTC_IE_ADDR - PORTCON_PORTB_IE_ADDR == 4u, "PORTCON IE stride");

static volatile GPIO_Bank_t *bank(const Dp32GpioLine *l) {
  return (volatile GPIO_Bank_t *)(GPIOA_BASE_ADDR + 0x800u * l->port);
}

void GPIO_Write(GpioLine line, bool high) {
  const Dp32GpioLine *l = &GPIO_LINES[line];
  if (l->port == DP32_GPIO_NO_LINE) return;
  if (high)
    GPIO_SetBit(&bank(l)->DATA, l->bit);
  else
    GPIO_ClearBit(&bank(l)->DATA, l->bit);
}

bool GPIO_Read(GpioLine line) {
  const Dp32GpioLine *l = &GPIO_LINES[line];
  return l->port != DP32_GPIO_NO_LINE && GPIO_CheckBit(&bank(l)->DATA, l->bit);
}

void GPIO_SetInput(GpioLine line, bool input) {
  const Dp32GpioLine *l = &GPIO_LINES[line];
  if (l->port == DP32_GPIO_NO_LINE) return;
  volatile uint32_t *inputEnable = (volatile uint32_t *)(PORTCON_PORTA_IE_ADDR + 4u * l->port);
  // Input buffer on only while reading, as the DP32G030 pin muxing expects.
  if (input) {
    *inputEnable |= 1U << l->bit;
    bank(l)->DIR &= ~(1U << l->bit);
  } else {
    *inputEnable &= ~(1U << l->bit);
    bank(l)->DIR |= 1U << l->bit;
  }
}

void GPIO_ClearBit(volatile uint32_t *pReg, uint8_t Bit)
{
	*pReg &= ~(1U << Bit);
}

uint8_t GPIO_CheckBit(volatile const uint32_t *pReg, uint8_t Bit)
{
	return (*pReg >> Bit) & 1U;
}


void GPIO_SetBit(volatile uint32_t *pReg, uint8_t Bit)
{
	*pReg |= 1U << Bit;
}


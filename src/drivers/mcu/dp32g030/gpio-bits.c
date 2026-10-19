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

#include "drivers/mcu/dp32g030/gpio-bits.h"
#include "hal/gpio.h"

void GPIO_Write(GpioLine line, bool high) {
  const Dp32GpioLine *l = &GPIO_LINES[line];
  if (high)
    GPIO_SetBit(&l->bank->DATA, l->bit);
  else
    GPIO_ClearBit(&l->bank->DATA, l->bit);
}

bool GPIO_Read(GpioLine line) {
  const Dp32GpioLine *l = &GPIO_LINES[line];
  return GPIO_CheckBit(&l->bank->DATA, l->bit);
}

void GPIO_SetInput(GpioLine line, bool input) {
  const Dp32GpioLine *l = &GPIO_LINES[line];
  // Input buffer on only while reading, as the DP32G030 pin muxing expects.
  if (input) {
    *l->inputEnable |= 1U << l->bit;
    l->bank->DIR &= ~(1U << l->bit);
  } else {
    *l->inputEnable &= ~(1U << l->bit);
    l->bank->DIR |= 1U << l->bit;
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


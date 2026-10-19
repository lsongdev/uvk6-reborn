#include "drivers/py32f071/adc.h"
#include "py32f0xx.h"

void PY32_ADC_Init(void) {
  RCC->CR = (RCC->CR & ~RCC_CR_ADC_DIV) | RCC_CR_ADC_DIV_0; // PCLK/4.
  // Regular group on the software trigger (EXTSEL 111).
  ADC1->CR2 = (ADC1->CR2 & ~ADC_CR2_EXTSEL) | ADC_CR2_EXTSEL;
  ADC1->CR2 |= ADC_CR2_CAL;
  for (uint32_t spins = 100000u; (ADC1->CR2 & ADC_CR2_CAL) && spins; --spins) continue;
  ADC1->CR2 |= ADC_CR2_ADON;
}

uint16_t PY32_ADC_Read(uint8_t channel) {
  // 41.5 cycles for every channel: the battery divider is high impedance.
  const uint32_t SMP_41_5 = 5u;
  volatile uint32_t *smpr = channel < 10u ? &ADC1->SMPR3 : &ADC1->SMPR2;
  uint32_t shift = 3u * (channel % 10u);
  *smpr = (*smpr & ~(7u << shift)) | SMP_41_5 << shift;
  ADC1->SQR3 = channel;
  ADC1->SR = 0;
  ADC1->CR2 |= ADC_CR2_SWSTART | ADC_CR2_EXTTRIG;
  for (uint32_t spins = 100000u; !(ADC1->SR & ADC_SR_EOC) && spins; --spins) continue;
  return (uint16_t)(ADC1->DR & 0xFFFu);
}

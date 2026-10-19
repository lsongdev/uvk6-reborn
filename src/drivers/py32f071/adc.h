#ifndef PY32F071_ADC_H
#define PY32F071_ADC_H

#include <stdint.h>

// ADC1, 12 bits, software trigger, one regular channel at a time.
void PY32_ADC_Init(void);
uint16_t PY32_ADC_Read(uint8_t channel);

#endif

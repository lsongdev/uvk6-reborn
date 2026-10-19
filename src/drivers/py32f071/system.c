#include "hal/system.h"
#include "hal/systick.h"
#include "py32f0xx.h"

void SYS_DelayMs(uint32_t Delay) { TIMER_DelayUs(Delay * 1000); }

void SYS_Reset(void) { NVIC_SystemReset(); }

// The bootloader leaves the core at 48 MHz (CPU_CLOCK_HZ); keep it.
void SYS_ConfigureClocks(void) {}

void SYS_ConfigureSysCon(void) {
  RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN |
                 RCC_IOPENR_GPIOFEN;
  RCC->AHBENR |= RCC_AHBENR_DMAEN;
  RCC->APBENR1 |= RCC_APBENR1_SPI2EN;
  RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN | RCC_APBENR2_SPI1EN | RCC_APBENR2_USART1EN |
                  RCC_APBENR2_ADCEN;
}

#include "hal/system.h"
#include "hal/systick.h"
#include "py32f0xx.h"

void SYS_DelayMs(uint32_t Delay) { TIMER_DelayUs(Delay * 1000); }

void SYS_Reset(void) { NVIC_SystemReset(); }

// The bootloader leaves the core at 48 MHz (CPU_CLOCK_HZ); keep it.
void SYS_ConfigureClocks(void) {}

// The bootloader jumps here with its own peripherals still running; it
// talks USART1 at 38400 baud too. A DMA channel it left on the USART1 RX
// request would take bytes from our ring, so the DMA controller, the request
// map and every peripheral we use start from reset. start.S has already
// disabled its interrupts.
static void resetLeftovers(void) {
  RCC->AHBRSTR |= RCC_AHBRSTR_DMARST;
  RCC->AHBRSTR &= ~RCC_AHBRSTR_DMARST;
  const uint32_t APB1 = RCC_APBRSTR1_SPI2RST | RCC_APBRSTR1_USART2RST | RCC_APBRSTR1_TIM7RST;
  const uint32_t APB2 = RCC_APBRSTR2_SPI1RST | RCC_APBRSTR2_USART1RST | RCC_APBRSTR2_ADCRST;
  RCC->APBRSTR1 |= APB1;
  RCC->APBRSTR1 &= ~APB1;
  RCC->APBRSTR2 |= APB2;
  RCC->APBRSTR2 &= ~APB2;
  SYSCFG->CFGR3 = 0; // DMA request map, channels 1-4.
  SYSCFG->CFGR4 = 0; // Channels 5-7.
}

void SYS_ConfigureSysCon(void) {
  RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN |
                 RCC_IOPENR_GPIOFEN;
  RCC->AHBENR |= RCC_AHBENR_DMAEN;
  RCC->APBENR1 |= RCC_APBENR1_SPI2EN;
  RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN | RCC_APBENR2_SPI1EN | RCC_APBENR2_USART1EN |
                  RCC_APBENR2_ADCEN;
  resetLeftovers();
}

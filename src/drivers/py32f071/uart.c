#include "hal/uart.h"
#include "hal/system.h"
#include "py32f0xx.h"

// USART1 at 38400 baud; DMA1 channel 2 loops over gUartRxRing.
uint8_t gUartRxRing[UART_RX_RING_BYTES];

#define RX_DMA DMA1_Channel2
#define DMA_MAP_USART1_RD 7u // SYSCFG_CFGR3 request number for USART1 RX.
#define BAUD 38400u

static uint32_t pclk(void) {
  // HPRE/PPRE: values below 8 (4 for PPRE) leave the clock undivided.
  static const uint8_t AHB_SHIFT[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
  static const uint8_t APB_SHIFT[8] = {0, 0, 0, 0, 1, 2, 3, 4};
  uint32_t hclk = CPU_CLOCK_HZ >> AHB_SHIFT[(RCC->CFGR & RCC_CFGR_HPRE) >> RCC_CFGR_HPRE_Pos];
  return hclk >> APB_SHIFT[(RCC->CFGR & RCC_CFGR_PPRE) >> RCC_CFGR_PPRE_Pos];
}

void UART_Init(void) {
  RX_DMA->CCR = 0;
  SYSCFG->CFGR3 = (SYSCFG->CFGR3 & ~(0x3Fu << 8)) | (DMA_MAP_USART1_RD << 8);
  RX_DMA->CPAR = (uint32_t)&USART1->DR;
  RX_DMA->CMAR = (uint32_t)gUartRxRing;
  RX_DMA->CNDTR = UART_RX_RING_BYTES;
  // Peripheral to memory, byte sized, memory increment, circular, high priority.
  RX_DMA->CCR = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PL_1;

  RCC->APBRSTR2 |= RCC_APBRSTR2_USART1RST;
  RCC->APBRSTR2 &= ~RCC_APBRSTR2_USART1RST;
  USART1->BRR = (pclk() + BAUD / 2u) / BAUD;
  USART1->CR3 = USART_CR3_DMAR;
  USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
  RX_DMA->CCR |= DMA_CCR_EN;
}

void UART_Send(const void *pBuffer, uint32_t Size) {
  const uint8_t *pData = pBuffer;
  for (uint32_t i = 0; i < Size; i++) {
    // A stuck transmitter must not freeze the main loop (or the PTT timeout).
    uint32_t spins = 100000u;
    while (!(USART1->SR & USART_SR_TXE)) {
      if (!--spins) return;
    }
    USART1->DR = pData[i];
  }
}

void _putchar(char c) { UART_Send((uint8_t *)&c, 1); }

// CNDTR counts down from the ring size and reloads in circular mode.
uint16_t UART_RxHead(void) {
  return (uint16_t)((UART_RX_RING_BYTES - RX_DMA->CNDTR) % UART_RX_RING_BYTES);
}

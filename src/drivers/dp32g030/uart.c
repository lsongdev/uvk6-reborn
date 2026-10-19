#include "hal/uart.h"
#include "drivers/dp32g030/dma.h"
#include "drivers/dp32g030/syscon.h"
#include "drivers/dp32g030/uart.h"

// UART1 at 38400 baud; DMA channel 0 loops over gUartRxRing.
static uint8_t gUartRxRing[UART_RX_RING_BYTES];

void UART_Init(void) {
  uint32_t Delta;
  uint32_t Positive;
  uint32_t Frequency;

  UART1->CTRL =
      (UART1->CTRL & ~UART_CTRL_UARTEN_MASK) | UART_CTRL_UARTEN_BITS_DISABLE;
  Delta = SYSCON_RC_FREQ_DELTA;
  Positive = (Delta & SYSCON_RC_FREQ_DELTA_RCHF_SIG_MASK) >>
             SYSCON_RC_FREQ_DELTA_RCHF_SIG_SHIFT;
  Frequency = (Delta & SYSCON_RC_FREQ_DELTA_RCHF_DELTA_MASK) >>
              SYSCON_RC_FREQ_DELTA_RCHF_DELTA_SHIFT;
  if (Positive) {
    Frequency += 48000000U;
  } else {
    Frequency = 48000000U - Frequency;
  }

  UART1->BAUD = Frequency / 39053U;
  UART1->CTRL = UART_CTRL_RXEN_BITS_ENABLE | UART_CTRL_TXEN_BITS_ENABLE |
                UART_CTRL_RXDMAEN_BITS_ENABLE;
  UART1->RXTO = 4;
  UART1->FC = 0;
  UART1->FIFO = UART_FIFO_RF_LEVEL_BITS_8_BYTE | UART_FIFO_RF_CLR_BITS_ENABLE |
                UART_FIFO_TF_CLR_BITS_ENABLE;
  UART1->IE = 0;

  DMA_CTR = (DMA_CTR & ~DMA_CTR_DMAEN_MASK) | DMA_CTR_DMAEN_BITS_DISABLE;

  DMA_CH0->MSADDR = (uint32_t)(uintptr_t)&UART1->RDR;
  DMA_CH0->MDADDR = (uint32_t)(uintptr_t)gUartRxRing;
  DMA_CH0->MOD = 0
                 // Source
                 | DMA_CH_MOD_MS_ADDMOD_BITS_NONE |
                 DMA_CH_MOD_MS_SIZE_BITS_8BIT |
                 DMA_CH_MOD_MS_SEL_BITS_HSREQ_MS1
                 // Destination
                 | DMA_CH_MOD_MD_ADDMOD_BITS_INCREMENT |
                 DMA_CH_MOD_MD_SIZE_BITS_8BIT | DMA_CH_MOD_MD_SEL_BITS_SRAM;
  DMA_INTEN = 0;
  DMA_INTST =
      0 | DMA_INTST_CH0_TC_INTST_BITS_SET | DMA_INTST_CH1_TC_INTST_BITS_SET |
      DMA_INTST_CH2_TC_INTST_BITS_SET | DMA_INTST_CH3_TC_INTST_BITS_SET |
      DMA_INTST_CH0_THC_INTST_BITS_SET | DMA_INTST_CH1_THC_INTST_BITS_SET |
      DMA_INTST_CH2_THC_INTST_BITS_SET | DMA_INTST_CH3_THC_INTST_BITS_SET;
  DMA_CH0->CTR = 0 | DMA_CH_CTR_CH_EN_BITS_ENABLE |
                 ((0xFF << DMA_CH_CTR_LENGTH_SHIFT) & DMA_CH_CTR_LENGTH_MASK) |
                 DMA_CH_CTR_LOOP_BITS_ENABLE | DMA_CH_CTR_PRI_BITS_MEDIUM;
  UART1->IF = UART_IF_RXTO_BITS_SET;

  DMA_CTR = (DMA_CTR & ~DMA_CTR_DMAEN_MASK) | DMA_CTR_DMAEN_BITS_ENABLE;

  UART1->CTRL |= UART_CTRL_UARTEN_BITS_ENABLE;
}

static void UART_Send(const void *pBuffer, uint32_t Size) {
  const uint8_t *pData = (const uint8_t *)pBuffer;
  uint32_t i;

  for (i = 0; i < Size; i++) {
    // The UART can fail or be stopped. Do not freeze the main loop
    // indefinitely (including the system's software PTT timeout).
    uint32_t spins = 100000u;
    while ((UART1->IF & UART_IF_TXFIFO_FULL_MASK) !=
           UART_IF_TXFIFO_FULL_BITS_NOT_SET) {
      if (!--spins) return;
    }
    UART1->TDR = pData[i];
  }
}

void _putchar(char c) { UART_Send((uint8_t *)&c, 1); }

static uint16_t UART_RxHead(void) { return DMA_CH0->ST & 0xFFFU; }

const SerialPort UART_PORT = {gUartRxRing, UART_RX_RING_BYTES, UART_RxHead, UART_Send, true};

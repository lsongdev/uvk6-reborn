#include "hal/spi.h"
#include "drivers/py32f071/spi.h"
#include "hal/gpio.h"
#include "py32f0xx.h"

// Byte-wide DR access: a 16-bit write would queue two frames in the FIFO.
#define DR8(spi) (*(volatile uint8_t *)&(spi)->DR)

static void configure(SPI_TypeDef *spi, uint32_t prescaler) {
  spi->CR1 = 0;
  spi->CR2 = 0;
  // Master, mode 3, MSB first, software NSS held high.
  spi->CR1 = SPI_CR1_MSTR | SPI_CR1_CPOL | SPI_CR1_CPHA | SPI_CR1_SSM | SPI_CR1_SSI |
             (prescaler << SPI_CR1_BR_Pos);
  spi->CR1 |= SPI_CR1_SPE;
}

// SPI1 to the ST7565 at PCLK/64 (750 kHz), as the stock-compatible firmware
// drives it; chip select is a GPIO line.
void SPI_Init(void) {
  configure(SPI1, 5u);
  GPIO_Write(GPIO_ST7565_CS, true);
}

void SPI_Select(bool selected) { GPIO_Write(GPIO_ST7565_CS, !selected); }

void SPI_Write(uint8_t value) {
  while (!(SPI1->SR & SPI_SR_TXE)) continue;
  DR8(SPI1) = value;
  while (!(SPI1->SR & SPI_SR_RXNE)) continue;
  (void)DR8(SPI1); // Drain each byte so the receiver cannot overrun.
}

void SPI_Flush(void) {
  while (SPI1->SR & (SPI_SR_FTLVL | SPI_SR_BSY)) continue;
  // Write-only bus: drop what the full-duplex receiver collected.
  while (SPI1->SR & SPI_SR_RXNE) (void)DR8(SPI1);
}

// SPI2 at PCLK/2 (24 MHz).
void PY32_SPI2_Init(void) { configure(SPI2, 0u); }

uint8_t PY32_SPI2_Transfer(uint8_t value) {
  while (!(SPI2->SR & SPI_SR_TXE)) continue;
  DR8(SPI2) = value;
  while (!(SPI2->SR & SPI_SR_RXNE)) continue;
  return DR8(SPI2);
}

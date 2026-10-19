#include "drivers/st7565/st7565.h"
#include "hal/gpio.h"
#include "hal/spi.h"
#include "hal/system.h"
#include "hal/systick.h"
#include <stdint.h>
#include <string.h>

static void ST7565_WriteByte(uint8_t value);

static uint8_t frameBufferSecond[8][ST7565_WIDTH];


static void ST7565_Reset(void) {
  GPIO_Write(GPIO_ST7565_RES, true);
  TIMER_DelayUs(1); // 1
  GPIO_Write(GPIO_ST7565_RES, false);
  SYS_DelayMs(1); // 20
  GPIO_Write(GPIO_ST7565_RES, true);
  SYS_DelayMs(5); // 120
}

static void ST7565_SelectColumnAndLine(uint8_t Column, uint8_t Line) {
  GPIO_Write(GPIO_ST7565_A0, false);
  SPI_Write(Line + 0xB0);
  SPI_Write(((Column >> 4) & 0x0F) | 0x10);
  SPI_Write(((Column >> 0) & 0x0F));
  SPI_Flush();
}

static void ST7565_FillScreen(uint8_t Value) {
  SPI_Select(true);

  for (uint8_t i = 0; i < 8; i++) {
    ST7565_SelectColumnAndLine(0, i);
    GPIO_Write(GPIO_ST7565_A0, true);
    for (uint8_t j = 0; j < 132; j++) {
      SPI_Write(Value);
    }
    SPI_Flush();
  }

  SPI_Select(false);
}

void ST7565_Blit(const uint8_t frame[ST7565_HEIGHT/8][ST7565_WIDTH]) {
  uint8_t Line;
  uint8_t Column;

  SPI_Select(true);
  ST7565_WriteByte(0x40);

  for (Line = 0; Line < ST7565_HEIGHT/8; Line++) {
    if (memcmp(frame[Line], frameBufferSecond[Line],
               ST7565_WIDTH) == 0) {
      continue;
    }

    ST7565_SelectColumnAndLine(4U, Line);
    GPIO_Write(GPIO_ST7565_A0, true);
    for (Column = 0; Column < ST7565_WIDTH; Column++) {
      SPI_Write(frame[Line][Column]);
    }
    SPI_Flush();

    memcpy(frameBufferSecond[Line], frame[Line],
           ST7565_WIDTH);
  }

  SPI_Select(false);
}

void ST7565_Init(bool full, uint8_t contrast) {
  if (full) {
    SPI_Init();
    ST7565_Reset();
    SPI_Select(true);
    ST7565_WriteByte(0xE2);
    SYS_DelayMs(5); // 120
  } else {
    SPI_Select(true);
  }

  ST7565_WriteByte(0xA2);
  ST7565_WriteByte(0xC0);
  ST7565_WriteByte(0xA1);
  ST7565_WriteByte(0xA6);
  ST7565_WriteByte(0xA4);
  ST7565_WriteByte(0x24);
  ST7565_WriteByte(0x81);
  ST7565_WriteByte(23 + contrast); // default 31

  if (full) {
    // setup power circuit
    ST7565_WriteByte(0x2B);
    SYS_DelayMs(1);
    ST7565_WriteByte(0x2E);
    SYS_DelayMs(1);

    // power circuit v1 v2 v3 v4
    for (uint8_t i = 0; i < 4; ++i) {
      ST7565_WriteByte(0x2F);
    }
    SYS_DelayMs(10); // 40
  }

  ST7565_WriteByte(0x40);
  ST7565_WriteByte(0xAF);
  SPI_Flush();
  SPI_Select(false);

  if (full) {
    ST7565_FillScreen(0x00);
  }
}

static void ST7565_WriteByte(uint8_t Value) {
  GPIO_Write(GPIO_ST7565_A0, false);
  SPI_Write(Value);
}

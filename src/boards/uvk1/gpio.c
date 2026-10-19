#include "boards/uvk1/pins.h"
#include "hal/gpio.h"
#include "hal/board.h"

const Py32GpioLine GPIO_LINES[] = {
  [GPIO_BK48X9_SCN] = PY32_GPIO_LINE(F, 9), // idle high
  [GPIO_BK48X9_SCL] = PY32_GPIO_LINE(B, 8), // idle high
  [GPIO_BK48X9_SDA] = PY32_GPIO_LINE(B, 9), // idle high
  [GPIO_ST7565_CS] = PY32_GPIO_LINE(B, 2),  // idle high
  [GPIO_ST7565_A0] = PY32_GPIO_LINE(A, 6), // idle high
  [GPIO_ST7565_RES] = PY32_GPIO_NONE,
  [GPIO_BK1080] = PY32_GPIO_NONE,
  [GPIO_AUDIO_PATH] = PY32_GPIO_LINE(A, 8), // idle low (mute)
  [GPIO_PTT] = PY32_GPIO_LINE(B, 10), // pull up
  // The matrix runs from the high pins down (reference PIN_COL(n) = PB6-n,
  // row n = PB15-n); measured on the radio 2026-10-19: MENU = PB6/PB15.
  [GPIO_KEY_OUT0] = PY32_GPIO_LINE(B, 6), // all columns idle high
  [GPIO_KEY_OUT1] = PY32_GPIO_LINE(B, 5),
  [GPIO_KEY_OUT2] = PY32_GPIO_LINE(B, 4),
  [GPIO_KEY_OUT3] = PY32_GPIO_LINE(B, 3),
  [GPIO_KEY_IN0] = PY32_GPIO_LINE(B, 15), // all rows pull up
  [GPIO_KEY_IN1] = PY32_GPIO_LINE(B, 14),
  [GPIO_KEY_IN2] = PY32_GPIO_LINE(B, 13),
  [GPIO_KEY_IN3] = PY32_GPIO_LINE(B, 12),
};
const uint8_t BOARD_KEYPAD_IDLE_LEVELS = 0x0F;

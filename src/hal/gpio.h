#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdbool.h>

// Board signals driven or read one at a time. Each board maps them to its
// own port and pin; drivers never name ports or registers.
typedef enum {
  GPIO_BK4819_SCN,
  GPIO_BK4819_SCL,
  GPIO_BK4819_SDA, // Bidirectional: see GPIO_SetInput.
  GPIO_ST7565_A0,
  GPIO_ST7565_RES,
  GPIO_BK1080,     // BK1080 enable (low = on); SI4732 reset on the mod.
  GPIO_AUDIO_PATH, // Speaker amplifier.
  GPIO_PTT,        // Input, low while pressed.
} GpioLine;

void GPIO_Write(GpioLine line, bool high);
bool GPIO_Read(GpioLine line);
// Switches a bidirectional line between input and push-pull output.
void GPIO_SetInput(GpioLine line, bool input);

#endif

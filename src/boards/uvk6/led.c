#include "hal/board.h"
#include "drivers/bk48x9/bk48x9.h"

// Preserve K6 REG33 bit 0 = green, bit 1 = red.
void BOARD_SetGreenLed(bool on) {
  BK48X9_ToggleGpioOut(BK48X9_GPIO6_PIN2,on);
}
void BOARD_SetRedLed(bool on) {
  BK48X9_ToggleGpioOut(BK48X9_GPIO5_PIN1,on);
}

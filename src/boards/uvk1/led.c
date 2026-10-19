#include "hal/board.h"
#include "drivers/bk48x9/bk48x9.h"

// K1 REG33 bit 1 = green, bit 0 = red (reference 5fedc09 and user test).
void BOARD_SetGreenLed(bool on) {
  BK48X9_ToggleGpioOut(BK48X9_GPIO5_PIN1,on);
}
void BOARD_SetRedLed(bool on) {
  BK48X9_ToggleGpioOut(BK48X9_GPIO6_PIN2,on);
}

#include "devices/led.h"
#include "hal/board.h"
void LED_SetGreen(bool on) {
  BOARD_SetGreenLed(on);
}
void LED_SetRed(bool on) {
  BOARD_SetRedLed(on);
}

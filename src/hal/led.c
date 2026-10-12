#include "hal/led.h"
#include "hal/gpio.h"
#include "drivers/dp32g030/gpio.h"
#include "drivers/bk4819/bk4819.h"
void LED_SetGreen(bool on) {
  BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_GREEN, on);
}
void LED_SetRed(bool on) {
  BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, on);
}
void LED_SetAux(bool on) {
  if (on)
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUX_LED);
  else
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUX_LED);
}

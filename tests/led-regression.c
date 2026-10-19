// Exercise the actual board LED mapping and RF GPIO read-modify-write.
#include "devices/led.h"
#include "drivers/bk48x9/bk48x9.h"
#include <assert.h>
static uint16_t gpio;
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) {
  assert(reg==BK48X9_REG_33);gpio=value;
}
int main(void) {
  const unsigned green = TEST_UVK1 ? 2u : 1u, red = TEST_UVK1 ? 1u : 2u;
  // LED operations must not change PA or RX_ENABLE, in either direction.
  for (unsigned tx=0;tx<2;++tx) {
    BK48X9_ToggleGpioOut(BK48X9_GPIO1_PIN29_PA_ENABLE,tx!=0);
    BK48X9_ToggleGpioOut(BK48X9_GPIO0_PIN28_RX_ENABLE,tx==0);
    uint16_t rf=gpio & ~3u;
    LED_SetGreen(false);LED_SetRed(false);assert(gpio==rf);
    LED_SetGreen(true);assert(gpio==(rf | green));
    LED_SetRed(true);assert(gpio==(rf | green | red));
    LED_SetGreen(false);assert(gpio==(rf | red));
    LED_SetRed(false);assert(gpio==rf);
  }
  return 0;
}

// Production builds must never authorize TX or turn on the PA.
#include "../src/hal/tx-device.c"
#include <assert.h>
DeviceConfig gDeviceConfig = {.preferences=CONFIG_DEFAULT_PREFERENCES};
uint16_t gBatteryVoltage=760;
static unsigned paStarts,paStops;
uint8_t GPIO_CheckBit(volatile const uint32_t *reg, uint8_t pin) {
  (void)reg; (void)pin; return 0;
}
void BK4819_ToggleGpioOut(BK4819_GPIO_PIN_t pin, bool enable) {
  assert(pin==BK4819_GPIO1_PIN29_PA_ENABLE);
  if(enable) ++paStarts; else ++paStops;
}
void BK4819_WriteRegister(BK4819_REGISTER_t reg,uint16_t value) {
  assert(reg==BK4819_REG_36 && value==0);
}
void LED_SetRed(bool on) { assert(!on); }
int main(void) {
  assert(!TX_DEVICE_Capabilities(145500000u).available);
  assert(TX_DEVICE_Capabilities(145500000u).power_mask==0);
  assert(!TX_DEVICE_Policy().authorized);
  TxPlan p={.frequency_hz=145500000u,.power=TX_POWER_LOW};
  assert(!TX_DEVICE_Prepare(&p));
  assert(!TX_DEVICE_Enable());
  TX_DEVICE_Disable();
  assert(paStarts==0 && paStops>=3);
  return 0;
}

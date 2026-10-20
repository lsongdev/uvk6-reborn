// ENABLE_TX=0 builds must never authorize TX or turn on the PA.
#include "../src/devices/tx-device.c"
#include <assert.h>
DeviceConfig gDeviceConfig = {.preferences=CONFIG_DEFAULT_PREFERENCES};
uint16_t gBatteryVoltage=760;
static unsigned paStarts,paStops;
bool GPIO_Read(GpioLine line) { (void)line; return false; }
void BK48X9_ToggleGpioOut(BK48X9_GPIO_PIN_t pin, bool enable) {
  assert(pin==BK48X9_GPIO1_PIN29_PA_ENABLE);
  if(enable) ++paStarts; else ++paStops;
}
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg,uint16_t value) {
  assert(reg==BK48X9_REG_36 && value==0);
}
void LED_SetRed(bool on) { assert(!on); }
int main(void) {
  assert(!TX_DEVICE_Capabilities(145500000u).available);
  assert(TX_DEVICE_Capabilities(145500000u).power_mask==0);
  assert(!TX_DEVICE_Policy().authorized);
  TxPlan p={.frequency_hz=145500000u,.power=TX_POWER_LOW};
  assert(!TX_DEVICE_Prepare(&p));
  assert(!TX_DEVICE_Enable());
  assert(!TX_DEVICE_BeginTail());
  TX_DEVICE_Disable();
  assert(paStarts==0 && paStops>=3);
  return 0;
}

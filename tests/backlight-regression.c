// Mock only the board PWM; exercise the actual backlight state machine.
#include "../src/devices/backlight.c"
#include <assert.h>
static uint32_t output;
void BOARD_BacklightInit(void) {}
void BOARD_BacklightSetLevel(uint8_t level) { output = level; }
DeviceConfig gDeviceConfig = {.preferences=CONFIG_DEFAULT_PREFERENCES};
uint8_t BL_TIME_VALUES[7]={0,5,10,20,60,120,255};
int main(void) {
  gDeviceConfig.preferences.backlight=1;
  BACKLIGHT_ApplySettings(); assert(state && countdown==5);
  for(unsigned i=0;i<5;++i) BACKLIGHT_Update();
  assert(!state && countdown==0);
  gDeviceConfig.preferences.backlight_on_squelch=BL_SQL_OPEN;
  BACKLIGHT_SetReception(true);
  for(unsigned i=0;i<20;++i) BACKLIGHT_Update();
  assert(state && countdown==5);
  BACKLIGHT_SetReception(false);assert(!state && countdown==0);
  gDeviceConfig.preferences.backlight=6;
  BACKLIGHT_ApplySettings();BACKLIGHT_SetReception(true);BACKLIGHT_SetReception(false);
  assert(state && countdown==255);
  gDeviceConfig.preferences.backlight=0;
  gDeviceConfig.preferences.brightness_low=3;
  BACKLIGHT_ApplySettings();assert(!state && output==3);
  BACKLIGHT_On();assert(!state && output==3);
  gDeviceConfig.preferences.backlight=1;
  gDeviceConfig.preferences.backlight_on_squelch=BL_SQL_ON;
  BACKLIGHT_ApplySettings();BACKLIGHT_SetReception(true);
  for(unsigned i=0;i<5;++i) BACKLIGHT_Update();
  assert(!state);
  return 0;
}

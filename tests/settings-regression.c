// Verify preference validation and side effects before touching hardware.
#include "../src/core/settings.c"
#include <assert.h>
DeviceConfig gDeviceConfig = {.preferences = CONFIG_DEFAULT_PREFERENCES};
static unsigned requests, lights, contrasts;
static bool saveWorks = true;
void DEVICE_RequestSave(void) { ++requests; }
bool DEVICE_SavePreferences(const ConfigPreferences *next) {
  if (!saveWorks) return false;
  gDeviceConfig.preferences=*next; return true;
}
void BACKLIGHT_ApplySettings(void) {
  ++lights;
  assert(gDeviceConfig.preferences.backlight < ARRAY_SIZE(BL_TIME_VALUES));
  assert(gDeviceConfig.preferences.brightness_low <= gDeviceConfig.preferences.brightness);
}
void DISPLAY_Init(bool full, uint8_t contrast) { (void)full; assert(contrast<=15); ++contrasts; }
int main(void) {
  assert(!SETTINGS_SetValue(SETTING_BACKLIGHT,7));
  assert(!SETTINGS_SetValue(SETTING_CONTRAST,16));
  assert(!SETTINGS_SetValue(SETTING_BATTERYCALIBRATION,1899));
  assert(!SETTINGS_SetValue(SETTING_BATTERYCALIBRATION,2155));
  assert(!SETTINGS_SetValue(SETTING_COUNT,1));
  assert(!requests && !lights && !contrasts);
  assert(SETTINGS_SetValue(SETTING_BACKLIGHT,3) && !requests);
  assert(SETTINGS_SetValue(SETTING_BACKLIGHT,6));
  SETTINGS_IncDecValue(SETTING_BACKLIGHT,true); assert(gDeviceConfig.preferences.backlight==0);
  SETTINGS_IncDecValue(SETTING_BACKLIGHT,false); assert(gDeviceConfig.preferences.backlight==6);
  for(unsigned i=0;i<100;++i) {
    SETTINGS_IncDecValue(SETTING_BACKLIGHT,true);
    assert(gDeviceConfig.preferences.backlight<7);
  }
  assert(SETTINGS_SetValue(SETTING_BRIGHTNESS_L,8));
  assert(!SETTINGS_SetValue(SETTING_BRIGHTNESS_L,9));
  assert(SETTINGS_SetValue(SETTING_BRIGHTNESS_H,3));
  assert(gDeviceConfig.preferences.brightness_low==3);
  SETTINGS_IncDecValue(SETTING_BRIGHTNESS_L,true); assert(gDeviceConfig.preferences.brightness_low==0);
  SETTINGS_IncDecValue(SETTING_BRIGHTNESS_L,false); assert(gDeviceConfig.preferences.brightness_low==3);
  SETTINGS_SetValue(SETTING_SQLOPENTIME,7); SETTINGS_IncDecValue(SETTING_SQLOPENTIME,true);
  assert(gDeviceConfig.preferences.sql_open_time==0);
  SETTINGS_IncDecValue(SETTING_SQLOPENTIME,false); assert(gDeviceConfig.preferences.sql_open_time==7);
  SETTINGS_SetValue(SETTING_SQLCLOSETIME,3); SETTINGS_IncDecValue(SETTING_SQLCLOSETIME,true);
  assert(gDeviceConfig.preferences.sql_close_time==0);
  assert(!SETTINGS_SetValue(SETTING_SQLCLOSETIME,4));
  assert(!SETTINGS_SetValue(SETTING_SQLOPENTIME,8));
  for(unsigned i=0;i<6;++i) SETTINGS_IncDecValue(SETTING_TX_TIMEOUT,true);
  assert(gDeviceConfig.preferences.tx_timeout_seconds==120);
  gDeviceConfig.preferences.battery_calibration=2100;
  gDeviceConfig.preferences.battery_type=2;
  gDeviceConfig.preferences.bound_240_280=1;
  gDeviceConfig.preferences.contrast=15;
  gDeviceConfig.radio.vfo_count=4;
  RadioUserState radio=gDeviceConfig.radio;
  ConfigPreferences before=gDeviceConfig.preferences;
  saveWorks=false; assert(!SETTINGS_ResetPreferences());
  assert(!memcmp(&before,&gDeviceConfig.preferences,sizeof(before)));
  saveWorks=true; assert(SETTINGS_ResetPreferences());
  assert(gDeviceConfig.preferences.battery_calibration==2100);
  assert(gDeviceConfig.preferences.battery_type==2 && gDeviceConfig.preferences.bound_240_280==1);
  assert(gDeviceConfig.preferences.sql_open_time==7 && gDeviceConfig.preferences.contrast==8);
  assert(!memcmp(&radio,&gDeviceConfig.radio,sizeof(radio)));
  return 0;
}

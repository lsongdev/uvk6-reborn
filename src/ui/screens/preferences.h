// Preference editing behind the settings screen: ranges, labels, side effects.
#ifndef UI_SCREENS_PREFERENCES_H
#define UI_SCREENS_PREFERENCES_H

#include <stdbool.h>
#include <stdint.h>
#include "store/preferences.h"

typedef enum {
  SETTING_BACKLIGHT,
  SETTING_BRIGHTNESS_H,
  SETTING_BRIGHTNESS_L,
  SETTING_CONTRAST,
  SETTING_BACKLIGHTONSQUELCH,
  SETTING_BATTERYCALIBRATION,
  SETTING_BATTERYTYPE,
  SETTING_BATTERYSTYLE,
  SETTING_KEYLOCK,
  SETTING_PTT_LOCK,
  SETTING_SI4732POWEROFF,
  SETTING_SQLOPENTIME,
  SETTING_SQLCLOSETIME,
  SETTING_BOUND240_280,
  SETTING_TX_TIMEOUT,
  SETTING_TX_BUSY_LOCK,
  SETTING_MIC,
  SETTING_COUNT,
} Setting;


uint32_t SETTINGS_GetValue(Setting s);
bool SETTINGS_SetValue(Setting s, uint32_t v);
void SETTINGS_GetValueText(Setting s, char *out, uint8_t size);
bool SETTINGS_ResetPreferences(void);
void SETTINGS_IncDecValue(Setting s, bool inc);

#endif

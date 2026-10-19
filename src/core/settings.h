#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

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

typedef enum {
  BL_SQL_OFF,
  BL_SQL_ON,
  BL_SQL_OPEN,
} BacklightOnSquelchMode;

typedef enum {
  BAT_1600,
  BAT_2200,
  BAT_3500,
} BatteryType;

typedef enum {
  BAT_CLEAN,
  BAT_PERCENT,
  BAT_VOLTAGE,
} BatteryStyle;



extern uint8_t BL_TIME_VALUES[7];
extern const char *BL_SQL_MODE_NAMES[3];

void SETTINGS_Save();
bool SETTINGS_Load(void);
uint32_t SETTINGS_GetFilterBound();
bool SETTINGS_IsPatchPresent();

uint32_t SETTINGS_GetValue(Setting s);
bool SETTINGS_SetValue(Setting s, uint32_t v);
void SETTINGS_GetValueText(Setting s, char *out, uint8_t size);
bool SETTINGS_ResetPreferences(void);
void SETTINGS_IncDecValue(Setting s, bool inc);

void SETTINGS_UpdateSave();

#endif /* end of include guard: SETTINGS_H */

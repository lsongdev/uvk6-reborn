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

typedef enum {
  EEPROM_BL24C64,   //
  EEPROM_BL24C128,  //
  EEPROM_BL24C256,  //
  EEPROM_BL24C512,  //
  EEPROM_BL24C1024, //
  EEPROM_M24M02,    // 256 KiB
  EEPROM_4MBIT,     // 512 KiB, eight I2C address banks
  EEPROM_UNKNOWN,
} EEPROMType;

extern const char *EEPROM_TYPE_NAMES[7];
extern const uint32_t EEPROM_SIZES[7];
extern const uint16_t PAGE_SIZES[7];


extern uint8_t BL_TIME_VALUES[7];
extern const char *BL_SQL_MODE_NAMES[3];

void SETTINGS_Save();
bool SETTINGS_Load(void);
uint32_t SETTINGS_GetFilterBound();
uint32_t SETTINGS_GetEEPROMSize();
bool SETTINGS_IsPatchPresent();

uint32_t SETTINGS_GetValue(Setting s);
bool SETTINGS_SetValue(Setting s, uint32_t v);
void SETTINGS_GetValueText(Setting s, char *out, uint8_t size);
bool SETTINGS_ResetPreferences(void);
void SETTINGS_IncDecValue(Setting s, bool inc);

void SETTINGS_UpdateSave();

#endif /* end of include guard: SETTINGS_H */

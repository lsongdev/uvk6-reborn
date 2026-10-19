#ifndef STORE_PREFERENCES_H
#define STORE_PREFERENCES_H

#include <stdint.h>

// Meaning of ConfigPreferences values (store/config-store.h) shared by the
// devices that apply them and the settings screen that edits them.
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

// Backlight timeout in seconds per preference index; 255 = always on.
extern uint8_t BL_TIME_VALUES[7];
extern const char *BL_SQL_MODE_NAMES[3];

// Squelch delay preferences one past the register range mean "the
// receiver chip's recommended delay".
#define PREFERENCES_SQL_OPEN_AUTO 8u
#define PREFERENCES_SQL_CLOSE_AUTO 4u

// VHF/UHF front-end filter boundary in 10 Hz units: 240 or 280 MHz.
#define PREFERENCES_FILTER_BOUND_240 24000000u
#define PREFERENCES_FILTER_BOUND_280 28000000u
uint32_t PREFERENCES_FilterBound(void);

#endif

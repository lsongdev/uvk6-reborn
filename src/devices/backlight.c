#include "store/device-config.h"
#include "devices/backlight.h"
#include "app/settings.h"
#include "hal/board.h"

static uint8_t duration = 2;
static uint8_t countdown;
static bool state = false;
static bool receptionOpen;

void BACKLIGHT_Init() {
  BOARD_BacklightInit();
  BACKLIGHT_ApplySettings();
}

void BACKLIGHT_ApplySettings(void) {
  const ConfigPreferences *p = &gDeviceConfig.preferences;
  BACKLIGHT_SetDuration(BL_TIME_VALUES[p->backlight]);
  state = duration != 0;
  BACKLIGHT_SetBrightness(state ? p->brightness : p->brightness_low);
}

void BACKLIGHT_SetBrightness(uint8_t brigtness) { BOARD_BacklightSetLevel(brigtness); }

void BACKLIGHT_Toggle(bool on) {
  if (state == on) {
    return;
  }
  state = on;
  BACKLIGHT_SetBrightness(on ? gDeviceConfig.preferences.brightness : gDeviceConfig.preferences.brightness_low);
}

void BACKLIGHT_On() {
  countdown = duration;
  BACKLIGHT_Toggle(countdown);
}

void BACKLIGHT_SetReception(bool open) {
  receptionOpen = open;
  uint8_t mode = gDeviceConfig.preferences.backlight_on_squelch;
  if (open && mode != BL_SQL_OFF)
    BACKLIGHT_On();
  else if (!open && mode == BL_SQL_OPEN && duration != 255) {
    countdown = 0;
    BACKLIGHT_Toggle(false);
  }
}

void BACKLIGHT_SetDuration(uint8_t durationSec) {
  duration = durationSec;
  countdown = durationSec;
}

void BACKLIGHT_Update() {
  if (receptionOpen && gDeviceConfig.preferences.backlight_on_squelch == BL_SQL_OPEN)
    return;
  if (countdown == 0 || countdown == 255) {
    return;
  }

  if (countdown == 1) {
    BACKLIGHT_Toggle(false);
    countdown = 0;
  } else {
    countdown--;
  }
}

#include "ui/screens/reset.h"
#include "core/device-config.h"
#include "hal/eeprom.h"
#include "external/CMSIS_5/Device/ARM/ARMCM0/Include/ARMCM0.h"
#include "core/settings.h"
#include "ui/graphics.h"
#include "ui/screens/statusline.h"

static bool confirm;
static bool failed;

static void enter(void) {
  confirm = failed = false;
  gDeviceConfig.preferences.keylock = false;
  STATUSLINE_SetText(EEPROM_DetectType() < EEPROM_UNKNOWN
                                 ? EEPROM_TYPE_NAMES[EEPROM_DetectType()]
                                 : "EEPROM unknown");
}
static void render(void) {
  if (EEPROM_DetectType() == EEPROM_UNKNOWN) {
    PrintMedium(2, 20, "EEPROM unknown");
    PrintMedium(2, 32, "Cannot initialize");
  } else if (failed) {
    PrintMedium(2, 20, "Write/verify failed");
    PrintMedium(2, 32, "EXIT: cancel");
  } else if (confirm) {
    PrintMedium(2, 20, "Initialize UVR2?");
    PrintMedium(2, 32, "1: YES  EXIT: NO");
  } else {
    PrintMedium(2, 20, "No UVR2 config");
    PrintMedium(2, 32, "Back up EEPROM!");
    PrintMedium(2, 44, "0: Initialize");
  }
}
static bool key(KEY_Code_t key, Key_State_t state) {
  if (state != KEY_RELEASED)
    return true;
  if (key == KEY_EXIT) {
    confirm = failed = false;
    return true;
  }
  if (EEPROM_DetectType() == EEPROM_UNKNOWN)
    return true;
  if (!confirm && key == KEY_0) {
    confirm = true;
    failed = false;
  } else if (confirm && key == KEY_1) {
    EEPROM_SetWritesEnabled(true); // Only after explicit user confirmation.
    if (DEVICE_Initialize())
      NVIC_SystemReset();
    else {
      EEPROM_SetWritesEnabled(false);
      failed = true;
      confirm = false;
    }
  }
  return true;
}

const Screen resetScreen = { .enter = enter, .render = render, .key = key };

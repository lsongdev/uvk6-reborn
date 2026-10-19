#include "ui/screens/reset.h"
#include "store/device-config.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "store/preferences.h"
#include "ui/graphics.h"
#include "devices/battery.h"
#include "core/text.h"
#include "ui/screens/statusline.h"

static bool confirm;
static bool failed;
static KEY_Code_t lastKey;
static Key_State_t lastState;
static bool readOnly(void) {
  return STORAGE_Ready() && !STORAGE_Size(STORAGE_CONFIG_A);
}

static void enter(void) {
  confirm = failed = false;
  lastKey = KEY_INVALID;
  lastState = KEY_RELEASED;
  gDeviceConfig.preferences.keylock = false;
  STATUSLINE_SetText(STORAGE_Ready() ? STORAGE_Name() : "Storage unknown");
}
static void render(void) {
  if (!STORAGE_Ready()) {
    PrintMedium(2, 20, "Storage unknown");
    PrintMedium(2, 32, "Cannot initialize");
  } else if (readOnly()) {
    PrintMedium(2, 18, "Storage read-only");
    char line[24];
    Text text = TEXT_Begin(line, sizeof(line));
    TEXT_Add(&text, "BAT ");
    TEXT_Number(&text, gBatteryVoltage / 100u, 1, 10);
    TEXT_Char(&text, '.');
    TEXT_Number(&text, gBatteryVoltage % 100u, 2, 10);
    TEXT_Add(&text, "V");
    PrintMedium(2, 30, line);
    text = TEXT_Begin(line, sizeof(line));
    TEXT_Add(&text, "KEY ");
    TEXT_Number(&text, lastKey, 1, 10);
    TEXT_Char(&text, '/');
    TEXT_Number(&text, lastState, 1, 10);
    PrintMedium(2, 42, line);
    PrintSmall(2, 55, "UART: back up flash");
  } else if (failed) {
    PrintMedium(2, 20, "Write/verify failed");
    PrintMedium(2, 32, "EXIT: cancel");
  } else if (confirm) {
    PrintMedium(2, 20, "Initialize UVR2?");
    PrintMedium(2, 32, "1: YES  EXIT: NO");
  } else {
    PrintMedium(2, 20, "No UVR2 config");
    PrintMedium(2, 32, "Back up first!");
    PrintMedium(2, 44, "0: Initialize");
  }
}
static bool key(KEY_Code_t key, Key_State_t state) {
  if (readOnly()) {
    lastKey = key;
    lastState = state;
    return true;
  }
  if (state != KEY_RELEASED)
    return true;
  if (key == KEY_EXIT) {
    confirm = failed = false;
    return true;
  }
  if (!STORAGE_Ready())
    return true;
  if (!confirm && key == KEY_0) {
    confirm = true;
    failed = false;
  } else if (confirm && key == KEY_1) {
    STORAGE_SetWritesEnabled(true); // Only after explicit user confirmation.
    if (DEVICE_Initialize())
      SYS_Reset();
    else {
      STORAGE_SetWritesEnabled(false);
      failed = true;
      confirm = false;
    }
  }
  return true;
}

const Screen resetScreen = { .enter = enter, .render = render, .key = key };

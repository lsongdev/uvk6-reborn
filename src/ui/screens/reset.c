#include "ui/screens/reset.h"
#include "store/device-config.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "app/settings.h"
#include "ui/graphics.h"
#include "ui/screens/statusline.h"

static bool confirm;
static bool failed;

static void enter(void) {
  confirm = failed = false;
  gDeviceConfig.preferences.keylock = false;
  STATUSLINE_SetText(STORAGE_Ready() ? STORAGE_Name() : "Storage unknown");
}
static void render(void) {
  if (!STORAGE_Ready()) {
    PrintMedium(2, 20, "Storage unknown");
    PrintMedium(2, 32, "Cannot initialize");
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

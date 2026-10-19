// Main loop: keys, rendering cadence, safety and the radio under every screen.
#include "store/device-config.h"
#include "app/loop.h"
#include "radio/radio.h"
#include "ui/screens/screen.h"
#include "ui/screens/home.h"
#include "ui/screens/reset.h"
#include "ui/screens/settings.h"
#include "devices/backlight.h"
#include "hal/storage.h"
#include "devices/keyboard.h"
#include "devices/display.h"
#include "hal/uart.h"
#include "app/link/uart-link.h"
#include "devices/battery.h"
#include "ui/menu.h"
#include "radio/receiver.h"
#include "hal/scheduler.h"
#include "app/settings.h"
#include "ui/graphics.h"
#include "ui/screens/statusline.h"

#define queueLen 20
#define itemSize sizeof(SystemMessages)

static char notificationMessage[16] = "";

static uint32_t gLastRender;
static uint32_t secondTimer;
static uint32_t appsKeyboardTimer;
static uint32_t radioTimer;

static uint32_t lastUartDataTime;

static bool isUartWaiting() {
  return lastUartDataTime && Now() - lastUartDataTime < 5000;
}

static void appRender() {
  if (!gRedrawScreen) {
    return;
  }

  if (Now() - gLastRender < 40) {
    return;
  }

  gRedrawScreen = false;

  UI_ClearScreen();

  SCREEN_Render();

  if (notificationMessage[0]) {
    FillRect(0, 32 - 5, 128, 9, C_FILL);
    PrintMediumEx(64, 32 + 2, POS_C, C_CLEAR, notificationMessage);
  }

  STATUSLINE_render(); // After the screen: it sets the title.

  DISPLAY_Blit();
  gLastRender = Now();
}

// For the radio before a blocking step (SSB patch load): show it now.
static void renderNow(void) {
  gRedrawScreen = true;
  gLastRender = Now() - 40;
  appRender();
}

static void systemUpdate() {
  BATTERY_UpdateBatteryInfo();
  BACKLIGHT_Update();
}

static bool checkKeylock(Key_State_t state, KEY_Code_t key) {
  bool isKeyLocked = gDeviceConfig.preferences.keylock;
  bool isPttLocked = gDeviceConfig.preferences.ptt_lock;
  // Side keys now change tuning, so the keyboard lock must cover them.
  bool isSpecialKey = key == KEY_PTT;
  bool isLongPressF = state == KEY_LONG_PRESSED && key == KEY_F;

  if (isLongPressF) {
    HOME_CancelShortcut();
    gDeviceConfig.preferences.keylock = !gDeviceConfig.preferences.keylock;
    SETTINGS_Save();
    return true;
  }

  /* if (gDeviceConfig.preferences.keylock && state == KEY_LONG_PRESSED && key == KEY_8) {
    captureScreen();
    return true;
  } */

  return isKeyLocked && (isPttLocked || !isSpecialKey) && !isLongPressF;
}

static void processKeyboard() {
  SystemMessages n = KEYBOARD_GetKey();
  // Process system notifications
  if (n.message == MSG_KEYPRESSED) {
    // Release cannot be swallowed by keylock, menu, or UART activity.
    if (n.key == KEY_PTT && n.state == KEY_RELEASED)
      RADIO_PttRelease();
    if (isUartWaiting()) return;
    BACKLIGHT_On();

    if (checkKeylock(n.state, n.key)) {
      gRedrawScreen = true;
      return;
    }

    // The top screen first; long MENU (settings) and EXIT (back) otherwise.
    if (SCREEN_Key(n.key, n.state)) {
      gRedrawScreen = true;
      gLastRender = 0;
    } else if (n.key == KEY_MENU && n.state == KEY_LONG_PRESSED) {
      if (SCREEN_Top() != &settingsScreen && SCREEN_Top() != &resetScreen)
        SCREEN_Push(&settingsScreen);
    } else if (n.key == KEY_EXIT && n.state == KEY_RELEASED) {
      SCREEN_Pop();
    }
  }
}

void initDisplay() {
  DISPLAY_Init(true, gDeviceConfig.preferences.contrast);
  BACKLIGHT_Init();
}

void SYS_Main() {
  BATTERY_UpdateBatteryInfo();

  SystemMessages n = KEYBOARD_GetKey();
  if (n.key == KEY_EXIT || !SETTINGS_Load()) {
    // Unknown storage contents are not our config: never write a marker or
    // restart automatically. Only explicit initialization may unlock writes.
    initDisplay();
    gDeviceConfig.preferences.battery_calibration = 2000;
    gDeviceConfig.preferences.backlight = 5;
    SCREEN_Reset(&resetScreen);
  } else {
    STORAGE_SetWritesEnabled(true);
    BATTERY_UpdateBatteryInfo();

    initDisplay();

    STATUSLINE_render(); // Something on screen while the radio tunes.
    DISPLAY_Blit();

    RADIO_SetRedrawHook(renderNow);
    RADIO_Init();
    SCREEN_Reset(&homeScreen);
  }

  for (;;) {

    // Highest-priority half-duplex safety check, even when another app owns
    // the UI. Physical PA shutoff must not depend on VFO repaint cadence.
    RADIO_TxSafetyUpdate();
    SETTINGS_UpdateSave();

    if (RADIO_IsReceiving() && !RADIO_IsWatching() && Now()-radioTimer >= SQL_DELAY) {
      RECEIVER_UpdateSquelch();
      radioTimer = Now();
    }

    RADIO_Update(); // Receiving and watching go on under every screen.
    SCREEN_Update();

    // common: render 2 times per second minimum
    if (Now() - gLastRender >= 500) {
      gRedrawScreen = true;
    }

    if (Now() - appsKeyboardTimer >= 14) {
      processKeyboard();
      appsKeyboardTimer = Now();
    }

    if (Now() - secondTimer >= 1000) {
      STATUSLINE_update();
      systemUpdate();
      secondTimer = Now();
    }

    appRender();

    while (UART_IsCommandAvailable()) {
      UART_HandleCommand();
      lastUartDataTime = Now();
    }

  }
}

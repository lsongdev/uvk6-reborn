#include "core/device-config.h"
#include "apps/system.h"
#include "apps/apps.h"
#include "apps/radio.h"
#include "hal/backlight.h"
#include "hal/eeprom.h"
#include "hal/keyboard.h"
#include "hal/display.h"
#include "hal/uart.h"
#include "external/CMSIS_5/Device/ARM/ARMCM0/Include/ARMCM0.h"
#include "core/battery.h"
#include "ui/menu.h"
#include "core/receiver.h"
#include "hal/scheduler.h"
#include "core/settings.h"
#include "ui/graphics.h"
#include "ui/statusline.h"

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

  APPS_render();

  if (notificationMessage[0]) {
    FillRect(0, 32 - 5, 128, 9, C_FILL);
    PrintMediumEx(64, 32 + 2, POS_C, C_CLEAR, notificationMessage);
  }

  STATUSLINE_render(); // coz of APPS_render calls STATUSLINE_SetText

  DISPLAY_Blit();
  gLastRender = Now();
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
    RADIO_APP_cancelShortcut();
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
      RADIO_APP_PttRelease();
    if (isUartWaiting()) return;
    BACKLIGHT_On();

    if (checkKeylock(n.state, n.key)) {
      gRedrawScreen = true;
      return;
    }

    if (APPS_key(n.key, n.state) || (MENU_IsActive() && n.key != KEY_EXIT)) {
      gRedrawScreen = true;
      gLastRender = 0;
    } else {
      if (n.key == KEY_MENU) {
        if (n.state == KEY_LONG_PRESSED) {
          APPS_run(APP_SETTINGS);
        } else if (n.state == KEY_RELEASED) {
          APPS_run(APP_RADIO);
        }
      }
      if (n.key == KEY_EXIT) {
        if (n.state == KEY_RELEASED) {
          APPS_exit();
        }
      }
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
    // Unknown EEPROM contents are not our config: never write a marker or
    // restart automatically. Only explicit initialization may unlock writes.
    initDisplay();
    gDeviceConfig.preferences.battery_calibration = 2000;
    gDeviceConfig.preferences.backlight = 5;
    APPS_run(APP_RESET);
  } else {
    EEPROM_SetWritesEnabled(true);
    BATTERY_UpdateBatteryInfo();

    initDisplay();

    // better UX
    STATUSLINE_render();
    DISPLAY_Blit();

    // Built-in frequency profiles no longer depend on EEPROM band records.

    APPS_run(APP_RADIO);
  }

  for (;;) {

    // Highest-priority half-duplex safety check, even when another app owns
    // the UI. Physical PA shutoff must not depend on VFO repaint cadence.
    RADIO_APP_TxSafetyUpdate();
    SETTINGS_UpdateSave();

    if (RADIO_APP_isReceiving() && !RADIO_APP_isWatching() && Now()-radioTimer >= SQL_DELAY) {
      RECEIVER_UpdateSquelch();
      radioTimer = Now();
    }

    // Radio keeps receiving and watching under any other screen.
    if (gCurrentApp != APP_RADIO) RADIO_APP_update();
    APPS_update();

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

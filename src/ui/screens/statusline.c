#include "ui/screens/statusline.h"
#include "core/device-config.h"
#include "core/settings.h"
#include "hal/storage.h"
#include "drivers/si473x/si473x.h"
#include "hal/display.h"
#include "core/battery.h"
#include "ui/numnav.h"
#include "hal/scheduler.h"
#include "ui/battery-icon.h"
#include "ui/graphics.h"
#include "core/text.h"
#include "radio/radio.h"
#include <string.h>

static uint8_t previousBatteryLevel = 255;
static bool showBattery = true;

static uint32_t lastEepromWrite = 0;
static char statuslineText[32] = {0};
static bool shortcut;

void STATUSLINE_SetShortcut(bool pending) {
  if (shortcut != pending) gRedrawScreen = true;
  shortcut = pending;
}

void STATUSLINE_SetText(const char *text) {
  if (strncmp(statuslineText, text, sizeof(statuslineText) - 1)) {
    Text t = TEXT_Begin(statuslineText, sizeof(statuslineText));
    TEXT_Add(&t, text);
    gRedrawScreen = true;
  }
}

void STATUSLINE_update(void) {
  uint8_t level = gBatteryPercent / 10;
  if (gBatteryPercent < BAT_WARN_PERCENT) {
    showBattery = !showBattery;
    gRedrawScreen = true;
  } else {
    showBattery = true;
  }
  if (previousBatteryLevel != level) {
    previousBatteryLevel = level;
    gRedrawScreen = true;
  }

  if ((bool)lastEepromWrite != gStorageWrite) {
    lastEepromWrite = gStorageWrite ? Now() : 0;
    gRedrawScreen = true;
  }
  if (lastEepromWrite && Now() - lastEepromWrite > 500) {
    lastEepromWrite = gStorageWrite = false;
    gRedrawScreen = true;
  }
}

void STATUSLINE_render(void) {
  UI_ClearStatus();

  const uint8_t BASE_Y = 4;

  DrawHLine(0, 6, LCD_WIDTH, C_FILL);

  uint8_t bars = previousBatteryLevel > 10 ? gBatteryPercent / 10
                                          : previousBatteryLevel;
  if (bars > 10) bars = 10;
  char batteryText[16];
  Text battery = TEXT_Begin(batteryText, sizeof(batteryText));
  uint8_t style = gDeviceConfig.preferences.battery_style;
  if (style == BAT_PERCENT) {
    TEXT_Number(&battery, gBatteryPercent, 1, 10);
    TEXT_Char(&battery, '%');
  } else if (style == BAT_VOLTAGE) {
    TEXT_Number(&battery, gBatteryVoltage / 100, 1, 10);
    TEXT_Char(&battery, '.');
    TEXT_Number(&battery, gBatteryVoltage % 100, 2, 10);
    TEXT_Char(&battery, 'V');
  }
  uint16_t batteryWidth = batteryText[0] ? UI_SmallTextWidth(batteryText) : 13;
  if (showBattery) {
    if (batteryText[0]) PrintSmallEx(LCD_WIDTH - 1, BASE_Y, POS_R, C_FILL, batteryText);
    else UI_Battery(bars);
  }

  // Title on the left; everything else right-aligned, steady state nearest
  // the battery and transient state further left: [F][EEPROM] [SI][lock] bat.
  char icons[8] = {'\0'};
  uint8_t idx = 0;
  if (gStorageWrite) icons[idx++] = SYM_EEPROM_W;
  if (gReceiver.plan.device == RX_DEVICE_BROADCAST || isSi4732On)
    icons[idx++] = SYM_BROADCAST;
  if (gDeviceConfig.preferences.keylock) icons[idx++] = SYM_LOCK;

  uint8_t right = batteryWidth + 4 < LCD_WIDTH ? LCD_WIDTH - 1 - batteryWidth - 3 : 0;
  uint16_t iconWidth = UI_SymbolsTextWidth(icons);
  PrintSymbolsEx(right, BASE_Y, POS_R, C_FILL, icons);
  if (iconWidth) right = iconWidth + 2 < right ? right - iconWidth - 2 : 0;
  if (shortcut) {
    PrintSmallEx(right, BASE_Y, POS_R, C_FILL, "F");
    right = right > 6 ? right - 6 : 0;
  }
  uint8_t textWidth = right > 2 ? right - 2 : 0;
  // RDS clock time centred; the title gives way, the right side never does.
  uint16_t minute;
  if (RADIO_Clock(&minute)) {
    char clockText[6];
    Text t = TEXT_Begin(clockText, sizeof(clockText));
    TEXT_Number(&t, minute / 60u, 2, 10);
    TEXT_Char(&t, ':');
    TEXT_Number(&t, minute % 60u, 2, 10);
    uint8_t half = UI_SmallTextWidth(clockText) / 2;
    if (LCD_WIDTH / 2 + half + 2 <= right) {
      PrintSmallEx(LCD_WIDTH / 2, BASE_Y, POS_C, C_FILL, clockText);
      if (textWidth > LCD_WIDTH / 2 - half - 3) textWidth = LCD_WIDTH / 2 - half - 3;
    }
  }
  char selection[32];
  const char *text = statuslineText;
  if (gIsNumNavInput) {
    Text t = TEXT_Begin(selection, sizeof(selection));
    TEXT_Add(&t, "Select: ");
    TEXT_Add(&t, gNumNavInput);
    text = selection;
  }
  UI_SmallTextWindow(0, BASE_Y, textWidth, text, 0);
}


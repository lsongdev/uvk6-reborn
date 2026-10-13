#include "ui/statusline.h"
#include "core/device-config.h"
#include "apps/apps.h"
#include "vfo/vfo.h"
#include "hal/eeprom.h"
#include "drivers/si473x/si473x.h"
#include "hal/display.h"
#include "core/bands.h"
#include "core/battery.h"
#include "core/channels.h"
#include "ui/numnav.h"
#include "hal/scheduler.h"
#include "ui/components/battery.h"
#include "ui/components/scan.h"
#include "ui/graphics.h"
#include <string.h>

static uint8_t previousBatteryLevel = 255;
static bool showBattery = true;

static uint32_t lastEepromWrite = 0;
static uint32_t lastTickerUpdate = 0;

static char statuslineText[32] = {0};
static char statuslineTicker[32] = {0};

void STATUSLINE_SetText(const char *pattern, ...) {
  char statuslineTextNew[32] = {0};
  va_list args;
  va_start(args, pattern);
  vsnprintf(statuslineTextNew, 31, pattern, args);
  va_end(args);
  if (strcmp(statuslineText, statuslineTextNew)) {
    strcpy(statuslineText, statuslineTextNew);
    gRedrawScreen = true;
  }
}

void STATUSLINE_update(void) {
  // BATTERY_UpdateBatteryInfo();
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

  if ((bool)lastEepromWrite != gEepromWrite) {
    lastEepromWrite = gEepromWrite ? Now() : 0;
    gRedrawScreen = true;
  }
  if (lastEepromWrite && Now() - lastEepromWrite > 500) {
    lastEepromWrite = gEepromWrite = false;
    gRedrawScreen = true;
  }

  if (Now() - lastTickerUpdate > 5000) {
    statuslineTicker[0] = '\0';
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
  uint16_t batteryWidth;
  switch (gDeviceConfig.preferences.battery_style) {
  case BAT_PERCENT:
    snprintf(batteryText, sizeof(batteryText), "%u%%", gBatteryPercent);
    batteryWidth = UI_SmallTextWidth(batteryText);
    if (showBattery)
      PrintSmallEx(LCD_WIDTH - 1, BASE_Y, POS_R, C_FILL, "%s", batteryText);
    break;
  case BAT_VOLTAGE:
    snprintf(batteryText, sizeof(batteryText), "%u.%02uV",
             gBatteryVoltage / 100, gBatteryVoltage % 100);
    batteryWidth = UI_SmallTextWidth(batteryText);
    if (showBattery)
      PrintSmallEx(LCD_WIDTH - 1, BASE_Y, POS_R, C_FILL, "%s", batteryText);
    break;
  default:
    batteryWidth = 13;
    if (showBattery) UI_Battery(bars);
    break;
  }

  char icons[8] = {'\0'};
  uint8_t idx = 0;

  if (gEepromWrite) {
    icons[idx++] = SYM_EEPROM_W;
  }

  if (gMonitorMode) {
    icons[idx++] = SYM_MONITOR;
  }

  if (ctx && (ctx->radio_type == RADIO_BK1080 || isSi4732On)) {
    icons[idx++] = SYM_BROADCAST;
  }

  if (gDeviceConfig.preferences.keylock) {
    icons[idx++] = SYM_LOCK;
  }

  uint8_t iconsRight = batteryWidth + 4 < LCD_WIDTH ? LCD_WIDTH - 1 - batteryWidth - 3 : 0;
  if (gCurrentApp == APP_VFO && VFO_isShortcutPending()) {
    PrintSmallEx(iconsRight, BASE_Y, POS_R, C_FILL, "F");
    iconsRight -= 8;
  }
  PrintSymbolsEx(iconsRight, BASE_Y, POS_R, C_FILL, "%s", icons);

  uint16_t iconWidth = UI_SymbolsTextWidth(icons);
  uint8_t textWidth = iconWidth + 2 < iconsRight ? iconsRight - iconWidth - 2 : 0;
  char selection[32];
  const char *text = statuslineTicker[0] == '\0' ? statuslineText : statuslineTicker;
  if (gIsNumNavInput) {
    snprintf(selection, sizeof(selection), "Select: %s", gNumNavInput);
    text = selection;
  }
  UI_SmallTextWindow(0, BASE_Y, textWidth, text, 0);
}

// Current receiver settings come from the Hz-based radio user state.
void STATUSLINE_RenderRadioSettings(void) {
  const RadioParameters *p = &gDeviceConfig.radio.vfos[gDeviceConfig.radio.selected_vfo].current.parameters;
  STATUSLINE_SetText("%s %u Hz SQ%u",
                     rxModeNames[p->mode], (unsigned)p->bandwidth_hz,
                     (unsigned)p->squelch);
}

#include "ui/statusline.h"
#include "core/device-config.h"
#include "apps/apps.h"
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

  if (showBattery) {
    uint8_t bars = previousBatteryLevel > 10 ? gBatteryPercent / 10
                                              : previousBatteryLevel;
    if (bars > 10)
      bars = 10;
    // Keep the main radio header compact; preserve the setting in other apps.
    if (gCurrentApp == APP_VFO1) {
      UI_Battery(bars);
    } else {
      switch (gSettings.batteryStyle) {
      case BAT_CLEAN:
        UI_Battery(bars);
        break;
      case BAT_PERCENT:
        PrintSmallEx(LCD_WIDTH - 1, BASE_Y, POS_R, C_INVERT, "%u%%",
                     gBatteryPercent);
        break;
      case BAT_VOLTAGE:
        PrintSmallEx(LCD_WIDTH - 1, BASE_Y, POS_R, C_FILL, "%u.%02uV",
                     gBatteryVoltage / 100, gBatteryVoltage % 100);
        break;
      }
    }
  }

  char icons[8] = {'\0'};
  uint8_t idx = 0;

  if (gEepromWrite) {
    icons[idx++] = SYM_EEPROM_W;
  }

  if (LOOT_Size() == LOOT_SIZE_MAX) {
    icons[idx++] = SYM_LOOT_FULL;
  }

  if (gMonitorMode) {
    icons[idx++] = SYM_MONITOR;
  }

  if (ctx->radio_type == RADIO_BK1080 || isSi4732On) {
    icons[idx++] = SYM_BROADCAST;
  }

  if (gSettings.upconverter) {
    icons[idx++] = SYM_CONVERTER;
  }

  if (gSettings.keylock) {
    icons[idx++] = SYM_LOCK;
  }

  if (gCurrentApp == APP_CH_LIST || gCurrentApp == APP_LOOT_LIST) {
    UI_Scanlists(LCD_XCENTER - 13, 0, gSettings.currentScanlist);
  }

  PrintSymbolsEx(LCD_WIDTH - 1 - 22, BASE_Y, POS_R, C_FILL, "%s", icons);

  if (gIsNumNavInput) {
    PrintSmall(0, BASE_Y, "Select: %s", gNumNavInput);
  } else {
    PrintSmall(0, BASE_Y,
               statuslineTicker[0] == '\0' ? statuslineText : statuslineTicker);
  }
}

// Current receiver settings come from the Hz-based radio user state.
void STATUSLINE_RenderRadioSettings(void) {
  const RadioParameters *p = &gDeviceConfig.radio.current.parameters;
  STATUSLINE_SetText("%s %u Hz SQ%u",
                     rxModeNames[p->mode], (unsigned)p->bandwidth_hz,
                     (unsigned)p->squelch);
}

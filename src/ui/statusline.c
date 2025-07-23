#include "statusline.h"
#include "../apps/apps.h"
#include "../driver/eeprom.h"
#include "../driver/si473x.h"
#include "../driver/st7565.h"
#include "../helper/bands.h"
#include "../helper/battery.h"
#include "../helper/channels.h"
#include "../helper/numnav.h"
#include "../scheduler.h"
#include "components.h"
#include "graphics.h"
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
    strncpy(statuslineText, statuslineTextNew, 31);
    gRedrawScreen = true;
  }
}

void STATUSLINE_SetTickerText(const char *pattern, ...) {
  char statuslineTextNew[32] = {0};
  va_list args;
  va_start(args, pattern);
  vsnprintf(statuslineTextNew, 31, pattern, args);
  va_end(args);
  if (strcmp(statuslineTicker, statuslineTextNew)) {
    strncpy(statuslineTicker, statuslineTextNew, 31);
    gRedrawScreen = true;
  }
  lastTickerUpdate = Now();
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
    switch (gSettings.batteryStyle) {
    case BAT_CLEAN:
      UI_Battery(previousBatteryLevel);
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

  /* if (gCurrentApp == APP_CH_LIST || gCurrentApp == APP_LOOT_LIST) {
    UI_Scanlists(LCD_XCENTER - 13, 0, gSettings.currentScanlist);
  } */

  PrintSymbolsEx(LCD_WIDTH - 1 - 22, BASE_Y, POS_R, C_FILL, "%s", icons);

  PrintSmall(0, BASE_Y,
             statuslineTicker[0] == '\0' ? statuslineText : statuslineTicker);
}

void STATUSLINE_RenderRadioSettings() {
  STATUSLINE_SetText(                                      //
      "%s %s AFC%s %s %s %s",                              //
      RADIO_GetParamValueString(ctx, PARAM_GAIN),          //
      RADIO_GetParamValueString(ctx, PARAM_BANDWIDTH),     //
      RADIO_GetParamValueString(ctx, PARAM_AFC),           //
      RADIO_GetParamValueString(ctx, PARAM_SQUELCH_TYPE),  //
      RADIO_GetParamValueString(ctx, PARAM_SQUELCH_VALUE), //
      RADIO_GetParamValueString(ctx, PARAM_MODULATION)     //
  );
}

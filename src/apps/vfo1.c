#include "vfo1.h"
#include "../dcs.h"
#include "../driver/gpio.h"
#include "../driver/uart.h"
#include "../external/printf/printf.h"
#include "../helper/bands.h"
#include "../helper/channels.h"
#include "../helper/measurements.h"
#include "../helper/numnav.h"
#include "../helper/regs-menu.h"
#include "../helper/scan.h"
#include "../inc/dp32g030/gpio.h"
#include "../radio.h"
#include "../scheduler.h"
#include "../ui/components.h"
#include "../ui/graphics.h"
#include "../ui/spectrum.h"
#include "../ui/statusline.h"
#include "apps.h"
#include "chcfg.h"
#include "chlist.h"
#include "finput.h"
#include <stdint.h>

static char String[16];
static const Step liveStep = STEP_5_0kHz;

static void updateBand(void) {
  uint32_t f = RADIO_GetParam(ctx, PARAM_FREQUENCY);
  if (!BANDS_InRange(f, gCurrentBand) ||
      gCurrentBand.meta.type == TYPE_BAND_DETACHED) {
    BANDS_SelectByFrequency(f, ctx->fixed_bounds);
  }
}

static void setChannel(uint16_t v) {
  RADIO_LoadChannelToVFO(gRadioState, RADIO_GetCurrentVFONumber(gRadioState),
                         v);
}

static void tuneTo(uint32_t f, uint32_t _) {
  (void)_;
  RADIO_SetParam(ctx, PARAM_FREQUENCY, f, true);
  RADIO_ApplySettings(ctx);
  updateBand();
}

void VFO1_init(void) {
  gLastActiveLoot = NULL;
  CHANNELS_LoadScanlist(TYPE_FILTER_CH, gSettings.currentScanlist);
  if (vfo->mode == MODE_CHANNEL) {
    setChannel(vfo->channel_index);
  }
  updateBand();

  SCAN_SetMode(SCAN_MODE_SINGLE);
  // SCAN_Init(false);
}

void VFO1_update(void) {}

static bool handleNumNav(KEY_Code_t key) {
  if (gIsNumNavInput) {
    NUMNAV_Input(key);
    return true;
  }

  if (key <= KEY_9) {
    NUMNAV_Init(vfo->channel_index, 0, CHANNELS_GetCountMax() - 1);
    gNumNavCallback = setChannel;
    return false;
  }

  return false;
}

static bool handleFrequencyChange(KEY_Code_t key) {
  if (key != KEY_UP && key != KEY_DOWN)
    return false;

  if (vfo->mode == MODE_CHANNEL) {
    CHANNELS_Next(key == KEY_UP);
  } else {
    RADIO_IncDecParam(ctx, PARAM_FREQUENCY, key == KEY_UP, true);
  }
  updateBand();
  return true;
}

static bool handleSSBFineTune(KEY_Code_t key) {
  if (ctx->radio_type != RADIO_SI4732 || !RADIO_IsSSB(ctx))
    return false;
  if (key != KEY_SIDE1 && key != KEY_SIDE2)
    return false;

  RADIO_AdjustParam(ctx, PARAM_FREQUENCY, key == KEY_SIDE1 ? 5 : -5, true);
  // TODO: SAVE
  return true;
}

static bool handleLongPress(KEY_Code_t key) {
  uint8_t vfoN = RADIO_GetCurrentVFONumber(gRadioState);

  switch (key) {
  case KEY_1:
    gChListFilter = TYPE_FILTER_BAND;
    APPS_run(APP_CH_LIST);
    return true;

  case KEY_2:
    if (gCurrentApp == APP_VFO1) {
      gSettings.iAmPro = !gSettings.iAmPro;
      SETTINGS_Save();
      return true;
    }
    return false;

  case KEY_3:
    RADIO_SaveCurrentVFO(gRadioState);
    RADIO_ToggleVFOMode(gRadioState, vfoN);
    updateBand();
    return true;

  case KEY_4:
    gShowAllRSSI = !gShowAllRSSI;
    return true;

  case KEY_5:
    RADIO_ToggleMultiwatch(gRadioState, !gRadioState->multiwatch_enabled);
    return true;

  case KEY_6:
    RADIO_IncDecParam(ctx, PARAM_POWER, true, true);
    return true;

  case KEY_7:
    RADIO_IncDecParam(ctx, PARAM_STEP, true, true);
    return true;

  case KEY_8:
    RADIO_IncDecParam(ctx, PARAM_TX_OFFSET, true, true);
    return true;

  case KEY_0:
    RADIO_IncDecParam(ctx, PARAM_MODULATION, true, true);
    return true;

  case KEY_SIDE1:
  case KEY_SIDE2:
    SP_NextGraphUnit(key == KEY_SIDE1);
    return true;

  default:
    return false;
  }
}

static bool handleRelease(KEY_Code_t key, Key_State_t state) {
  uint8_t vfoN = RADIO_GetCurrentVFONumber(gRadioState);

  switch (key) {
  case KEY_0:
  case KEY_1:
  case KEY_2:
  case KEY_3:
  case KEY_4:
  case KEY_5:
  case KEY_6:
  case KEY_7:
  case KEY_8:
  case KEY_9:
    gFInputCallback = tuneTo;
    FINPUT_setup(0, BK4819_F_MAX, UNIT_MHZ, false);
    APPS_run(APP_FINPUT);
    APPS_key(key, state);
    return true;

  case KEY_F:
    RADIO_SaveVFOToStorage(gRadioState, vfoN, &gChEd);
    gChNum = -1;
    APPS_run(APP_CH_CFG);
    return true;

  case KEY_STAR:
    APPS_run(APP_LOOT_LIST);
    return true;

  case KEY_SIDE1:
    gMonitorMode = !gMonitorMode;
    return true;

  case KEY_SIDE2:
    GPIO_FlipBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
    return true;

  case KEY_EXIT:
    if (gMonitorMode) {
      gMonitorMode = false;
      return true;
    }
    if (!APPS_exit()) {
      RADIO_SaveCurrentVFO(gRadioState);
      RADIO_SwitchVFO(gRadioState,
                      IncDecU(vfoN, 0, gRadioState->num_vfos, true));
      updateBand();
    }
    return true;

  default:
    return false;
  }
}

bool VFO1_key(KEY_Code_t key, Key_State_t state) {
  // return false;
  if (REGSMENU_Key(key, state)) {
    return true;
  }

  // Обработка NUM NAV в режиме канала
  if (state == KEY_RELEASED && vfo->mode == MODE_CHANNEL && !gIsNumNavInput) {
    if (handleNumNav(key)) {
      return true;
    }
  }

  // PTT
  if (key == KEY_PTT && !gIsNumNavInput) {
    RADIO_ToggleTX(ctx, state != KEY_RELEASED);
    return true;
  }

  // Обработка нажатий и удержаний
  if (state == KEY_RELEASED || state == KEY_LONG_PRESSED_CONT) {
    if (handleFrequencyChange(key))
      return true;
    if (handleSSBFineTune(key))
      return true;
  }

  // Длинные нажатия
  if (state == KEY_LONG_PRESSED) {
    return handleLongPress(key);
  }

  // Отпускания
  if (state == KEY_RELEASED) {
    return handleRelease(key, state);
  }

  return false;
}

// Single-frequency main screen, with a dedicated band row and S-meter.
// Coordinates are pixel baselines on the 128x64 display.
void VFO1_render(void) {
  const uint8_t FREQ_BASE = 41;
  uint32_t frequency = RADIO_GetParam(
      ctx, ctx->tx_state.is_active ? PARAM_TX_FREQUENCY_FACT : PARAM_FREQUENCY);
  const char *mode = RADIO_GetParamValueString(ctx, PARAM_MODULATION);

  STATUSLINE_SetText("RADIO");

  PrintMediumEx(2, 18, POS_L, C_FILL, "%.10s", gCurrentBand.name);
  if (ctx->tx_state.is_active)
    PrintMediumEx(LCD_WIDTH - 1, 18, POS_R, C_FILL, "TX");
  else if (ctx->tx_state.last_error)
    PrintSmallEx(LCD_WIDTH - 1, 18, POS_R, C_FILL, "TX ERR");

  // Left-aligned frequency; above 999 MHz use a slightly narrower font so
  // the 5 decimal places still fit alongside the mode and unit.
  if (frequency / MHZ < 1000)
    PrintBiggestDigitsEx(2, FREQ_BASE, POS_L, C_FILL, "%u.%05u",
                        frequency / MHZ, frequency % MHZ);
  else
    PrintBigDigitsEx(2, FREQ_BASE, POS_L, C_FILL, "%u.%05u",
                     frequency / MHZ, frequency % MHZ);

  // Mode (7px) plus the MHz unit (5px) and a 2px gap: 14px total.
  PrintMediumEx(LCD_WIDTH - 1, FREQ_BASE - 7, POS_R, C_FILL, "%s",
                mode ? mode : "?");
  PrintSmallEx(LCD_WIDTH - 1, FREQ_BASE, POS_R, C_FILL, "MHz");

  if (ctx->tx_state.is_active)
    UI_TxBar(52);
  else
    UI_SMeter(52);

  REGSMENU_Draw();
}

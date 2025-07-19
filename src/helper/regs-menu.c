#include "regs-menu.h"
#include "../apps/apps.h"
#include "../helper/menu.h"
#include "../radio.h"
#include "channels.h"
#include <string.h>

static const uint8_t MENU_Y = 6;
static const uint8_t MENU_ITEM_H = 7;
const uint8_t MENU_LINES_TO_SHOW = 7;

static bool inMenu;

static uint8_t PARAM_ITEMS[3][PARAM_COUNT] = {
    [RADIO_BK4819] =
        {
            PARAM_RADIO,
            PARAM_GAIN,
            PARAM_BANDWIDTH,
            PARAM_SQUELCH_VALUE,
            PARAM_MODULATION,
            PARAM_STEP,
            PARAM_AFC,
            PARAM_DEV,
            PARAM_MIC,
            PARAM_XTAL,
            PARAM_POWER,
        },
    [RADIO_SI4732] =
        {
            PARAM_RADIO,
            PARAM_GAIN,
            PARAM_BANDWIDTH,
            PARAM_SQUELCH_VALUE,
            PARAM_MODULATION,
            PARAM_STEP,
        },
    [RADIO_BK1080] =
        {
            PARAM_RADIO,
            PARAM_STEP,
        },
};

static uint8_t PARAM_SIZE[] = {
    [RADIO_BK4819] = 11,
    [RADIO_SI4732] = 6,
    [RADIO_BK1080] = 2,
};

static void getValS(const MenuItem *item, char *buf, uint8_t buf_size) {
  VFOContext *ctx = &RADIO_GetCurrentVFO(&gRadioState)->context;
  snprintf(buf, buf_size, "%s", RADIO_GetParamValueString(ctx, item->setting));
}

static void updateVal(const MenuItem *item, bool up) {
  RADIO_IncDecParam(, item->setting, up);
}

static void updateValueAlt(bool inc) {}

static const MenuItem regsMenuItems[3][PARAM_COUNT] = {
    [RADIO_BK4819] =
        {
            {PARAM_NAMES[PARAM_RADIO], PARAM_RADIO, getValS, updateVal},
            {PARAM_NAMES[PARAM_GAIN], PARAM_GAIN, getValS, updateVal},
            {PARAM_NAMES[PARAM_BANDWIDTH], PARAM_BANDWIDTH, getValS, updateVal},
            {PARAM_NAMES[PARAM_SQUELCH_VALUE], PARAM_SQUELCH_VALUE, getValS,
             updateVal},
            {PARAM_NAMES[PARAM_MODULATION], PARAM_MODULATION, getValS,
             updateVal},
            {PARAM_NAMES[PARAM_STEP], PARAM_STEP, getValS, updateVal},
            {PARAM_NAMES[PARAM_AFC], PARAM_AFC, getValS, updateVal},
            {PARAM_NAMES[PARAM_DEV], PARAM_DEV, getValS, updateVal},
            {PARAM_NAMES[PARAM_MIC], PARAM_MIC, getValS, updateVal},
            {PARAM_NAMES[PARAM_XTAL], PARAM_XTAL, getValS, updateVal},
            {PARAM_NAMES[PARAM_POWER], PARAM_POWER, getValS, updateVal},
        },
    [RADIO_SI4732] =
        {
            {PARAM_NAMES[PARAM_RADIO], PARAM_RADIO, getValS, updateVal},
            {PARAM_NAMES[PARAM_GAIN], PARAM_GAIN, getValS, updateVal},
            {PARAM_NAMES[PARAM_BANDWIDTH], PARAM_BANDWIDTH, getValS, updateVal},
            {PARAM_NAMES[PARAM_SQUELCH_VALUE], PARAM_SQUELCH_VALUE, getValS,
             updateVal},
            {PARAM_NAMES[PARAM_MODULATION], PARAM_MODULATION, getValS,
             updateVal},
            {PARAM_NAMES[PARAM_STEP], PARAM_STEP, getValS, updateVal},
        },
    [RADIO_BK1080] =
        {
            {PARAM_NAMES[PARAM_RADIO], PARAM_RADIO, getValS, updateVal},
            {PARAM_NAMES[PARAM_STEP], PARAM_STEP, getValS, updateVal},
        },
};

static const Menu regsMenu = {
    .title = "",
    .items = regsMenuItems,
    .num_items = ARRAY_SIZE(regsMenuItems),
    .itemHeight = MENU_ITEM_H,
};

void REGSMENU_Draw(VFOContext *ctx) {
  if (inMenu) {
    MENU_Render();
  }
}

bool REGSMENU_Key(KEY_Code_t key, Key_State_t state, VFOContext *ctx) {
  switch (key) {
  case KEY_0:
    inMenu = !inMenu;
    return true;
  case KEY_2:
  case KEY_8:
    if (inMenu) {
      // radio.fixedBoundsMode = false;
      RADIO_IncDecParam(ctx, PARAM_ITEMS[ctx->radio_type][currentIndex],
                        key == KEY_2, true);
      return true;
    }
    break;
  case KEY_1:
  case KEY_7:
    if (inMenu) {
      // radio.fixedBoundsMode = false;
      // updateValueAlt(key == KEY_1);
      return true;
    }
    break;
  case KEY_EXIT:
    if (inMenu) {
      inMenu = false;
      return true;
    }
    break;
  default:
    break;
  }

  return false;
}

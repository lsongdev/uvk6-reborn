#include "regs-menu.h"
#include "../apps/apps.h"
#include "../helper/menu.h"
#include "../radio.h"
#include "channels.h"
#include "../external/printf/printf.h"

static bool inMenu;

static MenuItem menuItems[11];

static Menu regsMenu = {
    .title = "",
    .items = menuItems,
    .itemHeight = 7,
    .y = 5,
    .width = 64,
};
static void initMenu();

static void getValS(const MenuItem *item, char *buf, uint8_t buf_size) {
  VFOContext *ctx = &RADIO_GetCurrentVFO(&gRadioState)->context;
  snprintf(buf, buf_size, "%s", RADIO_GetParamValueString(ctx, item->setting));
}

static void updateVal(const MenuItem *item, bool up) {
  VFOContext *ctx = &RADIO_GetCurrentVFO(&gRadioState)->context;
  RADIO_IncDecParam(ctx, item->setting, up, true);
  if (item->setting == PARAM_RADIO) {
    initMenu();
  }
}

static void updateValueAlt(bool inc) {}
static const uint8_t radioParamCount[3] = {
    [RADIO_BK4819] = 11,
    [RADIO_SI4732] = 6,
    [RADIO_BK1080] = 2,
};

static const ParamType radioParams[3][PARAM_COUNT] = {
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

static void initMenu() {
  VFOContext *ctx = &RADIO_GetCurrentVFO(&gRadioState)->context;
  regsMenu.num_items = radioParamCount[ctx->radio_type];
  for (uint8_t i = 0; i < regsMenu.num_items; ++i) {
    ParamType p = radioParams[ctx->radio_type][i];
    menuItems[i].name = PARAM_NAMES[p];
    menuItems[i].setting = p;
    menuItems[i].change_value = updateVal;
    menuItems[i].get_value_text = getValS;
  }
}

void REGSMENU_Draw(VFOContext *ctx) {
  if (inMenu) {
    MENU_Render();
  }
}

bool REGSMENU_Key(KEY_Code_t key, Key_State_t state, VFOContext *ctx) {
  if (inMenu && MENU_HandleInput(key, state)) {
    return true;
  }
  switch (key) {
  case KEY_0:
    inMenu = !inMenu;
    if (inMenu) {
      initMenu();
    }
    return true;
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

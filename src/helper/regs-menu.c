#include "regs-menu.h"
#include "../radio.h"
#include "../ui/graphics.h"
#include "channels.h"
#include "measurements.h"

static const uint8_t MENU_Y = 6;
static const uint8_t MENU_ITEM_H = 7;

static uint8_t currentIndex;
static bool inMenu;

static uint8_t PARAM_ITEMS[] = {
    PARAM_GAIN,
    PARAM_BANDWIDTH,
    PARAM_SQUELCH_VALUE,
    PARAM_MODULATION,
    PARAM_STEP,
    PARAM_RADIO,
    PARAM_AFC,
    PARAM_DEV,
    PARAM_MIC,
    PARAM_XTAL,
    PARAM_POWER,
};

static void updateValueAlt(bool inc) {}

const uint8_t MENU_LINES_TO_SHOW = 7;

void REGSMENU_Draw(VFOContext *ctx) {
  if (inMenu) {

    const uint8_t size = ARRAY_SIZE(PARAM_ITEMS);
    const uint16_t maxItems =
        size < MENU_LINES_TO_SHOW ? size : MENU_LINES_TO_SHOW;
    const uint16_t offset = Clamp(currentIndex - 2, 0, size - maxItems);

    FillRect(0, MENU_Y, LCD_XCENTER, maxItems * MENU_ITEM_H + 2, C_CLEAR);
    DrawRect(0, MENU_Y, LCD_XCENTER, maxItems * MENU_ITEM_H + 2, C_FILL);

    for (uint8_t i = 0; i < maxItems; ++i) {
      const uint16_t itemIndex = i + offset;
      const bool isCurrent = itemIndex == currentIndex;
      const Color color = isCurrent ? C_INVERT : C_FILL;
      const uint8_t ry = MENU_Y + 1 + i * MENU_ITEM_H;
      const uint8_t y = ry + 5;

      if (isCurrent) {
        FillRect(0, ry, LCD_XCENTER, MENU_ITEM_H, C_FILL);
      }

      PrintSmallEx(5, y, POS_L, color, "%s",
                   PARAM_NAMES[PARAM_ITEMS[itemIndex]]);
      PrintSmallEx(LCD_XCENTER - 5, y, POS_R, color, "%s",
                   RADIO_GetParamValueString(ctx, PARAM_ITEMS[itemIndex]));
    }
  }
}

bool REGSMENU_Key(KEY_Code_t key, Key_State_t state, VFOContext *ctx) {
  switch (key) {
  case KEY_0:
    inMenu = !inMenu;
    return true;
  case KEY_UP:
  case KEY_DOWN:
    if (inMenu) {
      currentIndex = IncDecU(currentIndex, 0, PARAM_COUNT, key == KEY_DOWN);
      return true;
    }
    break;
  case KEY_2:
  case KEY_8:
    if (inMenu) {
      // radio.fixedBoundsMode = false;
      RADIO_IncDecParam(ctx, PARAM_ITEMS[currentIndex], key == KEY_2, true);
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

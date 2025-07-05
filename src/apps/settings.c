#include "settings.h"
#include "../driver/backlight.h"
#include "../driver/eeprom.h"
#include "../driver/st7565.h"
#include "../driver/uart.h"
#include "../external/CMSIS_5/Device/ARM/ARMCM0/Include/ARMCM0.h"
#include "../helper/battery.h"
#include "../helper/measurements.h"
#include "../helper/menu.h"
#include "../helper/numnav.h"
#include "../misc.h"
#include "../radio.h"
#include "../settings.h"
#include "../ui/graphics.h"
#include "../ui/menu.h"
#include "../ui/statusline.h"
#include "apps.h"
#include "finput.h"
#include "textinput.h"
#include <string.h>

static uint8_t DEAD_BUF[] = {0xDE, 0xAD};

static const char *fltBound[] = {"240MHz", "280MHz"};

static const uint16_t BAT_CAL_MIN = 1900;

static void getSqlOpenT(const void *user_data, char *buf, uint8_t buf_size) {
  snprintf(buf, buf_size, "%ums", gSettings.sqlOpenTime * 5);
}

static void getSqlCloseT(const void *user_data, char *buf, uint8_t buf_size) {
  snprintf(buf, buf_size, "%ums", gSettings.sqlCloseTime * 5);
}

static void updateSqlOpenT(const void *user_data, bool up) {
  gSettings.sqlOpenTime = IncDecU(gSettings.sqlOpenTime, 0, 4, up);
}

static MenuItem dynamicItems[RUN_APPS_COUNT];
static Menu submenu = {
    .title = "Submenu",
    .items = dynamicItems,
    .num_items = 0,
};

static Menu sqlMenu = {
    .title = "SQL",
    .items =
        (MenuItem[]){
            {"Level"},
            {.name = "Open t",
             .get_value_text = getSqlOpenT,
             .change_value = updateSqlOpenT},
            {.name = "Close t", .get_value_text = getSqlCloseT},
        },
    .num_items = 3,
};

static Menu scanMenu = {
    .title = "Scan options",
    .items = (MenuItem[]){{"SQL", NULL, NULL, &sqlMenu}, {"Preferences"}},
    .num_items = 2,
};

static void onMainAppChoose(void *user_data) {
  // gSettings.mainApp = *(AppType_t *)(user_data);
}

static void onMainAppSubmenu(void *user_data) {
  Log("Submenu enter");
  submenu.title = "LALALA";
  submenu.num_items = RUN_APPS_COUNT;
  for (uint8_t i = 0; i < RUN_APPS_COUNT; ++i) {
    dynamicItems[i].name = apps[appsAvailableToRun[i]].name;
    dynamicItems[i].action = onMainAppChoose;
  }
}

static const MenuItem menuItems[] = {
    {"Main app", onMainAppSubmenu, NULL, &submenu},
    {"SCAN", NULL, NULL, &scanMenu},
    /* {"SQL open t", M_SQL_OPEN_T, 7},
    {"SQL close t", M_SQL_CLOSE_T, 3},
    {"FC t", M_FC_TIME, 4},
    {"SCAN listen t/o", M_SQL_TO_OPEN, ARRAY_SIZE(SCAN_TIMEOUT_NAMES)},
    {"SCAN stay t", M_SQL_TO_CLOSE, ARRAY_SIZE(SCAN_TIMEOUT_NAMES)},
    {"SCAN skip X_X", M_SKIP_GARBAGE_FREQS, 2},
    {"Contrast", M_CONTRAST, 16},
    {"BL high", M_BRIGHTNESS, 16},
    {"BL low", M_BRIGHTNESS_L, 16},
    {"BL time", M_BL_TIME, ARRAY_SIZE(BL_TIME_VALUES)},
    {"BL SQL mode", M_BL_SQL, ARRAY_SIZE(BL_SQL_MODE_NAMES)},
    {"DTMF decode", M_DTMF_DECODE, 2},
    {"SI power off", M_SI4732_POWER_OFF, 2},
    {"Upconv", M_UPCONVERTER, 0},
    {"Filter bound", M_FLT_BOUND, 2},
    {"BAT cal", M_BAT_CAL, 255},
    {"BAT type", M_BAT_TYPE, ARRAY_SIZE(BATTERY_TYPE_NAMES)},
    {"BAT style", M_BAT_STYLE, ARRAY_SIZE(BATTERY_STYLE_NAMES)},
    {"CH Display", M_CH_DISP_MODE, ARRAY_SIZE(CH_DISPLAY_MODE_NAMES)},
    {"Beep", M_BEEP, 2},
    {"Level in VFO", M_LEVEL_IN_VFO, 2},
    {"STE", M_STE, 2},
    {"Roger", M_ROGER, ARRAY_SIZE(rogerNames)},
    {"Tone local", M_TONE_LOCAL, 2},
    {"Lock PTT", M_PTT_LOCK, 2},
    {"Mutliwatch", M_MWATCH, 4},
    {"Reset", M_RESET, 2}, */
};

static void onSettingsEnter() { Log("Settings enter"); }

static Menu settingsMenu = {
    .title = "Settings",
    .items = menuItems,
    .num_items = ARRAY_SIZE(menuItems),
    .on_enter = onSettingsEnter,
};

void setMenuIndexAndRun(uint16_t v) {
  /* settingsMenu.cursor = v;
  MENU_Select(&settingsMenu); */
}

void SETTINGS_init(void) { MENU_Init(&settingsMenu); }
void SETTINGS_deinit(void) { BACKLIGHT_SetBrightness(gSettings.brightness); }

bool SETTINGS_key(KEY_Code_t key, Key_State_t state) {
  /* if (state == KEY_RELEASED) {
    if (!gIsNumNavInput && key <= KEY_9) {
      NUMNAV_Init(menu.cursor + 1, 1, menu.item_count);
      gNumNavCallback = setMenuIndexAndRun;
    }
    if (gIsNumNavInput) {
      menu.cursor = NUMNAV_Input(key) - 1;
      return true;
    }
  } */

  if ((state == KEY_RELEASED) && MENU_HandleInput(key)) {
    return true;
  }

  return false;
}

void SETTINGS_render(void) {
  if (gIsNumNavInput) {
    STATUSLINE_SetText("Select: %s", gNumNavInput);
  } else {
    STATUSLINE_SetText(apps[APP_SETTINGS].name);
  }

  MENU_Render();
}

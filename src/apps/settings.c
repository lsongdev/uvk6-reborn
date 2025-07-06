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

static const uint16_t BAT_CAL_MIN = 1900;

static void getValueString(const MenuItem *item, char *buf, uint8_t buf_size) {
  snprintf(buf, buf_size, "%s", SETTINGS_GetValueString(item->setting));
}

static void updateValue(const MenuItem *item, bool up) {
  SETTINGS_IncDecValue(item->setting, up);
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
            {"Open t", SETTING_SQLOPENTIME, getValueString, updateValue},
            {"Close t", SETTING_SQLCLOSETIME, getValueString, updateValue},
        },
    .num_items = 3,
};

static Menu scanMenu = {
    .title = "Scan options",
    .items =
        (MenuItem[]){
            {"SQL", .submenu = &sqlMenu},
            {"Preferences"},
            {"SCAN listen t/o", SETTING_SQOPENEDTIMEOUT, getValueString,
             updateValue},
            {"SCAN stay t", SETTING_SQCLOSEDTIMEOUT, getValueString,
             updateValue},
            {"SCAN skip X_X", SETTING_SKIPGARBAGEFREQUENCIES, getValueString,
             updateValue},
        },
    .num_items = 2,
};

static void onMainAppChoose(const MenuItem *item) {
  // gSettings.mainApp = *(AppType_t *)(user_data);
}

static void onMainAppSubmenu(const MenuItem *item) {
  Log("Submenu enter");
  submenu.title = "LALALA";
  submenu.num_items = RUN_APPS_COUNT;
  for (uint8_t i = 0; i < RUN_APPS_COUNT; ++i) {
    dynamicItems[i].name = apps[appsAvailableToRun[i]].name;
    dynamicItems[i].action = onMainAppChoose;
    dynamicItems[i].user_data = &i;
  }
}

static const MenuItem menuItems[] = {
    {"Main app", SETTING_MAINAPP, getValueString, updateValue},
    {"FC t", SETTING_FCTIME, getValueString, updateValue},
    {"DTMF decode", SETTING_DTMFDECODE, getValueString, updateValue},
    {"SCAN", .submenu = &scanMenu},
    /*
    {"Contrast", M_CONTRAST, 16},
    {"BL high", M_BRIGHTNESS, 16},
    {"BL low", M_BRIGHTNESS_L, 16},
    {"BL time", M_BL_TIME, ARRAY_SIZE(BL_TIME_VALUES)},
    {"BL SQL mode", M_BL_SQL, ARRAY_SIZE(BL_SQL_MODE_NAMES)},
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

  return state == KEY_RELEASED && MENU_HandleInput(key);
}

void SETTINGS_render(void) {
  if (gIsNumNavInput) {
    STATUSLINE_SetText("Select: %s", gNumNavInput);
  } else {
    STATUSLINE_SetText(apps[APP_SETTINGS].name);
  }

  MENU_Render();
}

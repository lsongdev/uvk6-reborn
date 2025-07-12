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

static void getValS(const MenuItem *item, char *buf, uint8_t buf_size) {
  snprintf(buf, buf_size, "%s", SETTINGS_GetValueString(item->setting));
}

static void updateValS(const MenuItem *item, bool up) {
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
            {"Open t", SETTING_SQLOPENTIME, getValS, updateValS},
            {"Close t", SETTING_SQLCLOSETIME, getValS, updateValS},
        },
    .num_items = 3,
};

static Menu scanMenu = {
    .title = "Scan options",
    .items =
        (MenuItem[]){
            {"SQL", .submenu = &sqlMenu},
            {"FC t", SETTING_FCTIME, getValS, updateValS},
            {"SCAN listen t/o", SETTING_SQOPENEDTIMEOUT, getValS, updateValS},
            {"SCAN stay t", SETTING_SQCLOSEDTIMEOUT, getValS, updateValS},
            {"SCAN skip X_X", SETTING_SKIPGARBAGEFREQUENCIES, getValS,
             updateValS},
        },
    .num_items = 5,
};

static Menu displayMenu = {
    .title = "Display",
    .items =
        (MenuItem[]){
            {"Contrast", SETTING_CONTRAST, getValS, updateValS},
            {"BL max", SETTING_BRIGHTNESS_H, getValS, updateValS},
            {"BL min", SETTING_BRIGHTNESS_L, getValS, updateValS},
            {"BL time", SETTING_BACKLIGHT, getValS, updateValS},
            {"BL SQL mode", SETTING_BACKLIGHTONSQUELCH, getValS, updateValS},
            {"CH display", SETTING_CHDISPLAYMODE, getValS, updateValS},
        },
};

static Menu radioMenu = {
    .title = "Radio",
    .items =
        (MenuItem[]){
            {"DTMF decode", SETTING_DTMFDECODE, getValS, updateValS},
            {"Filter bound", SETTING_BOUND240_280, getValS, updateValS},
            {"SI power off", SETTING_SI4732POWEROFF, getValS, updateValS},
            {"STE", SETTING_STE, getValS, updateValS},
            {"Tone local", SETTING_TONELOCAL, getValS, updateValS},
            {"Roger", SETTING_ROGER, getValS, updateValS},
            {"Multiwatch", SETTING_MULTIWATCH, getValS, updateValS},
        },
};

static Menu batteryMenu = {
    .title = "Battery",
    .items =
        (MenuItem[]){
            {"Bat type", SETTING_BATTERYTYPE, getValS, updateValS},
            {"Bat style", SETTING_BATTERYSTYLE, getValS, updateValS},
            {"BAT cal", SETTING_BATTERYCALIBRATION, getValS, updateValS},
        },
};

static const MenuItem menuItems[] = {
    {"Radio", .submenu = &radioMenu},
    {"Scan", .submenu = &scanMenu},
    {"Display", .submenu = &displayMenu},
    {"Battery", .submenu = &batteryMenu},
    {"Main app", SETTING_MAINAPP, getValS, updateValS},
    {"Beep", SETTING_BEEP, getValS, updateValS},
    {"Lock PTT", SETTING_PTT_LOCK, getValS, updateValS},
    /*
    {"Upconv", M_UPCONVERTER, 0},
    {"Level in VFO", M_LEVEL_IN_VFO, 2},
    {"Mutliwatch", M_MWATCH, 4},
    {"Reset", M_RESET, 2}, */
};

static Menu settingsMenu = {
    .title = "Settings",
    .items = menuItems,
    .num_items = ARRAY_SIZE(menuItems),
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

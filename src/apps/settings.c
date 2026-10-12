#include "apps/settings.h"
#include "hal/backlight.h"
#include "hal/uart.h"
#include "external/printf/printf.h"
#include "core/battery.h"
#include "ui/menu.h"
#include "core/misc.h"
#include "core/settings.h"
#include "core/device-config.h"
#include "apps/apps.h"
#include "apps/finput.h"

static uint8_t DEAD_BUF[] = {0xDE, 0xAD};

static void getValS(const MenuItem *item, char *buf, uint8_t buf_size) {
  snprintf(buf, buf_size, "%s", SETTINGS_GetValueString(item->setting));
}

static void updateValS(const MenuItem *item, bool up) {
  SETTINGS_IncDecValue(item->setting, up);
}

static void doCalibrate(uint32_t v, uint32_t _) {
  (void)_;
  SETTINGS_SetValue(SETTING_BATTERYCALIBRATION, BATTERY_GetCal(v * 100));
}

static bool calibrate(const MenuItem *item, KEY_Code_t key, Key_State_t state) {
  (void)item;

  if (state != KEY_RELEASED || key != KEY_MENU) {
    return false;
  }

  uint32_t currentVoltage =
      BATTERY_GetPreciseVoltage(SETTINGS_GetValue(SETTING_BATTERYCALIBRATION));

  gFInputValue1 = currentVoltage / 100;
  gFInputCallback = doCalibrate;
  FINPUT_setup(500, 860, UNIT_VOLTS, false);
  APPS_run(APP_FINPUT);
  return true;
}

// SQL submenu
static const MenuItem sqlMenuItems[] = {
    {"Open t", SETTING_SQLOPENTIME, getValS, updateValS},
    {"Close t", SETTING_SQLCLOSETIME, getValS, updateValS},
};

static Menu sqlMenu = {.title = "SQL",
                       .items = sqlMenuItems,
                       .num_items = ARRAY_SIZE(sqlMenuItems)};

// Scan submenu
// Display submenu
static const MenuItem displayMenuItems[] = {
    {"Contrast", SETTING_CONTRAST, getValS, updateValS},
    {"BL max", SETTING_BRIGHTNESS_H, getValS, updateValS},
    {"BL min", SETTING_BRIGHTNESS_L, getValS, updateValS},
    {"BL time", SETTING_BACKLIGHT, getValS, updateValS},
    {"BL SQL mode", SETTING_BACKLIGHTONSQUELCH, getValS, updateValS},
};

static Menu displayMenu = {.title = "Display",
                           .items = displayMenuItems,
                           .num_items = ARRAY_SIZE(displayMenuItems)};

static void getVfoCount(const MenuItem *item, char *out, uint8_t size) {
  (void)item; snprintf(out,size,"%u",gDeviceConfig.radio.vfo_count);
}
static void setVfoCount(const MenuItem *item, bool up) {
  (void)item;
  uint8_t count=gDeviceConfig.radio.vfo_count;
  if (up) count=count==RADIO_VFO_CAPACITY ? 2 : count+1;
  else count=count==2 ? RADIO_VFO_CAPACITY : count-1;
  if (RADIO_STATE_SetCount(&gDeviceConfig.radio,count)) DEVICE_RequestSave();
}
static void getWatch(const MenuItem *item, char *out, uint8_t size) {
  (void)item; snprintf(out,size,"%s",gDeviceConfig.radio.watch_enabled ? "On" : "Off");
}
static void setWatch(const MenuItem *item, bool up) {
  (void)item; (void)up; gDeviceConfig.radio.watch_enabled^=1;
  DEVICE_RequestSave();
}

// Radio submenu
static const MenuItem radioMenuItems[] = {
    {"VFO Count", .get_value_text=getVfoCount, .change_value=setVfoCount},
    {"Dual Watch", .get_value_text=getWatch, .change_value=setWatch},
    {"Filter bound", SETTING_BOUND240_280, getValS, updateValS},
    {"SI power off", SETTING_SI4732POWEROFF, getValS, updateValS},
};

static Menu radioMenu = {.title = "Radio",
                         .items = radioMenuItems,
                         .num_items = ARRAY_SIZE(radioMenuItems)};

// Battery submenu
static const MenuItem batMenuItems[] = {
    {"Bat type", SETTING_BATTERYTYPE, getValS, updateValS},
    {"Bat style", SETTING_BATTERYSTYLE, getValS, updateValS},
    {"BAT cal", SETTING_BATTERYCALIBRATION, getValS, .action = calibrate},
};

static Menu batteryMenu = {.title = "Battery",
                           .items = batMenuItems,
                           .num_items = ARRAY_SIZE(batMenuItems)};

static bool openBands(const MenuItem *item, KEY_Code_t key, Key_State_t state) {
  (void)item;
  if (key != KEY_MENU || state != KEY_RELEASED)
    return false;
  APPS_run(APP_BANDS);
  return true;
}

static void getTXTime(const MenuItem *item, char *out, uint8_t size) {
  (void)item;
  snprintf(out, size, "%u s", (unsigned)gDeviceConfig.preferences.tx_timeout_seconds);
}
static void setTXTime(const MenuItem *item, bool up) {
  (void)item;
  static const uint16_t values[] = {30, 60, 120, 180, 300, 600};
  uint16_t current = gDeviceConfig.preferences.tx_timeout_seconds;
  uint8_t i = 0;
  while (i < 6 && values[i] != current) ++i;
  if (i == 6) i = 2;
  else i = (uint8_t)((i + (up ? 1u : 5u)) % 6u);
  gDeviceConfig.preferences.tx_timeout_seconds = values[i];
  DEVICE_RequestSave();
}
static void getBusyLockout(const MenuItem *item, char *out, uint8_t size) {
  (void)item;
  snprintf(out, size, "%s", gDeviceConfig.preferences.tx_busy_lockout ? "On" : "Off");
}
static void setBusyLockout(const MenuItem *item, bool up) {
  (void)item; (void)up;
  gDeviceConfig.preferences.tx_busy_lockout ^= 1u;
  DEVICE_RequestSave();
}
static const MenuItem txSafetyItems[] = {
    {"TX timeout", .get_value_text = getTXTime, .change_value = setTXTime},
    {"Busy lock", .get_value_text = getBusyLockout, .change_value = setBusyLockout},
};
static Menu txSafetyMenu = {.title = "TX Safety",
                           .items = txSafetyItems,
                           .num_items = ARRAY_SIZE(txSafetyItems)};

// Main menu
static const MenuItem menuItems[] = {
    {"Bands", .action = openBands},
    {"SQL", .submenu = &sqlMenu},
    {"Radio", .submenu = &radioMenu},
    {"TX Safety", .submenu = &txSafetyMenu},
    {"Display", .submenu = &displayMenu},
    {"Battery", .submenu = &batteryMenu},
    {"Beep", SETTING_BEEP, getValS, updateValS},
    {"Lock PTT", SETTING_PTT_LOCK, getValS, updateValS},
};

static Menu settingsMenu = {
    .title = "Settings",
    .items = menuItems,
    .num_items = ARRAY_SIZE(menuItems),
};

void SETTINGS_init(void) { MENU_Init(&settingsMenu); }

void SETTINGS_deinit(void) {}

bool SETTINGS_key(KEY_Code_t key, Key_State_t state) {
  return MENU_HandleInput(key, state);
}

void SETTINGS_render(void) { MENU_Render(); }

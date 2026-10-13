#include "apps/settings.h"
#include "apps/apps.h"
#include "apps/radio.h"
#include "core/settings.h"
#include "core/device-config.h"
#include "core/battery.h"
#include "core/misc.h"
#include "core/measurements.h"
#include "core/radio.h"
#include "hal/tx-device.h"
#include "ui/menu.h"
#include "ui/graphics.h"
#include "ui/statusline.h"
#include "external/printf/printf.h"

static const char *notice;
static bool resetConfirm;
static uint8_t oldOpen, oldClose, oldBoundary;

static void getValue(const MenuItem *item, char *out, uint8_t size) {
  if (item->setting == SETTING_SI4732POWEROFF && !RADIO_GetReceiverCapabilities().si) {
    snprintf(out,size,"No SI"); return;
  }
  SETTINGS_GetValueText(item->setting,out,size);
}
static void changeValue(const MenuItem *item, bool up) {
  notice = NULL;
  if (item->setting == SETTING_SI4732POWEROFF && !RADIO_GetReceiverCapabilities().si) {
    notice = "SI4732 missing"; return;
  }
  SETTINGS_IncDecValue(item->setting,up);
}
static void doCalibrate(uint32_t value, uint32_t unused) {
  (void)unused;
  uint16_t calibration = BATTERY_GetCal(value*100u);
  if (calibration < 1900 || calibration > 2154) { notice="Invalid calibration"; return; }
  ConfigPreferences next = gDeviceConfig.preferences;
  next.battery_calibration = calibration;
  notice = DEVICE_SavePreferences(&next) ? "Voltage calibrated" : "Save failed";
}
static bool calibrate(const MenuItem *item, KEY_Code_t key, Key_State_t state) {
  (void)item;
  if (state != KEY_RELEASED || key != KEY_MENU) return false;
  uint32_t voltage = BATTERY_GetPreciseVoltage(gDeviceConfig.preferences.battery_calibration);
  MENU_EditNumber(500,860,100,voltage/100u,0,false,"V",doCalibrate);
  return true;
}
#define VALUE(name, id) {name, id, getValue, changeValue}
#define MENU_DEF(variable, caption, entries) static Menu variable = { \
    .title=caption, .items=entries, .num_items=ARRAY_SIZE(entries)}

static const MenuItem displayItems[] = {
    VALUE("Contrast",SETTING_CONTRAST),
    VALUE("Light time",SETTING_BACKLIGHT),
    VALUE("Brightness",SETTING_BRIGHTNESS_H),
    VALUE("Idle light",SETTING_BRIGHTNESS_L),
    VALUE("Light on RX",SETTING_BACKLIGHTONSQUELCH),
};
MENU_DEF(displayMenu,"Display",displayItems);

static void getRadioValue(const MenuItem *item, char *out, uint8_t size) {
  if (!item->setting) snprintf(out,size,"%u",gDeviceConfig.radio.slot_count);
  else snprintf(out,size,"%s",gDeviceConfig.radio.watch_enabled ? "On" : "Off");
}
static void changeRadioValue(const MenuItem *item, bool up) {
  notice = NULL;
  if (!item->setting) {
    uint8_t count=IncDecU(gDeviceConfig.radio.slot_count,2,RADIO_SLOT_CAPACITY+1u,up);
    if (!RADIO_STATE_SetCount(&gDeviceConfig.radio,count)) return;
  } else gDeviceConfig.radio.watch_enabled ^= 1u;
  DEVICE_RequestSave();
}
static const MenuItem receiveItems[] = {
    {"VFO count",0,getRadioValue,changeRadioValue},
    {"Dual watch",1,getRadioValue,changeRadioValue},
    VALUE("SQL open delay",SETTING_SQLOPENTIME),
    VALUE("SQL close delay",SETTING_SQLCLOSETIME),
    VALUE("SI4732 idle",SETTING_SI4732POWEROFF),
    VALUE("Filter split",SETTING_BOUND240_280),
};
MENU_DEF(receiveMenu,"Receive",receiveItems);
static const MenuItem transmitItems[] = {
    VALUE("Time limit",SETTING_TX_TIMEOUT),
    VALUE("Block if busy",SETTING_TX_BUSY_LOCK),
};
MENU_DEF(transmitMenu,"Transmit",transmitItems);
static const MenuItem batteryItems[] = {
    VALUE("Show charge",SETTING_BATTERYSTYLE),
    VALUE("Charge curve",SETTING_BATTERYTYPE),
    {"Cal voltage",SETTING_BATTERYCALIBRATION,getValue,.action=calibrate},
};
MENU_DEF(batteryMenu,"Battery",batteryItems);
static bool openBands(const MenuItem *item, KEY_Code_t key, Key_State_t state) {
  (void)item;
  if (key != KEY_MENU || state != KEY_RELEASED) return false;
  APPS_run(APP_BANDS); return true;
}
static void getSaveStatus(const MenuItem *item, char *out, uint8_t size) {
  (void)item;
  static const char *names[] = {"Read only","Saved","Pending","Failed"};
  snprintf(out,size,"%s",names[DEVICE_GetSaveStatus()]);
}
static bool saveNow(const MenuItem *item, KEY_Code_t key, Key_State_t state) {
  (void)item;
  if (key != KEY_MENU || state != KEY_RELEASED) return false;
  notice = DEVICE_SaveNow() ? "Settings saved" : "Save failed"; return true;
}
static void getTxStatus(const MenuItem *item, char *out, uint8_t size) {
  (void)item;
  const RadioSlot *vfo = &gDeviceConfig.radio.slots[gDeviceConfig.radio.selected_slot];
  TxCapabilities capability = TX_DEVICE_Capabilities(vfo->tx.frequency_hz ? vfo->tx.frequency_hz : vfo->current.frequency_hz);
  TxPolicy policy = TX_DEVICE_Policy();
  const char *status = !capability.available ? "HW locked" :
      !capability.power_mask ? "Uncalibrated" : !policy.authorized ? "Not allowed" : "Ready";
  snprintf(out,size,"%s",status);
}
static bool resetPreferences(const MenuItem *item, KEY_Code_t key, Key_State_t state) {
  (void)item;
  if (key != KEY_MENU || state != KEY_RELEASED) return false;
  resetConfirm = true; notice = NULL; return true;
}
static void getInfo(const MenuItem *item, char *out, uint8_t size) {
  ReceiverCapabilities capabilities = RADIO_GetReceiverCapabilities();
  switch (item->setting) {
  case 0:
#ifdef GIT_HASH
    snprintf(out,size,"%s",GIT_HASH);
#else
    snprintf(out,size,"dev");
#endif
    break;
  case 1: snprintf(out,size,"%u KiB",(unsigned)(SETTINGS_GetEEPROMSize()/1024u)); break;
  case 2: snprintf(out,size,"UVR2 %u",CONFIG_STORE_FORMAT); break;
  case 3: snprintf(out,size,"%s",capabilities.si ? "Present" : "Missing"); break;
  case 4: snprintf(out,size,"%s",capabilities.broadcast ? "Present" : "Missing"); break;
  default: snprintf(out,size,"%s",capabilities.ssb_patch ? "Present" : "Missing"); break;
  }
}
static const MenuItem aboutItems[] = {
    {"Firmware",0,getInfo}, {"EEPROM",1,getInfo}, {"Config",2,getInfo},
    {"SI4732",3,getInfo}, {"BK1080",4,getInfo}, {"SSB patch",5,getInfo},
};
MENU_DEF(aboutMenu,"About",aboutItems);
static const MenuItem systemItems[] = {
    {"TX status",.get_value_text=getTxStatus},
    {"Save status",.get_value_text=getSaveStatus,.action=saveNow},
    {"Reset prefs",.action=resetPreferences},
};
MENU_DEF(systemMenu,"System",systemItems);
static const MenuItem menuItems[] = {
    {"Display",.submenu=&displayMenu}, {"Receive",.submenu=&receiveMenu},
    {"Transmit",.submenu=&transmitMenu}, VALUE("Lock PTT too",SETTING_PTT_LOCK),
    {"Battery",.submenu=&batteryMenu}, {"Band presets",.action=openBands},
    {"System",.submenu=&systemMenu}, {"About",.submenu=&aboutMenu},
};
MENU_DEF(settingsMenu,"Settings",menuItems);

void SETTINGS_init(void) {
  notice = NULL; resetConfirm = false;
  oldOpen=gDeviceConfig.preferences.sql_open_time;
  oldClose=gDeviceConfig.preferences.sql_close_time;
  oldBoundary=gDeviceConfig.preferences.bound_240_280;
  MENU_Init(&settingsMenu);
}
void SETTINGS_deinit(void) {
  ConfigPreferences *p = &gDeviceConfig.preferences;
  if (oldOpen!=p->sql_open_time || oldClose!=p->sql_close_time || oldBoundary!=p->bound_240_280) {
    if (!RADIO_APP_ApplyRxSettings()) {
      p->sql_open_time=oldOpen; p->sql_close_time=oldClose; p->bound_240_280=oldBoundary;
      (void)RADIO_APP_ApplyRxSettings();
      DEVICE_RequestSave();
    }
  }
  if (DEVICE_GetSaveStatus()==DEVICE_PENDING) (void)DEVICE_SaveNow();
}
bool SETTINGS_key(KEY_Code_t key, Key_State_t state) {
  if (resetConfirm) {
    if (state==KEY_RELEASED && key==KEY_MENU) {
      resetConfirm=false;
      notice=SETTINGS_ResetPreferences() ? "Defaults restored" : "Save failed";
    } else if (state==KEY_RELEASED && key==KEY_EXIT) resetConfirm=false;
    return true;
  }
  notice=NULL;
  return MENU_HandleInput(key,state);
}
void SETTINGS_render(void) {
  if (resetConfirm) {
    STATUSLINE_SetText("Reset prefs?");
    PrintSmall(3,18,"Display, keys, TX limits");
    PrintSmall(3,29,"Keep radio & calibration");
    PrintSmall(3,45,"MENU: reset");
    PrintSmall(3,56,"EXIT: cancel");
    return;
  }
  MENU_Render();
  if (notice) STATUSLINE_SetText("%s",notice);
}

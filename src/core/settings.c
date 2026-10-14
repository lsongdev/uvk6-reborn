#include "core/settings.h"
#include "core/device-config.h"
#include "core/battery.h"
#include "core/misc.h"
#include "core/measurements.h"
#include "drivers/bk4819/bk4819.h"
#include "hal/backlight.h"
#include "hal/display.h"
#include "hal/eeprom.h"
#include "core/text.h"
#include <stddef.h>
#include <string.h>

// Only retired driver-local controls remain here. Persisted preferences have
// exactly one runtime owner: gDeviceConfig.preferences.
uint8_t BL_TIME_VALUES[7] = {0, 5, 10, 20, 60, 120, 255};
const char *BL_SQL_MODE_NAMES[3] = {"Off", "Timed", "Receiving"};
const char *EEPROM_TYPE_NAMES[7] = {"64 ?", "128", "256", "512", "1024", "M02?", "4Mb?"};
const uint32_t EEPROM_SIZES[7] = {8192,16384,32768,65536,131072,262144,524288};
const uint16_t PAGE_SIZES[7] = {32,64,64,128,128,256,128};

// Small descriptors avoid duplicated enum/boolean handlers. Bounds are
// checked before writing the native field or invoking a hardware effect.
typedef struct { uint8_t offset, width; uint16_t min, max; } Preference;
#define PREF(id, member, lo, hi) [id] = {offsetof(ConfigPreferences, member), \
    sizeof(((ConfigPreferences *)0)->member), lo, hi}
static const Preference preferences[SETTING_COUNT] = {
    PREF(SETTING_BACKLIGHT, backlight, 0, 6),
    PREF(SETTING_BRIGHTNESS_H, brightness, 0, 15),
    PREF(SETTING_BRIGHTNESS_L, brightness_low, 0, 15),
    PREF(SETTING_CONTRAST, contrast, 0, 15),
    PREF(SETTING_BACKLIGHTONSQUELCH, backlight_on_squelch, 0, 2),
    PREF(SETTING_BATTERYCALIBRATION, battery_calibration, 1900, 2154),
    PREF(SETTING_BATTERYTYPE, battery_type, 0, 2),
    PREF(SETTING_BATTERYSTYLE, battery_style, 0, 2),
    PREF(SETTING_KEYLOCK, keylock, 0, 1),
    PREF(SETTING_PTT_LOCK, ptt_lock, 0, 1),
    PREF(SETTING_SI4732POWEROFF, si4732_power_off, 0, 1),
    PREF(SETTING_SQLOPENTIME, sql_open_time, 0, 7),
    PREF(SETTING_SQLCLOSETIME, sql_close_time, 0, 3),
    PREF(SETTING_BOUND240_280, bound_240_280, 0, 1),
    PREF(SETTING_TX_TIMEOUT, tx_timeout_seconds, 15, 600),
    PREF(SETTING_TX_BUSY_LOCK, tx_busy_lockout, 0, 1),
    PREF(SETTING_MIC, mic, 0, 31),
};
#undef PREF
static const Preference *preference(Setting id) {
  return (unsigned)id < SETTING_COUNT && preferences[id].width ? &preferences[id] : NULL;
}
uint32_t SETTINGS_GetValue(Setting id) {
  const Preference *field = preference(id);
  if (!field) return 0;
  const uint8_t *address = (const uint8_t *)&gDeviceConfig.preferences + field->offset;
  if (field->width == 1) return *address;
  uint16_t value; memcpy(&value, address, sizeof(value)); return value;
}
static void writeValue(const Preference *field, uint32_t value) {
  uint8_t *address = (uint8_t *)&gDeviceConfig.preferences + field->offset;
  if (field->width == 1) *address = value;
  else { uint16_t v = value; memcpy(address, &v, sizeof(v)); }
}
bool SETTINGS_SetValue(Setting id, uint32_t value) {
  const Preference *field = preference(id);
  if (!field || value < field->min || value > field->max) return false;
  ConfigPreferences *p = &gDeviceConfig.preferences;
  if (id == SETTING_BRIGHTNESS_L && value > p->brightness) return false;
  if (SETTINGS_GetValue(id) == value) return true;
  writeValue(field, value);
  if (id == SETTING_BRIGHTNESS_H && p->brightness_low > value)
    p->brightness_low = value;
  switch (id) {
  case SETTING_BACKLIGHT:
  case SETTING_BRIGHTNESS_H:
  case SETTING_BRIGHTNESS_L:
    BACKLIGHT_ApplySettings(); break;
  case SETTING_CONTRAST: DISPLAY_Init(false, p->contrast); break;
  default: break;
  }
  DEVICE_RequestSave();
  return true;
}
void SETTINGS_IncDecValue(Setting id, bool up) {
  const Preference *field = preference(id);
  if (!field || id == SETTING_BATTERYCALIBRATION) return;
  uint32_t value = SETTINGS_GetValue(id), max = field->max;
  if (id == SETTING_BRIGHTNESS_L) max = gDeviceConfig.preferences.brightness;
  if (id == SETTING_TX_TIMEOUT) {
    static const uint16_t options[] = {30,60,120,180,300,600};
    unsigned i = 0;
    while (i < ARRAY_SIZE(options) && options[i] != value) ++i;
    if (i == ARRAY_SIZE(options)) i = 2;
    else i = (i + (up ? 1u : ARRAY_SIZE(options)-1u)) % ARRAY_SIZE(options);
    value = options[i];
  } else value = IncDecU(value, field->min, max+1u, up);
  (void)SETTINGS_SetValue(id, value);
}
void SETTINGS_GetValueText(Setting id, char *out, uint8_t size) {
  uint32_t value = SETTINGS_GetValue(id);
  const char *text = NULL;
  Text t = TEXT_Begin(out,size);
  switch (id) {
  case SETTING_BACKLIGHT:
    if (!value) text = "Off";
    else if (value == 6) text = "Always";
    else { TEXT_Number(&t,BL_TIME_VALUES[value],1,10); TEXT_Add(&t," s"); return; }
    break;
  case SETTING_SI4732POWEROFF: text = value ? "Power off" : "Mute"; break;
  case SETTING_BOUND240_280: text = value ? "280 MHz" : "240 MHz"; break;
  case SETTING_BACKLIGHTONSQUELCH: text = BL_SQL_MODE_NAMES[value]; break;
  case SETTING_BATTERYTYPE: {
    static const char *names[] = {"1600", "2200", "3500"}; text = names[value]; break;
  }
  case SETTING_BATTERYSTYLE: {
    static const char *names[] = {"Icon", "Percent", "Voltage"}; text = names[value]; break;
  }
  case SETTING_PTT_LOCK:
  case SETTING_KEYLOCK:
  case SETTING_TX_BUSY_LOCK: text = value ? "On" : "Off"; break;
  case SETTING_SQLOPENTIME:
  case SETTING_SQLCLOSETIME: TEXT_Add(&t,"Level "); TEXT_Number(&t,value,1,10); return;
  case SETTING_CONTRAST: // Stored 0..15, shown centred on 0.
    if (value < 8) TEXT_Char(&t,'-');
    TEXT_Number(&t,value < 8 ? 8-value : value-8,1,10); return;
  case SETTING_TX_TIMEOUT: TEXT_Number(&t,value,1,10); TEXT_Add(&t," s"); return;
  case SETTING_BATTERYCALIBRATION: {
    uint32_t voltage = BATTERY_GetPreciseVoltage(value);
    TEXT_Number(&t,voltage/10000u,1,10); TEXT_Char(&t,'.');
    TEXT_Number(&t,voltage/100u%100u,2,10); TEXT_Add(&t," V"); return;
  }
  default: TEXT_Number(&t,value,1,10); return;
  }
  TEXT_Add(&t,text);
}
bool SETTINGS_ResetPreferences(void) {
  ConfigPreferences next = CONFIG_DEFAULT_PREFERENCES;
  const ConfigPreferences *p = &gDeviceConfig.preferences;
  next.battery_calibration = p->battery_calibration;
  next.battery_type = p->battery_type;
  next.bound_240_280 = p->bound_240_280;
  next.sql_open_time = p->sql_open_time;
  next.sql_close_time = p->sql_close_time;
  next.si4732_power_off = p->si4732_power_off;
  next.mic = p->mic;
  next.deviation = p->deviation;
  next.keylock = p->keylock;
  if (!DEVICE_SavePreferences(&next)) return false;
  DISPLAY_Init(false, next.contrast);
  BACKLIGHT_ApplySettings();
  return true;
}
void SETTINGS_Save(void) { (void)DEVICE_SaveNow(); }
bool SETTINGS_Load(void) {
  return DEVICE_Load();
}
void SETTINGS_UpdateSave(void) { DEVICE_Update(); }
uint32_t SETTINGS_GetFilterBound(void) {
  return gDeviceConfig.preferences.bound_240_280 ? VHF_UHF_BOUND2 : VHF_UHF_BOUND1;
}
uint32_t SETTINGS_GetEEPROMSize(void) { return EEPROM_DetectSize(); }
bool SETTINGS_IsPatchPresent(void) {
  // A matching prefix does not authenticate the rest of the SSB patch.
  // For an unverified installation, SSB stays unavailable by default.
#ifndef UVK6_SSB_PATCH_CRC32
  return false;
#else
  static const uint8_t preamble[] = {0x15,0x00,0x03,0x74,0x0b,0xd4,0x84,0x60};
  uint32_t size = SETTINGS_GetEEPROMSize();
  if (size < 32768 || size < PATCH_SIZE) return false;
  uint8_t buf[32];
  uint32_t crc = 0xffffffffu;
  for (uint32_t offset=0;offset<PATCH_SIZE;offset+=sizeof(buf)) {
    uint16_t count = PATCH_SIZE-offset > sizeof(buf)
        ? sizeof(buf) : (uint16_t)(PATCH_SIZE-offset);
    if (EEPROM_ReadBuffer(size-PATCH_SIZE+offset,buf,count)!=0) return false;
    if (!offset && memcmp(buf,preamble,sizeof(preamble))!=0) return false;
    for (uint16_t i=0;i<count;++i) {
      crc ^= buf[i];
      for (unsigned bit=0;bit<8;++bit)
        crc = (crc>>1) ^ (0xedb88320u & (0u-(crc&1u)));
    }
  }
  return (~crc) == (uint32_t)UVK6_SSB_PATCH_CRC32;
#endif
}

// Exercise the real versioned store through the device save scheduler.
#include "../src/core/device-config.c"
#include <assert.h>
#include <string.h>
static uint8_t bytes[524288];
static uint32_t clockNow=100;
static unsigned writes;
static bool writeWorks=true;
uint32_t Now(void) { return clockNow; }
EEPROMType EEPROM_DetectType(void) { return EEPROM_4MBIT; }
uint32_t EEPROM_DetectSize(void) { return sizeof(bytes); }
bool CHANNEL_STORE_Init(void) { return true; }
int EEPROM_ReadBuffer(uint32_t at, void *out, uint16_t size) {
  assert(at+size<=sizeof(bytes)); memcpy(out,bytes+at,size); return 0;
}
bool EEPROM_TryWriteBuffer(uint32_t at, const uint8_t *in, uint16_t size) {
  ++writes;
  assert(at>=CONFIG_STORE_BASE && at+size<=CONFIG_STORE_END);
  if (!writeWorks) return false;
  memcpy(bytes+at,in,size); return true;
}
int main(void) {
  assert(DEVICE_GetSaveStatus()==DEVICE_UNAVAILABLE);
  assert(gDeviceConfig.preferences.battery_calibration==2000);
  assert(!DEVICE_SaveNow() && !writes);
  memset(bytes,255,sizeof(bytes));
  assert(DEVICE_Initialize());
  assert(DEVICE_GetSaveStatus()==DEVICE_SAVED);
  unsigned savedWrites=writes;
  DEVICE_Update(); assert(writes==savedWrites);
  gDeviceConfig.preferences.contrast=12;
  DEVICE_RequestSave(); assert(DEVICE_GetSaveStatus()==DEVICE_PENDING);
  clockNow=1599; DEVICE_Update(); assert(writes==savedWrites);
  // Later edits move the single save deadline, even across clock wrap.
  DEVICE_RequestSave(); clockNow=3098; DEVICE_Update(); assert(writes==savedWrites);
  clockNow=3099; DEVICE_Update(); assert(writes>savedWrites);
  assert(DEVICE_GetSaveStatus()==DEVICE_SAVED);
  gDeviceConfig.preferences.contrast=1;
  assert(DEVICE_Load() && gDeviceConfig.preferences.contrast==12);
  assert(gDeviceConfig.preferences.battery_calibration==2000);
  writeWorks=false;
  gDeviceConfig.preferences.contrast=13;
  DEVICE_RequestSave(); clockNow+=1500; DEVICE_Update();
  assert(DEVICE_GetSaveStatus()==DEVICE_SAVE_FAILED);
  savedWrites=writes; clockNow+=4999; DEVICE_Update(); assert(writes==savedWrites);
  ConfigPreferences before=gDeviceConfig.preferences, draft=before;
  draft.battery_calibration=2100;
  assert(!DEVICE_SavePreferences(&draft));
  assert(!memcmp(&before,&gDeviceConfig.preferences,sizeof(before)));
  writeWorks=true;
  assert(DEVICE_SaveNow()); assert(DEVICE_GetSaveStatus()==DEVICE_SAVED);
  assert(DEVICE_SavePreferences(&draft));
  assert(gDeviceConfig.preferences.battery_calibration==2100);
  assert(DEVICE_Load() && gDeviceConfig.preferences.battery_calibration==2100);
  clockNow=UINT32_MAX-100u;
  gDeviceConfig.preferences.contrast=14; DEVICE_RequestSave(); savedWrites=writes;
  clockNow=1398; DEVICE_Update(); assert(writes==savedWrites);
  clockNow=1399; DEVICE_Update(); assert(writes>savedWrites && DEVICE_GetSaveStatus()==DEVICE_SAVED);
  gDeviceConfig.preferences.brightness=3;
  gDeviceConfig.preferences.brightness_low=8;
  assert(DEVICE_SaveNow());
  assert(DEVICE_Load() && gDeviceConfig.preferences.brightness_low==3);
  assert(DEVICE_GetSaveStatus()==DEVICE_PENDING);
  return 0;
}

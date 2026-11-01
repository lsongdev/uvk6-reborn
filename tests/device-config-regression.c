// Exercise the real versioned store through the device save scheduler.
#include "../src/store/device-config.c"
#include "fake-storage.h"
#include <assert.h>
#include <string.h>
static uint32_t clockNow=100;
#define writes fakeWrites
uint32_t Now(void) { return clockNow; }
bool CHANNEL_STORE_Init(void) { return true; }
int main(void) {
  // Production calls CONFIG_Default at the entry point, before peripherals.
  assert(gDeviceConfig.preferences.battery_calibration==0);
  CONFIG_Default(&gDeviceConfig);
  assert(DEVICE_GetSaveStatus()==DEVICE_UNAVAILABLE);
  assert(gDeviceConfig.preferences.battery_calibration==2000);
  assert(!DEVICE_SaveNow() && !writes);
  memset(fakeArea,255,sizeof(fakeArea));
  assert(DEVICE_Initialize());
  assert(DEVICE_GetSaveStatus()==DEVICE_SAVED);
  assert(fakeLastWriteArea<=STORAGE_CONFIG_B); // Config areas only.
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
  fakeFailWrite=true;
  gDeviceConfig.preferences.contrast=13;
  DEVICE_RequestSave(); clockNow+=1500; DEVICE_Update();
  assert(DEVICE_GetSaveStatus()==DEVICE_SAVE_FAILED);
  savedWrites=writes; clockNow+=4999; DEVICE_Update(); assert(writes==savedWrites);
  ConfigPreferences before=gDeviceConfig.preferences, draft=before;
  draft.battery_calibration=2100;
  assert(!DEVICE_SavePreferences(&draft));
  assert(!memcmp(&before,&gDeviceConfig.preferences,sizeof(before)));
  fakeFailWrite=false;
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
  // Storage with a configuration area never runs volatile.
  assert(!DEVICE_UseVolatile());
  // Read-only bring-up storage (no configuration area): RAM defaults, and
  // saves succeed without a single storage write.
  fakeSize[STORAGE_CONFIG_A]=fakeSize[STORAGE_CONFIG_B]=0;
  assert(!DEVICE_Load() && DEVICE_UseVolatile());
  assert(gDeviceConfigReady && DEVICE_GetSaveStatus()==DEVICE_VOLATILE);
  assert(gDeviceConfig.preferences.contrast==8); // CONFIG_DEFAULT_PREFERENCES.
  savedWrites=writes;
  gDeviceConfig.preferences.contrast=11; DEVICE_RequestSave();
  clockNow+=2000; DEVICE_Update();
  assert(DEVICE_SaveNow() && writes==savedWrites && DEVICE_GetSaveStatus()==DEVICE_VOLATILE);
  fakeReady=false; assert(!DEVICE_UseVolatile()); // Missing storage stays unavailable.

  // K1's actual factory ADC points: first initialization, failed/no-config
  // load, and RAM mode all use the reference instead of the K6-era 2000.
  volatileConfig=false; // Simulate reboot after the read-only bring-up test.
  fakeReady=true;
  fakeSize[STORAGE_CONFIG_A]=fakeSize[STORAGE_CONFIG_B]=1024;
  memset(fakeArea,255,sizeof(fakeArea));
  static const uint16_t points[6]={1442,2010,2163,2193,2314,2600};
  for (unsigned i=0;i<6;++i) {
    fakeArea[STORAGE_CALIBRATION][0x140u+i*2u]=points[i];
    fakeArea[STORAGE_CALIBRATION][0x141u+i*2u]=points[i]>>8;
  }
  savedWrites=writes;
  assert(!DEVICE_Load() && writes==savedWrites);
  assert(gDeviceConfig.preferences.battery_calibration==2193);
  assert(DEVICE_Initialize());
  assert(gDeviceConfig.preferences.battery_calibration==2193);
  assert(DEVICE_Load() && gDeviceConfig.preferences.battery_calibration==2193);
  assert(DEVICE_GetSaveStatus()==DEVICE_SAVED);

  // Repair old default-only configs without resetting radio/settings. No
  // writes during load; the existing deferred save path commits the repair.
  gDeviceConfig.preferences.battery_calibration=2000;
  gDeviceConfig.preferences.contrast=12;
  assert(DEVICE_SaveNow()); savedWrites=writes;
  assert(DEVICE_Load() && writes==savedWrites);
  assert(gDeviceConfig.preferences.battery_calibration==2193);
  assert(gDeviceConfig.preferences.contrast==12 && DEVICE_GetSaveStatus()==DEVICE_PENDING);
  clockNow+=1500; DEVICE_Update(); assert(writes>savedWrites);
  assert(DEVICE_Load() && gDeviceConfig.preferences.battery_calibration==2193);
  assert(DEVICE_GetSaveStatus()==DEVICE_SAVED);

  // Explicit non-default calibration survives load and later saves.
  draft=gDeviceConfig.preferences; draft.battery_calibration=2240;
  assert(DEVICE_SavePreferences(&draft));
  savedWrites=writes;
  assert(DEVICE_Load() && gDeviceConfig.preferences.battery_calibration==2240);
  assert(writes==savedWrites && DEVICE_GetSaveStatus()==DEVICE_SAVED);
  fakeSize[STORAGE_CONFIG_A]=fakeSize[STORAGE_CONFIG_B]=0;
  assert(!DEVICE_Load() && DEVICE_UseVolatile());
  assert(gDeviceConfig.preferences.battery_calibration==2193);
  return 0;
}

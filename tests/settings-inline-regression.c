// Real Settings, preferences, input and framebuffer; only hardware/storage mocked.
#include "../src/ui/screens/preferences.c"
#include "../src/ui/screens/settings.c"
#include "../src/ui/menu.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

DeviceConfig gDeviceConfig = {.preferences=CONFIG_DEFAULT_PREFERENCES};
uint8_t gFrameBuffer[8][128];
bool gRedrawScreen;
static unsigned saves, appRuns, requests, reapplications;
static bool saveWorks=true, applyWorks=true;
static uint16_t calibrationResult=2100;
static uint32_t calibratedVoltage;
static ReceiverCapabilities capabilities = {.bk_am=true, .bk_bandwidth_mask=RX_BK_BANDWIDTH_ALL};
static char title[40];
static uint8_t panels[10][8][128];
static unsigned panelCount;
uint32_t BATTERY_GetPreciseVoltage(uint16_t cal) { assert(cal>=1900 && cal<=2154); return 82400; }
uint16_t BATTERY_GetCal(uint32_t voltage) { calibratedVoltage=voltage; return calibrationResult; }
void BACKLIGHT_ApplySettings(void) {}
void DISPLAY_Init(bool full, uint8_t contrast) { (void)full; (void)contrast; }
void DEVICE_RequestSave(void) { ++requests; }
DeviceSaveStatus DEVICE_GetSaveStatus(void) { return DEVICE_SAVED; }
bool DEVICE_SaveNow(void) { return saveWorks; }
bool DEVICE_SavePreferences(const ConfigPreferences *next) {
  if (!saveWorks) return false;
  gDeviceConfig.preferences=*next; ++saves; return true;
}
bool RADIO_ApplyRxSettings(void) { ++reapplications; return applyWorks; }
static unsigned slotChanges;
void RADIO_SlotsChanged(void) { ++slotChanges; }
ReceiverCapabilities RECEIVER_Capabilities(void) { return capabilities; }
TxCapabilities TX_DEVICE_Capabilities(uint32_t frequency_hz) { (void)frequency_hz; return (TxCapabilities){0}; }
TxPolicy TX_DEVICE_Policy(void) { return (TxPolicy){0}; }
uint32_t STORAGE_Capacity(void) { return 524288; }
const Screen bandsScreen = {0}, channelsScreen = {0};
static const Screen *lastScreen;
void SCREEN_Push(const Screen *screen) {
  assert(screen==&bandsScreen || screen==&channelsScreen); lastScreen=screen; ++appRuns;
}
void STATUSLINE_SetText(const char *text) { TEXT_Copy(title,sizeof(title),text); }
static void press(KEY_Code_t k) {
  bool handled=key(k,KEY_RELEASED);
  if (!handled) fprintf(stderr,"Unhandled key %u in %s at %u\n",k,active_menu ? active_menu->title : "none",active_menu ? active_menu->i : 0);
  assert(handled);
}
static void capture(void) {
  memset(gFrameBuffer,0,sizeof(gFrameBuffer));
  render(); PrintSmall(3,5,title);
  assert(panelCount<ARRAY_SIZE(panels));
  memcpy(panels[panelCount++],gFrameBuffer,sizeof(gFrameBuffer));
}
static void choose(KEY_Code_t index) { press(index); press(KEY_MENU); }
int main(int argc, char **argv) {
  RADIO_STATE_Default(&gDeviceConfig.radio);
  capabilities=(ReceiverCapabilities){.si=true,.ssb_patch=true,.bk_am=true,.bk_bandwidth_mask=RX_BK_BANDWIDTH_ALL};
  enter(); capture();
  choose(KEY_1); capture(); // Display directly contains contrast and all light settings.
  assert(active_menu==&displayMenu && menu_stack_top==1 && active_menu->num_items==5);
  press(KEY_EXIT);
  choose(KEY_2); capture(); // Receive
  press(KEY_2);press(KEY_STAR);capture(); // SQL delay is editable directly within Receive.
  assert(active_menu==&receiveMenu && menu_stack_top==1);
  press(KEY_4);capture(); // SI hardware option also stays in Receive.
  unsigned before=requests;
  capabilities.si=false;
  press(KEY_STAR); assert(requests==before && !strcmp(notice,"No SI4732"));
  capabilities.si=true;
  press(KEY_EXIT);
  choose(KEY_3); capture(); // Transmit
  press(KEY_EXIT); choose(KEY_5); capture(); // Battery
  choose(KEY_3);
  assert(active_menu==&batteryMenu && number.active && menu_stack_top==1 && !appRuns);
  assert(!strcmp(number.text,"8.24"));
  press(KEY_7);press(KEY_STAR);press(KEY_8);press(KEY_6);
  press(KEY_PTT);assert(number.active && !saves);
  press(KEY_MENU);assert(!number.active && saves==1 && calibratedVoltage==78600);
  assert(active_menu==&batteryMenu && menu_stack_top==1 && !appRuns);
  assert(gDeviceConfig.preferences.battery_calibration==2100);
  calibrationResult=2200;
  press(KEY_MENU);press(KEY_MENU);
  assert(saves==1 && gDeviceConfig.preferences.battery_calibration==2100);
  assert(!strcmp(notice,"Invalid value"));
  calibrationResult=2000; saveWorks=false;
  press(KEY_MENU);press(KEY_MENU);
  assert(saves==1 && gDeviceConfig.preferences.battery_calibration==2100);
  assert(!strcmp(notice,"Save failed")); saveWorks=true;
  press(KEY_MENU);press(KEY_4);press(KEY_MENU);assert(number.active && number.invalid && saves==1);
  assert(key(KEY_EXIT,KEY_LONG_PRESSED));assert(!number.active && saves==1);
  press(KEY_EXIT); choose(KEY_7); // Channels opens the channel list.
  assert(lastScreen==&channelsScreen && active_menu==&settingsMenu);
  choose(KEY_8); capture(); // About is last and also holds the former System items.
  assert(active_menu==&aboutMenu && menu_stack_top==1 && active_menu->num_items==9);
  assert(menuItems[ARRAY_SIZE(menuItems)-1].submenu==&aboutMenu);
  choose(KEY_9); capture(); // Reset confirmation
  press(KEY_EXIT); assert(!resetConfirm && saves==1);
  choose(KEY_9);press(KEY_MENU);assert(saves==2 && !resetConfirm);
  assert(gDeviceConfig.preferences.battery_calibration==2100);
  // A failed reapplication restores the prior engineering preferences.
  gDeviceConfig.preferences.sql_open_time=3;
  applyWorks=false;leave();
  assert(gDeviceConfig.preferences.sql_open_time==oldOpen && reapplications==2);
  MENU_Deinit();assert(!number.active);
  if(argc==2) {
    FILE *file=fopen(argv[1],"wb");assert(file);
    fprintf(file,"P5\n640 128\n255\n");
    for(unsigned y=0;y<128;++y) for(unsigned x=0;x<640;++x) {
      unsigned panel=(y/64)*5+x/128;
      unsigned px=x%128,py=y%64;
      bool ink=panels[panel][py/8][px]&(1u<<(py%8));
      fputc(ink ? 20 : 232,file);
    }
    assert(!fclose(file));
  }
  return 0;
}

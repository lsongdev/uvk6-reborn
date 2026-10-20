// Host-only adapters: render the production menus with deterministic data.
// No radio or storage hardware is accessed.
#include "ui/screens/settings.h"
#include "ui/screens/slot-menu.h"
#include "ui/screens/statusline.h"
#include "ui/screens/bands.h"
#include "ui/screens/channels.h"
#include "ui/screens/spectrum.h"
#include "ui/screens/tone-scan.h"
#include "ui/screens/save-channel.h"
#include "ui/graphics.h"
#include "radio/radio.h"
#include "radio/receiver.h"
#include "store/device-config.h"
#include "hal/storage.h"
#include "devices/tx-device.h"
#include "devices/battery.h"
#include "devices/backlight.h"
#include "devices/display.h"
#include "../tests/rf-ranges.h"
#include <stddef.h>

DeviceConfig gDeviceConfig;
const Screen bandsScreen = {0}, channelsScreen = {0};
const Screen spectrumScreen = {0}, toneScanScreen = {0};

void STATUSLINE_SetText(const char *text) {
  FillRect(0, 0, 128, 7, C_CLEAR);
  UI_SmallTextWindow(0, 4, 110, text, 0);
  DrawHLine(0, 6, 128, C_FILL);
  DrawRect(115, 0, 12, 5, C_FILL);
}
const RadioSlot *RADIO_Selected(void) { return &gDeviceConfig.radio.slots[0]; }
uint8_t RADIO_SelectedId(void) { return 0; }
const char *RADIO_Notice(void) { return NULL; }
void RADIO_AdjustRx(RadioParameterField field, bool up) { (void)field; (void)up; }
void RADIO_ResetToPreset(void) {}
bool RADIO_SetTx(const TxConfig *tx) { (void)tx; return false; }
bool RADIO_CycleTxPower(bool up) { (void)up; return false; }
void RADIO_Notify(const char *text) { (void)text; }
void RADIO_SlotsChanged(void) {}
bool RADIO_ApplyRxSettings(void) { return true; }
void SCREEN_Push(const Screen *screen) { (void)screen; }
bool SCREEN_Pop(void) { return true; }
void SAVE_CHANNEL_Open(void) {}
void DEVICE_RequestSave(void) {}
bool DEVICE_SaveNow(void) { return true; }
DeviceSaveStatus DEVICE_GetSaveStatus(void) { return DEVICE_SAVED; }
bool DEVICE_SavePreferences(const ConfigPreferences *preferences) { (void)preferences; return false; }
uint32_t STORAGE_Capacity(void) { return 512u * 1024u; }
ReceiverCapabilities RECEIVER_Capabilities(void) {
  return (ReceiverCapabilities){.bk_ranges=testBk4819Ranges, .si=true, .ssb_patch=true};
}
TxCapabilities TX_DEVICE_Capabilities(uint32_t hz) {
  (void)hz;
  return (TxCapabilities){.available=true, .ranges=testBk4819Ranges, .power_mask=7};
}
uint32_t BATTERY_GetPreciseVoltage(uint16_t cal) { (void)cal; return 76000; }
uint16_t BATTERY_GetCal(uint32_t voltage) { (void)voltage; return 2000; }
void BACKLIGHT_ApplySettings(void) {}
void DISPLAY_Init(bool full, uint8_t contrast) { (void)full; (void)contrast; }

static void settingsPage(unsigned page, const char *filename, void (*snapshot)(const char *)) {
  FillRect(0, 0, 128, 64, C_CLEAR);
  settingsScreen.enter();
  settingsScreen.key(KEY_1, KEY_RELEASED);
  for (unsigned i=0; i<page; ++i) settingsScreen.key(KEY_DOWN, KEY_RELEASED);
  if (page) settingsScreen.key(KEY_MENU, KEY_RELEASED);
  settingsScreen.render();
  snapshot(filename);
  settingsScreen.leave();
}

void UI_PreviewMenus(void (*snapshot)(const char *)) {
  gDeviceConfig = (DeviceConfig){.preferences=CONFIG_DEFAULT_PREFERENCES};
  gDeviceConfig.radio.slot_count=2;
  gDeviceConfig.radio.slots[0]=(RadioSlot){
      .current={.frequency_hz=438500000u,
                .parameters={RX_NFM,12500u,12500u,4,0}},
      .tx={.power=TX_POWER_MEDIUM}};
  FillRect(0, 0, 128, 64, C_CLEAR);
  slotMenuScreen.render(); snapshot("manual-slot-rx.pbm");
  // Real input scrolls the production slot menu down to the TX entries.
  for (unsigned i=0; i<MENU_TX_TAIL; ++i) slotMenuScreen.key(KEY_DOWN, KEY_RELEASED);
  FillRect(0, 0, 128, 64, C_CLEAR);
  slotMenuScreen.render(); snapshot("manual-slot-tx.pbm");
  settingsPage(0,"manual-settings.pbm",snapshot);
  settingsPage(1,"manual-receive.pbm",snapshot);
  settingsPage(2,"manual-transmit.pbm",snapshot);
  // Enter Display directly from the first root entry.
  settingsScreen.enter();
  settingsScreen.key(KEY_1,KEY_RELEASED);
  settingsScreen.key(KEY_MENU,KEY_RELEASED);
  FillRect(0,0,128,64,C_CLEAR);
  settingsScreen.render(); snapshot("manual-display.pbm");
  settingsScreen.leave();
}

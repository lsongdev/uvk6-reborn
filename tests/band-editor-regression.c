// Exercise draft cancellation, isolated persistence and capability filtering.
#include "../src/apps/bands.c"
#include <assert.h>

DeviceConfig gDeviceConfig;
bool gDeviceConfigReady = true;
static bool saveWorks = true;
static unsigned saves, exits;
static ReceiverCapabilities capabilities = {true, false, true};
bool DEVICE_SaveNow(void) { ++saves; return saveWorks; }
ReceiverCapabilities RADIO_GetReceiverCapabilities(void) { return capabilities; }
bool APPS_exit(void) { ++exits; return true; }
static void press(KEY_Code_t key) { assert(BANDS_key(key, KEY_RELEASED)); }

int main(void) {
  CONFIG_Default(&gDeviceConfig);
  BANDS_init();
  assert(selected == 5 && !editing);
  press(KEY_DOWN); // 70cm.
  press(KEY_MENU);
  assert(editing && draft.mode == RX_NFM);
  press(KEY_STAR); // AM in draft only.
  assert(draft.mode == RX_AM && gDeviceConfig.radio.presets[6].mode == RX_NFM);
  press(KEY_EXIT);
  assert(!editing && saves == 0);
  press(KEY_MENU);
  press(KEY_STAR);
  field = FIELD_SAVE;
  press(KEY_MENU);
  assert(!editing && saves == 1 && gDeviceConfig.radio.presets[6].mode == RX_AM);
  assert(gDeviceConfig.radio.current.parameters.mode == RX_NFM);
  assert(gDeviceConfig.radio.bands[6].parameters.mode == RX_NFM);
  assert(gDeviceConfig.radio.presets[5].mode == RX_NFM);

  press(KEY_MENU);
  field = FIELD_DEFAULTS;
  press(KEY_MENU);
  assert(draft.mode == RX_NFM && gDeviceConfig.radio.presets[6].mode == RX_AM);
  saveWorks = false;
  field = FIELD_SAVE;
  press(KEY_MENU);
  assert(editing && gDeviceConfig.radio.presets[6].mode == RX_AM);
  assert(message && saves == 2);
  saveWorks = true;
  press(KEY_MENU);
  assert(!editing && gDeviceConfig.radio.presets[6].mode == RX_NFM);

  selected = 2; // SW: SSB requires a patch.
  press(KEY_MENU);
  capabilities.ssb_patch = false;
  press(KEY_STAR);
  assert(draft.mode == RX_AM);
  capabilities.ssb_patch = true;
  press(KEY_STAR);
  assert(draft.mode == RX_LSB && draft.bandwidth_hz == 3000);
  press(KEY_EXIT); // Discard SSB draft.
  assert(gDeviceConfig.radio.presets[2].mode == RX_AM);

  selected = 3; // WFM has fixed bandwidth.
  press(KEY_MENU);
  field = RADIO_FIELD_BANDWIDTH;
  press(KEY_STAR);
  assert(draft.bandwidth_hz == 0 && message);
  field = FIELD_SAVE;
  capabilities = (ReceiverCapabilities){0};
  unsigned before = saves;
  press(KEY_MENU);
  assert(editing && saves == before); // Unavailable receiver cannot be saved.
  press(KEY_EXIT);

  selected = 5;
  press(KEY_MENU);
  field = FIELD_CURRENT;
  gDeviceConfig.radio.current.parameters.squelch = 7;
  press(KEY_MENU);
  assert(draft.squelch == 7 && gDeviceConfig.radio.presets[5].squelch == 4);
  press(KEY_SIDE1);
  press(KEY_SIDE2);
  press(KEY_PTT);
  assert(selected == 5 && draft.squelch == 7);
  press(KEY_EXIT);
  press(KEY_EXIT);
  assert(exits == 1);
  return 0;
}

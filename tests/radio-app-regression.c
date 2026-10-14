#include "core/device-config.h"
// Exercise the real radio controller and its screens; only hardware and
// drawing are mocked. Unity build keeps private state reachable.
#include "../src/radio/radio.c"
#include "../src/radio/seek.c"
#include "../src/radio/capture.c"
#include "../src/radio/watch.c"
#include "../src/radio/tx.c"
#include "../src/radio/spectrum.c"
#include "../src/screens/screen.c"
#include "../src/screens/home.c"
#include "../src/screens/slot-menu.c"
#include "../src/screens/save-channel.c"
#include "../src/screens/spectrum.c"
#include <assert.h>

Receiver gReceiver;
bool gRedrawScreen;
static char statusText[32];
void STATUSLINE_SetText(const char *text) { TEXT_Copy(statusText, sizeof(statusText), text); }
static bool shortcutShown;
void STATUSLINE_SetShortcut(bool pending) { shortcutShown = pending; }
static UI_RadioCardView drawn[RADIO_SLOT_CAPACITY];
static unsigned drawCount;
void UI_RadioCardRender(uint8_t y, const UI_RadioCardView *view) {
  unsigned id = view->id-'A';
  assert(id<RADIO_SLOT_CAPACITY && y==8+(id-RADIO_STATE_VisibleFirst(&gDeviceConfig.radio))*UI_RADIO_CARD_HEIGHT);
  drawn[id]=*view; ++drawCount;
}
uint8_t RX_DEVICE_SignalLevel(const RxPlan *plan, uint16_t rssi) { (void)plan; (void)rssi; return 8; }
void PrintSmall(uint8_t x, uint8_t y, const char *pattern) {
  (void)x; (void)y; (void)pattern;
}
void PrintSmallEx(uint8_t x, uint8_t y, TextPos pos, Color c, const char *pattern) {
  (void)x; (void)y; (void)pos; (void)c; (void)pattern;
}
void PrintMedium(uint8_t x, uint8_t y, const char *pattern) {
  (void)x; (void)y; (void)pattern;
}
void PrintMediumEx(uint8_t x, uint8_t y, TextPos pos, Color c, const char *pattern) {
  (void)x; (void)y; (void)pos; (void)c; (void)pattern;
}
static ReceiverCapabilities capabilities = {true, false, true};
static unsigned applied, applyAttempts;
static unsigned routed;
static unsigned appRuns;
static bool failApply;
static bool txFakeVerified, txFakePrepareOK, txFakeEnableOK;
static bool txFakePttPressed=true, txFakeVoltageValid=true;
static unsigned txFakePrepares, txFakeEnables, txFakeDisables;
// Host capabilities and hardware calls are isolated from the production HAL.
TxCapabilities TX_DEVICE_Capabilities(uint32_t frequency_hz) {
  (void)frequency_hz;
  return (TxCapabilities){txFakeVerified, txFakeVerified ? 1u : 0u};
}
TxPolicy TX_DEVICE_Policy(void) {
  return (TxPolicy){txFakeVerified, 1, {{430000000u, 440000000u}}};
}
bool TX_DEVICE_PttPressed(void) { return txFakePttPressed; }
bool TX_DEVICE_VoltageValid(void) { return txFakeVoltageValid; }
void TX_DEVICE_Disable(void) { ++txFakeDisables; }
static uint32_t txFakeHz;
bool TX_DEVICE_Prepare(const TxPlan *plan) {
  ++txFakePrepares;
  // Every scenario transmits in 438.5/438.6 at low power; talkback picks which.
  txFakeHz = plan->frequency_hz;
  assert((txFakeHz == 438500000u || txFakeHz == 438600000u) && plan->power == TX_POWER_LOW);
  return txFakePrepareOK;
}
bool TX_DEVICE_Enable(void) { ++txFakeEnables; return txFakeEnableOK; }
DeviceConfig gDeviceConfig;
bool gDeviceConfigReady = true;
static unsigned saveRequests;
static uint32_t clockNow=100;
static unsigned rdsReads, rdsClears, rdsIndex;
static bool rdsAvailable;
static uint32_t soughtFrequency;
static bool nativeSeekBusy;
static RxSeekResult nativeReply;
static unsigned nativeStarts, nativeCancels;
static uint32_t nativeFrequency;
static unsigned captureStarts, captureStops;
static CaptureSample captureReply;
static uint32_t captureHz;
static CaptureRange captureRange;
static RxDevice captureLeaves;
void RX_DEVICE_CaptureStart(CaptureRange range, RxDevice previous) {
  captureLeaves=previous; captureRange=range; ++captureStarts;
}
CaptureSample RX_DEVICE_CapturePoll(CaptureRange range, uint32_t *hz) {
  assert(range==captureRange); *hz=captureHz; return captureReply;
}
void RX_DEVICE_CaptureStop(void) { ++captureStops; }
static unsigned spectrumStarts, spectrumStops, spectrumTunes;
static uint32_t spectrumTuned;
static uint16_t spectrumRssi = 100;
void RX_DEVICE_SpectrumStart(RxDevice previous) { (void)previous; ++spectrumStarts; }
void RX_DEVICE_SpectrumTune(uint32_t hz) { spectrumTuned = hz; ++spectrumTunes; }
uint16_t RX_DEVICE_SpectrumRssi(void) { return spectrumRssi; }
void RX_DEVICE_SpectrumStop(void) { ++spectrumStops; }
static unsigned graphs;
void UI_SpectrumRender(uint8_t y, uint8_t height, const Spectrum *s) {
  assert(y == 8 + UI_RADIO_CARD_HEIGHT && y + height == LCD_HEIGHT && s); ++graphs;
}
bool RX_DEVICE_SeekNative(const RxPlan *plan) {
  return plan->device == RX_DEVICE_SI &&
         (plan->parameters.mode == RX_WFM || plan->parameters.mode == RX_AM);
}
bool RX_DEVICE_SeekStart(const RxPlan *plan, uint32_t low, uint32_t high) {
  (void)plan; (void)low; (void)high;
  ++nativeStarts; nativeSeekBusy = true; return true;
}
RxSeekResult RX_DEVICE_SeekPoll(RxDevice dev, uint32_t *found) {
  (void)dev;
  if (nativeSeekBusy) { nativeSeekBusy = false; return RX_SEEK_BUSY; }
  *found = nativeFrequency;
  return nativeReply;
}
void RX_DEVICE_SeekCancel(RxDevice dev) { (void)dev; ++nativeCancels; }
bool RX_DEVICE_SeekSignal(RxDevice dev, bool *found) {
  (void)dev; *found = gReceiver.plan.frequency_hz == soughtFrequency; return true;
}
bool RX_DEVICE_ReadRDS(RdsPacket *packet, bool clear) {
  ++rdsReads;
  *packet=(RdsPacket){0};
  if (clear) { ++rdsClears; return true; }
  // Eight PS groups ("AAAAAAAA"), then RadioText "Hi RT" as two 2A
  // segments, each sent twice for confirmation.
  static const uint16_t rt[2][2]={{0x4869,0x2052},{0x540d,0x2020}};
  if (rdsAvailable && rdsIndex<12) {
    packet->synchronized=true;
    packet->fifo_used=12-rdsIndex;
    packet->group=rdsIndex<8 ? (RdsGroup){.blocks={0x40a7,rdsIndex%4,0,0x4141}} :
        (RdsGroup){.blocks={0x40a7,0x2000|(rdsIndex%2),rt[rdsIndex%2][0],rt[rdsIndex%2][1]}};
    ++rdsIndex;
  }
  return true;
}
void DISPLAY_Blit(void) {}
void FillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color) {
  (void)x; (void)y; (void)w; (void)h; (void)color;
}
void DEVICE_RequestSave(void) { ++saveRequests; }
static uint32_t failFrequency;

uint32_t Now(void) { return clockNow; }
ReceiverCapabilities RECEIVER_Capabilities(void) { return capabilities; }
bool RECEIVER_Apply(const RxPlan *plan) {
  ++applyAttempts;
  if (failApply || (failFrequency && plan->frequency_hz == failFrequency))
    return false;
  ++applied;
  gReceiver.plan = *plan;
  return true;
}
static unsigned concurrentPrepares, audioSelects, chipStops;
bool RX_DEVICE_ApplyConcurrent(const RxPlan *plan) {
  ++concurrentPrepares;
  return !failApply && (!failFrequency || plan->frequency_hz!=failFrequency);
}
void RX_DEVICE_StopChip(RxDevice device) { (void)device;++chipStops; }
bool RECEIVER_Select(const RxPlan *plan, uint8_t devices, bool open) {
  assert(devices & (1u<<plan->device));++audioSelects;
  gReceiver.plan=*plan;
  gReceiver.open=open;
  return true;
}
bool RECEIVER_Measure(void) { return gReceiver.open; }
static uint32_t watchSignalFrequency;
static bool watchSignalOK=true;
static uint32_t watchSecondSignalFrequency;
bool RX_DEVICE_WatchSignal(const RxPlan *plan, bool *found) {
  *found=plan->frequency_hz==watchSignalFrequency || plan->frequency_hz==watchSecondSignalFrequency; return watchSignalOK;
}
void RECEIVER_SetOpen(bool open) { gReceiver.open = open; }
void RECEIVER_Mute(void) {
  gReceiver.open = false;
  ++routed;
}
// Text input is a separate screen; record what it was asked to edit.
static char *gTextinputText;
static uint8_t gTextInputSize;
static void (*gTextInputCallback)(void);
void TEXT_INPUT_Open(char *text, uint8_t max, void (*onAccept)(void), bool replaceTop) {
  gTextinputText = text; gTextInputSize = max; gTextInputCallback = onAccept; ++appRuns;
  if (replaceTop) SCREEN_Pop();
}
// Any other screen on top of home (settings, text input, ...).
static const Screen otherScreen = {0};
static UserChannel fakeChannels[USER_CHANNEL_COUNT + 1];
static bool fakeChannelUsed[USER_CHANNEL_COUNT + 1];
bool CHANNEL_STORE_Init(void) { return true; }
bool CHANNEL_STORE_Available(void) { return true; }
bool CHANNEL_STORE_IsOccupied(uint16_t number) {
  return number > 0 && number <= USER_CHANNEL_COUNT && fakeChannelUsed[number];
}
bool CHANNEL_STORE_Load(uint16_t number, UserChannel *channel) {
  if (!CHANNEL_STORE_IsOccupied(number) || !channel) return false;
  *channel = fakeChannels[number]; return true;
}
bool CHANNEL_STORE_Save(uint16_t number, const UserChannel *channel) {
  if (!number || number > USER_CHANNEL_COUNT || !channel) return false;
  fakeChannels[number] = *channel; fakeChannelUsed[number] = true; return true;
}
uint16_t CHANNEL_STORE_FirstEmpty(void) {
  for (uint16_t number=1; number<=USER_CHANNEL_COUNT; ++number)
    if (!fakeChannelUsed[number]) return number;
  return 0;
}
bool CHANNEL_STORE_Next(uint16_t number, bool up, uint16_t *next) {
  if (!number || number>USER_CHANNEL_COUNT || !next) return false;
  for (uint16_t offset=1; offset<=USER_CHANNEL_COUNT; ++offset) {
    uint16_t candidate=up ? (number-1+offset)%USER_CHANNEL_COUNT+1
                          : (number-1+USER_CHANNEL_COUNT-offset)%USER_CHANNEL_COUNT+1;
    if (fakeChannelUsed[candidate]) { *next=candidate; return true; }
  }
  return false;
}

static void setup(uint32_t frequency, RadioParameters values) {
  // Unwind the previous scenario's screens first: leaving home releases PTT
  // and cancels FC/Seek, which must not count against this scenario.
  SCREEN_Reset(&homeScreen);
  capturing=false;
  captureStarts=captureStops=0; captureReply=CAPTURE_BUSY;
  spectrumOn=false; spectrumStarts=spectrumStops=spectrumTunes=0; spectrumRssi=100;
  keypad = txInput = (NumberInput){0};
  clockNow=100;
  rdsReads=rdsClears=rdsIndex=0;
  rdsAvailable=false;
  watchSignalFrequency=watchSecondSignalFrequency=0;watchSignalOK=true;
  concurrentPrepares=audioSelects=chipStops=0;
  initialized = confirming = false;
  menuIndex = 0;
  WATCH_Stop(&watcher); watchDelay=false; watchResumeAt=0; watchStatus=NULL;
  functionPending = false;
  selectedSlot = 0;
  receivingSlot = UINT8_MAX;
  memset(labels, 0, sizeof(labels));
  transmitter = (TxRuntime){0};
  txFakePttPressed=txFakeVoltageValid=true;
  pttPending = false;
  seek = (Seek){0};
  runtime = (RxRuntime){0};
  message = NULL;
  RADIO_STATE_Default(&gDeviceConfig.radio);
  gDeviceConfig.radio.slots[0].tx = (TxConfig){0};
  gDeviceConfig.preferences.tx_timeout_seconds = 120;
  gDeviceConfig.preferences.tx_busy_lockout = 0;
  gDeviceConfig.preferences.ptt_lock = false;
  gDeviceConfig.preferences.keylock = false;
  txFakeVerified = false;
  txFakePrepareOK = txFakeEnableOK = true;
  txFakePrepares = txFakeEnables = txFakeDisables = 0;
  gReceiver.open = false;
  assert(RADIO_STATE_Set(&gDeviceConfig.radio, 0, frequency, values));
  gReceiver = (Receiver){.plan = {.device = RX_DEVICE_BK, .frequency_hz = frequency}};
  applied = routed = appRuns = saveRequests = applyAttempts = 0;
  failApply = false;
  failFrequency = 0;
  soughtFrequency = 0;
  nativeFrequency = 100100000;
  nativeReply = RX_SEEK_FOUND;
  nativeStarts = nativeCancels = 0;
  capabilities = (ReceiverCapabilities){true, false, true};
  RADIO_Init();
}
static void press(KEY_Code_t key) { assert(SCREEN_Key(key, KEY_RELEASED)); }
static void update(void) { RADIO_Update(); SCREEN_Update(); }
#define inMenu (SCREEN_Top()==&slotMenuScreen)
#define openMenu(index) (SCREEN_Push(&slotMenuScreen), menuIndex=(index))

int main(void) {
  // MR mode retains channel stepping and save/name operations only.
  RadioParameters testRx = {RX_NFM,12500,12000,4};
  setup(145500000, testRx);
  fakeChannels[12] = (UserChannel){.name="TEST",.frequency_hz=438500000,.rx=testRx};
  fakeChannelUsed[12] = true;
  press(KEY_F);press(KEY_3);assert(activeSlot.source==RADIO_SOURCE_CHANNEL);
  assert(activeSlot.current.frequency_hz==438500000);
  press(KEY_UP);assert(activeSlot.current.frequency_hz==438500000); // One occupied channel wraps to itself.
  // Save as channel: the current channel is offered first, overwriting is
  // confirmed, then the name (prefilled with the old one) is asked for.
  openMenu(MENU_SAVE_CHANNEL);press(KEY_MENU);assert((SCREEN_Top()==&saveChannelScreen));
  press(KEY_MENU);assert((SCREEN_Top()==&saveChannelScreen)); // First press confirms overwrite.
  unsigned runsBeforeName=appRuns;
  press(KEY_MENU);assert(!(SCREEN_Top()==&saveChannelScreen) && appRuns==runsBeforeName+1);
  assert(!strcmp(gTextinputText,"TEST") && gTextInputSize==USER_CHANNEL_NAME_MAX);
  assert(!strcmp(fakeChannels[12].name,"TEST")); // Nothing saved before the name.
  strcpy(gTextinputText,"NAMED");gTextInputCallback();
  assert(!strcmp(fakeChannels[12].name,"NAMED") && !strcmp(message,"Saved"));
  assert(gDeviceConfig.radio.slots[0].current.frequency_hz==145500000);
  SCREEN_Reset(&homeScreen);
  // Both F-prefix and side-key controls must refuse tuning in MR mode.
  unsigned mrApplies = applied, mrSaves = saveRequests;
  uint32_t mrFrequency = activeSlot.current.frequency_hz;
  press(KEY_F); press(KEY_1);
  assert(!strcmp(message,"VFO only") && !functionPending);
  press(KEY_F); press(KEY_0);
  assert(!strcmp(message,"VFO only") && !functionPending);
  press(KEY_SIDE1); assert(!strcmp(message,"VFO only"));
  press(KEY_SIDE2); assert(!strcmp(message,"VFO only"));
  assert(applied==mrApplies && saveRequests==mrSaves &&
         activeSlot.current.frequency_hz==mrFrequency);
  // Digits in MR enter a channel number in the body, like a frequency.
  fakeChannels[150] = (UserChannel){.name="FAR",.frequency_hz=145500000,.rx=testRx};
  fakeChannelUsed[150] = true;
  press(KEY_1);press(KEY_5);assert(keypad.active && keypadChannel);
  SCREEN_Render();
  press(KEY_0);press(KEY_MENU);
  assert(!keypad.active && gDeviceConfig.radio.slots[0].channel_number==150);
  assert(activeSlot.current.frequency_hz==145500000 && activeSlot.source==RADIO_SOURCE_CHANNEL);
  press(KEY_7);press(KEY_MENU);
  assert(!strcmp(message,"Channel unavailable") && gDeviceConfig.radio.slots[0].channel_number==150);
  press(KEY_0);press(KEY_MENU);assert(keypad.active && keypad.invalid); // Below 1.
  press(KEY_EXIT);press(KEY_EXIT);assert(!keypad.active);
  press(KEY_1);press(KEY_2);press(KEY_MENU);
  assert(gDeviceConfig.radio.slots[0].channel_number==12 && activeSlot.current.frequency_hz==mrFrequency);
  fakeChannelUsed[150] = false;
  press(KEY_F);press(KEY_3);assert(activeSlot.source==RADIO_SOURCE_VFO);
  memset(fakeChannelUsed,0,sizeof(fakeChannelUsed));
  // A broadcast's RDS station name is the suggested name for a new channel.
  setup(95900000,(RadioParameters){RX_WFM,100000,0,0,0});
  labels[0].frequency_hz=95900000;memcpy(labels[0].ps,"LOVE959 ",9);
  openMenu(MENU_SAVE_CHANNEL);press(KEY_MENU);assert((SCREEN_Top()==&saveChannelScreen));
  press(KEY_MENU);assert(!strcmp(gTextinputText,"LOVE959")); // Trailing PS padding trimmed.
  gTextInputCallback();
  assert(fakeChannelUsed[1] && !strcmp(fakeChannels[1].name,"LOVE959"));
  memset(fakeChannelUsed,0,sizeof(fakeChannelUsed));SCREEN_Reset(&homeScreen);
  // Without a PS the frequency is offered; an emptied name falls back to it.
  labels[0].ps[0]=0;
  openMenu(MENU_SAVE_CHANNEL);press(KEY_MENU);press(KEY_MENU);
  assert(!strcmp(gTextinputText,"95.900MHz"));
  gTextinputText[0]=0;gTextInputCallback();
  assert(fakeChannelUsed[1] && !strcmp(fakeChannels[1].name,"95.900MHz"));
  memset(fakeChannelUsed,0,sizeof(fakeChannelUsed));SCREEN_Reset(&homeScreen);
  // A channel deleted in Settings → Channels sends its slot back to VFO,
  // and returning to the main screen retunes it.
  setup(145500000, testRx);
  fakeChannels[12] = (UserChannel){.name="TEST",.frequency_hz=438500000,.rx=testRx};
  fakeChannelUsed[12] = true;
  press(KEY_F);press(KEY_3);assert(activeSlot.source==RADIO_SOURCE_CHANNEL);
  SCREEN_Push(&otherScreen);
  fakeChannelUsed[12] = false;RADIO_ChannelsChanged();
  assert(gDeviceConfig.radio.slots[0].source==RADIO_SOURCE_VFO && !gDeviceConfig.radio.slots[0].channel_number);
  SCREEN_Pop();
  assert(activeSlot.source==RADIO_SOURCE_VFO && runtime.current.frequency_hz==145500000);
  memset(fakeChannelUsed,0,sizeof(fakeChannelUsed));
  RadioParameters fm = {RX_NFM, 12500, 12000, 4};
  setup(438500000, fm);
  press(KEY_F);press(KEY_0);
  assert(!functionPending && !confirming && appRuns == 0 && applied == 2);
  assert(activeSlot.current.frequency_hz == 438500000 && activeSlot.current.parameters.mode == RX_AM);
  press(KEY_F);press(KEY_0);
  assert(activeSlot.current.parameters.mode == RX_NFM);
  press(KEY_0);assert(keypad.active && appRuns == 0); // Plain 0 still enters frequency input.

  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  press(KEY_F);press(KEY_0);
  assert(!confirming && activeSlot.current.parameters.mode == RX_AM && gReceiver.plan.device == RX_DEVICE_BK);
  assert(activeSlot.current.frequency_hz == 100000000);

  setup(7100000, (RadioParameters){RX_AM,5000,6000,0});
  capabilities.ssb_patch = false;
  press(KEY_F);press(KEY_0);
  assert(!functionPending && activeSlot.current.parameters.mode == RX_AM);
  assert(!strcmp(message,"No option") && appRuns == 0);

  setup(438500000, fm);
  assert(SCREEN_Key(KEY_F, KEY_PRESSED) == false);
  press(KEY_F);
  assert(functionPending && activeSlot.active_band == 6);
  assert(SCREEN_Key(KEY_1, KEY_PRESSED));
  assert(applied == 1); // Execute on release, once.
  press(KEY_1);
  assert(!functionPending && activeSlot.active_band == 0 && appRuns == 0);
  assert(activeSlot.current.frequency_hz == 225000 && activeSlot.current.parameters.mode == RX_AM);
  press(KEY_1);
  assert(keypad.active && appRuns == 0); // The prefix is consumed; plain 1 enters frequency input.

  // F6 cycles only the selected VFO's TX intent, saves it and consumes the key.
  setup(438500000, fm);
  unsigned powerSaves = saveRequests, powerApplies = applied;
  TxConfig otherTx = gDeviceConfig.radio.slots[1].tx;
  press(KEY_F);
  assert(SCREEN_Key(KEY_6, KEY_PRESSED));
  assert(activeSlot.tx.power == TX_POWER_LOW && saveRequests == powerSaves);
  press(KEY_6);
  assert(activeSlot.tx.power == TX_POWER_MEDIUM && !functionPending && !keypad.active);
  assert(!strcmp(message, "TX Power: Medium"));
  press(KEY_F);press(KEY_6);
  assert(activeSlot.tx.power == TX_POWER_HIGH);
  press(KEY_F);press(KEY_6);
  assert(activeSlot.tx.power == TX_POWER_LOW && saveRequests == powerSaves + 3);
  assert(gDeviceConfig.radio.slots[0].tx.power == TX_POWER_LOW && !activeSlot.tx.enabled);
  assert(!memcmp(&otherTx, &gDeviceConfig.radio.slots[1].tx, sizeof(otherTx)));
  assert(applied == powerApplies && !txFakePrepares);
  activeSlot.source = gDeviceConfig.radio.slots[0].source = RADIO_SOURCE_CHANNEL;
  press(KEY_F);press(KEY_6);
  assert(activeSlot.tx.power == TX_POWER_LOW && saveRequests == powerSaves + 3);
  assert(!strcmp(message, "VFO only") && !keypad.active && !functionPending);
  // The menu shares the power cycle, not the notice: the value is on screen.
  activeSlot.source = gDeviceConfig.radio.slots[0].source = RADIO_SOURCE_VFO;
  openMenu(MENU_TX_POWER); message = NULL;
  press(KEY_STAR); assert(activeSlot.tx.power == TX_POWER_MEDIUM && !message);
  press(KEY_F); assert(activeSlot.tx.power == TX_POWER_LOW && !message);
  press(KEY_EXIT);

  setup(438500000, fm);
  press(KEY_F);press(KEY_EXIT);
  assert(!functionPending && applied == 1);
  press(KEY_F);press(KEY_F);
  assert(!functionPending && applied == 1);
  press(KEY_F);press(KEY_3);
  assert(!functionPending && !strcmp(message,"No saved channels") && appRuns == 0 && applied == 1);

  press(KEY_F);
  clockNow = functionAt+2999;update();assert(functionPending);
  clockNow++;update();assert(!functionPending);
  press(KEY_1);assert(keypad.active && appRuns == 0);
  SCREEN_Key(KEY_EXIT, KEY_LONG_PRESSED);
  press(KEY_F);clockNow += 3000; // Timeout is also checked before key dispatch.
  press(KEY_1);assert(!functionPending && keypad.active && appRuns == 0 && applied == 1);
  SCREEN_Key(KEY_EXIT, KEY_LONG_PRESSED);
  clockNow = UINT32_MAX-1000;
  press(KEY_F);clockNow += 2999;update();assert(functionPending);
  clockNow++;update();assert(!functionPending);

  setup(438500000, fm);
  press(KEY_F);assert(SCREEN_Key(KEY_PTT,KEY_PRESSED));assert(!functionPending);
  press(KEY_F);assert(!SCREEN_Key(KEY_MENU,KEY_LONG_PRESSED));
  assert(!functionPending && !inMenu);
  press(KEY_F);HOME_CancelShortcut();assert(!functionPending); // System lock transition.
  press(KEY_F);SCREEN_Push(&otherScreen);assert(!functionPending);
  SCREEN_Pop();assert(!functionPending); // Back on the main screen.
  press(KEY_MENU);assert(inMenu);
  press(KEY_F);assert(!functionPending && appRuns == 0); // Local menu adjustment.
  press(KEY_EXIT);press(KEY_SIDE1);assert(activeSlot.active_band == 0);

  setup(438500000, fm);
  gDeviceConfig.radio.presets[0] = (RadioParameters){RX_AM,5000,3000,2};
  press(KEY_F);press(KEY_1);
  assert(activeSlot.current.parameters.step_hz == 5000 && activeSlot.current.parameters.bandwidth_hz == 3000);
  assert(activeSlot.current.parameters.squelch == 2 && saveRequests == 2);

  setup(438500000, fm);
  assert(runtime.active && applied == 1 && activeSlot.active_band == 6);
  press(KEY_SIDE1);
  assert(activeSlot.current.frequency_hz == 225000 && activeSlot.current.parameters.mode == RX_AM && activeSlot.active_band == 0);
  press(KEY_SIDE1);
  assert(activeSlot.current.frequency_hz == 999000 && activeSlot.active_band == 1);

  // The same 100 MHz carrier must use BK for NFM and SI for WFM.
  setup(100000000, (RadioParameters){RX_NFM, 12500, 12000, 0});
  assert(runtime.active && gReceiver.plan.device == RX_DEVICE_BK);
  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0});
  assert(runtime.active && gReceiver.plan.device == RX_DEVICE_SI);
  update();assert(!rdsReads);
  clockNow=400;update();assert(rdsClears==1);
  rdsAvailable=true;
  clockNow=500;update();assert(rdsIndex==4 && !rds.decoder.ps[0]);
  gRedrawScreen=false;clockNow=600;update();
  assert(!strcmp(rds.decoder.ps,"AAAAAAAA") && gRedrawScreen);
  unsigned readsBefore=rdsReads;
  press(KEY_SIDE2);assert(!rds.decoder.ps[0]);
  clockNow=1000;update();assert(rdsReads==readsBefore); // NFM doesn't poll SI RDS.

  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0});
  strcpy(rds.decoder.ps,"OLD FM");
  failFrequency=100100000;press(KEY_UP);
  assert(runtime.active && activeSlot.current.frequency_hz==100000000 && !rds.decoder.ps[0]);

  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0});
  capabilities.si=false;capabilities.broadcast=true;request(100000000,activeSlot.current.parameters);
  assert(runtime.current.device==RX_DEVICE_BROADCAST);
  clockNow=500;update();assert(!rdsReads); // BK1080 has no RDS path.

  setup(438500000, fm);
  press(KEY_SIDE2);
  assert(runtime.active && !confirming && activeSlot.current.parameters.mode == RX_AM);
  assert(activeSlot.current.frequency_hz == 438500000);
  SCREEN_Render(); assert(drawn[0].changed); // Assert the visible preset-delta marker.
  press(KEY_SIDE2);
  assert(activeSlot.current.parameters.mode == RX_NFM && activeSlot.current.frequency_hz == 438500000);

  setup(7100000, (RadioParameters){RX_AM, 5000, 6000, 0});
  unsigned before = applied;
  press(KEY_SIDE2);
  assert(!confirming && activeSlot.current.parameters.mode == RX_LSB);
  assert(activeSlot.current.parameters.bandwidth_hz == 3000 && applied == before+1);
  assert(activeSlot.current.frequency_hz == 7100000);

  setup(7100000, (RadioParameters){RX_AM, 5000, 6000, 0});
  capabilities.ssb_patch = false;
  press(KEY_SIDE2);
  assert(!confirming && activeSlot.current.parameters.mode == RX_AM);

  setup(438500000, fm);
  RADIO_TuneTo(95925000);
  assert(confirming && pending.frequency_hz == 95930000);
  assert(activeSlot.current.frequency_hz == 438500000);
  press(KEY_EXIT);
  assert(activeSlot.current.frequency_hz == 438500000 && runtime.active);
  RADIO_TuneTo(95925000);
  press(KEY_MENU);
  assert(activeSlot.current.frequency_hz == 95930000 && activeSlot.current.parameters.mode == RX_WFM && gReceiver.plan.device == RX_DEVICE_SI);
  press(KEY_SIDE2);
  assert(!confirming && activeSlot.current.parameters.mode == RX_AM);
  assert(activeSlot.current.frequency_hz == 95930000 && gReceiver.plan.device == RX_DEVICE_BK);
  assert(activeSlot.current.parameters.bandwidth_hz == 6000);
  press(KEY_SIDE2);
  assert(!confirming && activeSlot.current.parameters.mode == RX_NFM);
  assert(activeSlot.current.frequency_hz == 95930000 && gReceiver.plan.device == RX_DEVICE_BK);

  // WFM to NFM also applies immediately when SSB modes are unavailable.
  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0});
  capabilities.ssb_patch = false;
  press(KEY_SIDE2);
  assert(!confirming && activeSlot.current.parameters.mode == RX_AM);
  press(KEY_SIDE2);
  assert(!confirming && activeSlot.current.parameters.mode == RX_NFM);
  assert(activeSlot.current.frequency_hz == 100000000 && gReceiver.plan.device == RX_DEVICE_BK);

  setup(147987500, fm);
  press(KEY_UP);
  assert(activeSlot.current.frequency_hz == 148000000 && activeSlot.active_band == 7 && activeSlot.current.parameters.step_hz == 25000);
  assert(gReceiver.plan.device == RX_DEVICE_BK);

  setup(438500000, fm);
  capabilities = (ReceiverCapabilities){false, false, false};
  press(KEY_SIDE1);
  assert(!runtime.active && activeSlot.current.frequency_hz == 225000 && routed == 1);
  assert(gReceiver.plan.frequency_hz==438500000u); // Unsupported target was not committed.
  press(KEY_UP);
  assert(activeSlot.current.frequency_hz == 226000 && !runtime.active);
  press(KEY_SIDE1); // MW has no receiver.
  press(KEY_SIDE1); // SW has no receiver.
  press(KEY_SIDE1); // Broadcast has no receiver.
  press(KEY_SIDE1); // Airband is available on BK.
  assert(runtime.active && activeSlot.current.frequency_hz == 125000000 && activeSlot.current.parameters.mode == RX_AM);

  setup(438500000, fm);
  press(KEY_MENU);
  assert(inMenu);
  press(KEY_SIDE1);
  press(KEY_SIDE2);
  assert(activeSlot.current.frequency_hz == 438500000 && applied == 1);
  press(KEY_EXIT);
  assert(!inMenu && appRuns == 0);
  // A saved preset is applied on band entry; live overrides never replace it.
  setup(438500000, fm);
  gDeviceConfig.radio.presets[6] = (RadioParameters){RX_AM,25000,6000,2};
  press(KEY_SIDE2); // Temporary AM, preserving the edited preset fields.
  assert(gDeviceConfig.radio.presets[6].step_hz == 25000);
  press(KEY_SIDE1); // Leave 70cm for LW.
  RADIO_TuneTo(438500000); // Enter 70cm again.
  assert(activeSlot.current.parameters.mode == RX_AM && activeSlot.current.parameters.step_hz == 25000);
  assert(activeSlot.current.parameters.squelch == 2);
  SCREEN_Render(); assert(!drawn[0].changed); // Re-entered preset is unmodified.
  press(KEY_SIDE2); // Temporary NFM override.
  assert(gDeviceConfig.radio.presets[6].mode == RX_AM);
  setup(438500000, fm);
  // A transient failed tune restores the last working receiver.
  failFrequency = 438512500;
  press(KEY_UP);
  assert(runtime.active && activeSlot.current.frequency_hz == 438500000 && applied == 2 && routed == 0);
  failFrequency = 0;
  failApply = true;
  press(KEY_UP);
  assert(activeSlot.current.frequency_hz == 438500000 && activeSlot.current.parameters.mode == RX_NFM && applied == 2);
  assert(!runtime.active && !gReceiver.open && routed == 1);

  // F,* starts forward-only BK software seek. Intermediate tunes are not
  // persisted, and a stable carrier commits exactly once.
  setup(438500000, fm);
  unsigned savedBeforeSeek = saveRequests;
  soughtFrequency = 438512500;
  press(KEY_F);press(KEY_STAR);
  assert(seek.active && !seekNative && !functionPending);
  assert(activeSlot.current.frequency_hz == 438500000);
  update();
  assert(seek.active && gReceiver.plan.frequency_hz==438512500u &&
         saveRequests == savedBeforeSeek);
  clockNow += 185;
  update(); // First squelch sample.
  assert(seek.active && saveRequests == savedBeforeSeek);
  clockNow += 85;
  update(); // Stable second sample.
  assert(!seek.active && activeSlot.current.frequency_hz == 438512500);
  assert(saveRequests == savedBeforeSeek + 1);

  // EXIT cancels without persisting and restores the exact original plan.
  setup(438500000, fm);
  savedBeforeSeek = saveRequests;
  press(KEY_F);press(KEY_STAR);
  update();
  press(KEY_EXIT);
  assert(!seek.active && runtime.active &&
         activeSlot.current.frequency_hz == 438500000 &&
         gReceiver.plan.frequency_hz==438500000u && saveRequests == savedBeforeSeek);
  assert(SCREEN_Key(KEY_UP, KEY_PRESSED) == false); // Manual tuning unchanged.

  // Hardware seek must not overwrite RadioUserState until the final tune.
  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  savedBeforeSeek = saveRequests;
  press(KEY_F);press(KEY_STAR);
  assert(seek.active && seekNative && nativeStarts == 0);
  update();
  assert(nativeStarts == 1 && saveRequests == savedBeforeSeek);
  clockNow += 100;update();
  clockNow += 100;update();
  assert(!seek.active && activeSlot.current.frequency_hz == 100100000);
  assert(saveRequests == savedBeforeSeek+1 && nativeCancels == 1);

  // No signal: reach hardware band end, wrap, reach the origin, stop.
  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  savedBeforeSeek = saveRequests;
  nativeReply = RX_SEEK_LIMIT;
  press(KEY_F);press(KEY_STAR);
  update();
  clockNow += 100;update();
  clockNow += 100;update();
  assert(seek.active && seek.wrapped && gReceiver.plan.frequency_hz==64000000u);
  assert(saveRequests == savedBeforeSeek);
  clockNow += 190;update(); // Check the band start once.
  clockNow += 10;update(); // Continue native seek from the band start.
  clockNow += 100;update();
  clockNow += 100;update();
  assert(!seek.active && activeSlot.current.frequency_hz == 100000000);
  assert(gReceiver.plan.frequency_hz==100000000u && saveRequests == savedBeforeSeek);

  // The single VFO owns TX intent, never a per-band transmit flag.
  setup(438500000, fm);
  assert(!gDeviceConfig.radio.slots[0].tx.enabled);
  assert(SCREEN_Key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX disabled"));
  SCREEN_Render();
  assert(drawn[0].header_message && !strcmp(drawn[0].header_message,"TX disabled"));
  assert(!drawn[1].header_message);
  press(KEY_MENU);
  for (unsigned i=0;i<MENU_TX_ENABLE;++i) press(KEY_DOWN);
  assert(menuIndex == MENU_TX_ENABLE);
  unsigned savedBeforeTX = saveRequests;
  press(KEY_MENU);
  assert(gDeviceConfig.radio.slots[0].tx.enabled && saveRequests == savedBeforeTX+1);
  press(KEY_DOWN);
  press(KEY_MENU);
  assert(gDeviceConfig.radio.slots[0].tx.power == TX_POWER_MEDIUM);
  assert(saveRequests == savedBeforeTX+2);
  press(KEY_F);
  assert(gDeviceConfig.radio.slots[0].tx.power == TX_POWER_LOW);
  press(KEY_EXIT);
  assert(!inMenu);
  assert(SCREEN_Key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX HW locked"));
  assert(transmitter.mode == RADIO_RX);
  RADIO_PttRelease();
  assert(transmitter.mode == RADIO_RX);
  // RX mode is still authoritative, even when a TX request is stored.
  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  gDeviceConfig.radio.slots[0].tx.enabled = activeSlot.tx.enabled = true;
  assert(SCREEN_Key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX: NFM only"));
  assert(transmitter.mode == RADIO_RX);
  RADIO_PttRelease();

  // Busy-channel lockout remains active even when Monitor is on.
  setup(438500000, fm);
  gDeviceConfig.radio.slots[0].tx.enabled = activeSlot.tx.enabled = true;
  gDeviceConfig.preferences.tx_busy_lockout = 1;
  txFakeVerified = true;
  gReceiver.open = false; // A stale/cleared audio flag cannot bypass lockout.
  watchSignalFrequency = 438500000;
  assert(SCREEN_Key(KEY_PTT, KEY_PRESSED));
  assert(pttPending && !txFakePrepares);
  clockNow += 180;
  RADIO_TxSafetyUpdate();
  assert(!strcmp(message, "Channel busy"));
  assert(txFakePrepares == 0 && transmitter.mode == RADIO_RX);
  RADIO_PttRelease();

  // Exercise the full one-VFO PTT lifecycle using a host-only fake PA:
  // no EEPROM mutation, timeout closes PA, RX restores, release re-arms.
  setup(438500000, fm);
  gDeviceConfig.radio.slots[0].tx.enabled = activeSlot.tx.enabled = true;
  gDeviceConfig.preferences.tx_timeout_seconds = 30;
  txFakeVerified = true;
  unsigned savesBeforePTT = saveRequests;
  unsigned appliedBeforePTT = applied;
  assert(SCREEN_Key(KEY_PTT, KEY_PRESSED));
  assert(transmitter.mode == RADIO_TX);
  assert(txFakePrepares == 1 && txFakeEnables == 1 &&
         txFakeDisables == 1 && saveRequests == savesBeforePTT);
  clockNow += 30000u;
  RADIO_TxSafetyUpdate();
  assert(transmitter.mode == RADIO_RX && transmitter.wait_release);
  assert(txFakeDisables == 2 && applied == appliedBeforePTT + 1);
  assert(saveRequests == savesBeforePTT && activeSlot.current.frequency_hz == 438500000);
  assert(SCREEN_Key(KEY_PTT, KEY_PRESSED));
  assert(txFakeEnables == 1); // TOT latch: no auto-rekey while held.
  RADIO_PttRelease();
  assert(!transmitter.wait_release);
  assert(SCREEN_Key(KEY_PTT, KEY_PRESSED) && transmitter.mode == RADIO_TX);
  RADIO_PttRelease();
  assert(transmitter.mode == RADIO_RX);

  // Prepare failure must never call enable, and requires a fresh PTT edge.
  setup(438500000, fm);
  gDeviceConfig.radio.slots[0].tx.enabled = activeSlot.tx.enabled = true;
  txFakeVerified = true;
  txFakePrepareOK = false;
  assert(SCREEN_Key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX error"));
  assert(transmitter.mode == RADIO_RX && transmitter.wait_release);
  assert(txFakePrepares == 1 && txFakeEnables == 0 && txFakeDisables == 2);
  RADIO_PttRelease();
  assert(!transmitter.wait_release);

  // Failed RX restoration must latch a permanent fault with PA disabled.
  setup(438500000, fm);
  gDeviceConfig.radio.slots[0].tx.enabled = activeSlot.tx.enabled = true;
  txFakeVerified = true;
  assert(SCREEN_Key(KEY_PTT, KEY_PRESSED) && transmitter.mode == RADIO_TX);
  failApply = true;
  RADIO_PttRelease();
  assert(transmitter.mode == RADIO_FAULT && txFakeDisables >= 2);
  assert(!runtime.active);
  assert(SCREEN_Key(KEY_PTT, KEY_PRESSED));
  assert(txFakeEnables == 1); // No retry from a faulted radio.

  // SSB does not gain an unreliable RSSI-only auto-search path.
  setup(7100000, (RadioParameters){RX_USB,5000,3000,0});
  press(KEY_F);press(KEY_STAR);
  assert(!seek.active && !strcmp(message,"SSB seek off"));
  // Tone controls replace Monitor without changing its runtime flag.
  setup(438500000,fm);menuIndex=0;press(KEY_MENU);
  for(unsigned i=0;i<MENU_RX_TONE;++i) press(KEY_DOWN);
  assert(menuIndex==MENU_RX_TONE);
  press(KEY_MENU);
  assert(activeSlot.current.parameters.rx_tone==1 && runtime.current.parameters.rx_tone==1);
  press(KEY_F);assert(activeSlot.current.parameters.rx_tone==0);
  for(unsigned i=MENU_RX_TONE;i<MENU_TX_SHIFT;++i) press(KEY_DOWN);
  assert(menuIndex==MENU_TX_SHIFT && activeSlot.tx.shift==TX_SHIFT_OFF);
  // TX Offset is inert while the shift is Off.
  press(KEY_DOWN);press(KEY_MENU);assert(!txInput.active && !strcmp(message,"TX Shift is Off"));
  press(KEY_UP);press(KEY_MENU);assert(activeSlot.tx.shift==TX_SHIFT_PLUS);
  unsigned appliesBefore=applied;
  press(KEY_DOWN);press(KEY_MENU);
  assert(appRuns==0 && txInput.active);
  press(KEY_0);press(KEY_STAR);press(KEY_6);press(KEY_MENU);
  assert(gDeviceConfig.radio.slots[0].tx.offset_hz==600000 && applied==appliesBefore);
  assert(TX_MODEL_Frequency(&activeSlot.tx,438500000)==439100000);
  // Split edits its own TX frequency; the offset is kept for +/-.
  press(KEY_UP);press(KEY_MENU);press(KEY_MENU);assert(activeSlot.tx.shift==TX_SHIFT_SPLIT);
  press(KEY_DOWN);press(KEY_MENU);
  press(KEY_4);press(KEY_3);press(KEY_8);press(KEY_STAR);press(KEY_6);press(KEY_MENU);
  assert(gDeviceConfig.radio.slots[0].tx.split_hz==438600000 && activeSlot.tx.offset_hz==600000);
  assert(activeSlot.current.frequency_hz==438500000);
  press(KEY_MENU);press(KEY_7);press(KEY_0);press(KEY_0);press(KEY_MENU);
  assert(txInput.active && txInput.invalid && gDeviceConfig.radio.slots[0].tx.split_hz==438600000);
  assert(SCREEN_Key(KEY_EXIT,KEY_LONG_PRESSED));
  assert(inMenu);
  press(KEY_DOWN);assert(menuIndex==MENU_TX_TONE);
  press(KEY_MENU);assert(gDeviceConfig.radio.slots[0].tx.tone==1);
  press(KEY_F);assert(gDeviceConfig.radio.slots[0].tx.tone==0);
  // Reset to defaults: the band preset comes back, frequency and TX stay.
  press(KEY_DOWN);assert(menuIndex==MENU_SPECTRUM);
  press(KEY_DOWN);assert(menuIndex==MENU_RESET);
  activeSlot.current.parameters.squelch=9;
  press(KEY_MENU);
  assert(activeSlot.current.parameters.squelch==gDeviceConfig.radio.presets[activeSlot.active_band].squelch);
  assert(activeSlot.current.frequency_hz==438500000 && activeSlot.tx.split_hz==438600000);
  unsigned savesBeforePlaceholder=saveRequests;
  appliesBefore=applied;
  press(KEY_DOWN);assert(menuIndex==MENU_SAVE_CHANNEL);
  press(KEY_MENU);assert((SCREEN_Top()==&saveChannelScreen));press(KEY_EXIT);assert(!(SCREEN_Top()==&saveChannelScreen));
  assert(saveRequests==savesBeforePlaceholder && applied==appliesBefore);
  assert(inMenu && !confirming);
  press(KEY_DOWN);assert(menuIndex==MENU_RX_MODE); // Menu cycles through RX/TX settings and channel actions.
  // Dual VFO: selection is a receive transaction, not an alias to one state.
  setup(145500000, fm);
  assert(selectedSlot==0 && receivingSlot==0);
  SCREEN_Render();
  assert(drawn[0].selected && !drawn[1].selected && !drawn[1].signal_valid);
  assert(drawn[1].frequency_hz==438500000 && drawn[1].activity==UI_RADIO_CARD_IDLE);
  RadioSlot originalA=gDeviceConfig.radio.slots[0];
  press(KEY_F); press(KEY_2);
  assert(selectedSlot==1 && receivingSlot==1 && !functionPending);
  assert(gDeviceConfig.radio.selected_slot==1 && activeSlot.current.frequency_hz==438500000);
  assert(!memcmp(&originalA,&gDeviceConfig.radio.slots[0],sizeof(originalA)));
  press(KEY_UP);
  assert(activeSlot.current.frequency_hz==438512500 && gDeviceConfig.radio.slots[0].current.frequency_hz==145500000);
  press(KEY_MENU);menuIndex=MENU_TX_TONE;press(KEY_MENU);press(KEY_EXIT);
  assert(gDeviceConfig.radio.slots[1].tx.tone==1 && !gDeviceConfig.radio.slots[0].tx.tone);
  gReceiver.open=true;
  SCREEN_Render();assert(drawn[1].selected && drawn[1].activity==UI_RADIO_CARD_RECEIVING);
  assert(!drawn[0].signal_valid && drawn[0].activity==UI_RADIO_CARD_IDLE);
  press(KEY_F);press(KEY_2);
  assert(selectedSlot==0 && activeSlot.current.frequency_hz==145500000);
  press(KEY_F);press(KEY_2);
  assert(selectedSlot==1 && activeSlot.current.frequency_hz==438512500);
  // Reboot restores B and its exact parameters, including temporary overrides.
  initialized=false;runtime=(RxRuntime){0};RADIO_Init();
  assert(selectedSlot==1 && receivingSlot==1 && activeSlot.current.frequency_hz==438512500);
  // Per-VFO memories and shared presets, even when both use the same band.
  request(145600000, (RadioParameters){RX_AM,25000,6000,2,0});
  gDeviceConfig.radio.presets[5].step_hz=50000;
  press(KEY_F);press(KEY_2);
  assert(activeSlot.current.parameters.mode==RX_NFM && activeSlot.current.parameters.step_hz==12500);
  press(KEY_F);press(KEY_2);
  assert(activeSlot.current.parameters.mode==RX_AM && activeSlot.current.parameters.step_hz==25000);
  assert(gDeviceConfig.radio.slots[0].bands[5].frequency_hz==145500000);
  // Apply failure restores A and does not commit B selection or request save.
  setup(145500000,fm);unsigned oldSaves=saveRequests;
  failFrequency=438500000;press(KEY_F);press(KEY_2);
  assert(selectedSlot==0 && receivingSlot==0 && runtime.active);
  assert(gDeviceConfig.radio.selected_slot==0 && saveRequests==oldSaves);
  assert(!strcmp(message,"Tune failed") && gReceiver.plan.frequency_hz==145500000u);
  SCREEN_Render();assert(!strcmp(statusText,"DW A")); // Two slots always watch.
  assert(!strcmp(drawn[0].header_message,"Tune failed") && !drawn[1].header_message);
  // Both applies fail: ownership is cleared and no row claims reception.
  failApply=true;press(KEY_F);press(KEY_2);
  assert(selectedSlot==0 && receivingSlot==UINT8_MAX && !runtime.active);
  SCREEN_Render();assert(!drawn[0].signal_valid && !drawn[1].signal_valid);
  assert(drawn[0].activity==UI_RADIO_CARD_UNAVAILABLE);
  setup(145500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,7100000,
      (RadioParameters){RX_LSB,1000,3000,0,0}));
  capabilities.ssb_patch=false;oldSaves=saveRequests;press(KEY_F);press(KEY_2);
  assert(selectedSlot==0 && runtime.current.frequency_hz==145500000 && saveRequests==oldSaves);
  assert(!strcmp(message,"SSB patch missing"));
  // RDS labels stay with their VFO/frequency, never the physical parser owner.
  setup(100000000,(RadioParameters){RX_WFM,100000,0,0});
  rdsAvailable=true;update();clockNow+=300;update();
  clockNow+=100;update();clockNow+=100;update();
  assert(labels[0].ps[0] && !labels[1].ps[0]);
  clockNow+=15001;update();assert(!labels[0].ps[0]);
  rdsAvailable=true;rdsIndex=0;clockNow+=100;update();clockNow+=100;update();
  assert(labels[0].ps[0]);
  press(KEY_F);press(KEY_2);SCREEN_Render();
  assert(drawn[0].name && drawn[0].name[0] && (!drawn[1].name || !drawn[1].name[0]));
  assert(!rds.decoder.ps[0] && !drawn[0].signal_valid);
  press(KEY_F);press(KEY_2);request(100100000,activeSlot.current.parameters);
  assert(!labels[0].ps[0]);
  // Keypad entry owns every key until it ends: F2 cannot switch slots.
  setup(145500000,fm);press(KEY_1);assert(keypad.active);
  press(KEY_F);press(KEY_2);assert(keypad.active && selectedSlot==0);
  // Confirmation/menu/search/held PTT consume F2 without switching.
  setup(145500000,fm);press(KEY_MENU);press(KEY_F);press(KEY_2);
  assert(selectedSlot==0);press(KEY_EXIT);
  confirming=true;press(KEY_F);press(KEY_2);assert(selectedSlot==0);press(KEY_EXIT);
  RADIO_StartSeek();press(KEY_F);press(KEY_2);assert(selectedSlot==0);press(KEY_EXIT);
  transmitter.wait_release=true;press(KEY_F);press(KEY_2);assert(selectedSlot==0);
  transmitter.wait_release=false;
  // TX belongs to B and restores B even though A is also saved.
  setup(145500000,fm);press(KEY_F);press(KEY_2);
  gDeviceConfig.radio.slots[1].tx.enabled=activeSlot.tx.enabled=true;txFakeVerified=true;
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  assert(receivingSlot==UINT8_MAX);press(KEY_F);press(KEY_2);assert(selectedSlot==1);
  RADIO_PttRelease();assert(transmitter.mode==RADIO_RX && receivingSlot==1);
  assert(runtime.current.frequency_hz==438500000 && !gDeviceConfig.radio.slots[0].tx.enabled);
  // Missing boot capabilities retain B selection and explicit unavailable state.
  setup(145500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,7100000,
      (RadioParameters){RX_LSB,1000,3000,0,0}));
  gDeviceConfig.radio.selected_slot=1;initialized=false;capabilities.ssb_patch=false;
  RADIO_Init();assert(selectedSlot==1 && receivingSlot==UINT8_MAX && !runtime.active);
  assert(activeSlot.current.parameters.mode==RX_LSB);
  // Four saved VFOs, only two visible rows. F2 cycles by enabled count.
  setup(145500000,fm);assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,4));
  press(KEY_F);press(KEY_2);assert(selectedSlot==1);
  press(KEY_F);press(KEY_2);assert(selectedSlot==2);
  assert(RADIO_STATE_VisibleFirst(&gDeviceConfig.radio)==2);
  SCREEN_Render();assert(drawn[2].selected && drawn[3].id=='D');
  press(KEY_UP);assert(gDeviceConfig.radio.slots[2].current.frequency_hz==145512500);
  assert(gDeviceConfig.radio.slots[0].current.frequency_hz==145500000);
  press(KEY_F);press(KEY_2);assert(selectedSlot==3);
  press(KEY_F);press(KEY_2);assert(selectedSlot==0);
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,3));
  press(KEY_F);press(KEY_2);press(KEY_F);press(KEY_2);
  assert(selectedSlot==2 && RADIO_STATE_VisibleFirst(&gDeviceConfig.radio)==1);
  SCREEN_Render();assert(drawn[1].id=='B' && drawn[2].selected);
  press(KEY_F);press(KEY_2);assert(selectedSlot==0);
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,1));
  assert(gDeviceConfig.radio.slot_count==1);
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,3));
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,2));
  assert(gDeviceConfig.radio.slots[2].current.frequency_hz==145512500); // Hidden state remains.
  // Broadcast background + BK event: the broadcast plays while B is quiet,
  // even when it is the selected slot; a talk on B takes the speaker.
  setup(95900000,(RadioParameters){RX_WFM,100000,0,0,0});
  unsigned broadcastSaves=saveRequests, broadcastApplies=applied;
  for(unsigned i=0;i<15;++i) { update();clockNow+=50; }
  assert(watcher.has_background && watcher.background.id==0 && watcher.count==1);
  assert(WATCH_Owner(&watcher)==0 && receivingSlot==0 && gReceiver.open);
  assert(concurrentPrepares>=1 && applied==broadcastApplies); // Polls never retune SI.
  SCREEN_Render();assert(!strcmp(statusText,"DW A"));
  watchSecondSignalFrequency=438500000;
  for(unsigned i=0;i<20;++i) { update();clockNow+=50; }
  assert(WATCH_Owner(&watcher)==1 && receivingSlot==1 && gReceiver.open && selectedSlot==0);
  SCREEN_Render();assert(!strcmp(statusText,"DW B"));
  // No time limit on a talk.
  for(unsigned i=0;i<200;++i) { update();clockNow+=50; }
  assert(receivingSlot==1 && gReceiver.open);
  // End of B: two quiet seconds for a reply, then the broadcast returns.
  watchSecondSignalFrequency=0;update();clockNow+=50;update();
  assert(!gReceiver.open && watcher.phase==WATCH_HANG);
  for(unsigned i=0;i<WATCH_HANG_MS/50+1;++i) { update();clockNow+=50; }
  assert(receivingSlot==0 && gReceiver.open && applied==broadcastApplies);
  assert(selectedSlot==0 && saveRequests==broadcastSaves);
  // Selecting B pauses (both chips stop), then the same watch resumes.
  press(KEY_F);press(KEY_2);assert(selectedSlot==1 && chipStops==2);
  clockNow+=2000;
  for(unsigned i=0;i<15;++i) { update();clockNow+=50; }
  assert(WATCH_Owner(&watcher)==0 && gReceiver.open);

  // Menus leave watch running: with BK A selected, B's broadcast keeps
  // playing in the slot menu; only an RX edit there pauses watch.
  setup(438500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,95900000,(RadioParameters){RX_WFM,100000,0,0,0}));
  for(unsigned i=0;i<10;++i) { update();clockNow+=50; }
  assert(WATCH_Owner(&watcher)==1 && gReceiver.open);
  unsigned menuStops=chipStops;
  press(KEY_MENU);assert(inMenu);
  for(unsigned i=0;i<10;++i) { update();clockNow+=50; }
  assert(WATCH_Owner(&watcher)==1 && gReceiver.open && chipStops==menuStops);
  menuIndex=MENU_RX_STEP;press(KEY_MENU);
  assert(watcher.phase==WATCH_OFF && chipStops==menuStops+2);
  press(KEY_EXIT);press(KEY_EXIT);assert(!inMenu);
  clockNow+=2000;for(unsigned i=0;i<10;++i) { update();clockNow+=50; }
  assert(WATCH_Owner(&watcher)==1 && gReceiver.open);

  // FC on BK A leaves B's broadcast playing, and EXIT does not retune it.
  setup(438500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,95900000,(RadioParameters){RX_WFM,100000,0,0,0}));
  for(unsigned i=0;i<10;++i) { update();clockNow+=50; }
  assert(WATCH_Owner(&watcher)==1 && gReceiver.open);
  unsigned fcStops=chipStops, fcApplies=applyAttempts;
  press(KEY_F);press(KEY_4);
  // Only BK slots leave the watch; nothing is stopped or retuned.
  assert(capturing && captureLeaves==RX_DEVICE_BK && chipStops==fcStops);
  assert(watcher.phase!=WATCH_OFF && watcher.bk_suspended);
  assert(gReceiver.open && gReceiver.plan.device==RX_DEVICE_SI && receivingSlot==1);
  for(unsigned i=0;i<10;++i) { update();clockNow+=50; }
  assert(gReceiver.open && gReceiver.plan.device==RX_DEVICE_SI);
  press(KEY_EXIT);
  assert(!capturing && gReceiver.open && receivingSlot==1 && applyAttempts==fcApplies);
  assert(!watcher.bk_suspended && watcher.phase==WATCH_POLL); // BK slots rejoin at once.
  // A BK talk owning the speaker hands it back to the broadcast for FC.
  watchSignalFrequency=438500000;
  for(unsigned i=0;i<10;++i) { update();clockNow+=50; }
  assert(WATCH_Owner(&watcher)==0);
  press(KEY_F);press(KEY_4);
  assert(capturing && gReceiver.open && gReceiver.plan.device==RX_DEVICE_SI);
  press(KEY_EXIT);watchSignalFrequency=0;

  // RDS stays live on the SI background while a BK talk owns the speaker.
  setup(438500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,100000000,
      (RadioParameters){RX_WFM,100000,0,0,0}));
  watchSignalFrequency=438500000;rdsAvailable=true;
  unsigned parallelSaves=saveRequests;
  for(unsigned i=0;i<40;++i) { update();clockNow+=50; }
  assert(receivingSlot==0 && gReceiver.open && labels[1].ps[0] && !labels[0].ps[0]);
  assert(rdsReads && rdsClears && saveRequests==parallelSaves);
  SCREEN_Render();assert(drawn[1].name && drawn[1].name[0]);
  // A new RadioText owns B's header for 10 s, then band and frequency return.
  assert(labels[1].text[0] && labels[1].rt_until && drawn[1].radio_text);
  rdsAvailable=false;clockNow+=RADIO_RT_SHOW_MS;update();SCREEN_Render();
  assert(!labels[1].rt_until && !drawn[1].radio_text && labels[1].text[0]);
  // PTT stops every watched chip and restores the selected RX plan.
  txFakeVerified=true;activeSlot.tx.enabled=gDeviceConfig.radio.slots[0].tx.enabled=true;
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  assert(chipStops==2 && watcher.phase==WATCH_OFF && transmitter.previous_rx.frequency_hz==438500000);
  RADIO_PttRelease();assert(receivingSlot==0 && runtime.current.frequency_hz==438500000);

  // Two BK events: polling never commits RX frequencies, selection or saves.
  setup(145500000,fm);
  watchSignalFrequency=438500000;unsigned watchSaves=saveRequests;
  update();assert(watcher.phase==WATCH_SETTLE && !RADIO_IsReceiving());
  SCREEN_Render();assert(drawn[0].activity==UI_RADIO_CARD_IDLE && !strcmp(statusText,"DW -"));
  clockNow+=180;update();assert(watcher.phase==WATCH_POLL);
  update();assert(watcher.events[watcher.cursor].id==1 && selectedSlot==0);
  clockNow+=180;update();assert(watcher.phase==WATCH_VERIFY);
  clockNow+=80;update();assert(watcher.phase==WATCH_HOLD && RADIO_IsReceiving());
  assert(gReceiver.open && receivingSlot==1);
  SCREEN_Render();assert(drawn[1].activity==UI_RADIO_CARD_RECEIVING && !drawn[1].selected);
  assert(drawn[0].selected && !drawn[0].signal_valid);
  assert(saveRequests==watchSaves && activeSlot.current.frequency_hz==145500000);
  assert(gDeviceConfig.radio.slots[1].current.frequency_hz==438500000 && !gDeviceConfig.radio.selected_slot);
  press(KEY_UP);assert(watcher.phase==WATCH_OFF && receivingSlot==0);
  assert(runtime.current.frequency_hz==145512500 && selectedSlot==0);
  unsigned afterManual=concurrentPrepares;clockNow+=1999;update();
  assert(concurrentPrepares==afterManual);
  clockNow++;update();assert(watcher.phase==WATCH_SETTLE);
  // Other screens (settings, text input) leave reception and watch running.
  SCREEN_Push(&otherScreen);assert(watcher.phase==WATCH_SETTLE);
  clockNow+=WATCH_SETTLE_MS;update();assert(watcher.phase!=WATCH_OFF);
  // A slot count change rebuilds the watch group.
  RADIO_SlotsChanged();assert(watcher.phase==WATCH_OFF && receivingSlot==0);
  // Talkback: PTT during B's talk answers B with B's own TX settings, and
  // never transmits on the selected A instead.
  setup(438500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,438600000,fm));
  watchSignalFrequency=438600000;
  update();clockNow+=180;update();update();
  clockNow+=180;update();clockNow+=80;update();assert(receivingSlot==1);
  gDeviceConfig.radio.slots[0].tx.enabled=activeSlot.tx.enabled=true;txFakeVerified=true;
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_RX);
  assert(!strcmp(message,"TX disabled") && !txFakePrepares); // B's TX is off: refuse.
  assert(watcher.phase==WATCH_OFF && selectedSlot==0 && receivingSlot==1);
  RADIO_PttRelease();
  gDeviceConfig.radio.slots[1].tx=(TxConfig){true,TX_POWER_LOW,0,0};
  clockNow+=2000;update();clockNow+=180;update();update();
  clockNow+=180;update();clockNow+=80;update();
  assert(watcher.phase==WATCH_HOLD && receivingSlot==1);
  // A's TX is now off: transmitting proves B's own settings are used.
  gDeviceConfig.radio.slots[0].tx.enabled=activeSlot.tx.enabled=false;
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX && txSlot==1);
  assert(txFakeHz==438600000 && watcher.phase==WATCH_OFF && selectedSlot==0);
  assert(transmitter.previous_rx.frequency_hz==438600000);
  SCREEN_Render();
  assert(drawn[1].activity==UI_RADIO_CARD_TRANSMITTING && !strcmp(drawn[1].power_label,"LOW"));
  assert(drawn[0].selected && drawn[0].activity!=UI_RADIO_CARD_TRANSMITTING);
  RADIO_PttRelease();assert(runtime.current.frequency_hz==438600000 && receivingSlot==1);
  // The reply window counts too: B has just stopped talking.
  clockNow+=2000;update();clockNow+=180;update();update();
  clockNow+=180;update();clockNow+=80;update();
  watchSignalFrequency=0;clockNow+=50;update();assert(watcher.phase==WATCH_HANG);
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED) && txSlot==1 && transmitter.mode==RADIO_TX);
  RADIO_PttRelease();
  // With nobody talking, PTT stays on the selected slot.
  gDeviceConfig.radio.slots[0].tx.enabled=activeSlot.tx.enabled=true;
  clockNow+=2000;update();assert(watcher.phase==WATCH_SETTLE);
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED) && txSlot==0 && transmitter.mode==RADIO_TX);
  assert(txFakeHz==438500000 && transmitter.previous_rx.frequency_hz==438500000);
  RADIO_PttRelease();assert(runtime.current.frequency_hz==438500000 && receivingSlot==0);
  // BCL samples the talkback slot after restoring it.
  for (unsigned scenario=0; scenario<4; ++scenario) {
    setup(438500000,fm);txFakeVerified=true;
    gDeviceConfig.radio.slots[1].tx.enabled=true;
    assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,438600000,fm));
    watchSignalFrequency=438600000;
    update();clockNow+=180;update();update();
    clockNow+=180;update();clockNow+=80;update();
    assert(receivingSlot==1 && watcher.phase==WATCH_HOLD);
    gDeviceConfig.preferences.tx_busy_lockout=1;
    watchSignalFrequency=scenario==0 ? 438600000 : 0;
    watchSignalOK=scenario!=2;
    assert(SCREEN_Key(KEY_PTT,KEY_PRESSED));
    assert(pttPending && receivingSlot==1 && txSlot==1 && !txFakePrepares);
    // Neither repeated PTT nor other controls may change the pending plan.
    assert(SCREEN_Key(KEY_PTT,KEY_PRESSED));
    press(KEY_UP);assert(activeSlot.current.frequency_hz==438500000);
    clockNow+=179;update();assert(!txFakePrepares);
    if (scenario==3) RADIO_PttRelease();
    clockNow++;update();
    if (scenario==1) {
      assert(transmitter.mode==RADIO_TX && txFakePrepares==1);
    } else {
      assert(!txFakePrepares && transmitter.mode==RADIO_RX);
      if (scenario!=3) {
        assert(transmitter.wait_release);
        assert(!strcmp(message,scenario==0 ? "Channel busy" : "TX busy/locked"));
      }
    }
    RADIO_PttRelease();
  }
  // Every enabled slot is watched; selection and paging never move.
  setup(145500000,fm);RADIO_STATE_SetCount(&gDeviceConfig.radio,4);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,2,145600000,fm));
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,3,438600000,fm));
  press(KEY_F);press(KEY_2);press(KEY_F);press(KEY_2);assert(selectedSlot==2);
  watchDelay=false;watchSignalFrequency=438600000;
  for(unsigned i=0;i<30;++i) { update();clockNow+=50; }
  assert(watcher.count==4 && WATCH_Owner(&watcher)==3 && selectedSlot==2);
  SCREEN_Render();
  assert(drawn[2].selected && !drawn[2].signal_valid && !strcmp(statusText,"DW D"));
  // Duplicates collapse to one source: stay on the primary, no busy retunes.
  setup(145500000,fm);RADIO_STATE_Set(&gDeviceConfig.radio,1,145500000,fm);
  unsigned noVisits=concurrentPrepares;
  update();assert(watcher.phase==WATCH_OFF && concurrentPrepares==noVisits);
  SCREEN_Render();assert(!strcmp(statusText,"DW A"));
  assert(!drawn[0].header_message && !drawn[1].header_message); // Expected: no notice.
  for (unsigned i=0;i<10;++i) { clockNow+=100;update(); }
  assert(concurrentPrepares==noVisits);
  // Tune/sample errors back off; the primary restore is attempted only once.
  setup(145500000,fm);failApply=true;
  update();update();update();assert(watcher.phase==WATCH_RETRY);
  unsigned attempts=applyAttempts;
  for(unsigned i=0;i<10;++i) { clockNow+=100;update(); }
  assert(applyAttempts==attempts && receivingSlot==UINT8_MAX);
  failApply=false;clockNow+=WATCH_RETRY_MS;update();assert(watcher.phase==WATCH_SETTLE);
  // Reducing count in Settings changes selection on return, preserving hidden VFO data.
  setup(145500000,fm);RADIO_STATE_SetCount(&gDeviceConfig.radio,4);
  press(KEY_F);press(KEY_2);press(KEY_F);press(KEY_2);assert(selectedSlot==2);
  SCREEN_Push(&otherScreen);assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,2));
  RADIO_SlotsChanged();SCREEN_Pop();
  assert(selectedSlot==0 && activeSlot.current.frequency_hz==145500000);
  // Draft input keeps RX/storage unchanged, owns PTT and pauses DW indefinitely.
  setup(438500000,fm);
  
  unsigned beforeApply=applied, beforeSave=saveRequests;
  press(KEY_4);press(KEY_3);press(KEY_8);press(KEY_STAR);press(KEY_6);
  assert(keypad.active && !strcmp(keypad.text,"438.6") && appRuns==0);
  assert(activeSlot.current.frequency_hz==438500000 && applied==beforeApply && saveRequests==beforeSave);
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED));press(KEY_PTT);
  assert(keypad.active && !pttPending && transmitter.mode==RADIO_RX && !txFakePrepares);
  press(KEY_F);press(KEY_2);press(KEY_SIDE1);press(KEY_SIDE2);press(KEY_UP);
  assert(selectedSlot==0 && keypad.active && !functionPending && applied==beforeApply);
  clockNow+=5000;update();assert(watcher.phase==WATCH_OFF);
  SCREEN_Render();assert(drawn[0].edit_text && !drawn[1].edit_text);
  assert(SCREEN_Key(KEY_EXIT,KEY_LONG_PRESSED));
  assert(!keypad.active && applied==beforeApply && saveRequests==beforeSave);
  press(KEY_4);press(KEY_3);press(KEY_8);press(KEY_STAR);press(KEY_6);press(KEY_MENU);
  assert(!keypad.active && activeSlot.current.frequency_hz==438600000 && applied==beforeApply+1);
  press(KEY_0);press(KEY_MENU);assert(keypad.active && keypad.invalid);
  SCREEN_Push(&otherScreen);assert(!keypad.active);
  // Lock PTT too only applies while the keyboard is locked.
  setup(438500000,fm);
  gDeviceConfig.radio.slots[0].tx.enabled=activeSlot.tx.enabled=true;
  txFakeVerified=true;
  gDeviceConfig.preferences.ptt_lock=true;
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  RADIO_PttRelease();
  gDeviceConfig.preferences.keylock=true;
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_RX);
  assert(txFakePrepares==1);
  // Applying global receive settings does not save or replace VFO parameters.
  unsigned savedBeforeRefresh=saveRequests;
  RadioSlot vfoBeforeRefresh=gDeviceConfig.radio.slots[0];
  assert(RADIO_ApplyRxSettings());
  assert(saveRequests==savedBeforeRefresh);
  assert(!memcmp(&vfoBeforeRefresh,&gDeviceConfig.radio.slots[0],sizeof(vfoBeforeRefresh)));
  // Physical release and battery faults stop TX before queued UI events.
  setup(438500000,fm);txFakeVerified=true;
  activeSlot.tx.enabled=gDeviceConfig.radio.slots[0].tx.enabled=true;
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  txFakePttPressed=false;RADIO_TxSafetyUpdate();
  assert(transmitter.mode==RADIO_RX && !pttPending && receivingSlot==selectedSlot);
  txFakePttPressed=true;
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  txFakeVoltageValid=false;RADIO_TxSafetyUpdate();
  assert(transmitter.mode==RADIO_RX && transmitter.wait_release);
  assert(!strcmp(message,"TX: bad battery"));
  SCREEN_Render();assert(!strcmp(drawn[0].header_message,"TX: bad battery"));
  press(KEY_PTT);

  // An inexact target still requires the existing hardware quantization confirmation.
  setup(100000000,(RadioParameters){RX_WFM,100000,0,0});
  press(KEY_9);press(KEY_5);press(KEY_STAR);press(KEY_9);press(KEY_2);press(KEY_5);press(KEY_MENU);
  assert(!keypad.active && confirming && activeSlot.current.frequency_hz==100000000);
  press(KEY_EXIT);assert(!confirming && activeSlot.current.frequency_hz==100000000);
  // F4 binds capture, not numeric input. Detection owns PTT/UI.
  setup(438500000,fm);
  RadioSlot fcOrigin=gDeviceConfig.radio.slots[0];
  unsigned fcSaves=saveRequests;
  press(KEY_F);press(KEY_4);
  assert(capturing && !keypad.active && !functionPending);
  assert(!RADIO_IsReceiving() && receivingSlot==UINT8_MAX && captureStarts==1);
  assert(SCREEN_Key(KEY_PTT,KEY_PRESSED));press(KEY_PTT);
  press(KEY_F);press(KEY_2);
  press(KEY_MENU);assert(capturing); // Nothing on screen yet: MENU has nothing to adopt.
  press(KEY_EXIT);
  assert(!capturing && selectedSlot==0 && !txFakePrepares);
  assert(!memcmp(&fcOrigin,&gDeviceConfig.radio.slots[0],sizeof(fcOrigin)) && saveRequests==fcSaves);
  press(KEY_F);press(KEY_4);
  clockNow+=500;update();clockNow+=500;update();
  assert(capture.range==CAPTURE_RANGE_UHF); // No-results Auto still advances.
  // Live view: range in the header, scope as the mode, no reading yet.
  SCREEN_Render();assert(!strcmp(drawn[0].header_message,"FC UHF"));
  assert(!strcmp(drawn[0].mode_text,"Auto") && !strcmp(drawn[0].edit_text,"---"));
  // Two close samples tune the bound VFO directly: NFM, TX off, one save.
  gDeviceConfig.radio.slots[0].tx.enabled=activeSlot.tx.enabled=true;
  RadioSlot other=gDeviceConfig.radio.slots[1];
  captureReply=CAPTURE_SAMPLE;captureHz=438512340;
  clockNow+=20;update();assert(capturing);
  SCREEN_Render();assert(drawn[0].frequency_hz==438512340 && !drawn[0].edit_text);
  // MENU adopts the reading on screen before any confirmation.
  {
    RadioSlot before=gDeviceConfig.radio.slots[0];unsigned saves=saveRequests;
    press(KEY_MENU);
    assert(!capturing && runtime.current.frequency_hz==438512340 && saveRequests==saves+1);
    gDeviceConfig.radio.slots[0]=activeSlot=before;
    request(438500000,fm);fcSaves=saveRequests;
    gDeviceConfig.radio.slots[0].tx.enabled=activeSlot.tx.enabled=true;
    press(KEY_F);press(KEY_4);captureReply=CAPTURE_SAMPLE;captureHz=438512340;
    clockNow+=20;update();assert(capturing);
  }
  captureHz+=3000;clockNow+=20;update();
  assert(!capturing && RADIO_IsReceiving() && runtime.current.frequency_hz==captureHz);
  assert(activeSlot.current.frequency_hz==captureHz && gReceiver.plan.frequency_hz==captureHz);
  assert(activeSlot.current.parameters.mode==RX_NFM && !activeSlot.current.parameters.rx_tone && !activeSlot.tx.enabled);
  assert(saveRequests==fcSaves+1 && !memcmp(&other,&gDeviceConfig.radio.slots[1],sizeof(other)));
  // Without a carrier FC keeps running; EXIT and application exit restore
  // without writes.
  setup(145500000,fm);fcSaves=saveRequests;
  press(KEY_F);press(KEY_4);
  for (unsigned i=0;i<120;++i) { clockNow+=500;update(); }
  assert(capturing && saveRequests==fcSaves);
  press(KEY_EXIT);
  assert(!capturing && saveRequests==fcSaves && gReceiver.plan.frequency_hz==145500000);
  SCREEN_Render();assert(!drawn[0].header_message && drawn[0].frequency_hz==145500000);
  press(KEY_F);press(KEY_4);SCREEN_Push(&otherScreen);
  assert(!capturing && saveRequests==fcSaves);
  // From SI/BK1080 the origin stays committed until a carrier is adopted.
  for (unsigned chip=0;chip<2;++chip) {
    setup(95900000,(RadioParameters){RX_WFM,100000,0,0});
    if (chip) {
      capabilities=(ReceiverCapabilities){false,true,false};
      request(95900000,activeSlot.current.parameters);
    }
    fcOrigin=gDeviceConfig.radio.slots[0];fcSaves=saveRequests;
    press(KEY_F);press(KEY_4);
    unsigned reads=rdsReads;clockNow+=100;update();assert(rdsReads==reads);
    press(KEY_EXIT);
    assert(gReceiver.plan.device==(chip ? RX_DEVICE_BROADCAST : RX_DEVICE_SI) && gReceiver.plan.frequency_hz==95900000);
    assert(saveRequests==fcSaves && !memcmp(&fcOrigin,&gDeviceConfig.radio.slots[0],sizeof(fcOrigin)));
    press(KEY_F);press(KEY_4);captureReply=CAPTURE_SAMPLE;captureHz=145500000;
    clockNow+=20;update();clockNow+=20;update();
    assert(!capturing && gReceiver.plan.device==RX_DEVICE_BK && runtime.current.frequency_hz==145500000);
    capabilities=(ReceiverCapabilities){true,false,true};captureReply=CAPTURE_BUSY;
  }
  // An unsupported candidate is ignored; a failed tune restores the origin
  // and never commits the candidate.
  setup(145500000,fm);fcSaves=saveRequests;
  press(KEY_F);press(KEY_4);captureReply=CAPTURE_SAMPLE;captureHz=700000000;
  clockNow+=20;update();clockNow+=20;update();assert(capturing);
  captureHz=145600000;failFrequency=captureHz;
  clockNow+=20;update();clockNow+=20;update();
  assert(!capturing && runtime.active && runtime.current.frequency_hz==145500000);
  assert(gDeviceConfig.radio.slots[0].current.frequency_hz==145500000 && saveRequests==fcSaves);
  // Side keys restart detection with the next scope and forget prior hits.
  failFrequency=0;captureReply=CAPTURE_BUSY;press(KEY_F);press(KEY_4);
  press(KEY_SIDE2);assert(capturing && capture.scope==CAPTURE_VHF && !capture.hits);
  press(KEY_SIDE1);assert(capture.scope==CAPTURE_UHF && capture.range==CAPTURE_RANGE_UHF);
  press(KEY_EXIT);assert(runtime.current.frequency_hz==145500000 && saveRequests==fcSaves);
  // Hidden VFOs can be the bound owner as well; F2 is swallowed during FC.
  setup(145500000,fm);
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,4));
  for (unsigned id=1;id<4;++id) {
    press(KEY_F);press(KEY_2);assert(selectedSlot==id);
    RadioSlot untouched=gDeviceConfig.radio.slots[0];fcSaves=saveRequests;
    press(KEY_F);press(KEY_4);captureReply=CAPTURE_BUSY;
    press(KEY_F);press(KEY_2);assert(selectedSlot==id && capturing);
    captureReply=CAPTURE_SAMPLE;captureHz=145600000+id*10000u;
    clockNow+=20;update();clockNow+=20;update();
    assert(gDeviceConfig.radio.slots[id].current.frequency_hz==captureHz && saveRequests==fcSaves+1);
    assert(!memcmp(&untouched,&gDeviceConfig.radio.slots[0],sizeof(untouched)));
  }
  // MR and input retain their existing controls; F4 never starts capture there.
  setup(145500000,fm);activeSlot.source=RADIO_SOURCE_CHANNEL;
  press(KEY_F);press(KEY_4);assert(!capturing && !captureStarts);

  // Spectrum: opened from the slot menu, the card follows the cursor.
  setup(145500000,fm);
  openMenu(MENU_SPECTRUM);press(KEY_MENU);
  assert(SCREEN_Top()==&spectrumScreen && spectrumOn && spectrumStarts==1);
  assert(spectrum.step_hz==12500 && spectrum.center_hz==145500000 && !RADIO_IsReceiving());
  update();assert(spectrumTuned==144700000 && spectrumTunes==1); // 64 steps below.
  update();assert(spectrumTunes==1); // Still settling.
  for (unsigned i=0;i<SPECTRUM_POINTS;++i) { clockNow+=2; update(); }
  assert(spectrum.level[0]==50 && spectrum.level[127]==50 && spectrum.next==0);
  spectrumRssi=0;clockNow+=2;update();assert(spectrum.level[0]==1); // Measured, even at zero.
  RadioSlotStatus spec;
  RADIO_SlotStatus(0,&spec);assert(spec.frequency_hz==145500000 && spec.receiving);
  press(KEY_UP);RADIO_SlotStatus(0,&spec);assert(spec.frequency_hz==145512500);
  SCREEN_Render();assert(graphs==1 && drawn[0].step_hz==12500 && !strcmp(drawn[0].band_name,"Span 1.600 MHz"));
  press(KEY_STAR);assert(spectrum.step_hz==25000 && spectrum.center_hz==145512500 && spectrum.cursor==64);
  press(KEY_F);press(KEY_F);assert(spectrum.step_hz==6250);
  press(KEY_F);assert(spectrum.step_hz==6250); // Finest already.
  // F→5 opens the same screen from the main screen.
  press(KEY_EXIT);press(KEY_F);press(KEY_5);
  assert(SCREEN_Top()==&spectrumScreen && spectrumOn && !functionPending);
  press(KEY_STAR);press(KEY_F);press(KEY_F);
  // Covered by settings: reception returns; uncovered: the sweep restarts.
  SCREEN_Push(&otherScreen);assert(!spectrumOn && spectrumStops==2 && RADIO_IsReceiving());
  SCREEN_Pop();assert(spectrumOn && spectrumStarts==3);
  // EXIT leaves the slot as it was; MENU tunes it to the cursor.
  press(KEY_EXIT);assert(SCREEN_Top()==&homeScreen && !spectrumOn);
  assert(activeSlot.current.frequency_hz==145500000 && RADIO_IsReceiving());
  openMenu(MENU_SPECTRUM);press(KEY_MENU);press(KEY_DOWN);press(KEY_DOWN);press(KEY_MENU);
  assert(SCREEN_Top()==&homeScreen && !spectrumOn && activeSlot.current.frequency_hz==145475000);
  assert(gDeviceConfig.radio.slots[0].current.frequency_hz==145475000 && RADIO_IsReceiving());
  // Points BK cannot receive are recorded empty without tuning.
  setup(18300000,(RadioParameters){RX_AM,5000,5000,0}); // 17.9..18.7 MHz.
  openMenu(MENU_SPECTRUM);press(KEY_MENU);
  update();assert(spectrum.next==16 && spectrumTuned==18000000 && !spectrum.level[0] && !spectrum.level[15]);
  // MR can look but not tune.
  setup(145500000,fm);activeSlot.source=gDeviceConfig.radio.slots[0].source=RADIO_SOURCE_CHANNEL;
  openMenu(MENU_SPECTRUM);press(KEY_MENU);press(KEY_UP);press(KEY_MENU);
  assert(!spectrumOn && activeSlot.current.frequency_hz==145500000 && !strcmp(message,"VFO only"));
  return 0;
}

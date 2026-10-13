#include "core/device-config.h"
// Exercise real main-screen state transitions; only hardware/UI plumbing is mocked.
#include "../src/apps/radio.c"
#include <assert.h>

Receiver gReceiver;
bool gRedrawScreen;
static char statusText[32];
void STATUSLINE_SetText(const char *pattern, ...) {
  va_list args; va_start(args, pattern);
  vsnprintf(statusText, sizeof(statusText), pattern, args); va_end(args);
}
static UI_RadioCardView drawn[RADIO_SLOT_CAPACITY];
static unsigned drawCount;
void UI_RadioCardRender(uint8_t y, const UI_RadioCardView *view, UI_RadioCardState *state, uint32_t now) {
  unsigned id = view->id-'A';
  assert(id<RADIO_SLOT_CAPACITY && y==8+(id-RADIO_STATE_VisibleFirst(&gDeviceConfig.radio))*UI_RADIO_CARD_HEIGHT && state==&cards[id]);
  (void)now; drawn[id]=*view; ++drawCount;
}
uint8_t UI_SMeterLevel(void) { return 8; }
void PrintSmall(uint8_t x, uint8_t y, const char *pattern, ...) {
  (void)x; (void)y; (void)pattern;
}
void PrintSmallEx(uint8_t x, uint8_t y, TextPos pos, Color c, const char *pattern, ...) {
  (void)x; (void)y; (void)pos; (void)c; (void)pattern;
}
void PrintMediumEx(uint8_t x, uint8_t y, TextPos pos, Color c, const char *pattern, ...) {
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
bool TX_DEVICE_Prepare(const TxPlan *plan) {
  ++txFakePrepares;
  assert(plan->frequency_hz == 438500000u && plan->power == TX_POWER_LOW);
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
void RX_DEVICE_CaptureStart(CaptureRange range, RxDevice previous) {
  (void)previous; captureRange=range; ++captureStarts;
}
CaptureSample RX_DEVICE_CapturePoll(CaptureRange range, uint32_t *hz) {
  assert(range==captureRange); *hz=captureHz; return captureReply;
}
void RX_DEVICE_CaptureStop(void) { ++captureStops; }
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
void UI_RadioCardShowFrequency(UI_RadioCardState *state, uint32_t now) { state->tuned_at = now; }
bool UI_RadioCardUpdate(UI_RadioCardState *state, uint32_t now) { (void)state; (void)now; return false; }
bool RX_DEVICE_ReadRDS(RdsPacket *packet, bool clear) {
  ++rdsReads;
  *packet=(RdsPacket){0};
  if (clear) { ++rdsClears; return true; }
  if (rdsAvailable && rdsIndex<8) {
    packet->synchronized=true;
    packet->fifo_used=8-rdsIndex;
    packet->group=(RdsGroup){.blocks={0x40a7,rdsIndex%4,0,0x4141}};
    ++rdsIndex;
  }
  return true;
}
void DISPLAY_Blit(void) {}
void FillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color) {
  (void)x; (void)y; (void)w; (void)h; (void)color;
}
void PrintMediumBoldEx(uint8_t x, uint8_t y, TextPos pos, Color color, const char *format, ...) {
  (void)x; (void)y; (void)pos; (void)color; (void)format;
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
void APPS_run(AppType_t app) { (void)app; ++appRuns; }
char *gTextinputText;
uint8_t gTextInputSize;
void (*gTextInputCallback)(void);
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
bool APPS_key(KEY_Code_t key, Key_State_t keyState) { (void)key; (void)keyState; return true; }

static void setup(uint32_t frequency, RadioParameters values) {
  capturePhase=FC_OFF;
  captureStarts=captureStops=0; captureReply=CAPTURE_BUSY;
  number = (NumberInput){0};
  clockNow=100;
  rdsReads=rdsClears=rdsIndex=0;
  rdsAvailable=false;
  watchSignalFrequency=watchSecondSignalFrequency=0;watchSignalOK=true;
  concurrentPrepares=audioSelects=chipStops=0;
  initialized = inMenu = confirming = false;
  menuIndex = 0;
  WATCH_Stop(&watcher); watchDelay=false; watchResumeAt=0;
  functionPending = false;
  selectedSlot = inputSlot = 0;
  receivingSlot = UINT8_MAX;
  memset(cards, 0, sizeof(cards));
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
  RADIO_APP_init();
}
static void press(KEY_Code_t key) { assert(RADIO_APP_key(key, KEY_RELEASED)); }

int main(void) {
  // MR mode retains channel stepping and save/name operations only.
  RadioParameters testRx = {RX_NFM,12500,12000,4};
  setup(145500000, testRx);
  fakeChannels[12] = (UserChannel){.name="TEST",.frequency_hz=438500000,.rx=testRx};
  fakeChannelUsed[12] = true;
  press(KEY_F);press(KEY_3);assert(activeSlot.source==RADIO_SOURCE_CHANNEL);
  assert(activeSlot.current.frequency_hz==438500000);
  press(KEY_UP);assert(activeSlot.current.frequency_hz==438500000); // One occupied channel wraps to itself.
  inMenu=true;menuIndex=MENU_CHANNEL_NAME;press(KEY_MENU);
  assert(gTextinputText==CHANNEL_MANAGER_Name(selectedSlot));
  strcpy(gTextinputText,"NAMED");gTextInputCallback();
  menuIndex=MENU_CHANNEL_SAVE;press(KEY_MENU);assert(CHANNEL_MANAGER_Active());
  press(KEY_MENU);assert(CHANNEL_MANAGER_Active()); // First press confirms overwrite.
  press(KEY_MENU);assert(!CHANNEL_MANAGER_Active());
  assert(!strcmp(fakeChannels[12].name,"NAMED"));
  assert(gDeviceConfig.radio.slots[0].current.frequency_hz==145500000);
  inMenu=false;
  // Both F-prefix and side-key controls must refuse tuning in MR mode.
  unsigned mrApplies = applied, mrSaves = saveRequests;
  uint32_t mrFrequency = activeSlot.current.frequency_hz;
  press(KEY_F); press(KEY_1);
  assert(!strcmp(message,"Channel mode") && !functionPending);
  press(KEY_F); press(KEY_0);
  assert(!strcmp(message,"Channel mode") && !functionPending);
  press(KEY_SIDE1); assert(!strcmp(message,"Channel mode"));
  press(KEY_SIDE2); assert(!strcmp(message,"Channel mode"));
  assert(applied==mrApplies && saveRequests==mrSaves &&
         activeSlot.current.frequency_hz==mrFrequency);
  press(KEY_F);press(KEY_3);assert(activeSlot.source==RADIO_SOURCE_VFO);
  memset(fakeChannelUsed,0,sizeof(fakeChannelUsed));
  RadioParameters fm = {RX_NFM, 12500, 12000, 4};
  setup(438500000, fm);
  press(KEY_F);press(KEY_0);
  assert(!functionPending && !confirming && appRuns == 0 && applied == 2);
  assert(activeSlot.current.frequency_hz == 438500000 && activeSlot.current.parameters.mode == RX_AM);
  press(KEY_F);press(KEY_0);
  assert(activeSlot.current.parameters.mode == RX_NFM);
  press(KEY_0);assert(number.active && appRuns == 0); // Plain 0 still enters frequency input.

  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  press(KEY_F);press(KEY_0);
  assert(!confirming && activeSlot.current.parameters.mode == RX_AM && gReceiver.plan.device == RX_DEVICE_BK);
  assert(activeSlot.current.frequency_hz == 100000000);

  setup(7100000, (RadioParameters){RX_AM,5000,6000,0});
  capabilities.ssb_patch = false;
  press(KEY_F);press(KEY_0);
  assert(!functionPending && activeSlot.current.parameters.mode == RX_AM);
  assert(!strcmp(message,"No other mode") && appRuns == 0);

  setup(438500000, fm);
  assert(RADIO_APP_key(KEY_F, KEY_PRESSED) == false);
  press(KEY_F);
  assert(functionPending && activeSlot.active_band == 6);
  assert(RADIO_APP_key(KEY_1, KEY_PRESSED));
  assert(applied == 1); // Execute on release, once.
  press(KEY_1);
  assert(!functionPending && activeSlot.active_band == 0 && appRuns == 0);
  assert(activeSlot.current.frequency_hz == 225000 && activeSlot.current.parameters.mode == RX_AM);
  press(KEY_1);
  assert(number.active && appRuns == 0); // The prefix is consumed; plain 1 enters frequency input.

  // F6 cycles only the selected VFO's TX intent, saves it and consumes the key.
  setup(438500000, fm);
  unsigned powerSaves = saveRequests, powerApplies = applied;
  TxConfig otherTx = gDeviceConfig.radio.slots[1].tx;
  press(KEY_F);
  assert(RADIO_APP_key(KEY_6, KEY_PRESSED));
  assert(activeSlot.tx.power == TX_POWER_LOW && saveRequests == powerSaves);
  press(KEY_6);
  assert(activeSlot.tx.power == TX_POWER_MEDIUM && !functionPending && !number.active);
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
  assert(!strcmp(message, "Channel is read only") && !number.active && !functionPending);

  setup(438500000, fm);
  press(KEY_F);press(KEY_EXIT);
  assert(!functionPending && applied == 1);
  press(KEY_F);press(KEY_F);
  assert(!functionPending && applied == 1);
  press(KEY_F);press(KEY_3);
  assert(!functionPending && !strcmp(message,"No saved channels") && appRuns == 0 && applied == 1);

  press(KEY_F);
  clockNow = functionAt+2999;RADIO_APP_update();assert(functionPending);
  clockNow++;RADIO_APP_update();assert(!functionPending);
  press(KEY_1);assert(number.active && appRuns == 0);
  RADIO_APP_key(KEY_EXIT, KEY_LONG_PRESSED);
  press(KEY_F);clockNow += 3000; // Timeout is also checked before key dispatch.
  press(KEY_1);assert(!functionPending && number.active && appRuns == 0 && applied == 1);
  RADIO_APP_key(KEY_EXIT, KEY_LONG_PRESSED);
  clockNow = UINT32_MAX-1000;
  press(KEY_F);clockNow += 2999;RADIO_APP_update();assert(functionPending);
  clockNow++;RADIO_APP_update();assert(!functionPending);

  setup(438500000, fm);
  press(KEY_F);assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED));assert(!functionPending);
  press(KEY_F);assert(!RADIO_APP_key(KEY_MENU,KEY_LONG_PRESSED));
  assert(!functionPending && !inMenu);
  press(KEY_F);RADIO_APP_cancelShortcut();assert(!functionPending); // System lock transition.
  press(KEY_F);RADIO_APP_deinit();assert(!functionPending);
  press(KEY_F);RADIO_APP_init();assert(!functionPending); // Returning from another app.
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
  RADIO_APP_update();assert(!rdsReads);
  clockNow=400;RADIO_APP_update();assert(rdsClears==1);
  rdsAvailable=true;
  clockNow=500;RADIO_APP_update();assert(rdsIndex==4 && !rds.decoder.ps[0]);
  gRedrawScreen=false;clockNow=600;RADIO_APP_update();
  assert(!strcmp(rds.decoder.ps,"AAAAAAAA") && gRedrawScreen);
  unsigned readsBefore=rdsReads;
  press(KEY_SIDE2);assert(!rds.decoder.ps[0]);
  clockNow=1000;RADIO_APP_update();assert(rdsReads==readsBefore); // NFM doesn't poll SI RDS.

  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0});
  strcpy(rds.decoder.ps,"OLD FM");
  failFrequency=100100000;press(KEY_UP);
  assert(runtime.active && activeSlot.current.frequency_hz==100000000 && !rds.decoder.ps[0]);

  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0});
  capabilities.si=false;capabilities.broadcast=true;request(100000000,activeSlot.current.parameters);
  assert(runtime.current.device==RX_DEVICE_BROADCAST);
  clockNow=500;RADIO_APP_update();assert(!rdsReads); // BK1080 has no RDS path.

  setup(438500000, fm);
  press(KEY_SIDE2);
  assert(runtime.active && !confirming && activeSlot.current.parameters.mode == RX_AM);
  assert(activeSlot.current.frequency_hz == 438500000);
  RADIO_APP_render(); assert(drawn[0].changed); // Assert the visible preset-delta marker.
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
  tuneTo(95925000);
  assert(confirming && pending.frequency_hz == 95930000);
  assert(activeSlot.current.frequency_hz == 438500000);
  press(KEY_EXIT);
  assert(activeSlot.current.frequency_hz == 438500000 && runtime.active);
  tuneTo(95925000);
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
  tuneTo(438500000); // Enter 70cm again.
  assert(activeSlot.current.parameters.mode == RX_AM && activeSlot.current.parameters.step_hz == 25000);
  assert(activeSlot.current.parameters.squelch == 2);
  RADIO_APP_render(); assert(!drawn[0].changed); // Re-entered preset is unmodified.
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
  RADIO_APP_update();
  assert(seek.active && gReceiver.plan.frequency_hz==438512500u &&
         saveRequests == savedBeforeSeek);
  clockNow += 185;
  RADIO_APP_update(); // First squelch sample.
  assert(seek.active && saveRequests == savedBeforeSeek);
  clockNow += 85;
  RADIO_APP_update(); // Stable second sample.
  assert(!seek.active && activeSlot.current.frequency_hz == 438512500);
  assert(saveRequests == savedBeforeSeek + 1);

  // EXIT cancels without persisting and restores the exact original plan.
  setup(438500000, fm);
  savedBeforeSeek = saveRequests;
  press(KEY_F);press(KEY_STAR);
  RADIO_APP_update();
  press(KEY_EXIT);
  assert(!seek.active && runtime.active &&
         activeSlot.current.frequency_hz == 438500000 &&
         gReceiver.plan.frequency_hz==438500000u && saveRequests == savedBeforeSeek);
  assert(RADIO_APP_key(KEY_UP, KEY_PRESSED) == false); // Manual tuning unchanged.

  // Hardware seek must not overwrite RadioUserState until the final tune.
  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  savedBeforeSeek = saveRequests;
  press(KEY_F);press(KEY_STAR);
  assert(seek.active && seekNative && nativeStarts == 0);
  RADIO_APP_update();
  assert(nativeStarts == 1 && saveRequests == savedBeforeSeek);
  clockNow += 100;RADIO_APP_update();
  clockNow += 100;RADIO_APP_update();
  assert(!seek.active && activeSlot.current.frequency_hz == 100100000);
  assert(saveRequests == savedBeforeSeek+1 && nativeCancels == 1);

  // No signal: reach hardware band end, wrap, reach the origin, stop.
  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  savedBeforeSeek = saveRequests;
  nativeReply = RX_SEEK_LIMIT;
  press(KEY_F);press(KEY_STAR);
  RADIO_APP_update();
  clockNow += 100;RADIO_APP_update();
  clockNow += 100;RADIO_APP_update();
  assert(seek.active && seek.wrapped && gReceiver.plan.frequency_hz==64000000u);
  assert(saveRequests == savedBeforeSeek);
  clockNow += 190;RADIO_APP_update(); // Check the band start once.
  clockNow += 10;RADIO_APP_update(); // Continue native seek from the band start.
  clockNow += 100;RADIO_APP_update();
  clockNow += 100;RADIO_APP_update();
  assert(!seek.active && activeSlot.current.frequency_hz == 100000000);
  assert(gReceiver.plan.frequency_hz==100000000u && saveRequests == savedBeforeSeek);

  // The single VFO owns TX intent, never a per-band transmit flag.
  setup(438500000, fm);
  assert(!gDeviceConfig.radio.slots[0].tx.enabled);
  assert(RADIO_APP_key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX disabled"));
  RADIO_APP_render();
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
  assert(RADIO_APP_key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX HW locked"));
  assert(transmitter.mode == RADIO_RX);
  RADIO_APP_PttRelease();
  assert(transmitter.mode == RADIO_RX);
  // RX mode is still authoritative, even when a TX request is stored.
  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  gDeviceConfig.radio.slots[0].tx.enabled = activeSlot.tx.enabled = true;
  assert(RADIO_APP_key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX mode not supported"));
  assert(transmitter.mode == RADIO_RX);
  RADIO_APP_PttRelease();

  // Busy-channel lockout remains active even when Monitor is on.
  setup(438500000, fm);
  gDeviceConfig.radio.slots[0].tx.enabled = activeSlot.tx.enabled = true;
  gDeviceConfig.preferences.tx_busy_lockout = 1;
  txFakeVerified = true;
  gReceiver.open = false; // A stale/cleared audio flag cannot bypass lockout.
  watchSignalFrequency = 438500000;
  assert(RADIO_APP_key(KEY_PTT, KEY_PRESSED));
  assert(pttPending && !txFakePrepares);
  clockNow += 180;
  RADIO_APP_TxSafetyUpdate();
  assert(!strcmp(message, "Channel busy"));
  assert(txFakePrepares == 0 && transmitter.mode == RADIO_RX);
  RADIO_APP_PttRelease();

  // Exercise the full one-VFO PTT lifecycle using a host-only fake PA:
  // no EEPROM mutation, timeout closes PA, RX restores, release re-arms.
  setup(438500000, fm);
  gDeviceConfig.radio.slots[0].tx.enabled = activeSlot.tx.enabled = true;
  gDeviceConfig.preferences.tx_timeout_seconds = 30;
  txFakeVerified = true;
  unsigned savesBeforePTT = saveRequests;
  unsigned appliedBeforePTT = applied;
  assert(RADIO_APP_key(KEY_PTT, KEY_PRESSED));
  assert(transmitter.mode == RADIO_TX);
  assert(txFakePrepares == 1 && txFakeEnables == 1 &&
         txFakeDisables == 1 && saveRequests == savesBeforePTT);
  clockNow += 30000u;
  RADIO_APP_TxSafetyUpdate();
  assert(transmitter.mode == RADIO_RX && transmitter.wait_release);
  assert(txFakeDisables == 2 && applied == appliedBeforePTT + 1);
  assert(saveRequests == savesBeforePTT && activeSlot.current.frequency_hz == 438500000);
  assert(RADIO_APP_key(KEY_PTT, KEY_PRESSED));
  assert(txFakeEnables == 1); // TOT latch: no auto-rekey while held.
  RADIO_APP_PttRelease();
  assert(!transmitter.wait_release);
  assert(RADIO_APP_key(KEY_PTT, KEY_PRESSED) && transmitter.mode == RADIO_TX);
  RADIO_APP_PttRelease();
  assert(transmitter.mode == RADIO_RX);

  // Prepare failure must never call enable, and requires a fresh PTT edge.
  setup(438500000, fm);
  gDeviceConfig.radio.slots[0].tx.enabled = activeSlot.tx.enabled = true;
  txFakeVerified = true;
  txFakePrepareOK = false;
  assert(RADIO_APP_key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX hardware error"));
  assert(transmitter.mode == RADIO_RX && transmitter.wait_release);
  assert(txFakePrepares == 1 && txFakeEnables == 0 && txFakeDisables == 2);
  RADIO_APP_PttRelease();
  assert(!transmitter.wait_release);

  // Failed RX restoration must latch a permanent fault with PA disabled.
  setup(438500000, fm);
  gDeviceConfig.radio.slots[0].tx.enabled = activeSlot.tx.enabled = true;
  txFakeVerified = true;
  assert(RADIO_APP_key(KEY_PTT, KEY_PRESSED) && transmitter.mode == RADIO_TX);
  failApply = true;
  RADIO_APP_PttRelease();
  assert(transmitter.mode == RADIO_FAULT && txFakeDisables >= 2);
  assert(!runtime.active);
  assert(RADIO_APP_key(KEY_PTT, KEY_PRESSED));
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
  for(unsigned i=MENU_RX_TONE;i<MENU_TX_FREQUENCY;++i) press(KEY_DOWN);
  assert(menuIndex==MENU_TX_FREQUENCY);
  unsigned appliesBefore=applied;
  press(KEY_MENU);
  assert(appRuns==0 && number.active && inputTx);
  press(KEY_4);press(KEY_3);press(KEY_8);press(KEY_STAR);press(KEY_6);
  press(KEY_MENU);
  assert(gDeviceConfig.radio.slots[0].tx.frequency_hz==438600000 && applied==appliesBefore);
  assert(activeSlot.current.frequency_hz==438500000);
  press(KEY_MENU);press(KEY_7);press(KEY_0);press(KEY_0);press(KEY_MENU);
  assert(number.active && number.invalid && gDeviceConfig.radio.slots[0].tx.frequency_hz==438600000);
  assert(RADIO_APP_key(KEY_EXIT,KEY_LONG_PRESSED));
  inMenu=true;press(KEY_F);assert(!gDeviceConfig.radio.slots[0].tx.frequency_hz);
  press(KEY_DOWN);assert(menuIndex==MENU_TX_TONE);
  press(KEY_MENU);assert(gDeviceConfig.radio.slots[0].tx.tone==1);
  press(KEY_F);assert(gDeviceConfig.radio.slots[0].tx.tone==0);
  unsigned savesBeforePlaceholder=saveRequests;
  appliesBefore=applied;
  press(KEY_DOWN);assert(menuIndex==MENU_CHANNEL_NAME);
  press(KEY_MENU);assert(gTextinputText==CHANNEL_MANAGER_Name(selectedSlot));
  press(KEY_DOWN);assert(menuIndex==MENU_CHANNEL_SAVE);
  press(KEY_MENU);assert(CHANNEL_MANAGER_Active());press(KEY_EXIT);assert(!CHANNEL_MANAGER_Active());
  assert(saveRequests==savesBeforePlaceholder && applied==appliesBefore);
  assert(inMenu && !confirming);
  press(KEY_DOWN);assert(menuIndex==MENU_RX_MODE); // Menu cycles through RX/TX settings and channel actions.
  // Dual VFO: selection is a receive transaction, not an alias to one state.
  setup(145500000, fm);
  assert(selectedSlot==0 && receivingSlot==0);
  RADIO_APP_render();
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
  RADIO_APP_render();assert(drawn[1].selected && drawn[1].activity==UI_RADIO_CARD_RECEIVING);
  assert(!drawn[0].signal_valid && drawn[0].activity==UI_RADIO_CARD_IDLE);
  press(KEY_F);press(KEY_2);
  assert(selectedSlot==0 && activeSlot.current.frequency_hz==145500000);
  press(KEY_F);press(KEY_2);
  assert(selectedSlot==1 && activeSlot.current.frequency_hz==438512500);
  // Reboot restores B and its exact parameters, including temporary overrides.
  initialized=false;runtime=(RxRuntime){0};RADIO_APP_init();
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
  RADIO_APP_render();assert(!strcmp(statusText,"RADIO"));
  assert(!strcmp(drawn[0].header_message,"Tune failed") && !drawn[1].header_message);
  // Both applies fail: ownership is cleared and no row claims reception.
  failApply=true;press(KEY_F);press(KEY_2);
  assert(selectedSlot==0 && receivingSlot==UINT8_MAX && !runtime.active);
  RADIO_APP_render();assert(!drawn[0].signal_valid && !drawn[1].signal_valid);
  assert(drawn[0].activity==UI_RADIO_CARD_UNAVAILABLE);
  setup(145500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,7100000,
      (RadioParameters){RX_LSB,1000,3000,0,0}));
  capabilities.ssb_patch=false;oldSaves=saveRequests;press(KEY_F);press(KEY_2);
  assert(selectedSlot==0 && runtime.current.frequency_hz==145500000 && saveRequests==oldSaves);
  assert(!strcmp(message,"SSB patch missing"));
  // RDS labels stay with their VFO/frequency, never the physical parser owner.
  setup(100000000,(RadioParameters){RX_WFM,100000,0,0});
  rdsAvailable=true;RADIO_APP_update();clockNow+=300;RADIO_APP_update();
  clockNow+=100;RADIO_APP_update();clockNow+=100;RADIO_APP_update();
  assert(labels[0].ps[0] && !labels[1].ps[0]);
  clockNow+=15001;RADIO_APP_update();assert(!labels[0].ps[0]);
  rdsAvailable=true;rdsIndex=0;clockNow+=100;RADIO_APP_update();clockNow+=100;RADIO_APP_update();
  assert(labels[0].ps[0]);
  press(KEY_F);press(KEY_2);RADIO_APP_render();
  assert(drawn[0].name && drawn[0].name[0] && (!drawn[1].name || !drawn[1].name[0]));
  assert(!rds.decoder.ps[0] && !drawn[0].signal_valid);
  press(KEY_F);press(KEY_2);request(100100000,activeSlot.current.parameters);
  assert(!labels[0].ps[0]);
  // Input callbacks are bound to their originating VFO.
  setup(145500000,fm);press(KEY_1);assert(inputSlot==0);
  switchSlot();unsigned beforeInput=applied;
  press(KEY_MENU);assert(!number.active && applied==beforeInput && selectedSlot==1);
  // Confirmation/menu/search/held PTT consume F2 without switching.
  setup(145500000,fm);press(KEY_MENU);press(KEY_F);press(KEY_2);
  assert(selectedSlot==0);press(KEY_EXIT);
  confirming=true;press(KEY_F);press(KEY_2);assert(selectedSlot==0);press(KEY_EXIT);
  startSeek();press(KEY_F);press(KEY_2);assert(selectedSlot==0);press(KEY_EXIT);
  transmitter.wait_release=true;press(KEY_F);press(KEY_2);assert(selectedSlot==0);
  transmitter.wait_release=false;
  // TX belongs to B and restores B even though A is also saved.
  setup(145500000,fm);press(KEY_F);press(KEY_2);
  gDeviceConfig.radio.slots[1].tx.enabled=activeSlot.tx.enabled=true;txFakeVerified=true;
  assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  assert(receivingSlot==UINT8_MAX);press(KEY_F);press(KEY_2);assert(selectedSlot==1);
  RADIO_APP_PttRelease();assert(transmitter.mode==RADIO_RX && receivingSlot==1);
  assert(runtime.current.frequency_hz==438500000 && !gDeviceConfig.radio.slots[0].tx.enabled);
  // Missing boot capabilities retain B selection and explicit unavailable state.
  setup(145500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,7100000,
      (RadioParameters){RX_LSB,1000,3000,0,0}));
  gDeviceConfig.radio.selected_slot=1;initialized=false;capabilities.ssb_patch=false;
  RADIO_APP_init();assert(selectedSlot==1 && receivingSlot==UINT8_MAX && !runtime.active);
  assert(activeSlot.current.parameters.mode==RX_LSB);
  // Four saved VFOs, only two visible rows. F2 cycles by enabled count.
  setup(145500000,fm);assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,4));
  press(KEY_F);press(KEY_2);assert(selectedSlot==1);
  press(KEY_F);press(KEY_2);assert(selectedSlot==2);
  assert(RADIO_STATE_VisibleFirst(&gDeviceConfig.radio)==2);
  RADIO_APP_render();assert(drawn[2].selected && drawn[3].id=='D');
  press(KEY_UP);assert(gDeviceConfig.radio.slots[2].current.frequency_hz==145512500);
  assert(gDeviceConfig.radio.slots[0].current.frequency_hz==145500000);
  press(KEY_F);press(KEY_2);assert(selectedSlot==3);
  press(KEY_F);press(KEY_2);assert(selectedSlot==0);
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,3));
  press(KEY_F);press(KEY_2);press(KEY_F);press(KEY_2);
  assert(selectedSlot==2 && RADIO_STATE_VisibleFirst(&gDeviceConfig.radio)==1);
  RADIO_APP_render();assert(drawn[1].id=='B' && drawn[2].selected);
  press(KEY_F);press(KEY_2);assert(selectedSlot==0);
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,1));
  assert(gDeviceConfig.radio.slot_count==1);
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,3));
  assert(RADIO_STATE_SetWatch(&gDeviceConfig.radio,1,true));
  assert(!RADIO_STATE_SetWatch(&gDeviceConfig.radio,2,true));
  assert(RADIO_STATE_SetWatch(&gDeviceConfig.radio,0,false));
  assert(RADIO_STATE_SetWatch(&gDeviceConfig.radio,2,true));
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,2));
  assert(!gDeviceConfig.radio.slots[2].watch); // Hidden state remains, hidden watcher doesn't.
  assert(gDeviceConfig.radio.slots[2].current.frequency_hz==145512500);
  // RX Watch is independent of activeSlot frequency changes, capped at two participants.
  setup(145500000,fm);menuIndex=MENU_RX_WATCH;press(KEY_MENU);press(KEY_MENU);
  assert(!gDeviceConfig.radio.slots[0].watch);press(KEY_EXIT);press(KEY_UP);
  assert(!gDeviceConfig.radio.slots[0].watch);
  // Separate chips keep receiving; audio selection never retunes either chip.
  setup(95900000,(RadioParameters){RX_WFM,100000,0,0,0});
  gDeviceConfig.radio.watch_enabled=true;watchSignalFrequency=95900000;
  watchSecondSignalFrequency=438500000;
  unsigned broadcastSaves=saveRequests;
  for(unsigned i=0;i<15;++i) { RADIO_APP_update();clockNow+=50; }
  assert(watcher.parallel && watcher.phase==WATCH_HOLD && receivingSlot==0 && gReceiver.open);
  assert(concurrentPrepares==1); // SI already tuned; only BK needs preparation.
  unsigned broadcastApplies=applied, broadcastSelects=audioSelects;
  for(unsigned i=0;i<5;++i) { clockNow+=3000;RADIO_APP_update(); }
  assert(receivingSlot==0 && gReceiver.open && applied==broadcastApplies);
  assert(audioSelects==broadcastSelects && concurrentPrepares==1);
  assert(selectedSlot==0 && saveRequests==broadcastSaves);
  // Select B: idle B permits A playback; B's signal then takes audio only.
  watchSecondSignalFrequency=0;
  press(KEY_F);press(KEY_2);assert(selectedSlot==1 && chipStops==2);
  clockNow+=2000;
  for(unsigned i=0;i<15;++i) { RADIO_APP_update();clockNow+=50; }
  assert(watcher.phase==WATCH_HOLD && receivingSlot==0 && gReceiver.open);
  unsigned parallelApplies=applied, parallelPrepares=concurrentPrepares;
  watchSecondSignalFrequency=438500000;
  for(unsigned i=0;i<4;++i) { RADIO_APP_update();clockNow+=50; }
  assert(receivingSlot==1 && gReceiver.open && selectedSlot==1);
  assert(applied==parallelApplies && concurrentPrepares==parallelPrepares);
  clockNow+=10000;RADIO_APP_update();assert(receivingSlot==1 && gReceiver.open);
  // End of B signal: a silent one-second gap, then resume A without retuning.
  watchSecondSignalFrequency=0;clockNow+=50;RADIO_APP_update();assert(!gReceiver.open);
  clockNow+=WATCH_HANG_MS;RADIO_APP_update();assert(receivingSlot==0 && gReceiver.open);
  assert(applied==parallelApplies && concurrentPrepares==parallelPrepares);

  // RDS remains live on SI while selected BK owns the speaker.
  setup(438500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,100000000,
      (RadioParameters){RX_WFM,100000,0,0,0}));
  gDeviceConfig.radio.watch_enabled=true;watchSignalFrequency=438500000;
  watchSecondSignalFrequency=100000000;rdsAvailable=true;
  unsigned parallelSaves=saveRequests;
  for(unsigned i=0;i<40;++i) { RADIO_APP_update();clockNow+=50; }
  assert(receivingSlot==0 && gReceiver.open && labels[1].ps[0] && !labels[0].ps[0]);
  assert(rdsReads && rdsClears);
  assert(saveRequests==parallelSaves && concurrentPrepares==1);
  // PTT stops both background receivers and restores the selected RX plan.
  txFakeVerified=true;activeSlot.tx.enabled=gDeviceConfig.radio.slots[0].tx.enabled=true;
  assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  assert(chipStops==2 && watcher.phase==WATCH_OFF && transmitter.previous_rx.frequency_hz==438500000);
  RADIO_APP_PttRelease();assert(receivingSlot==0 && runtime.current.frequency_hz==438500000);

  // Watch RF visits never commit RX frequencies, selected index or EEPROM saves.
  setup(145500000,fm);gDeviceConfig.radio.watch_enabled=true;
  watchSignalFrequency=438500000;unsigned watchSaves=saveRequests;
  RADIO_APP_update();assert(watcher.phase==WATCH_SETTLE && !RADIO_APP_isReceiving());
  clockNow+=180;RADIO_APP_update();assert(watcher.phase==WATCH_TUNE);
  RADIO_APP_update();assert(receivingSlot==1 && selectedSlot==0);
  clockNow+=180;RADIO_APP_update();assert(watcher.phase==WATCH_VERIFY);
  clockNow+=80;RADIO_APP_update();assert(watcher.phase==WATCH_HOLD && RADIO_APP_isReceiving());
  assert(gReceiver.open);RADIO_APP_render();assert(drawn[1].activity==UI_RADIO_CARD_RECEIVING && !drawn[1].selected);
  assert(drawn[0].selected && !drawn[0].signal_valid);
  assert(saveRequests==watchSaves && activeSlot.current.frequency_hz==145500000);
  assert(gDeviceConfig.radio.slots[1].current.frequency_hz==438500000 && !gDeviceConfig.radio.selected_slot);
  press(KEY_UP);assert(watcher.phase==WATCH_OFF && receivingSlot==0);
  assert(runtime.current.frequency_hz==145512500 && selectedSlot==0);
  unsigned afterManual=applied;clockNow+=1999;RADIO_APP_update();assert(applied==afterManual);
  clockNow++;RADIO_APP_update();assert(watcher.phase==WATCH_SETTLE);
  RADIO_APP_deinit();assert(watcher.phase==WATCH_OFF && receivingSlot==0);
  // PTT preempts background listening and uses A's plan, never B's carrier.
  setup(438500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,438600000,fm));
  gDeviceConfig.radio.watch_enabled=true;watchSignalFrequency=438600000;
  RADIO_APP_update();clockNow+=180;RADIO_APP_update();RADIO_APP_update();
  clockNow+=180;RADIO_APP_update();clockNow+=80;RADIO_APP_update();assert(receivingSlot==1);
  gDeviceConfig.radio.slots[0].tx.enabled=activeSlot.tx.enabled=true;txFakeVerified=true;
  assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  assert(watcher.phase==WATCH_OFF && selectedSlot==0 && transmitter.previous_rx.frequency_hz==438500000);
  RADIO_APP_PttRelease();assert(runtime.current.frequency_hz==438500000 && receivingSlot==0);
  // BCL samples A after restoring from B; cleared audio state is irrelevant.
  for (unsigned scenario=0; scenario<4; ++scenario) {
    setup(438500000,fm);txFakeVerified=true;
    gDeviceConfig.radio.slots[0].tx.enabled=activeSlot.tx.enabled=true;
    assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,438600000,fm));
    gDeviceConfig.radio.watch_enabled=true;watchSignalFrequency=438600000;
    RADIO_APP_update();clockNow+=180;RADIO_APP_update();RADIO_APP_update();
    clockNow+=180;RADIO_APP_update();clockNow+=80;RADIO_APP_update();
    assert(receivingSlot==1 && watcher.phase==WATCH_HOLD);
    gDeviceConfig.preferences.tx_busy_lockout=1;
    watchSignalFrequency=scenario==0 ? 438500000 : 0;
    watchSignalOK=scenario!=2;
    assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED));
    assert(pttPending && receivingSlot==0 && !txFakePrepares);
    // Neither repeated PTT nor other controls may change the pending plan.
    assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED));
    press(KEY_UP);assert(activeSlot.current.frequency_hz==438500000);
    clockNow+=179;RADIO_APP_update();assert(!txFakePrepares);
    if (scenario==3) RADIO_APP_PttRelease();
    clockNow++;RADIO_APP_update();
    if (scenario==1) {
      assert(transmitter.mode==RADIO_TX && txFakePrepares==1);
    } else {
      assert(!txFakePrepares && transmitter.mode==RADIO_RX);
      if (scenario!=3) {
        assert(transmitter.wait_release);
        assert(!strcmp(message,scenario==0 ? "Channel busy" : "Busy check failed"));
      }
    }
    RADIO_APP_PttRelease();
  }
  // An offscreen watch owner is reported without moving selection or paging.
  setup(145500000,fm);RADIO_STATE_SetCount(&gDeviceConfig.radio,4);
  press(KEY_F);press(KEY_2);press(KEY_F);press(KEY_2);assert(selectedSlot==2);
  gDeviceConfig.radio.watch_enabled=true;watchDelay=false;RADIO_APP_update();RADIO_APP_render();
  assert(drawn[2].selected && !drawn[2].signal_valid && !strcmp(statusText,"DW A"));
  // Dedup/unsupported groups stay on the primary, without busy-loop retunes.
  setup(145500000,fm);RADIO_STATE_Set(&gDeviceConfig.radio,1,145500000,fm);
  gDeviceConfig.radio.watch_enabled=true;unsigned noVisits=applied;
  RADIO_APP_update();assert(watcher.phase==WATCH_OFF && applied==noVisits);
  RADIO_APP_render();assert(!strcmp(statusText,"DW A"));
  assert(!strcmp(drawn[0].header_message,"DW: same channel") && !drawn[1].header_message);
  for (unsigned i=0;i<10;++i) { clockNow+=100;RADIO_APP_update(); }
  assert(applied==noVisits);
  // Apply/sample errors back off; even primary restore is attempted only once.
  setup(145500000,fm);gDeviceConfig.radio.watch_enabled=true;failApply=true;
  RADIO_APP_update();RADIO_APP_update();RADIO_APP_update();assert(watcher.phase==WATCH_RETRY);
  unsigned attempts=applyAttempts;
  for(unsigned i=0;i<10;++i) { clockNow+=100;RADIO_APP_update(); }
  assert(applyAttempts==attempts && receivingSlot==UINT8_MAX);
  failApply=false;clockNow+=WATCH_RETRY_MS;RADIO_APP_update();assert(runtime.active);
  // RDS belongs to the actual background WFM receiver, not the selected NFM VFO.
  setup(145500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,100000000,(RadioParameters){RX_WFM,100000,0,0,0}));
  gDeviceConfig.radio.watch_enabled=true;watchSignalFrequency=100000000;rdsAvailable=true;
  RADIO_APP_update();clockNow+=180;RADIO_APP_update();RADIO_APP_update();
  clockNow+=350;RADIO_APP_update();clockNow+=80;RADIO_APP_update();
  assert(receivingSlot==1 && selectedSlot==0 && watcher.phase==WATCH_HOLD);
  clockNow+=300;RADIO_APP_update();clockNow+=100;RADIO_APP_update();clockNow+=100;RADIO_APP_update();
  assert(!labels[0].ps[0] && labels[1].ps[0]);
  RADIO_APP_render();assert(drawn[1].name && drawn[1].name[0] && !drawn[0].name[0]);
  // Reducing count in Settings changes selection on return, preserving hidden VFO data.
  setup(145500000,fm);RADIO_STATE_SetCount(&gDeviceConfig.radio,4);
  press(KEY_F);press(KEY_2);press(KEY_F);press(KEY_2);assert(selectedSlot==2);
  RADIO_APP_deinit();assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,2));RADIO_APP_init();
  assert(selectedSlot==0 && activeSlot.current.frequency_hz==145500000);
  // Draft input keeps RX/storage unchanged, owns PTT and pauses DW indefinitely.
  setup(438500000,fm);
  gDeviceConfig.radio.watch_enabled=true;
  unsigned beforeApply=applied, beforeSave=saveRequests;
  press(KEY_4);press(KEY_3);press(KEY_8);press(KEY_STAR);press(KEY_6);
  assert(number.active && !strcmp(number.text,"438.6") && appRuns==0);
  assert(activeSlot.current.frequency_hz==438500000 && applied==beforeApply && saveRequests==beforeSave);
  assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED));press(KEY_PTT);
  assert(number.active && !pttPending && transmitter.mode==RADIO_RX && !txFakePrepares);
  press(KEY_F);press(KEY_2);press(KEY_SIDE1);press(KEY_SIDE2);press(KEY_UP);
  assert(selectedSlot==0 && number.active && !functionPending && applied==beforeApply);
  clockNow+=5000;RADIO_APP_update();assert(watcher.phase==WATCH_OFF);
  RADIO_APP_render();assert(drawn[0].edit_text && !drawn[1].edit_text);
  assert(RADIO_APP_key(KEY_EXIT,KEY_LONG_PRESSED));
  assert(!number.active && applied==beforeApply && saveRequests==beforeSave);
  press(KEY_4);press(KEY_3);press(KEY_8);press(KEY_STAR);press(KEY_6);press(KEY_MENU);
  assert(!number.active && activeSlot.current.frequency_hz==438600000 && applied==beforeApply+1);
  press(KEY_0);press(KEY_MENU);assert(number.active && number.invalid);
  RADIO_APP_deinit();assert(!number.active);
  // Lock PTT too only applies while the keyboard is locked.
  setup(438500000,fm);
  gDeviceConfig.radio.slots[0].tx.enabled=activeSlot.tx.enabled=true;
  txFakeVerified=true;
  gDeviceConfig.preferences.ptt_lock=true;
  assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  RADIO_APP_PttRelease();
  gDeviceConfig.preferences.keylock=true;
  assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_RX);
  assert(txFakePrepares==1);
  // Applying global receive settings does not save or replace VFO parameters.
  unsigned savedBeforeRefresh=saveRequests;
  RadioSlot vfoBeforeRefresh=gDeviceConfig.radio.slots[0];
  assert(RADIO_APP_ApplyRxSettings());
  assert(saveRequests==savedBeforeRefresh);
  assert(!memcmp(&vfoBeforeRefresh,&gDeviceConfig.radio.slots[0],sizeof(vfoBeforeRefresh)));
  // Physical release and battery faults stop TX before queued UI events.
  setup(438500000,fm);txFakeVerified=true;
  activeSlot.tx.enabled=gDeviceConfig.radio.slots[0].tx.enabled=true;
  assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  txFakePttPressed=false;RADIO_APP_TxSafetyUpdate();
  assert(transmitter.mode==RADIO_RX && !pttPending && receivingSlot==selectedSlot);
  txFakePttPressed=true;
  assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  txFakeVoltageValid=false;RADIO_APP_TxSafetyUpdate();
  assert(transmitter.mode==RADIO_RX && transmitter.wait_release);
  assert(!strcmp(message,"TX battery voltage"));
  RADIO_APP_render();assert(!strcmp(drawn[0].header_message,"TX battery voltage"));
  press(KEY_PTT);

  // An inexact target still requires the existing hardware quantization confirmation.
  setup(100000000,(RadioParameters){RX_WFM,100000,0,0});
  press(KEY_9);press(KEY_5);press(KEY_STAR);press(KEY_9);press(KEY_2);press(KEY_5);press(KEY_MENU);
  assert(!number.active && confirming && activeSlot.current.frequency_hz==100000000);
  press(KEY_EXIT);assert(!confirming && activeSlot.current.frequency_hz==100000000);
  // F4 binds capture, not numeric input. Detection and preview own PTT/UI.
  setup(438500000,fm);
  RadioSlot fcOrigin=gDeviceConfig.radio.slots[0];
  unsigned fcSaves=saveRequests;
  press(KEY_F);press(KEY_4);
  assert(capturePhase==FC_DETECT && !number.active && !functionPending);
  assert(!RADIO_APP_isReceiving() && receivingSlot==UINT8_MAX && captureStarts==1);
  assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED));press(KEY_PTT);
  press(KEY_F);press(KEY_2);press(KEY_MENU); // Cancel while detecting.
  assert(capturePhase==FC_OFF && selectedSlot==0 && !txFakePrepares);
  assert(!memcmp(&fcOrigin,&gDeviceConfig.radio.slots[0],sizeof(fcOrigin)) && saveRequests==fcSaves);
  press(KEY_F);press(KEY_4);
  clockNow+=500;RADIO_APP_update();clockNow+=500;RADIO_APP_update();
  assert(capture.range==CAPTURE_RANGE_UHF); // No-results Auto still advances.
  captureReply=CAPTURE_SAMPLE;captureHz=438512340;
  clockNow+=20;RADIO_APP_update();assert(capturePhase==FC_DETECT);
  captureHz+=3000;clockNow+=20;RADIO_APP_update();
  assert(capturePhase==FC_PREVIEW && RADIO_APP_isReceiving());
  assert(runtime.current.frequency_hz==438500000 && activeSlot.current.frequency_hz==438500000);
  assert(saveRequests==fcSaves && gReceiver.plan.frequency_hz==captureHz);
  RADIO_APP_render();assert(drawn[0].frequency_hz==captureHz && drawn[0].mode==RX_NFM);
  assert(!drawn[0].name && !drawn[0].rds);
  assert(RADIO_APP_key(KEY_PTT,KEY_PRESSED));press(KEY_PTT);assert(!txFakePrepares);
  press(KEY_EXIT);assert(capturePhase==FC_OFF && gReceiver.plan.frequency_hz==438500000);
  assert(saveRequests==fcSaves && !memcmp(&fcOrigin,&gDeviceConfig.radio.slots[0],sizeof(fcOrigin)));
  // Preview adoption affects only the bound VFO and requests one save.
  press(KEY_F);press(KEY_4);press(KEY_SIDE1);press(KEY_SIDE1); // UHF
  assert(capture.scope==CAPTURE_UHF && capture.range==CAPTURE_RANGE_UHF);
  clockNow+=20;RADIO_APP_update();clockNow+=20;RADIO_APP_update();
  assert(capturePhase==FC_PREVIEW);
  gDeviceConfig.radio.slots[0].tx.enabled=activeSlot.tx.enabled=true;
  RadioSlot other=gDeviceConfig.radio.slots[1];
  press(KEY_MENU);
  assert(capturePhase==FC_OFF && runtime.current.frequency_hz==captureHz);
  assert(activeSlot.current.parameters.mode==RX_NFM && !activeSlot.current.parameters.rx_tone && !activeSlot.tx.enabled);
  assert(saveRequests==fcSaves+1 && !memcmp(&other,&gDeviceConfig.radio.slots[1],sizeof(other)));
  // Timeout, application exit and same-frequency preview restore without writes.
  setup(145500000,fm);fcSaves=saveRequests;
  press(KEY_F);press(KEY_4);clockNow+=15000;RADIO_APP_update();
  assert(capturePhase==FC_OFF && saveRequests==fcSaves && gReceiver.plan.frequency_hz==145500000);
  RADIO_APP_render();assert(!strcmp(drawn[0].header_message,"FC timeout") && !drawn[1].header_message);
  assert(drawn[0].frequency_hz==145500000);
  press(KEY_F);press(KEY_4);captureReply=CAPTURE_SAMPLE;captureHz=145500000;
  clockNow+=20;RADIO_APP_update();clockNow+=20;RADIO_APP_update();
  assert(capturePhase==FC_PREVIEW);RADIO_APP_deinit();
  assert(capturePhase==FC_OFF && saveRequests==fcSaves);
  // SI/BK1080 origin remains committed through preview; cancel restores it.
  for (unsigned chip=0;chip<2;++chip) {
    setup(95900000,(RadioParameters){RX_WFM,100000,0,0});
    if (chip) {
      capabilities=(ReceiverCapabilities){false,true,false};
      request(95900000,activeSlot.current.parameters);
    }
    fcOrigin=gDeviceConfig.radio.slots[0];fcSaves=saveRequests;
    press(KEY_F);press(KEY_4);captureReply=CAPTURE_SAMPLE;captureHz=145500000;
    clockNow+=20;RADIO_APP_update();clockNow+=20;RADIO_APP_update();
    assert(capturePhase==FC_PREVIEW && gReceiver.plan.device == RX_DEVICE_BK);
    unsigned reads=rdsReads;clockNow+=100;RADIO_APP_update();assert(rdsReads==reads);
    press(KEY_EXIT);
    assert(gReceiver.plan.device==(chip ? RX_DEVICE_BROADCAST : RX_DEVICE_SI) && gReceiver.plan.frequency_hz==95900000);
    assert(saveRequests==fcSaves && !memcmp(&fcOrigin,&gDeviceConfig.radio.slots[0],sizeof(fcOrigin)));
  }
  // Unsupported candidate cannot preview; failed preview and failed restore mute.
  setup(145500000,fm);fcSaves=saveRequests;
  press(KEY_F);press(KEY_4);captureReply=CAPTURE_SAMPLE;captureHz=700000000;
  clockNow+=20;RADIO_APP_update();clockNow+=20;RADIO_APP_update();assert(capturePhase==FC_DETECT);
  press(KEY_SIDE1);captureHz=145500000;failFrequency=captureHz;
  clockNow+=20;RADIO_APP_update();clockNow+=20;RADIO_APP_update();
  assert(capturePhase==FC_OFF && !runtime.active && receivingSlot==UINT8_MAX && saveRequests==fcSaves);
  // Final adoption failure restores the origin and never commits the candidate.
  setup(145500000,fm);fcSaves=saveRequests;
  press(KEY_F);press(KEY_4);captureReply=CAPTURE_SAMPLE;captureHz=145600000;
  clockNow+=20;RADIO_APP_update();clockNow+=20;RADIO_APP_update();
  assert(capturePhase==FC_PREVIEW);failFrequency=captureHz;press(KEY_MENU);
  assert(capturePhase==FC_OFF && runtime.active && runtime.current.frequency_hz==145500000);
  assert(gDeviceConfig.radio.slots[0].current.frequency_hz==145500000 && saveRequests==fcSaves);
  // A restart from preview forgets prior hits while retaining the committed origin.
  failFrequency=0;press(KEY_F);press(KEY_4);
  clockNow+=20;RADIO_APP_update();clockNow+=20;RADIO_APP_update();assert(capturePhase==FC_PREVIEW);
  press(KEY_SIDE2);assert(capturePhase==FC_DETECT && capture.scope==CAPTURE_VHF && !capture.hits);
  press(KEY_EXIT);assert(runtime.current.frequency_hz==145500000 && saveRequests==fcSaves);
  // Hidden VFOs can be the bound owner as well; F2 is swallowed until adoption.
  setup(145500000,fm);
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,4));
  for (unsigned id=1;id<4;++id) {
    press(KEY_F);press(KEY_2);assert(selectedSlot==id);
    RadioSlot untouched=gDeviceConfig.radio.slots[0];fcSaves=saveRequests;
    press(KEY_F);press(KEY_4);captureReply=CAPTURE_SAMPLE;captureHz=145600000+id*10000u;
    clockNow+=20;RADIO_APP_update();clockNow+=20;RADIO_APP_update();assert(capturePhase==FC_PREVIEW);
    press(KEY_F);press(KEY_2);assert(selectedSlot==id);
    press(KEY_MENU);
    assert(gDeviceConfig.radio.slots[id].current.frequency_hz==captureHz && saveRequests==fcSaves+1);
    assert(!memcmp(&untouched,&gDeviceConfig.radio.slots[0],sizeof(untouched)));
  }
  // MR and input retain their existing controls; F4 never starts capture there.
  setup(145500000,fm);activeSlot.source=RADIO_SOURCE_CHANNEL;
  press(KEY_F);press(KEY_4);assert(capturePhase==FC_OFF && !captureStarts);
  return 0;
}

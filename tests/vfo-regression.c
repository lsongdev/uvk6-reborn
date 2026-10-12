// Exercise real main-screen state transitions; only hardware/UI plumbing is mocked.
#include "../src/apps/vfo.c"
#include <assert.h>

RadioState state;
RadioState *gRadioState = &state;
RadioState *vfo = &state;
RadioReceiver *ctx = &state.context;
bool gRedrawScreen;
static char statusText[32];
void STATUSLINE_SetText(const char *pattern, ...) {
  va_list args; va_start(args, pattern);
  vsnprintf(statusText, sizeof(statusText), pattern, args); va_end(args);
}
static UI_VfoView drawn[RADIO_VFO_CAPACITY];
static unsigned drawCount;
void UI_VfoRender(uint8_t y, const UI_VfoView *view, UI_VfoState *state, uint32_t now) {
  unsigned id = view->id-'A';
  assert(id<RADIO_VFO_CAPACITY && y==8+(id-RADIO_STATE_VisibleFirst(&gDeviceConfig.radio))*UI_VFO_HEIGHT && state==&cards[id]);
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
bool gMonitorMode;
uint32_t gFInputValue1, gFInputValue2;
void (*gFInputCallback)(uint32_t, uint32_t);
static ReceiverCapabilities capabilities = {true, false, true};
static RadioParameters savedParameters;
static unsigned applied, applyAttempts;
static unsigned routed;
static unsigned appRuns;
static bool failApply;
Settings gSettings;
static bool txFakeVerified, txFakePrepareOK, txFakeEnableOK;
static unsigned txFakePrepares, txFakeEnables, txFakeDisables;
// These capabilities exist exclusively in the host test. Production HAL
// returns unverified, and cannot energize the PA.
TxCapabilities TX_DEVICE_Capabilities(void) {
  return (TxCapabilities){txFakeVerified, txFakeVerified};
}
TxPolicy TX_DEVICE_Policy(void) {
  return (TxPolicy){txFakeVerified, 430000000u, 440000000u};
}
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
  (void)dev; *found = ctx->frequency*10u == soughtFrequency; return true;
}
void UI_VfoShowFrequency(UI_VfoState *state, uint32_t now) { state->tuned_at = now; }
bool UI_VfoUpdate(UI_VfoState *state, uint32_t now) { (void)state; (void)now; return false; }
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
ReceiverCapabilities RADIO_GetReceiverCapabilities(void) { return capabilities; }
RadioParameters RADIO_ReadParameters(const RadioReceiver *context) {
  (void)context;
  return savedParameters;
}
bool RADIO_ApplyRxPlan(RadioReceiver *context, const RxPlan *plan) {
  ++applyAttempts;
  if (failApply || (failFrequency && plan->frequency_hz == failFrequency))
    return false;
  ++applied;
  context->frequency = plan->frequency_hz/10;
  context->radio_type = plan->device == RX_DEVICE_BK ? RADIO_BK4819
                      : plan->device == RX_DEVICE_SI ? RADIO_SI4732 : RADIO_BK1080;
  return true;
}
static uint32_t watchSignalFrequency;
static bool watchSignalOK=true;
bool RX_DEVICE_WatchSignal(const RxPlan *plan, bool *found) {
  *found=plan->frequency_hz==watchSignalFrequency; return watchSignalOK;
}
void RADIO_SetAudioOpen(bool open) { (void)open; }
void RADIO_StopReception(void) {
  vfo->is_open = false;
  ++routed;
}
void APPS_run(AppType_t app) { (void)app; ++appRuns; }
bool APPS_key(KEY_Code_t key, Key_State_t keyState) { (void)key; (void)keyState; return true; }
void FINPUT_setup(uint32_t min, uint32_t max, InputUnit unit, bool range) {
  (void)min; (void)max; (void)unit; (void)range;
}

static void setup(uint32_t frequency, RadioParameters values) {
  clockNow=100;
  rdsReads=rdsClears=rdsIndex=0;
  rdsAvailable=false;
  watchSignalFrequency=0;watchSignalOK=true;
  initialized = changed = inMenu = confirming = false;
  WATCH_Stop(&watcher); watchDelay=false; watchResumeAt=0;
  functionPending = false;
  selectedVfo = inputVfo = 0;
  receivingVfo = UINT8_MAX;
  memset(cards, 0, sizeof(cards));
  memset(labels, 0, sizeof(labels));
  transmitter = (TxRuntime){0};
  seek = (Seek){0};
  runtime = (RxRuntime){0};
  message = NULL;
  RADIO_STATE_Default(&gDeviceConfig.radio);
  gDeviceConfig.radio.vfos[0].tx = (TxConfig){0};
  gDeviceConfig.preferences.tx_timeout_seconds = 120;
  gDeviceConfig.preferences.tx_busy_lockout = 0;
  gMonitorMode = false;
  gSettings.pttLock = false;
  txFakeVerified = false;
  txFakePrepareOK = txFakeEnableOK = true;
  txFakePrepares = txFakeEnables = txFakeDisables = 0;
  vfo->is_open = false;
  assert(RADIO_STATE_Set(&gDeviceConfig.radio, 0, frequency, values));
  *ctx = (RadioReceiver){.frequency = frequency/10};
  savedParameters = values;
  applied = routed = appRuns = saveRequests = applyAttempts = 0;
  failApply = false;
  failFrequency = 0;
  soughtFrequency = 0;
  nativeFrequency = 100100000;
  nativeReply = RX_SEEK_FOUND;
  nativeStarts = nativeCancels = 0;
  capabilities = (ReceiverCapabilities){true, false, true};
  VFO_init();
}
static void press(KEY_Code_t key) { assert(VFO_key(key, KEY_RELEASED)); }

int main(void) {
  RadioParameters fm = {RX_NFM, 12500, 12000, 4};
  setup(438500000, fm);
  press(KEY_F);press(KEY_0);
  assert(!functionPending && !confirming && appRuns == 0 && applied == 2);
  assert(user.current.frequency_hz == 438500000 && user.current.parameters.mode == RX_AM);
  press(KEY_F);press(KEY_0);
  assert(user.current.parameters.mode == RX_NFM);
  press(KEY_0);assert(appRuns == 1); // Plain 0 still enters frequency input.

  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  press(KEY_F);press(KEY_0);
  assert(!confirming && user.current.parameters.mode == RX_AM && ctx->radio_type == RADIO_BK4819);
  assert(user.current.frequency_hz == 100000000);

  setup(7100000, (RadioParameters){RX_AM,5000,6000,0});
  capabilities.ssb_patch = false;
  press(KEY_F);press(KEY_0);
  assert(!functionPending && user.current.parameters.mode == RX_AM);
  assert(!strcmp(message,"No other mode") && appRuns == 0);

  setup(438500000, fm);
  assert(VFO_key(KEY_F, KEY_PRESSED) == false);
  press(KEY_F);
  assert(functionPending && user.active_band == 6);
  assert(VFO_key(KEY_1, KEY_PRESSED));
  assert(applied == 1); // Execute on release, once.
  press(KEY_1);
  assert(!functionPending && user.active_band == 0 && appRuns == 0);
  assert(user.current.frequency_hz == 225000 && user.current.parameters.mode == RX_AM);
  press(KEY_1);
  assert(appRuns == 1); // The prefix is consumed; plain 1 enters frequency input.

  setup(438500000, fm);
  gMonitorMode = true;
  press(KEY_F);press(KEY_EXIT);
  assert(!functionPending && gMonitorMode && applied == 1);
  press(KEY_F);press(KEY_F);
  assert(!functionPending && applied == 1);
  press(KEY_F);press(KEY_3);
  assert(!functionPending && !strcmp(message,"Not supported") && appRuns == 0 && applied == 1);

  press(KEY_F);
  clockNow = functionAt+2999;VFO_update();assert(functionPending);
  clockNow++;VFO_update();assert(!functionPending);
  press(KEY_1);assert(appRuns == 1);
  press(KEY_F);clockNow += 3000; // Timeout is also checked before key dispatch.
  press(KEY_1);assert(!functionPending && appRuns == 2 && applied == 1);
  clockNow = UINT32_MAX-1000;
  press(KEY_F);clockNow += 2999;VFO_update();assert(functionPending);
  clockNow++;VFO_update();assert(!functionPending);

  setup(438500000, fm);
  press(KEY_F);assert(VFO_key(KEY_PTT,KEY_PRESSED));assert(!functionPending);
  press(KEY_F);assert(!VFO_key(KEY_MENU,KEY_LONG_PRESSED));
  assert(!functionPending && !inMenu);
  press(KEY_F);VFO_cancelShortcut();assert(!functionPending); // System lock transition.
  press(KEY_F);VFO_deinit();assert(!functionPending);
  press(KEY_F);VFO_init();assert(!functionPending); // Returning from another app.
  press(KEY_MENU);assert(inMenu);
  press(KEY_F);assert(!functionPending && appRuns == 0); // Local menu adjustment.
  press(KEY_EXIT);press(KEY_SIDE1);assert(user.active_band == 0);

  setup(438500000, fm);
  gDeviceConfig.radio.presets[0] = (RadioParameters){RX_AM,5000,3000,2};
  press(KEY_F);press(KEY_1);
  assert(user.current.parameters.step_hz == 5000 && user.current.parameters.bandwidth_hz == 3000);
  assert(user.current.parameters.squelch == 2 && saveRequests == 2);

  setup(438500000, fm);
  assert(runtime.active && applied == 1 && user.active_band == 6);
  press(KEY_SIDE1);
  assert(user.current.frequency_hz == 225000 && user.current.parameters.mode == RX_AM && user.active_band == 0);
  press(KEY_SIDE1);
  assert(user.current.frequency_hz == 999000 && user.active_band == 1);

  // The same 100 MHz carrier must use BK for NFM and SI for WFM.
  setup(100000000, (RadioParameters){RX_NFM, 12500, 12000, 0});
  assert(runtime.active && ctx->radio_type == RADIO_BK4819);
  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0});
  assert(runtime.active && ctx->radio_type == RADIO_SI4732);
  VFO_update();assert(!rdsReads);
  clockNow=400;VFO_update();assert(rdsClears==1);
  rdsAvailable=true;
  clockNow=500;VFO_update();assert(rdsIndex==4 && !rds.decoder.ps[0]);
  gRedrawScreen=false;clockNow=600;VFO_update();
  assert(!strcmp(rds.decoder.ps,"AAAAAAAA") && gRedrawScreen);
  unsigned readsBefore=rdsReads;
  press(KEY_SIDE2);assert(!rds.decoder.ps[0]);
  clockNow=1000;VFO_update();assert(rdsReads==readsBefore); // NFM doesn't poll SI RDS.

  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0});
  strcpy(rds.decoder.ps,"OLD FM");
  failFrequency=100100000;press(KEY_UP);
  assert(runtime.active && user.current.frequency_hz==100000000 && !rds.decoder.ps[0]);

  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0});
  capabilities.si=false;capabilities.broadcast=true;request(100000000,user.current.parameters);
  assert(runtime.current.device==RX_DEVICE_BROADCAST);
  clockNow=500;VFO_update();assert(!rdsReads); // BK1080 has no RDS path.

  setup(438500000, fm);
  press(KEY_SIDE2);
  assert(runtime.active && !confirming && user.current.parameters.mode == RX_AM);
  assert(user.current.frequency_hz == 438500000 && changed);
  press(KEY_SIDE2);
  assert(user.current.parameters.mode == RX_NFM && user.current.frequency_hz == 438500000);

  setup(7100000, (RadioParameters){RX_AM, 5000, 6000, 0});
  unsigned before = applied;
  press(KEY_SIDE2);
  assert(!confirming && user.current.parameters.mode == RX_LSB);
  assert(user.current.parameters.bandwidth_hz == 3000 && applied == before+1);
  assert(user.current.frequency_hz == 7100000);

  setup(7100000, (RadioParameters){RX_AM, 5000, 6000, 0});
  capabilities.ssb_patch = false;
  press(KEY_SIDE2);
  assert(!confirming && user.current.parameters.mode == RX_AM);

  setup(438500000, fm);
  tuneTo(9592500, 0);
  assert(confirming && pending.frequency_hz == 95930000);
  assert(user.current.frequency_hz == 438500000);
  press(KEY_EXIT);
  assert(user.current.frequency_hz == 438500000 && runtime.active);
  tuneTo(9592500, 0);
  press(KEY_MENU);
  assert(user.current.frequency_hz == 95930000 && user.current.parameters.mode == RX_WFM && ctx->radio_type == RADIO_SI4732);
  press(KEY_SIDE2);
  assert(!confirming && user.current.parameters.mode == RX_AM);
  assert(user.current.frequency_hz == 95930000 && ctx->radio_type == RADIO_BK4819);
  assert(user.current.parameters.bandwidth_hz == 6000);
  press(KEY_SIDE2);
  assert(!confirming && user.current.parameters.mode == RX_NFM);
  assert(user.current.frequency_hz == 95930000 && ctx->radio_type == RADIO_BK4819);

  // WFM to NFM also applies immediately when SSB modes are unavailable.
  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0});
  capabilities.ssb_patch = false;
  press(KEY_SIDE2);
  assert(!confirming && user.current.parameters.mode == RX_AM);
  press(KEY_SIDE2);
  assert(!confirming && user.current.parameters.mode == RX_NFM);
  assert(user.current.frequency_hz == 100000000 && ctx->radio_type == RADIO_BK4819);

  setup(147987500, fm);
  press(KEY_UP);
  assert(user.current.frequency_hz == 148000000 && user.active_band == 7 && user.current.parameters.step_hz == 25000);
  assert(ctx->radio_type == RADIO_BK4819);

  setup(438500000, fm);
  capabilities = (ReceiverCapabilities){false, false, false};
  press(KEY_SIDE1);
  assert(!runtime.active && user.current.frequency_hz == 225000 && routed == 1);
  assert(ctx->frequency == 43850000); // Unsupported target was not committed.
  press(KEY_UP);
  assert(user.current.frequency_hz == 226000 && !runtime.active);
  press(KEY_SIDE1); // MW has no receiver.
  press(KEY_SIDE1); // SW has no receiver.
  press(KEY_SIDE1); // Broadcast has no receiver.
  press(KEY_SIDE1); // Airband is available on BK.
  assert(runtime.active && user.current.frequency_hz == 125000000 && user.current.parameters.mode == RX_AM);

  setup(438500000, fm);
  press(KEY_MENU);
  assert(inMenu);
  press(KEY_SIDE1);
  press(KEY_SIDE2);
  assert(user.current.frequency_hz == 438500000 && applied == 1);
  press(KEY_EXIT);
  assert(!inMenu && appRuns == 0);
  // A saved preset is applied on band entry; live overrides never replace it.
  setup(438500000, fm);
  gDeviceConfig.radio.presets[6] = (RadioParameters){RX_AM,25000,6000,2};
  press(KEY_SIDE2); // Temporary AM, preserving the edited preset fields.
  assert(gDeviceConfig.radio.presets[6].step_hz == 25000);
  press(KEY_SIDE1); // Leave 70cm for LW.
  tuneTo(43850000, 0); // Enter 70cm again.
  assert(user.current.parameters.mode == RX_AM && user.current.parameters.step_hz == 25000);
  assert(user.current.parameters.squelch == 2 && !changed);
  press(KEY_SIDE2); // Temporary NFM override.
  assert(gDeviceConfig.radio.presets[6].mode == RX_AM);
  setup(438500000, fm);
  // A transient failed tune restores the last working receiver.
  failFrequency = 438512500;
  press(KEY_UP);
  assert(runtime.active && user.current.frequency_hz == 438500000 && applied == 2 && routed == 0);
  failFrequency = 0;
  failApply = true;
  press(KEY_UP);
  assert(user.current.frequency_hz == 438500000 && user.current.parameters.mode == RX_NFM && applied == 2);
  assert(!runtime.active && !vfo->is_open && routed == 1);

  // F,* starts forward-only BK software seek. Intermediate tunes are not
  // persisted, and a stable carrier commits exactly once.
  setup(438500000, fm);
  unsigned savedBeforeSeek = saveRequests;
  soughtFrequency = 438512500;
  press(KEY_F);press(KEY_STAR);
  assert(seek.active && !seekNative && !functionPending);
  assert(user.current.frequency_hz == 438500000);
  VFO_update();
  assert(seek.active && ctx->frequency == 43851250 &&
         saveRequests == savedBeforeSeek);
  clockNow += 185;
  VFO_update(); // First squelch sample.
  assert(seek.active && saveRequests == savedBeforeSeek);
  clockNow += 85;
  VFO_update(); // Stable second sample.
  assert(!seek.active && user.current.frequency_hz == 438512500);
  assert(saveRequests == savedBeforeSeek + 1);

  // EXIT cancels without persisting and restores the exact original plan.
  setup(438500000, fm);
  savedBeforeSeek = saveRequests;
  press(KEY_F);press(KEY_STAR);
  VFO_update();
  press(KEY_EXIT);
  assert(!seek.active && runtime.active &&
         user.current.frequency_hz == 438500000 &&
         ctx->frequency == 43850000 && saveRequests == savedBeforeSeek);
  assert(VFO_key(KEY_UP, KEY_PRESSED) == false); // Manual tuning unchanged.

  // Hardware seek must not overwrite RadioUserState until the final tune.
  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  savedBeforeSeek = saveRequests;
  press(KEY_F);press(KEY_STAR);
  assert(seek.active && seekNative && nativeStarts == 0);
  VFO_update();
  assert(nativeStarts == 1 && saveRequests == savedBeforeSeek);
  clockNow += 100;VFO_update();
  clockNow += 100;VFO_update();
  assert(!seek.active && user.current.frequency_hz == 100100000);
  assert(saveRequests == savedBeforeSeek+1 && nativeCancels == 1);

  // No signal: reach hardware band end, wrap, reach the origin, stop.
  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  savedBeforeSeek = saveRequests;
  nativeReply = RX_SEEK_LIMIT;
  press(KEY_F);press(KEY_STAR);
  VFO_update();
  clockNow += 100;VFO_update();
  clockNow += 100;VFO_update();
  assert(seek.active && seek.wrapped && ctx->frequency == 6400000);
  assert(saveRequests == savedBeforeSeek);
  clockNow += 190;VFO_update(); // Check the band start once.
  clockNow += 10;VFO_update(); // Continue native seek from the band start.
  clockNow += 100;VFO_update();
  clockNow += 100;VFO_update();
  assert(!seek.active && user.current.frequency_hz == 100000000);
  assert(ctx->frequency == 10000000 && saveRequests == savedBeforeSeek);

  // The single VFO owns TX intent, never a per-band transmit flag.
  setup(438500000, fm);
  assert(!gDeviceConfig.radio.vfos[0].tx.enabled);
  assert(VFO_key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX disabled"));
  press(KEY_MENU);
  for (unsigned i=0;i<5;++i) press(KEY_DOWN);
  assert(menuIndex == 5);
  unsigned savedBeforeTX = saveRequests;
  press(KEY_MENU);
  assert(gDeviceConfig.radio.vfos[0].tx.enabled && saveRequests == savedBeforeTX+1);
  press(KEY_DOWN);
  press(KEY_MENU);
  assert(gDeviceConfig.radio.vfos[0].tx.power == TX_POWER_MEDIUM);
  assert(saveRequests == savedBeforeTX+2);
  press(KEY_F);
  assert(gDeviceConfig.radio.vfos[0].tx.power == TX_POWER_LOW);
  press(KEY_EXIT);
  assert(!inMenu);
  assert(VFO_key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX HW locked"));
  assert(transmitter.mode == RADIO_RX);
  VFO_PttRelease();
  assert(transmitter.mode == RADIO_RX);
  // RX mode is still authoritative, even when a TX request is stored.
  setup(100000000, (RadioParameters){RX_WFM,100000,0,0});
  gDeviceConfig.radio.vfos[0].tx.enabled = true;
  assert(VFO_key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX mode not supported"));
  assert(transmitter.mode == RADIO_RX);
  VFO_PttRelease();

  // Busy-channel lockout remains active even when Monitor is on.
  setup(438500000, fm);
  gDeviceConfig.radio.vfos[0].tx.enabled = true;
  gDeviceConfig.preferences.tx_busy_lockout = 1;
  txFakeVerified = true;
  vfo->is_open = true;
  gMonitorMode = true;
  assert(VFO_key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "Channel busy"));
  assert(txFakePrepares == 0 && transmitter.mode == RADIO_RX);
  VFO_PttRelease();

  // Exercise the full one-VFO PTT lifecycle using a host-only fake PA:
  // no EEPROM mutation, timeout closes PA, RX restores, release re-arms.
  setup(438500000, fm);
  gDeviceConfig.radio.vfos[0].tx.enabled = true;
  gDeviceConfig.preferences.tx_timeout_seconds = 30;
  txFakeVerified = true;
  unsigned savesBeforePTT = saveRequests;
  unsigned appliedBeforePTT = applied;
  assert(VFO_key(KEY_PTT, KEY_PRESSED));
  assert(transmitter.mode == RADIO_TX);
  assert(txFakePrepares == 1 && txFakeEnables == 1 &&
         txFakeDisables == 1 && saveRequests == savesBeforePTT);
  clockNow += 30000u;
  VFO_TxSafetyUpdate();
  assert(transmitter.mode == RADIO_RX && transmitter.wait_release);
  assert(txFakeDisables == 2 && applied == appliedBeforePTT + 1);
  assert(saveRequests == savesBeforePTT && user.current.frequency_hz == 438500000);
  assert(VFO_key(KEY_PTT, KEY_PRESSED));
  assert(txFakeEnables == 1); // TOT latch: no auto-rekey while held.
  VFO_PttRelease();
  assert(!transmitter.wait_release);
  assert(VFO_key(KEY_PTT, KEY_PRESSED) && transmitter.mode == RADIO_TX);
  VFO_PttRelease();
  assert(transmitter.mode == RADIO_RX);

  // Prepare failure must never call enable, and requires a fresh PTT edge.
  setup(438500000, fm);
  gDeviceConfig.radio.vfos[0].tx.enabled = true;
  txFakeVerified = true;
  txFakePrepareOK = false;
  assert(VFO_key(KEY_PTT, KEY_PRESSED));
  assert(!strcmp(message, "TX hardware error"));
  assert(transmitter.mode == RADIO_RX && transmitter.wait_release);
  assert(txFakePrepares == 1 && txFakeEnables == 0 && txFakeDisables == 2);
  VFO_PttRelease();
  assert(!transmitter.wait_release);

  // Failed RX restoration must latch a permanent fault with PA disabled.
  setup(438500000, fm);
  gDeviceConfig.radio.vfos[0].tx.enabled = true;
  txFakeVerified = true;
  assert(VFO_key(KEY_PTT, KEY_PRESSED) && transmitter.mode == RADIO_TX);
  failApply = true;
  VFO_PttRelease();
  assert(transmitter.mode == RADIO_FAULT && txFakeDisables >= 2);
  assert(!runtime.active);
  assert(VFO_key(KEY_PTT, KEY_PRESSED));
  assert(txFakeEnables == 1); // No retry from a faulted radio.

  // SSB does not gain an unreliable RSSI-only auto-search path.
  setup(7100000, (RadioParameters){RX_USB,5000,3000,0});
  press(KEY_F);press(KEY_STAR);
  assert(!seek.active && !strcmp(message,"SSB seek off"));
  // Tone controls replace Monitor without changing its runtime flag.
  setup(438500000,fm);menuIndex=0;press(KEY_MENU);
  for(unsigned i=0;i<4;++i) press(KEY_DOWN);
  assert(menuIndex==4);
  press(KEY_MENU);
  assert(user.current.parameters.rx_tone==1 && runtime.current.parameters.rx_tone==1);
  assert(!gMonitorMode);
  press(KEY_F);assert(user.current.parameters.rx_tone==0);
  for(unsigned i=0;i<3;++i) press(KEY_DOWN);
  assert(menuIndex==7);
  unsigned appliesBefore=applied;
  press(KEY_MENU);
  assert(appRuns==1 && gFInputCallback==setTxFrequency);
  setTxFrequency(43860000,0);
  assert(gDeviceConfig.radio.vfos[0].tx.frequency_hz==438600000 && applied==appliesBefore);
  assert(user.current.frequency_hz==438500000);
  setTxFrequency(70000000,0);
  assert(gDeviceConfig.radio.vfos[0].tx.frequency_hz==438600000 && !strcmp(message,"Invalid TX freq"));
  inMenu=true;press(KEY_F);assert(!gDeviceConfig.radio.vfos[0].tx.frequency_hz);
  press(KEY_DOWN);assert(menuIndex==8);
  press(KEY_MENU);assert(gDeviceConfig.radio.vfos[0].tx.tone==1);
  press(KEY_F);assert(gDeviceConfig.radio.vfos[0].tx.tone==0);
  unsigned savesBeforePlaceholder=saveRequests;
  appliesBefore=applied;
  press(KEY_DOWN);assert(menuIndex==MENU_RX_WATCH);
  press(KEY_DOWN);assert(menuIndex==MENU_CHANNEL_NAME);
  press(KEY_MENU);assert(!strcmp(message,"Not implemented"));
  press(KEY_STAR);press(KEY_F);
  press(KEY_DOWN);assert(menuIndex==MENU_CHANNEL_SAVE);
  press(KEY_MENU);press(KEY_STAR);press(KEY_F);
  assert(saveRequests==savesBeforePlaceholder && applied==appliesBefore);
  assert(inMenu && !confirming);
  press(KEY_DOWN);assert(menuIndex==0); // Includes placeholders, no section titles.
  // Dual VFO: selection is a receive transaction, not an alias to one state.
  setup(145500000, fm);
  assert(selectedVfo==0 && receivingVfo==0);
  VFO_render();
  assert(drawn[0].selected && !drawn[1].selected && !drawn[1].signal_valid);
  assert(drawn[1].frequency_hz==438500000 && drawn[1].activity==UI_VFO_IDLE);
  VfoConfig originalA=gDeviceConfig.radio.vfos[0];
  press(KEY_F); press(KEY_2);
  assert(selectedVfo==1 && receivingVfo==1 && !functionPending);
  assert(gDeviceConfig.radio.selected_vfo==1 && user.current.frequency_hz==438500000);
  assert(!memcmp(&originalA,&gDeviceConfig.radio.vfos[0],sizeof(originalA)));
  press(KEY_UP);
  assert(user.current.frequency_hz==438512500 && gDeviceConfig.radio.vfos[0].current.frequency_hz==145500000);
  press(KEY_MENU);menuIndex=8;press(KEY_MENU);press(KEY_EXIT);
  assert(gDeviceConfig.radio.vfos[1].tx.tone==1 && !gDeviceConfig.radio.vfos[0].tx.tone);
  vfo->is_open=true;
  VFO_render();assert(drawn[1].selected && drawn[1].activity==UI_VFO_RECEIVING);
  assert(!drawn[0].signal_valid && drawn[0].activity==UI_VFO_IDLE);
  press(KEY_F);press(KEY_2);
  assert(selectedVfo==0 && user.current.frequency_hz==145500000);
  press(KEY_F);press(KEY_2);
  assert(selectedVfo==1 && user.current.frequency_hz==438512500);
  // Reboot restores B and its exact parameters, including temporary overrides.
  initialized=false;runtime=(RxRuntime){0};VFO_init();
  assert(selectedVfo==1 && receivingVfo==1 && user.current.frequency_hz==438512500);
  // Per-VFO memories and shared presets, even when both use the same band.
  request(145600000, (RadioParameters){RX_AM,25000,6000,2,0});
  gDeviceConfig.radio.presets[5].step_hz=50000;
  press(KEY_F);press(KEY_2);
  assert(user.current.parameters.mode==RX_NFM && user.current.parameters.step_hz==12500);
  press(KEY_F);press(KEY_2);
  assert(user.current.parameters.mode==RX_AM && user.current.parameters.step_hz==25000);
  assert(gDeviceConfig.radio.vfos[0].bands[5].frequency_hz==145500000);
  // Apply failure restores A and does not commit B selection or request save.
  setup(145500000,fm);unsigned oldSaves=saveRequests;
  failFrequency=438500000;press(KEY_F);press(KEY_2);
  assert(selectedVfo==0 && receivingVfo==0 && runtime.active);
  assert(gDeviceConfig.radio.selected_vfo==0 && saveRequests==oldSaves);
  assert(!strcmp(message,"Tune failed") && ctx->frequency==14550000);
  VFO_render();assert(!strcmp(statusText,"RADIO"));
  assert(!strcmp(drawn[0].message,"Tune failed") && !drawn[1].message);
  // Both applies fail: ownership is cleared and no row claims reception.
  failApply=true;press(KEY_F);press(KEY_2);
  assert(selectedVfo==0 && receivingVfo==UINT8_MAX && !runtime.active);
  VFO_render();assert(!drawn[0].signal_valid && !drawn[1].signal_valid);
  assert(drawn[0].activity==UI_VFO_UNAVAILABLE);
  setup(145500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,7100000,
      (RadioParameters){RX_LSB,1000,3000,0,0}));
  capabilities.ssb_patch=false;oldSaves=saveRequests;press(KEY_F);press(KEY_2);
  assert(selectedVfo==0 && runtime.current.frequency_hz==145500000 && saveRequests==oldSaves);
  assert(!strcmp(message,"SSB patch missing"));
  // RDS labels stay with their VFO/frequency, never the physical parser owner.
  setup(100000000,(RadioParameters){RX_WFM,100000,0,0});
  rdsAvailable=true;VFO_update();clockNow+=300;VFO_update();
  clockNow+=100;VFO_update();clockNow+=100;VFO_update();
  assert(labels[0].ps[0] && !labels[1].ps[0]);
  clockNow+=15001;VFO_update();assert(!labels[0].ps[0]);
  rdsAvailable=true;rdsIndex=0;clockNow+=100;VFO_update();clockNow+=100;VFO_update();
  assert(labels[0].ps[0]);
  press(KEY_F);press(KEY_2);VFO_render();
  assert(drawn[0].name && drawn[0].name[0] && (!drawn[1].name || !drawn[1].name[0]));
  assert(!rds.decoder.ps[0] && !drawn[0].signal_valid);
  press(KEY_F);press(KEY_2);request(100100000,user.current.parameters);
  assert(!labels[0].ps[0]);
  // Input callbacks are bound to their originating VFO.
  setup(145500000,fm);press(KEY_1);assert(inputVfo==0);
  switchVfo();unsigned beforeInput=applied;
  gFInputCallback(14570000,0);assert(applied==beforeInput && selectedVfo==1);
  // Confirmation/menu/search/held PTT consume F2 without switching.
  setup(145500000,fm);press(KEY_MENU);press(KEY_F);press(KEY_2);
  assert(selectedVfo==0);press(KEY_EXIT);
  confirming=true;press(KEY_F);press(KEY_2);assert(selectedVfo==0);press(KEY_EXIT);
  startSeek();press(KEY_F);press(KEY_2);assert(selectedVfo==0);press(KEY_EXIT);
  transmitter.wait_release=true;press(KEY_F);press(KEY_2);assert(selectedVfo==0);
  transmitter.wait_release=false;
  // TX belongs to B and restores B even though A is also saved.
  setup(145500000,fm);press(KEY_F);press(KEY_2);
  gDeviceConfig.radio.vfos[1].tx.enabled=true;txFakeVerified=true;
  assert(VFO_key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  assert(receivingVfo==UINT8_MAX);press(KEY_F);press(KEY_2);assert(selectedVfo==1);
  VFO_PttRelease();assert(transmitter.mode==RADIO_RX && receivingVfo==1);
  assert(runtime.current.frequency_hz==438500000 && !gDeviceConfig.radio.vfos[0].tx.enabled);
  // Missing boot capabilities retain B selection and explicit unavailable state.
  setup(145500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,7100000,
      (RadioParameters){RX_LSB,1000,3000,0,0}));
  gDeviceConfig.radio.selected_vfo=1;initialized=false;capabilities.ssb_patch=false;
  VFO_init();assert(selectedVfo==1 && receivingVfo==UINT8_MAX && !runtime.active);
  assert(user.current.parameters.mode==RX_LSB);
  // Four saved VFOs, only two visible rows. F2 cycles by enabled count.
  setup(145500000,fm);assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,4));
  press(KEY_F);press(KEY_2);assert(selectedVfo==1);
  press(KEY_F);press(KEY_2);assert(selectedVfo==2);
  assert(RADIO_STATE_VisibleFirst(&gDeviceConfig.radio)==2);
  VFO_render();assert(drawn[2].selected && drawn[3].id=='D');
  press(KEY_UP);assert(gDeviceConfig.radio.vfos[2].current.frequency_hz==145512500);
  assert(gDeviceConfig.radio.vfos[0].current.frequency_hz==145500000);
  press(KEY_F);press(KEY_2);assert(selectedVfo==3);
  press(KEY_F);press(KEY_2);assert(selectedVfo==0);
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,3));
  press(KEY_F);press(KEY_2);press(KEY_F);press(KEY_2);
  assert(selectedVfo==2 && RADIO_STATE_VisibleFirst(&gDeviceConfig.radio)==1);
  VFO_render();assert(drawn[1].id=='B' && drawn[2].selected);
  press(KEY_F);press(KEY_2);assert(selectedVfo==0);
  assert(!RADIO_STATE_SetCount(&gDeviceConfig.radio,1));
  assert(!RADIO_STATE_SetWatch(&gDeviceConfig.radio,2,true));
  assert(RADIO_STATE_SetWatch(&gDeviceConfig.radio,0,false));
  assert(RADIO_STATE_SetWatch(&gDeviceConfig.radio,2,true));
  assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,2));
  assert(!gDeviceConfig.radio.vfos[2].watch); // Hidden state remains, hidden watcher doesn't.
  assert(gDeviceConfig.radio.vfos[2].current.frequency_hz==145512500);
  // RX Watch is independent of user frequency changes, capped at two participants.
  setup(145500000,fm);menuIndex=MENU_RX_WATCH;press(KEY_MENU);press(KEY_MENU);
  assert(!gDeviceConfig.radio.vfos[0].watch);press(KEY_EXIT);press(KEY_UP);
  assert(!gDeviceConfig.radio.vfos[0].watch);
  // Watch RF visits never commit RX frequencies, selected index or EEPROM saves.
  setup(145500000,fm);gDeviceConfig.radio.watch_enabled=true;
  watchSignalFrequency=438500000;unsigned watchSaves=saveRequests;
  VFO_update();assert(watcher.phase==WATCH_SETTLE && !VFO_isReceiving());
  clockNow+=180;VFO_update();assert(watcher.phase==WATCH_TUNE);
  VFO_update();assert(receivingVfo==1 && selectedVfo==0);
  clockNow+=180;VFO_update();assert(watcher.phase==WATCH_VERIFY);
  clockNow+=80;VFO_update();assert(watcher.phase==WATCH_HOLD && VFO_isReceiving());
  assert(vfo->is_open);VFO_render();assert(drawn[1].activity==UI_VFO_RECEIVING && !drawn[1].selected);
  assert(drawn[0].selected && !drawn[0].signal_valid);
  assert(saveRequests==watchSaves && user.current.frequency_hz==145500000);
  assert(gDeviceConfig.radio.vfos[1].current.frequency_hz==438500000 && !gDeviceConfig.radio.selected_vfo);
  press(KEY_UP);assert(watcher.phase==WATCH_OFF && receivingVfo==0);
  assert(runtime.current.frequency_hz==145512500 && selectedVfo==0);
  unsigned afterManual=applied;clockNow+=1999;VFO_update();assert(applied==afterManual);
  clockNow++;VFO_update();assert(watcher.phase==WATCH_SETTLE);
  VFO_deinit();assert(watcher.phase==WATCH_OFF && receivingVfo==0);
  // PTT preempts background listening and uses A's plan, never B's carrier.
  setup(438500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,438600000,fm));
  gDeviceConfig.radio.watch_enabled=true;watchSignalFrequency=438600000;
  VFO_update();clockNow+=180;VFO_update();VFO_update();
  clockNow+=180;VFO_update();clockNow+=80;VFO_update();assert(receivingVfo==1);
  gDeviceConfig.radio.vfos[0].tx.enabled=true;txFakeVerified=true;
  assert(VFO_key(KEY_PTT,KEY_PRESSED) && transmitter.mode==RADIO_TX);
  assert(watcher.phase==WATCH_OFF && selectedVfo==0 && transmitter.previous_rx.frequency_hz==438500000);
  VFO_PttRelease();assert(runtime.current.frequency_hz==438500000 && receivingVfo==0);
  // An offscreen watch owner is reported without moving selection or paging.
  setup(145500000,fm);RADIO_STATE_SetCount(&gDeviceConfig.radio,4);
  press(KEY_F);press(KEY_2);press(KEY_F);press(KEY_2);assert(selectedVfo==2);
  gDeviceConfig.radio.watch_enabled=true;watchDelay=false;VFO_update();VFO_render();
  assert(drawn[2].selected && !drawn[2].signal_valid && !strcmp(statusText,"DW A"));
  // Dedup/unsupported groups stay on the primary, without busy-loop retunes.
  setup(145500000,fm);RADIO_STATE_Set(&gDeviceConfig.radio,1,145500000,fm);
  gDeviceConfig.radio.watch_enabled=true;unsigned noVisits=applied;
  VFO_update();assert(watcher.phase==WATCH_OFF && applied==noVisits);
  VFO_render();assert(!strcmp(statusText,"DW A"));
  assert(!strcmp(drawn[0].message,"DW: same channel") && !drawn[1].message);
  for (unsigned i=0;i<10;++i) { clockNow+=100;VFO_update(); }
  assert(applied==noVisits);
  // Apply/sample errors back off; even primary restore is attempted only once.
  setup(145500000,fm);gDeviceConfig.radio.watch_enabled=true;failApply=true;
  VFO_update();VFO_update();VFO_update();assert(watcher.phase==WATCH_RETRY);
  unsigned attempts=applyAttempts;
  for(unsigned i=0;i<10;++i) { clockNow+=100;VFO_update(); }
  assert(applyAttempts==attempts && receivingVfo==UINT8_MAX);
  failApply=false;clockNow+=WATCH_RETRY_MS;VFO_update();assert(runtime.active);
  // RDS belongs to the actual background WFM receiver, not the selected NFM VFO.
  setup(145500000,fm);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio,1,100000000,(RadioParameters){RX_WFM,100000,0,0,0}));
  gDeviceConfig.radio.watch_enabled=true;watchSignalFrequency=100000000;rdsAvailable=true;
  VFO_update();clockNow+=180;VFO_update();VFO_update();
  clockNow+=350;VFO_update();clockNow+=80;VFO_update();
  assert(receivingVfo==1 && selectedVfo==0 && watcher.phase==WATCH_HOLD);
  clockNow+=300;VFO_update();clockNow+=100;VFO_update();clockNow+=100;VFO_update();
  assert(!labels[0].ps[0] && labels[1].ps[0]);
  VFO_render();assert(drawn[1].name && drawn[1].name[0] && !drawn[0].name[0]);
  // Reducing count in Settings changes selection on return, preserving hidden VFO data.
  setup(145500000,fm);RADIO_STATE_SetCount(&gDeviceConfig.radio,4);
  press(KEY_F);press(KEY_2);press(KEY_F);press(KEY_2);assert(selectedVfo==2);
  VFO_deinit();assert(RADIO_STATE_SetCount(&gDeviceConfig.radio,2));VFO_init();
  assert(selectedVfo==0 && user.current.frequency_hz==145500000);
  return 0;
}

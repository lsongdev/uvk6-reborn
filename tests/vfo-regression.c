// Exercise real main-screen state transitions; only hardware/UI plumbing is mocked.
#include "../src/apps/vfo1.c"
#include <assert.h>

RadioState state;
RadioState *gRadioState = &state;
RadioState *vfo = &state;
RadioReceiver *ctx = &state.context;
bool gRedrawScreen;
bool gMonitorMode;
uint32_t gFInputValue1, gFInputValue2;
void (*gFInputCallback)(uint32_t, uint32_t);
static ReceiverCapabilities capabilities = {true, false, true};
static RadioParameters savedParameters;
static unsigned applied;
static unsigned routed;
static unsigned appRuns;
static bool failApply;
DeviceConfig gDeviceConfig;
bool gDeviceConfigReady = true;
static unsigned saveRequests;
static uint32_t clockNow=100;
static unsigned rdsReads, rdsClears, rdsIndex;
static bool rdsAvailable;
static uint32_t soughtFrequency;
static bool nativeSeekBusy;
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
  return RX_SEEK_FOUND;
}
void RX_DEVICE_SeekCancel(RxDevice dev) { (void)dev; ++nativeCancels; }
bool RX_DEVICE_SeekSignal(RxDevice dev, bool *found) {
  (void)dev; *found = ctx->frequency*10u == soughtFrequency; return true;
}
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
  if (failApply || (failFrequency && plan->frequency_hz == failFrequency))
    return false;
  ++applied;
  context->frequency = plan->frequency_hz/10;
  context->radio_type = plan->device == RX_DEVICE_BK ? RADIO_BK4819
                      : plan->device == RX_DEVICE_SI ? RADIO_SI4732 : RADIO_BK1080;
  return true;
}
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
  initialized = changed = inMenu = confirming = false;
  functionPending = false;
  seek = (Seek){0};
  runtime = (RxRuntime){0};
  message = NULL;
  RADIO_STATE_Default(&gDeviceConfig.radio);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio, frequency, values));
  *ctx = (RadioReceiver){.frequency = frequency/10};
  savedParameters = values;
  applied = routed = appRuns = saveRequests = 0;
  failApply = false;
  failFrequency = 0;
  soughtFrequency = 0;
  nativeFrequency = 100100000;
  nativeStarts = nativeCancels = 0;
  capabilities = (ReceiverCapabilities){true, false, true};
  VFO1_init();
}
static void press(KEY_Code_t key) { assert(VFO1_key(key, KEY_RELEASED)); }

int main(void) {
  RadioParameters fm = {RX_NFM, 12500, 12000, 4, false};
  setup(438500000, fm);
  press(KEY_F);press(KEY_0);
  assert(!functionPending && !confirming && appRuns == 0 && applied == 2);
  assert(user.current.frequency_hz == 438500000 && user.current.parameters.mode == RX_AM);
  press(KEY_F);press(KEY_0);
  assert(user.current.parameters.mode == RX_NFM);
  press(KEY_0);assert(appRuns == 1); // Plain 0 still enters frequency input.

  setup(100000000, (RadioParameters){RX_WFM,100000,0,0,false});
  press(KEY_F);press(KEY_0);
  assert(!confirming && user.current.parameters.mode == RX_AM && ctx->radio_type == RADIO_BK4819);
  assert(user.current.frequency_hz == 100000000);

  setup(7100000, (RadioParameters){RX_AM,5000,6000,0,false});
  capabilities.ssb_patch = false;
  press(KEY_F);press(KEY_0);
  assert(!functionPending && user.current.parameters.mode == RX_AM);
  assert(!strcmp(message,"No other mode") && appRuns == 0);

  setup(438500000, fm);
  assert(VFO1_key(KEY_F, KEY_PRESSED) == false);
  press(KEY_F);
  assert(functionPending && user.active_band == 6);
  assert(VFO1_key(KEY_1, KEY_PRESSED));
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
  press(KEY_F);press(KEY_2);
  assert(!functionPending && !strcmp(message,"Not supported") && appRuns == 0 && applied == 1);

  press(KEY_F);
  clockNow = functionAt+2999;VFO1_update();assert(functionPending);
  clockNow++;VFO1_update();assert(!functionPending);
  press(KEY_1);assert(appRuns == 1);
  press(KEY_F);clockNow += 3000; // Timeout is also checked before key dispatch.
  press(KEY_1);assert(!functionPending && appRuns == 2 && applied == 1);
  clockNow = UINT32_MAX-1000;
  press(KEY_F);clockNow += 2999;VFO1_update();assert(functionPending);
  clockNow++;VFO1_update();assert(!functionPending);

  setup(438500000, fm);
  press(KEY_F);assert(VFO1_key(KEY_PTT,KEY_PRESSED));assert(!functionPending);
  press(KEY_F);assert(!VFO1_key(KEY_MENU,KEY_LONG_PRESSED));
  assert(!functionPending && !inMenu);
  press(KEY_F);VFO1_cancelShortcut();assert(!functionPending); // System lock transition.
  press(KEY_F);VFO1_deinit();assert(!functionPending);
  press(KEY_F);VFO1_init();assert(!functionPending); // Returning from another app.
  press(KEY_MENU);assert(inMenu);
  press(KEY_F);assert(!functionPending && appRuns == 0); // Local menu adjustment.
  press(KEY_EXIT);press(KEY_SIDE1);assert(user.active_band == 0);

  setup(438500000, fm);
  gDeviceConfig.radio.presets[0] = (RadioParameters){RX_AM,5000,3000,2,false};
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
  setup(100000000, (RadioParameters){RX_NFM, 12500, 12000, 0, false});
  assert(runtime.active && ctx->radio_type == RADIO_BK4819);
  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0, false});
  assert(runtime.active && ctx->radio_type == RADIO_SI4732);
  VFO1_update();assert(!rdsReads);
  clockNow=400;VFO1_update();assert(rdsClears==1);
  rdsAvailable=true;
  clockNow=500;VFO1_update();assert(rdsIndex==4 && !rds.decoder.ps[0]);
  gRedrawScreen=false;clockNow=600;VFO1_update();
  assert(!strcmp(rds.decoder.ps,"AAAAAAAA") && gRedrawScreen);
  unsigned readsBefore=rdsReads;
  press(KEY_SIDE2);assert(!rds.decoder.ps[0]);
  clockNow=1000;VFO1_update();assert(rdsReads==readsBefore); // NFM doesn't poll SI RDS.

  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0, false});
  strcpy(rds.decoder.ps,"OLD FM");
  failFrequency=100100000;press(KEY_UP);
  assert(runtime.active && user.current.frequency_hz==100000000 && !rds.decoder.ps[0]);

  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0, false});
  capabilities.si=false;capabilities.broadcast=true;request(100000000,user.current.parameters);
  assert(runtime.current.device==RX_DEVICE_BROADCAST);
  clockNow=500;VFO1_update();assert(!rdsReads); // BK1080 has no RDS path.

  setup(438500000, fm);
  press(KEY_SIDE2);
  assert(runtime.active && !confirming && user.current.parameters.mode == RX_AM);
  assert(user.current.frequency_hz == 438500000 && changed);
  press(KEY_SIDE2);
  assert(user.current.parameters.mode == RX_NFM && user.current.frequency_hz == 438500000);

  setup(7100000, (RadioParameters){RX_AM, 5000, 6000, 0, false});
  unsigned before = applied;
  press(KEY_SIDE2);
  assert(!confirming && user.current.parameters.mode == RX_LSB);
  assert(user.current.parameters.bandwidth_hz == 3000 && applied == before+1);
  assert(user.current.frequency_hz == 7100000);

  setup(7100000, (RadioParameters){RX_AM, 5000, 6000, 0, false});
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
  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0, false});
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
  gDeviceConfig.radio.presets[6] = (RadioParameters){RX_AM,25000,6000,2,false};
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
  return 0;
}

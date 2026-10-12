// Exercise real main-screen state transitions; only hardware/UI plumbing is mocked.
#include "../src/apps/vfo1.c"
#include <assert.h>

RadioState state;
RadioState *gRadioState = &state;
ExtendedVFOContext *vfo = &state.vfos[0];
VFOContext *ctx = &state.vfos[0].context;
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
void DEVICE_RequestSave(void) { ++saveRequests; }
static uint32_t failFrequency;

uint32_t Now(void) { return 100; }
ReceiverCapabilities RADIO_GetReceiverCapabilities(void) { return capabilities; }
RadioParameters RADIO_ReadParameters(const VFOContext *context) {
  (void)context;
  return savedParameters;
}
bool RADIO_ApplyRxPlan(VFOContext *context, const RxPlan *plan) {
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
void RADIO_ToggleTX(VFOContext *context, bool on) { (void)context; (void)on; }
void APPS_run(AppType_t app) { (void)app; ++appRuns; }
bool APPS_key(KEY_Code_t key, Key_State_t keyState) { (void)key; (void)keyState; return true; }
void FINPUT_setup(uint32_t min, uint32_t max, InputUnit unit, bool range) {
  (void)min; (void)max; (void)unit; (void)range;
}

static void setup(uint32_t frequency, RadioParameters values) {
  initialized = changed = inMenu = confirming = false;
  runtime = (RxRuntime){0};
  message = NULL;
  RADIO_STATE_Default(&gDeviceConfig.radio);
  assert(RADIO_STATE_Set(&gDeviceConfig.radio, frequency, values));
  *ctx = (VFOContext){.frequency = frequency/10};
  savedParameters = values;
  applied = routed = appRuns = saveRequests = 0;
  failApply = false;
  failFrequency = 0;
  capabilities = (ReceiverCapabilities){true, false, true};
  VFO1_init();
}
static void press(KEY_Code_t key) { assert(VFO1_key(key, KEY_RELEASED)); }

int main(void) {
  RadioParameters fm = {RX_NFM, 12500, 12000, 4, false};
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

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
  for (unsigned i = 0; i < sizeof(lastFrequency)/sizeof(lastFrequency[0]); ++i)
    lastFrequency[i] = 0;
  *ctx = (VFOContext){.frequency = frequency/10};
  savedParameters = values;
  applied = routed = appRuns = 0;
  failApply = false;
  failFrequency = 0;
  capabilities = (ReceiverCapabilities){true, false, true};
  VFO1_init();
}
static void press(KEY_Code_t key) { assert(VFO1_key(key, KEY_RELEASED)); }

int main(void) {
  RadioParameters fm = {RX_NFM, 12500, 12000, 4, false};
  setup(438500000, fm);
  assert(runtime.active && applied == 1 && bandIndex == 6);
  press(KEY_SIDE1);
  assert(frequencyHz == 225000 && parameters.mode == RX_AM && bandIndex == 0);
  press(KEY_SIDE1);
  assert(frequencyHz == 999000 && bandIndex == 1);

  // The same 100 MHz carrier must use BK for NFM and SI for WFM.
  setup(100000000, (RadioParameters){RX_NFM, 12500, 12000, 0, false});
  assert(runtime.active && ctx->radio_type == RADIO_BK4819);
  setup(100000000, (RadioParameters){RX_WFM, 100000, 0, 0, false});
  assert(runtime.active && ctx->radio_type == RADIO_SI4732);

  setup(438500000, fm);
  press(KEY_SIDE2);
  assert(runtime.active && !confirming && parameters.mode == RX_AM);
  assert(frequencyHz == 438500000 && changed);
  press(KEY_SIDE2);
  assert(parameters.mode == RX_NFM && frequencyHz == 438500000);

  setup(7100000, (RadioParameters){RX_AM, 5000, 6000, 0, false});
  unsigned before = applied;
  press(KEY_SIDE2);
  assert(confirming && parameters.mode == RX_AM && applied == before);
  press(KEY_EXIT);
  assert(!confirming && parameters.mode == RX_AM && applied == before);
  press(KEY_SIDE2);
  press(KEY_MENU);
  assert(parameters.mode == RX_LSB && parameters.bandwidth_hz == 3000 && applied == before+1);

  setup(7100000, (RadioParameters){RX_AM, 5000, 6000, 0, false});
  capabilities.ssb_patch = false;
  press(KEY_SIDE2);
  assert(!confirming && parameters.mode == RX_AM);

  setup(438500000, fm);
  tuneTo(9592500, 0);
  assert(confirming && pending.frequency_hz == 95930000);
  assert(frequencyHz == 438500000);
  press(KEY_EXIT);
  assert(frequencyHz == 438500000 && runtime.active);
  tuneTo(9592500, 0);
  press(KEY_MENU);
  assert(frequencyHz == 95930000 && parameters.mode == RX_WFM && ctx->radio_type == RADIO_SI4732);
  press(KEY_SIDE2);
  assert(confirming && parameters.mode == RX_WFM);
  press(KEY_EXIT);
  assert(parameters.mode == RX_WFM);

  setup(147987500, fm);
  press(KEY_UP);
  assert(frequencyHz == 148000000 && bandIndex == 7 && parameters.step_hz == 25000);
  assert(ctx->radio_type == RADIO_BK4819);

  setup(438500000, fm);
  capabilities = (ReceiverCapabilities){false, false, false};
  press(KEY_SIDE1);
  assert(!runtime.active && frequencyHz == 225000 && routed == 1);
  assert(ctx->frequency == 43850000); // Unsupported target was not committed.
  press(KEY_UP);
  assert(frequencyHz == 226000 && !runtime.active);
  press(KEY_SIDE1); // MW has no receiver.
  press(KEY_SIDE1); // SW has no receiver.
  press(KEY_SIDE1); // Broadcast has no receiver.
  press(KEY_SIDE1); // Airband is available on BK.
  assert(runtime.active && frequencyHz == 125000000 && parameters.mode == RX_AM);

  setup(438500000, fm);
  press(KEY_MENU);
  assert(inMenu);
  press(KEY_SIDE1);
  press(KEY_SIDE2);
  assert(frequencyHz == 438500000 && applied == 1);
  press(KEY_EXIT);
  assert(!inMenu && appRuns == 0);
  // A transient failed tune restores the last working receiver.
  failFrequency = 438512500;
  press(KEY_UP);
  assert(runtime.active && frequencyHz == 438500000 && applied == 2 && routed == 0);
  failFrequency = 0;
  failApply = true;
  press(KEY_UP);
  assert(frequencyHz == 438500000 && parameters.mode == RX_NFM && applied == 2);
  assert(!runtime.active && !vfo->is_open && routed == 1);
  return 0;
}

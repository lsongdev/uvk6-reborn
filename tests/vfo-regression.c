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

uint32_t Now(void) { return 100; }
ReceiverCapabilities RADIO_GetReceiverCapabilities(void) { return capabilities; }
RadioParameters RADIO_ReadParameters(const VFOContext *context) {
  (void)context;
  return savedParameters;
}
bool RADIO_ApplyRxPlan(VFOContext *context, const RxPlan *plan) {
  if (failApply)
    return false;
  ++applied;
  context->frequency = plan->frequency_hz/10;
  context->radio_type = plan->device == RX_DEVICE_BK ? RADIO_BK4819
                      : plan->device == RX_DEVICE_SI ? RADIO_SI4732 : RADIO_BK1080;
  return true;
}
void RADIO_SwitchAudioToVFO(RadioState *radio, uint8_t index) {
  assert(radio == &state && index == 0 && !vfo->is_open);
  ++routed;
}
void RADIO_ToggleTX(VFOContext *context, bool on) { (void)context; (void)on; }
void APPS_run(AppType_t app) { (void)app; ++appRuns; }
bool APPS_key(KEY_Code_t key, Key_State_t keyState) { (void)key; (void)keyState; return true; }
void FINPUT_setup(uint32_t min, uint32_t max, InputUnit unit, bool range) {
  (void)min; (void)max; (void)unit; (void)range;
}

static void setup(uint32_t frequency, RadioParameters values) {
  initialized = receiving = changed = inMenu = confirming = false;
  message = NULL;
  for (unsigned i = 0; i < sizeof(lastFrequency)/sizeof(lastFrequency[0]); ++i)
    lastFrequency[i] = 0;
  *ctx = (VFOContext){.frequency = frequency/10};
  savedParameters = values;
  applied = routed = appRuns = 0;
  failApply = false;
  capabilities = (ReceiverCapabilities){true, false, true};
  VFO1_init();
}
static void press(KEY_Code_t key) { assert(VFO1_key(key, KEY_RELEASED)); }

int main(void) {
  RadioParameters fm = {RX_FM, 12500, 12000, 4, false};
  setup(438500000, fm);
  assert(receiving && applied == 1 && bandIndex == 6);
  press(KEY_SIDE1);
  assert(frequencyHz == 225000 && parameters.mode == RX_AM && bandIndex == 0);
  press(KEY_SIDE1);
  assert(frequencyHz == 999000 && bandIndex == 1);

  setup(438500000, fm);
  press(KEY_SIDE2);
  assert(receiving && !confirming && parameters.mode == RX_AM);
  assert(frequencyHz == 438500000 && changed);
  press(KEY_SIDE2);
  assert(parameters.mode == RX_FM && frequencyHz == 438500000);

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
  assert(frequencyHz == 438500000 && receiving);
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
  assert(!receiving && frequencyHz == 225000 && routed == 1);
  assert(ctx->frequency == 43850000); // Unsupported target was not committed.
  press(KEY_UP);
  assert(frequencyHz == 226000 && !receiving);
  press(KEY_SIDE1); // MW has no receiver.
  press(KEY_SIDE1); // SW has no receiver.
  press(KEY_SIDE1); // Broadcast has no receiver.
  press(KEY_SIDE1); // Airband is available on BK.
  assert(receiving && frequencyHz == 125000000 && parameters.mode == RX_AM);

  setup(438500000, fm);
  press(KEY_MENU);
  assert(inMenu);
  press(KEY_SIDE1);
  press(KEY_SIDE2);
  assert(frequencyHz == 438500000 && applied == 1);
  press(KEY_EXIT);
  assert(!inMenu && appRuns == 0);
  failApply = true;
  press(KEY_UP);
  assert(frequencyHz == 438500000 && parameters.mode == RX_FM && applied == 1);
  assert(!receiving && !vfo->is_open && routed == 1);
  return 0;
}

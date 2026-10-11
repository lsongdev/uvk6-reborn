// Host regression tests exercise production tuning logic with hardware mocked.
#include "../src/radio.c"
#include <assert.h>

static bool hasSi = true;
Settings gSettings;
static unsigned probes;
static bool hardwareApplyOk = true;
static unsigned hardwareApplies, audioMutes;
bool RX_DEVICE_Apply(const RxPlan *plan, RxDevice previous) {
  (void)plan; (void)previous;
  ++hardwareApplies;
  return hardwareApplyOk;
}
void AUDIO_ToggleSpeaker(bool on) { if (!on) ++audioMutes; }
void BK4819_ToggleAFDAC(bool on) { (void)on; }
void BK4819_ToggleAFBit(bool on) { (void)on; }
void BOARD_ToggleGreen(bool on) { (void)on; }
void BACKLIGHT_On(void) {}
void BACKLIGHT_Toggle(bool on) { (void)on; }
void SYS_DelayMs(uint32_t ms) { (void)ms; }
bool BK1080_Detect(void) { ++probes; return !hasSi; }
bool SI47XX_Detect(void) { ++probes; return hasSi; }
bool SETTINGS_IsPatchPresent(void) { return true; }
const char *TX_OFFSET_NAMES[4] = {"None", "+", "-", "Freq"};
const char *TX_POWER_NAMES[4] = {"Low", "Mid", "High", "Min"};
uint16_t BK1080_ReadRegister(BK1080_Register_t reg) {
  (void)reg;
  return hasSi ? 0 : 0x1080;
}
void Log(const char *pattern, ...) { (void)pattern; }
bool BK4819_BandwidthFromHz(uint32_t hz, uint16_t *bw) {
  if (hz != 12000 && hz != 6000) return false;
  *bw = hz == 12000 ? BK4819_FILTER_BW_12k : BK4819_FILTER_BW_6k;
  return true;
}
bool SI47XX_BandwidthFromHz(uint32_t hz, bool ssb, uint16_t *bw) {
  (void)ssb;
  if (hz != 3000 && hz != 6000) return false;
  *bw = 3;
  return true;
}
void LogC(LogColor color, const char *pattern, ...) {
  (void)color; (void)pattern;
}
uint32_t Now(void) { return 100; }
void PrintRTXCode(char *out, uint8_t type, uint8_t code) {
  (void)type; (void)code; out[0] = '\0';
}
int16_t Rssi2DBm(uint16_t rssi) { return rssi; }
void mhzToS(char *out, uint32_t f) { (void)f; out[0] = '\0'; }
void bkAttToS(char *out, uint8_t gain) { (void)gain; out[0] = '\0'; }

void testFrequencyInput(void);
void testBandNames(void);

int main(int argc, char **argv) {
  (void)argv;
  hasSi = argc == 1;
  VFOContext context = {.radio_type = RADIO_SI4732, .frequency = 9592500,
                        .modulation = MOD_FM};
  RADIO_UpdateCurrentBand(&context);
  RADIO_SetParam(&context, PARAM_FREQUENCY, 43850000, true);
  assert(context.frequency == 43850000);
  assert(context.radio_type == RADIO_BK4819);
  assert(context.current_band == &bk4819_bands[0]);
  assert(context.dirty[PARAM_RADIO] && context.dirty[PARAM_FREQUENCY]);
  assert(context.gain == AUTO_GAIN_INDEX);
  assert(context.save_to_eeprom);

  RADIO_SetParam(&context, PARAM_FREQUENCY, 9592500, true);
  assert(context.frequency == 9592500);
  assert(context.radio_type == (hasSi ? RADIO_SI4732 : RADIO_BK1080));
  assert(context.current_band == (hasSi ? &si4732_bands[2] : &bk1080_band));
  assert(context.modulation == MOD_FM && context.gain == 0);

  if (hasSi) {
  RADIO_SetParam(&context, PARAM_FREQUENCY, 710000, true);
  assert(context.frequency == 710000);
  assert(context.radio_type == RADIO_SI4732);
  assert(context.modulation == MOD_AM);
  assert(context.current_band == &si4732_bands[0]);
  RADIO_SetParam(&context, PARAM_FREQUENCY, 14550000, true);
  assert(context.frequency == 14550000);
  assert(context.radio_type == RADIO_BK4819);

  } else {
  context.modulation = MOD_FM;
  RADIO_SetParam(&context, PARAM_FREQUENCY, 9592500, false);
  assert(context.radio_type == RADIO_BK1080);
  assert(context.current_band == &bk1080_band);
  assert(!RADIO_IsParamValid(&context, PARAM_FREQUENCY, 43850000));
  RADIO_SetParam(&context, PARAM_FREQUENCY, 43850000, false);
  assert(context.radio_type == RADIO_BK4819);
  assert(context.frequency == 43850000);
  RADIO_SetParam(&context, PARAM_FREQUENCY, 710000, false);
  assert(context.frequency == 43850000); // No HF receiver on this board.
  RADIO_SetParam(&context, PARAM_FREQUENCY, BK4819_F_MAX + 1, false);
  assert(context.frequency == 43850000);

  }
  if (hasSi) {
  VFO stored = {.radio = RADIO_SI4732, .rxF = 9592500,
                .modulation = MOD_FM, .gainIndex = 0};
  setCommonParamsFromCh(&context, &stored);
  assert(context.frequency == 9592500 && context.volume == 100);
  assert(context.dirty[PARAM_VOLUME]);
  // A full-width EEPROM name without NUL must never overflow ctx->name.
  memset(stored.name, 'X', sizeof(stored.name));
  stored.radio = RADIO_BK4819;
  stored.gainIndex = AUTO_GAIN_INDEX;
  setCommonParamsFromCh(&context, &stored);
  assert(context.radio_type == RADIO_SI4732 && context.gain == 0);
  assert(context.name[sizeof(context.name) - 1] == '\0');
  for (unsigned i = 0; i < sizeof(context.name) - 1; ++i)
    assert(context.name[i] == 'X');
  }
  RadioState snapshot = {.num_vfos = 1};
  snapshot.vfos[0].context = context;
  snapshot.vfos[0].mode = MODE_VFO;
  memcpy(snapshot.vfos[0].context.name, "VFO Test", 9);
  snapshot.vfos[0].context.power = TX_POW_LOW;
  VFO exported;
  memset(&exported, 0xff, sizeof(exported));
  RADIO_SaveVFOToStorage(&snapshot, 0, &exported);
  assert(exported.meta.type == TYPE_VFO && !exported.meta.readonly);
  assert(exported.name[9] == '\0' && strcmp(exported.name, "VFO Test") == 0);
  assert(exported.power == TX_POW_LOW && !exported.allowTx);
  assert(exported.rxF == context.frequency && exported.modulation == context.modulation);
  // Even trusted EEPROM metadata cannot authorize TX in an RX-only build.
  context.tx_allowed = true;
  context.radio_type = RADIO_BK4819;
  context.modulation = MOD_FM;
  assert(checkTX(&context) == TX_DISABLED);
  // The new hardware path must not commit context on command failure.
  snapshot.vfos[0].context = (VFOContext){
      .radio_type = RADIO_BK4819, .modulation = MOD_FM,
      .frequency = 43850000, .filter = FILTER_AUTO
  };
  gRadioState = &snapshot;
  updateContext();
  RadioParameters wide = {RX_WFM, 100000, 0, 0, false};
  RxPlan receivePlan;
  assert(RADIO_MODEL_Plan(100000000, wide,
         RADIO_GetReceiverCapabilities(), &receivePlan) == RX_OK);
  hardwareApplyOk = false;
  assert(!RADIO_ApplyRxPlan(ctx, &receivePlan));
  assert(hardwareApplies == 1 && audioMutes > 0);
  assert(ctx->frequency == 43850000 &&
         ctx->radio_type == RADIO_BK4819 && ctx->modulation == MOD_FM);
  hardwareApplyOk = true;
  assert(RADIO_ApplyRxPlan(ctx, &receivePlan));
  assert(hardwareApplies == 2 && ctx->frequency == 10000000 &&
         ctx->radio_type == (hasSi ? RADIO_SI4732 : RADIO_BK1080) &&
         !ctx->tx_allowed && !ctx->save_to_eeprom);
  assert(!ctx->dirty[PARAM_FREQUENCY] && !ctx->dirty[PARAM_MODULATION]);
  // No implicit WFM -> BK narrowband fallback.
  RxPlan invalid = receivePlan;
  invalid.device = RX_DEVICE_BK;
  assert(!RADIO_ApplyRxPlan(ctx, &invalid));
  assert(hardwareApplies == 2);

  unsigned initialProbes = probes;
  RADIO_GetReceiverCapabilities();
  RADIO_GetReceiverCapabilities();
  assert(probes == initialProbes);
  testFrequencyInput();
  testBandNames();
  return 0;
}

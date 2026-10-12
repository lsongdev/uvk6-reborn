#include "hal/led.h"
#include "../src/core/radio.c"
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
void LED_SetGreen(bool on) { (void)on; }
void BACKLIGHT_On(void) {}
void BACKLIGHT_Toggle(bool on) { (void)on; }
void SYS_DelayMs(uint32_t ms) { (void)ms; }
bool BK1080_Detect(void) { ++probes; return !hasSi; }
bool SI47XX_Detect(void) { ++probes; return hasSi; }
bool SETTINGS_IsPatchPresent(void) { return true; }
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

int main(int argc, char **argv) {
  (void)argv;
  hasSi = argc == 1;
  RadioState state;
  gRadioState = &state;
  RADIO_InitState(&state);
  ctx->frequency = 43850000;
  ctx->radio_type = RADIO_BK4819;
  ctx->modulation = MOD_FM;
  RadioParameters wide = {RX_WFM, 100000, 0, 0};
  RxPlan plan;
  assert(RADIO_MODEL_Plan(100000000, wide,
         RADIO_GetReceiverCapabilities(), &plan) == RX_OK);
  hardwareApplyOk = false;
  assert(!RADIO_ApplyRxPlan(ctx, &plan));
  assert(hardwareApplies == 1 && audioMutes > 0);
  assert(ctx->frequency == 43850000 && ctx->radio_type == RADIO_BK4819);
  hardwareApplyOk = true;
  assert(RADIO_ApplyRxPlan(ctx, &plan));
  assert(hardwareApplies == 2 && ctx->frequency == 10000000 &&
         ctx->radio_type == (hasSi ? RADIO_SI4732 : RADIO_BK1080));
  RxPlan invalid = plan; invalid.device = RX_DEVICE_BK;
  assert(!RADIO_ApplyRxPlan(ctx, &invalid));
  assert(hardwareApplies == 2);
  unsigned initialProbes = probes;
  RADIO_GetReceiverCapabilities();
  RADIO_GetReceiverCapabilities();
  assert(probes == initialProbes);
  testFrequencyInput();
  return 0;
}

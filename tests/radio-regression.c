// Host regression tests exercise production tuning logic with hardware mocked.
#include "../src/radio.c"
#include <assert.h>

static bool hasSi = true;
Settings gSettings;
const char *TX_OFFSET_NAMES[4] = {"None", "+", "-", "Freq"};
const char *TX_POWER_NAMES[4] = {"Low", "Mid", "High", "Min"};
uint16_t BK1080_ReadRegister(BK1080_Register_t reg) {
  (void)reg;
  return hasSi ? 0 : 0x1080;
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

int main(void) {
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
  assert(context.radio_type == RADIO_SI4732);
  assert(context.current_band == &si4732_bands[2]);
  assert(context.modulation == MOD_FM && context.gain == 0);

  RADIO_SetParam(&context, PARAM_FREQUENCY, 710000, true);
  assert(context.frequency == 710000);
  assert(context.radio_type == RADIO_SI4732);
  assert(context.modulation == MOD_AM);
  assert(context.current_band == &si4732_bands[0]);
  RADIO_SetParam(&context, PARAM_FREQUENCY, 14550000, true);
  assert(context.frequency == 14550000);
  assert(context.radio_type == RADIO_BK4819);

  hasSi = false;
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

  hasSi = true;
  VFO stored = {.radio = RADIO_SI4732, .rxF = 9592500,
                .modulation = MOD_FM, .gainIndex = 0};
  setCommonParamsFromCh(&context, &stored);
  assert(context.frequency == 9592500 && context.volume == 100);
  assert(context.dirty[PARAM_VOLUME]);
  stored.radio = RADIO_BK4819;
  stored.gainIndex = AUTO_GAIN_INDEX;
  setCommonParamsFromCh(&context, &stored);
  assert(context.radio_type == RADIO_SI4732 && context.gain == 0);
  testFrequencyInput();
  testBandNames();
  return 0;
}

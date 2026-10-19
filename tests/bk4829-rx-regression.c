// Real chip RX/SQL drivers over a register model. Golden values are from
// F4HWN 5fedc09 and ApeX 41cc768; no hardware or storage writes occur.
#include "drivers/bk48x9/bk48x9.h"
#include "hal/board.h"
#include <assert.h>
#include <string.h>

static uint16_t regs[128], event;
static uint16_t rxTransitions[16];
static unsigned transitions;
void BK48X9_BusInit(void) {}
void BK48X9_ConfigureMicrophone(uint8_t mic) { (void)mic; }
void SYS_DelayMs(uint32_t ms) { (void)ms; }
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) {
  if (reg == BK48X9_REG_0C) return event ? 1 : 0;
  if (reg == BK48X9_REG_02) { uint16_t result = event; event = 0; return result; }
  return regs[reg];
}
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) {
  regs[reg] = value;
  if (reg == BK48X9_REG_30) {
    assert(transitions < 16); rxTransitions[transitions++] = value;
  }
}
int main(void) {
#if TEST_BK4829
  BK48X9_Init(16, 130);
  assert(regs[0x37] == 0x9D1F && regs[0x36] == 0 && regs[0x33] == 0x9040);
  assert(regs[0x1F] == 0xC65A && regs[0x7E] == 0x303E);
  assert(regs[0x10] == 0x0318 && regs[0x14] == 0x0210);
  assert(regs[0x49] == 0x2AB2 && regs[0x7B] == 0x73DC);
  regs[0x3C] = 0xABCD;
  BK48X9_SetRxCalibration(-2, 44, 14);
  assert(regs[0x3B] == 22654 && regs[0x48] == 0xB2CE && regs[0x3C] == 0xABCD);
  BK48X9_SetRxCalibration(-1001, 255, 255);
  assert(regs[0x3B] == 22656 && regs[0x48] == 0xB3A8);
  BK48X9_SetRxCalibration(1001, 0, 0);
  assert(regs[0x3B] == 22656 && regs[0x48] == 0xB000);
  BK48X9_SetRxCalibration(-2, 44, 14);
  BK48X9_RX_TurnOn();
  assert(regs[0x37] == 0x9F1F && transitions == 2 && rxTransitions[0] == 0 && rxTransitions[1] == 0xBFF1);
  BK48X9_SetAF(BK48X9_AF_MUTE);
  BK48X9_SetRxMode(false);
  assert(regs[0x47] == 0x6042 && !(regs[0x31] & 1));
  assert(regs[0x42] == 0x6B5A && regs[0x2A] == 0x7400 && regs[0x2B] == 0 && regs[0x2F] == 0x9890);
  assert(regs[0x54] == 0x9009 && regs[0x55] == 0x31A9 && regs[0x3D] == 0x2AAB);
  assert(regs[0x48] == 0xB2CF && !(regs[0x73] & 0x10) && regs[0x3C] == 0xABCD);
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_6k); assert(regs[0x43] == 0x2058);
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_12k); assert(regs[0x43] == 0x4048);
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_26k); assert(regs[0x43] == 0x3028);
  // User delays reach REG_4E; Auto resolves to the reference 5/3.
  BK48X9_SetupSquelch((SQL){.ro=50,.rc=40,.no=70,.nc=38,.go=16,.gc=8}, 1, 1);
  assert(regs[0x4E] == 0x4B10);
  BK48X9Capabilities caps = BK48X9_GetCapabilities();
  assert(caps.am && caps.squelch_open_delay == 5 && caps.squelch_close_delay == 3);
  BK48X9_SetupSquelch((SQL){.ro=50,.rc=40,.no=70,.nc=38,.go=16,.gc=8},
                      caps.squelch_open_delay, caps.squelch_close_delay);
  assert(regs[0x4D] == 0xA008 && regs[0x4E] == 0x6F10 && regs[0x4F] == 0x2646 && regs[0x78] == 0x3228);
  BK48X9_CssOff();
  event = BK48X9_REG_02_SQUELCH_LOST;
  assert(BK48X9_IsSquelchOpen());
  BK48X9_TuneTo(14550000u, true);
  assert(!BK48X9_IsSquelchOpen()); // Stale state cannot survive a fast hop.
  assert(regs[0x38] == (14550000u & 0xFFFF) && regs[0x39] == (14550000u >> 16));
  assert(transitions == 4 && rxTransitions[2] == 0 && rxTransitions[3] == 0xBFF1);
  event = BK48X9_REG_02_SQUELCH_LOST;
  assert(BK48X9_IsSquelchOpen()); // Fresh event still opens.
  BK48X9_ToggleAFDAC(false);
  BK48X9_TuneTo(43850000u, true);
  assert(!(regs[0x30] & BK48X9_REG_30_ENABLE_AF_DAC)); // Sweep stays silent.
  assert(regs[0x3F] == (BK48X9_REG_3F_SQUELCH_FOUND | BK48X9_REG_3F_SQUELCH_LOST));
  BK48X9_SetCTCSSFrequency(670); assert(regs[0x51] == 0x9040 && regs[0x07] == 1383);
  BK48X9_SetCDCSSCodeWord(0x123456); assert(regs[0x51] == 0xA033 && regs[0x07] == 2775);
  // AM: stock demodulator values, AFC off, AM filter preset below 26k.
  BK48X9_SetRxMode(true);
  assert((regs[0x31] & 1) && regs[0x42] == 0x6F5C && regs[0x2A] == 0x7434 && (regs[0x73] & 0x10));
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_6k); assert(regs[0x43] == 0x345C);
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_12k); assert(regs[0x43] == 0x345C);
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_26k); assert(regs[0x43] == 0x3028);
  BK48X9_SetRxMode(false);
  assert(!(regs[0x31] & 1) && regs[0x42] == 0x6B5A && !(regs[0x73] & 0x10));
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_6k); assert(regs[0x43] == 0x2058);
#else
  // The BK4819 backend keeps its original VCO restart and AF routing.
  BK48X9Capabilities caps = BK48X9_GetCapabilities();
  assert(caps.am && caps.squelch_open_delay == 1 && caps.squelch_close_delay == 1); // Auto = K6's 1/1.
  BK48X9_RX_TurnOn(); BK48X9_TuneTo(14550000u, true);
  assert(rxTransitions[0] == 0x0200 && rxTransitions[2] == 0x0200);
  BK48X9_SetAF(BK48X9_AF_FM); assert(regs[0x47] == 0x6140);
#endif
  return 0;
}

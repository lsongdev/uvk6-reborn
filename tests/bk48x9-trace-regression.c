// Preserve the complete register-write sequence across the family refactor.
// Baselines are captured from the pre-refactor driver, not derived from its
// replacement. The same scenario runs against each selected chip backend.
#include "drivers/bk48x9/bk48x9.h"
#include "hal/board.h"
#include <assert.h>
#include <stdio.h>
static uint16_t regs[128];
static uint32_t trace=2166136261u;
static unsigned writes;
void BK48X9_BusInit(void) {}
void SYS_DelayMs(uint32_t ms) { (void)ms; }
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) { return reg==0x0C ? 0 : regs[reg]; }
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) {
  regs[reg]=value;++writes;
  trace=(trace ^ (uint8_t)reg)*16777619u;
  trace=(trace ^ (uint8_t)value)*16777619u;
  trace=(trace ^ (uint8_t)(value>>8))*16777619u;
}
int main(void) {
  BK48X9_Init(16,130);
  BK48X9_SetRxCalibration(-2,44,14);
  BK48X9_RX_TurnOn();BK48X9_SetRxMode(false);
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_12k);BK48X9_SetAGC(0);
  BK48X9_SetAFC(8);BK48X9_SetAFCSpeed(63);
  BK48X9_SetupSquelch((SQL){50,40,70,38,8,16},1,1);
  BK48X9_SelectFilter(14550000u,24000000u);BK48X9_CssOff();
  BK48X9_TuneTo(14550000u,true);BK48X9_SetAF(BK48X9_AF_FM);
  BK48X9_ToggleAFDAC(false);BK48X9_TuneTo(43875000u,true);
  BK48X9_SetCTCSSFrequency(670);BK48X9_CssResetDetection(false);
  BK48X9_TuneTo(43875000u,true);BK48X9_SetCDCSSCodeWord(0x123456u);
  BK48X9_CssResetDetection(true);BK48X9_ExitSubAu();
  BK48X9_EnableFrequencyScanEx(F_SC_T_0_2s);BK48X9_StopScan();
  BK48X9_InvalidateRxMode();
  BK48X9_ConfigureVoiceTx(43875000u,16,1300,24000000u);
  BK48X9_SetCTCSSFrequency(670);BK48X9_PrepareTransmit();
  BK48X9_SetupPowerAmplifier(90,43875000u);
  BK48X9_ToggleGpioOut(BK48X9_GPIO1_PIN29_PA_ENABLE,true);
  BK48X9_ToggleGpioOut(BK48X9_GPIO1_PIN29_PA_ENABLE,false);
  BK48X9_WriteRegister(BK48X9_REG_36,0);BK48X9_EnterTxMute();
  BK48X9_Idle();BK48X9_ExitSubAu();BK48X9_InvalidateRxMode();
  BK48X9_RX_TurnOn();BK48X9_SetRxMode(false);
  BK48X9_ToggleGpioOut(BK48X9_GPIO0_PIN28_RX_ENABLE,true);
  BK48X9_TuneTo(14550000u,true);
  BK48X9_SetAGC(8);(void)BK48X9_GetAttenuation();
  BK48X9_SetRxMode(true);BK48X9_SetRxMode(false);
  printf("%u %u %08x\n",TEST_BK4829,writes,trace);
#ifdef EXPECTED_TRACE
  assert(trace==EXPECTED_TRACE && writes==EXPECTED_WRITES);
#endif
  return 0;
}

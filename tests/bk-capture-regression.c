#include "drivers/bk48x9/bk48x9.h"
#include <assert.h>

static uint16_t high, low, scanWord;
static unsigned lowReads, idleCalls;
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) {
  if (reg==BK48X9_REG_0D) return high;
  assert(reg==BK48X9_REG_0E); ++lowReads; return low;
}
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) {
  assert(reg==BK48X9_REG_32); scanWord=value;
}
void BK48X9_Idle(void) { ++idleCalls; }
int main(void) {
  uint32_t raw=123;
  high=0x8000;
  assert(!BK48X9_GetFrequencyScanResult(&raw) && raw==123 && !lowReads);
  high=0x0123;low=0x4567;
  assert(BK48X9_GetFrequencyScanResult(&raw) && raw==0x01234567u && lowReads==1);
  high=0x7fff;low=0xffff;
  assert(BK48X9_GetFrequencyScanResult(&raw) && raw==0x07ffffffu);
  BK48X9_EnableFrequencyScanEx(F_SC_T_0_2s);assert(scanWord==0x0245);
  BK48X9_EnableFrequencyScanEx(F_SC_T_0_4s);assert(scanWord==0x4245);
  BK48X9_DisableFrequencyScan();assert(scanWord==0x0244);
  BK48X9_StopScan();BK48X9_StopScan();assert(scanWord==0x0244 && idleCalls==2);
  return 0;
}

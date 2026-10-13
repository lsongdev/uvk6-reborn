#include "drivers/bk4819/bk4819.h"
#include <assert.h>

static uint16_t high, low, scanWord;
static unsigned lowReads, idleCalls;
uint16_t BK4819_ReadRegister(BK4819_REGISTER_t reg) {
  if (reg==BK4819_REG_0D) return high;
  assert(reg==BK4819_REG_0E); ++lowReads; return low;
}
void BK4819_WriteRegister(BK4819_REGISTER_t reg, uint16_t value) {
  assert(reg==BK4819_REG_32); scanWord=value;
}
void BK4819_Idle(void) { ++idleCalls; }
int main(void) {
  uint32_t raw=123;
  high=0x8000;
  assert(!BK4819_GetFrequencyScanResult(&raw) && raw==123 && !lowReads);
  high=0x0123;low=0x4567;
  assert(BK4819_GetFrequencyScanResult(&raw) && raw==0x01234567u && lowReads==1);
  high=0x7fff;low=0xffff;
  assert(BK4819_GetFrequencyScanResult(&raw) && raw==0x07ffffffu);
  BK4819_EnableFrequencyScanEx(F_SC_T_0_2s);assert(scanWord==0x0245);
  BK4819_EnableFrequencyScanEx(F_SC_T_0_4s);assert(scanWord==0x4245);
  BK4819_DisableFrequencyScan();assert(scanWord==0x0244);
  BK4819_StopScan();BK4819_StopScan();assert(scanWord==0x0244 && idleCalls==2);
  return 0;
}

#include "drivers/bk48x9/bk48x9.h"

bool BK48X9_GetFrequencyScanResult(uint32_t *frequency) {
  uint16_t high = BK48X9_ReadRegister(BK48X9_REG_0D);
  bool finished = (high & 0x8000) == 0;

  if (finished) {
    uint16_t low = BK48X9_ReadRegister(BK48X9_REG_0E);
    *frequency = (uint32_t)((high & 0x7FF) << 16) | low;
  }

  return finished;
}


void BK48X9_EnableFrequencyScanEx(FreqScanTime time) {
  BK48X9_WriteRegister(BK48X9_REG_32, 0x0245 | (time << 14));
}


void BK48X9_DisableFrequencyScan(void) {
  BK48X9_WriteRegister(BK48X9_REG_32, 0x0244);
}

void BK48X9_StopScan(void) {
  BK48X9_DisableFrequencyScan();
  BK48X9_Idle();
}


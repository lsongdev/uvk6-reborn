#include "drivers/bk4819/bk4819.h"

bool BK4819_GetFrequencyScanResult(uint32_t *frequency) {
  uint16_t high = BK4819_ReadRegister(BK4819_REG_0D);
  bool finished = (high & 0x8000) == 0;

  if (finished) {
    uint16_t low = BK4819_ReadRegister(BK4819_REG_0E);
    *frequency = (uint32_t)((high & 0x7FF) << 16) | low;
  }

  return finished;
}


void BK4819_EnableFrequencyScanEx(FreqScanTime time) {
  BK4819_WriteRegister(BK4819_REG_32, 0x0245 | (time << 14));
}


void BK4819_DisableFrequencyScan(void) {
  BK4819_WriteRegister(BK4819_REG_32, 0x0244);
}

void BK4819_StopScan(void) {
  BK4819_DisableFrequencyScan();
  BK4819_Idle();
}


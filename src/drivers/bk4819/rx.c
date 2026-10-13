#include "drivers/bk4819/bk4819.h"

static uint8_t lastMode = UINT8_MAX;

void BK4819_InvalidateRxMode(void) { lastMode = UINT8_MAX; }

// Preserve the former NFM/AM register sequence. Keeping this entry point
// specific to production RX avoids linking legacy BK WFM/SSB/bypass setup.
void BK4819_SetRxMode(bool am) {
  if (lastMode == (uint8_t)am) return;
  lastMode = (uint8_t)am;
  BK4819_SetAF(BK4819_AF_FM);
  BK4819_SetRegValue(RS_AFC_DIS, 0);
  BK4819_XtalSet(XTAL_2_26M);
  uint16_t reg31 = BK4819_ReadRegister(BK4819_REG_31);
  BK4819_WriteRegister(BK4819_REG_31, am ? reg31 | 1u : reg31 & ~1u);
  BK4819_WriteRegister(0x42, am ? 0x6F5C : 0x6B5A);
  BK4819_WriteRegister(0x2A, am ? 0x7434 : 0x7400);
  BK4819_WriteRegister(BK4819_REG_2B, am ? 0x0600 : 0);
  BK4819_WriteRegister(0x2F, am ? 0x9990 : 0x9890);
}

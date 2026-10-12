#include "drivers/bk4819/bk4819.h"

// Track FOUND/LOST events; never open on an unverified/stale tone.
static bool cssDetected;
void BK4819_CssResetDetection(bool digital) {
  cssDetected = false;
  BK4819_WriteRegister(BK4819_REG_3F, 0);
  // Bounded drain: a stuck interrupt must not block the main loop.
  for (unsigned i=0; i<8 && (BK4819_ReadRegister(BK4819_REG_0C)&1u); ++i) {
    BK4819_WriteRegister(BK4819_REG_02, 0);
    (void)BK4819_ReadRegister(BK4819_REG_02);
  }
  BK4819_WriteRegister(BK4819_REG_3F, digital ?
    BK4819_REG_3F_CDCSS_FOUND | BK4819_REG_3F_CDCSS_LOST :
    BK4819_REG_3F_CTCSS_FOUND | BK4819_REG_3F_CTCSS_LOST);
}
bool BK4819_CssIsDetected(bool digital) {
  uint16_t found = digital ? BK4819_REG_02_CDCSS_FOUND : BK4819_REG_02_CTCSS_FOUND;
  uint16_t lost = digital ? BK4819_REG_02_CDCSS_LOST : BK4819_REG_02_CTCSS_LOST;
  for (unsigned i=0; i<8 && (BK4819_ReadRegister(BK4819_REG_0C)&1u); ++i) {
    BK4819_WriteRegister(BK4819_REG_02, 0);
    uint16_t status = BK4819_ReadRegister(BK4819_REG_02);
    if (status & found) cssDetected = true;
    if (status & lost) cssDetected = false;
  }
  return cssDetected;
}


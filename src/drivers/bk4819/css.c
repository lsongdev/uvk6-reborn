#include "drivers/bk4819/bk4819.h"
#include "drivers/bk4819/internal.h"

// Interrupt events (REG_02, flagged by REG_0C bit 0) feed both the CTCSS/DCS
// match and, on the BK4829, the squelch state: reading REG_02 consumes the
// events, so one pump serves both. Never open on an unverified/stale tone.
static bool cssDetected, cssDigital, squelchOpen;
static uint16_t cssMask;

// The BK4829's REG_0C bit 1 does not hold a steady squelch state (it toggles
// on a strong signal), so it reports squelch through SQUELCH_LOST (audio
// open) and SQUELCH_FOUND (closed) events, as the reference K1 firmware does.
static uint16_t squelchMask(void) {
  return isBk4829() ? BK4819_REG_3F_SQUELCH_FOUND | BK4819_REG_3F_SQUELCH_LOST : 0;
}
static void writeMask(void) { BK4819_WriteRegister(BK4819_REG_3F, cssMask | squelchMask()); }

// Bounded: a stuck interrupt must not block the main loop.
static void pump(void) {
  uint16_t found = cssDigital ? BK4819_REG_02_CDCSS_FOUND : BK4819_REG_02_CTCSS_FOUND;
  uint16_t lost = cssDigital ? BK4819_REG_02_CDCSS_LOST : BK4819_REG_02_CTCSS_LOST;
  for (unsigned i=0; i<8 && (BK4819_ReadRegister(BK4819_REG_0C)&1u); ++i) {
    BK4819_WriteRegister(BK4819_REG_02, 0);
    uint16_t status = BK4819_ReadRegister(BK4819_REG_02);
    if (cssMask && (status & found)) cssDetected = true;
    if (cssMask && (status & lost)) cssDetected = false;
    if (status & BK4819_REG_02_SQUELCH_LOST) squelchOpen = true;
    if (status & BK4819_REG_02_SQUELCH_FOUND) squelchOpen = false;
  }
}

static void drain(void) {
  BK4819_WriteRegister(BK4819_REG_3F, 0);
  for (unsigned i=0; i<8 && (BK4819_ReadRegister(BK4819_REG_0C)&1u); ++i) {
    BK4819_WriteRegister(BK4819_REG_02, 0);
    (void)BK4819_ReadRegister(BK4819_REG_02);
  }
}

void BK4819_CssResetDetection(bool digital) {
  cssDetected = false;
  cssDigital = digital;
  cssMask = digital ? BK4819_REG_3F_CDCSS_FOUND | BK4819_REG_3F_CDCSS_LOST
                    : BK4819_REG_3F_CTCSS_FOUND | BK4819_REG_3F_CTCSS_LOST;
  drain();
  squelchOpen = false;
  writeMask();
}
void BK4819_CssOff(void) {
  cssDetected = false;
  cssMask = 0;
  writeMask();
}
bool BK4819_CssIsDetected(bool digital) {
  (void)digital; // The type set by BK4819_CssResetDetection.
  pump();
  return cssDetected;
}

void BK4819_SquelchEventsReset(void) {
  drain();
  squelchOpen = false;
  writeMask();
}
bool BK4819_SquelchEventOpen(void) {
  pump();
  return squelchOpen;
}

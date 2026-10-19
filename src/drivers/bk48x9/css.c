#include "drivers/bk48x9/bk48x9.h"
#include "drivers/bk48x9/internal.h"

// Interrupt events (REG_02, flagged by REG_0C bit 0) feed both the CTCSS/DCS
// match and, on the BK4829, the squelch state: reading REG_02 consumes the
// events, so one pump serves both. Never open on an unverified/stale tone.
static bool cssDetected, cssDigital, squelchOpen;
static uint16_t cssMask;

// The BK4829's REG_0C bit 1 does not hold a steady squelch state (it toggles
// on a strong signal), so it reports squelch through SQUELCH_LOST (audio
// open) and SQUELCH_FOUND (closed) events, as the reference K1 firmware does.
static uint16_t squelchMask(void) {
  return BK48X9_CHIP.squelch_interrupts;
}
static void writeMask(void) { BK48X9_WriteRegister(BK48X9_REG_3F, cssMask | squelchMask()); }

// Bounded: a stuck interrupt must not block the main loop.
static void pump(void) {
  uint16_t found = cssDigital ? BK48X9_REG_02_CDCSS_FOUND : BK48X9_REG_02_CTCSS_FOUND;
  uint16_t lost = cssDigital ? BK48X9_REG_02_CDCSS_LOST : BK48X9_REG_02_CTCSS_LOST;
  for (unsigned i=0; i<8 && (BK48X9_ReadRegister(BK48X9_REG_0C)&1u); ++i) {
    BK48X9_WriteRegister(BK48X9_REG_02, 0);
    uint16_t status = BK48X9_ReadRegister(BK48X9_REG_02);
    if (cssMask && (status & found)) cssDetected = true;
    if (cssMask && (status & lost)) cssDetected = false;
    if (status & BK48X9_REG_02_SQUELCH_LOST) squelchOpen = true;
    if (status & BK48X9_REG_02_SQUELCH_FOUND) squelchOpen = false;
  }
}

static void drain(void) {
  BK48X9_WriteRegister(BK48X9_REG_3F, 0);
  for (unsigned i=0; i<8 && (BK48X9_ReadRegister(BK48X9_REG_0C)&1u); ++i) {
    BK48X9_WriteRegister(BK48X9_REG_02, 0);
    (void)BK48X9_ReadRegister(BK48X9_REG_02);
  }
}

void BK48X9_CssResetDetection(bool digital) {
  cssDetected = false;
  cssDigital = digital;
  cssMask = digital ? BK48X9_REG_3F_CDCSS_FOUND | BK48X9_REG_3F_CDCSS_LOST
                    : BK48X9_REG_3F_CTCSS_FOUND | BK48X9_REG_3F_CTCSS_LOST;
  drain();
  squelchOpen = false;
  writeMask();
}
void BK48X9_CssOff(void) {
  cssDetected = false;
  cssMask = 0;
  writeMask();
}
bool BK48X9_CssIsDetected(bool digital) {
  (void)digital; // The type set by BK48X9_CssResetDetection.
  pump();
  return cssDetected;
}

void BK48X9_SquelchEventsReset(void) {
  drain();
  squelchOpen = false;
  writeMask();
}
bool BK48X9_SquelchEventOpen(void) {
  pump();
  return squelchOpen;
}

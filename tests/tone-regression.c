#include "core/tone.h"
#include "drivers/bk48x9/bk48x9.h"
#include "drivers/bk48x9/internal.h"
#include <assert.h>
#include <string.h>
void _putchar(char c) { (void)c; }
static uint16_t events[16], mask;
static unsigned count, at, clears;
static bool stuck;
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) {
  if (reg == BK48X9_REG_0C) return stuck || at<count;
  if (reg == BK48X9_REG_02) return at<count ? events[at++] : 0;
  return 0;
}
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) {
  if (reg == BK48X9_REG_3F) mask=value;
  if (reg == BK48X9_REG_02) { assert(value==0); ++clears; }
}
static void event(uint16_t value) { count=1; at=0; events[0]=value; }
// The backend's squelch interrupts, as drivers/bk48x9/bk48*.c define them.
const BK48X9Chip BK48X9_CHIP = {.squelch_interrupts = TEST_BK4829 ? 0x000C : 0};
int main(void) {
  char text[24];
  TONE_Format(0,text);assert(!strcmp(text,"Off"));
  TONE_Format(1,text);assert(!strcmp(text,"67.0 Hz"));
  TONE_Format(51,text);assert(!strcmp(text,"D023N"));
  TONE_Format(155,text);assert(!strcmp(text,"D023I"));
  for(unsigned i=0;i<RADIO_TONE_COUNT;++i) {
    assert(TONE_Valid(i));
    assert(TONE_Next(TONE_Next(i,true),false)==i);
    assert(TONE_Code(i)<(TONE_Type(i)==CODE_TYPE_CONTINUOUS_TONE ? 50 : 104));
  }
  assert(!TONE_Valid(259));
  assert(DCS_GetGolayCodeWord(CODE_TYPE_DIGITAL,0) ==
    (DCS_GetGolayCodeWord(CODE_TYPE_REVERSE_DIGITAL,0)^0x7fffffu));
#if !TEST_BK4829
  event(BK48X9_REG_02_CTCSS_FOUND);
  BK48X9_CssResetDetection(false); // A stale FOUND must not survive retuning.
  assert(mask==(BK48X9_REG_3F_CTCSS_FOUND|BK48X9_REG_3F_CTCSS_LOST));
  assert(!BK48X9_CssIsDetected(false));
  event(BK48X9_REG_02_CTCSS_FOUND);assert(BK48X9_CssIsDetected(false));
  assert(BK48X9_CssIsDetected(false)); // Holds until LOST.
  event(BK48X9_REG_02_CTCSS_LOST);assert(!BK48X9_CssIsDetected(false));
  BK48X9_CssResetDetection(true);
  assert(mask==(BK48X9_REG_3F_CDCSS_FOUND|BK48X9_REG_3F_CDCSS_LOST));
  event(BK48X9_REG_02_CTCSS_FOUND);assert(!BK48X9_CssIsDetected(true));
  event(BK48X9_REG_02_CDCSS_FOUND);assert(BK48X9_CssIsDetected(true));
  event(BK48X9_REG_02_CDCSS_FOUND|BK48X9_REG_02_CDCSS_LOST);
  assert(!BK48X9_CssIsDetected(true));
  stuck=true;unsigned before=clears;
  assert(!BK48X9_CssIsDetected(true));assert(clears-before==8);
  before=clears;BK48X9_CssResetDetection(false);assert(clears-before==8);
  stuck=false;
  BK48X9_CssOff(); assert(mask==0); // BK4819 keeps its original masks.
#else
  // BK4829: squelch from LOST (open) / FOUND (closed) events, sharing the
  // pump with tone detection so neither consumes the other's events.
  BK48X9_CssResetDetection(false); // CTCSS armed alongside squelch events.
  BK48X9_SquelchEventsReset();
  assert(mask==(BK48X9_REG_3F_CTCSS_FOUND|BK48X9_REG_3F_CTCSS_LOST|
                BK48X9_REG_3F_SQUELCH_FOUND|BK48X9_REG_3F_SQUELCH_LOST));
  assert(!BK48X9_SquelchEventOpen());
  event(BK48X9_REG_02_SQUELCH_LOST);assert(BK48X9_SquelchEventOpen());
  assert(BK48X9_SquelchEventOpen()); // Holds without new events.
  event(BK48X9_REG_02_SQUELCH_FOUND|BK48X9_REG_02_CTCSS_FOUND);
  assert(!BK48X9_SquelchEventOpen() && BK48X9_CssIsDetected(false));
  event(BK48X9_REG_02_SQUELCH_LOST);
  assert(BK48X9_CssIsDetected(false) && BK48X9_SquelchEventOpen()); // Each keeps its own state.
  BK48X9_CssOff();
  assert(mask==(BK48X9_REG_3F_SQUELCH_FOUND|BK48X9_REG_3F_SQUELCH_LOST));
  BK48X9_CssResetDetection(false); // Retune: stale open squelch is forgotten.
  assert(!BK48X9_SquelchEventOpen());
#endif
  return 0;
}

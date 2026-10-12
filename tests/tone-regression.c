#include "core/tone.h"
#include "drivers/bk4819/bk4819.h"
#include <assert.h>
#include <string.h>
void _putchar(char c) { (void)c; }
static uint16_t events[16], mask;
static unsigned count, at, clears;
static bool stuck;
uint16_t BK4819_ReadRegister(BK4819_REGISTER_t reg) {
  if (reg == BK4819_REG_0C) return stuck || at<count;
  if (reg == BK4819_REG_02) return at<count ? events[at++] : 0;
  return 0;
}
void BK4819_WriteRegister(BK4819_REGISTER_t reg, uint16_t value) {
  if (reg == BK4819_REG_3F) mask=value;
  if (reg == BK4819_REG_02) { assert(value==0); ++clears; }
}
static void event(uint16_t value) { count=1; at=0; events[0]=value; }
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
  event(BK4819_REG_02_CTCSS_FOUND);
  BK4819_CssResetDetection(false); // A stale FOUND must not survive retuning.
  assert(mask==(BK4819_REG_3F_CTCSS_FOUND|BK4819_REG_3F_CTCSS_LOST));
  assert(!BK4819_CssIsDetected(false));
  event(BK4819_REG_02_CTCSS_FOUND);assert(BK4819_CssIsDetected(false));
  assert(BK4819_CssIsDetected(false)); // Holds until LOST.
  event(BK4819_REG_02_CTCSS_LOST);assert(!BK4819_CssIsDetected(false));
  BK4819_CssResetDetection(true);
  assert(mask==(BK4819_REG_3F_CDCSS_FOUND|BK4819_REG_3F_CDCSS_LOST));
  event(BK4819_REG_02_CTCSS_FOUND);assert(!BK4819_CssIsDetected(true));
  event(BK4819_REG_02_CDCSS_FOUND);assert(BK4819_CssIsDetected(true));
  event(BK4819_REG_02_CDCSS_FOUND|BK4819_REG_02_CDCSS_LOST);
  assert(!BK4819_CssIsDetected(true));
  stuck=true;unsigned before=clears;
  assert(!BK4819_CssIsDetected(true));assert(clears-before==8);
  before=clears;BK4819_CssResetDetection(false);assert(clears-before==8);
  return 0;
}

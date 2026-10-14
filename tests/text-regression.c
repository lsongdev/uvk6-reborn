// The printf-free text builder must match the libc formats it replaced.
#include "core/text.h"
#include "core/tone.h"
#include "core/dcs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void number(uint32_t value, uint8_t width, uint8_t base) {
  char got[16], want[16];
  Text t = TEXT_Begin(got, sizeof(got));
  TEXT_Number(&t, value, width, base);
  snprintf(want, sizeof(want), base == 8 ? "%0*o" : "%0*u", width, value);
  assert(!strcmp(got, want));
}

int main(void) {
  static const uint32_t values[] = {0, 1, 7, 8, 9, 10, 99, 100, 512, 999, 1000,
    65535, 1340000000u, UINT32_MAX};
  for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
    for (uint8_t width = 0; width <= 6; ++width) {
      number(values[i], width, 10);
      number(values[i], width, 8);
    }
  // Truncation always keeps the terminator inside the buffer.
  char small[5];
  Text t = TEXT_Begin(small, sizeof(small));
  TEXT_Add(&t, "CH ");
  TEXT_Number(&t, 123, 3, 10);
  TEXT_Char(&t, '?');
  assert(!strcmp(small, "CH 1"));
  TEXT_Copy(small, sizeof(small), NULL);
  assert(!small[0]);
  // Every tone renders exactly as the old "%u.%u Hz" / "D%03o%c" formats.
  for (RadioTone tone = 0; tone <= RADIO_TONE_COUNT; ++tone) {
    char got[24], want[24];
    TONE_Format(tone, got);
    if (!TONE_Valid(tone)) snprintf(want, sizeof(want), "Invalid");
    else if (!tone) snprintf(want, sizeof(want), "Off");
    else if (TONE_Type(tone) == CODE_TYPE_CONTINUOUS_TONE) {
      uint16_t hz10 = CTCSS_Options[TONE_Code(tone)];
      snprintf(want, sizeof(want), "%u.%u Hz", hz10/10, hz10%10);
    } else snprintf(want, sizeof(want), "D%03o%c", DCS_Options[TONE_Code(tone)],
                    TONE_Type(tone) == CODE_TYPE_DIGITAL ? 'N' : 'I');
    assert(!strcmp(got, want));
  }
  return 0;
}

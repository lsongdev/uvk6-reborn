#include "ui/frequency.h"
#include "core/receiver.h"
#include "core/measurements.h"
#include "core/text.h"
#include <string.h>

const char *UI_FormatFrequency(uint32_t frequency_hz, char text[16]) {
  bool khz = frequency_hz < 1000000;
  uint32_t unit = khz ? 1000 : 1000000;
  uint32_t divisor = khz ? unit : 1000;
  unsigned decimals = khz ? 0 : 3;
  while (divisor > 1 && frequency_hz % divisor) {
    divisor /= 10;
    ++decimals;
  }
  Text t = TEXT_Begin(text, 16);
  TEXT_Number(&t, frequency_hz / unit, 1, 10);
  if (decimals) {
    TEXT_Char(&t, '.');
    TEXT_Number(&t, frequency_hz % unit / divisor, decimals, 10);
  }
  return khz ? "kHz" : "MHz";
}

void UI_FrequencyText(uint32_t frequency_hz, char text[20]) {
  const char *unit = UI_FormatFrequency(frequency_hz, text);
  Text t = TEXT_Begin(text + strlen(text), 20 - strlen(text));
  TEXT_Char(&t, ' ');
  TEXT_Add(&t, unit);
}



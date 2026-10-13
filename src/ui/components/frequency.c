#include "ui/components/frequency.h"
#include "core/receiver.h"
#include "core/measurements.h"

const char *UI_FormatFrequency(uint32_t frequency_hz, char text[16]) {
  bool khz = frequency_hz < 1000000;
  uint32_t unit = khz ? 1000 : 1000000;
  uint32_t divisor = khz ? unit : 1000;
  unsigned decimals = khz ? 0 : 3;
  while (divisor > 1 && frequency_hz % divisor) {
    divisor /= 10;
    ++decimals;
  }
  if (decimals)
    snprintf(text, 16, "%u.%0*u", frequency_hz / unit, (int)decimals,
             frequency_hz % unit / divisor);
  else
    snprintf(text, 16, "%u", frequency_hz / unit);
  return khz ? "kHz" : "MHz";
}



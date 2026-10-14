#include "ui/signal.h"

// Eight ascending columns for a 0..14 strength.
void UI_SignalIcon(uint8_t x, uint8_t baseline, uint8_t level) {
  if (level > 14) level = 14;
  uint8_t bars = (level*8u+13u)/14u;
  for (uint8_t i = 0; i < 8; ++i) {
    if (i < bars) FillRect(x+i*3, baseline-i, 2, i+1, C_FILL);
    else DrawHLine(x+i*3, baseline, 2, C_FILL);
  }
}

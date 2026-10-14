#ifndef UI_SIGNAL_H
#define UI_SIGNAL_H
#include "ui/graphics.h"

// Eight ascending columns; baseline is the bottom row, level is 0..14.
void UI_SignalIcon(uint8_t x, uint8_t baseline, uint8_t level);
#endif

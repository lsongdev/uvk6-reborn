#ifndef UI_COMPONENT_SIGNAL_H
#define UI_COMPONENT_SIGNAL_H
#include "ui/graphics.h"

void UI_RSSIBar(uint8_t y);
uint8_t UI_SMeterLevel(void);
// Eight ascending columns; baseline is the bottom row, level is 0..14.
void UI_SignalIcon(uint8_t x, uint8_t baseline, uint8_t level);
void UI_SMeter(uint8_t y);
#endif

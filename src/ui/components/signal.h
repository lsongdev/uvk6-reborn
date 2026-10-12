#ifndef UI_COMPONENT_SIGNAL_H
#define UI_COMPONENT_SIGNAL_H
#include "ui/graphics.h"

void UI_TxBar(uint8_t y);
void UI_RSSIBar(uint8_t y);
uint8_t UI_SMeterLevel(void);
void UI_SignalBar(uint8_t x, uint8_t y, uint8_t level);
void UI_SMeter(uint8_t y);
#endif

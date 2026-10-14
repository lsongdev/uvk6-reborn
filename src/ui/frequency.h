#ifndef UI_FREQUENCY_H
#define UI_FREQUENCY_H
#include "ui/graphics.h"

// Returns the unit; text must have room for 16 bytes. MHz uses at least
// three decimal places; extra digits preserve the actual tuned frequency.
const char *UI_FormatFrequency(uint32_t frequency_hz, char text[16]);
// The same digits followed by a space and the unit, e.g. "438.500 MHz".
void UI_FrequencyText(uint32_t frequency_hz, char text[20]);
#endif

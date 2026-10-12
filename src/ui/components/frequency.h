#ifndef UI_COMPONENT_FREQUENCY_H
#define UI_COMPONENT_FREQUENCY_H
#include "ui/graphics.h"

void UI_BigFrequency(uint8_t y, uint32_t f);
// Returns the unit; text must have room for 16 bytes. MHz uses at least
// three decimal places; extra digits preserve the actual tuned frequency.
const char *UI_FormatFrequency(uint32_t frequency_hz, char text[16]);
#endif

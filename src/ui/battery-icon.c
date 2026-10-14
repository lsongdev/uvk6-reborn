#include "ui/battery-icon.h"

void UI_Battery(uint8_t Level) {
  DrawRect(LCD_WIDTH - 13, 0, 12, 5, C_FILL);
  FillRect(LCD_WIDTH - 12, 1, Level, 3, C_FILL);
  DrawVLine(LCD_WIDTH - 1, 1, 3, C_FILL);

  if (Level > 10) {
    DrawHLine(LCD_WIDTH - 4, 1, 3, C_INVERT);
    DrawHLine(LCD_WIDTH - 8, 1, 5, C_INVERT);
    DrawHLine(LCD_WIDTH - 4, 3, 3, C_INVERT);
  }
}


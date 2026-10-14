#include "ui/spectrum.h"
#include "ui/graphics.h"

static uint8_t barHeight(uint8_t level, uint8_t floor, uint8_t height) {
  if (!level) return 0;
  uint8_t above = level > floor ? (uint8_t)(level - floor) : 0;
  uint8_t h = (uint8_t)(1u + (above >> 1u));
  return h < height ? h : height - 1u;
}

void UI_SpectrumRender(uint8_t y, uint8_t height, const Spectrum *s) {
  FillRect(0, y, LCD_WIDTH, height, C_CLEAR);
  uint8_t floor = SPECTRUM_Floor(s), bottom = y + height;
  for (uint8_t x = 0; x < SPECTRUM_POINTS && x < LCD_WIDTH; ++x) {
    uint8_t h = barHeight(s->level[x], floor, height);
    if (h) DrawVLine(x, bottom - h, h, C_FILL);
    uint8_t p = barHeight(s->peak[x], floor, height);
    if (p > h + 1u) PutPixel(x, bottom - p, C_FILL);
  }
  for (uint8_t row = y; row < bottom; row += 2) PutPixel(s->cursor, row, C_INVERT);
  DrawHLine(s->next ? s->next - 1 : 0, y, 3, C_FILL);
}

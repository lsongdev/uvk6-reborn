#include "ui/graphics.h"
#include "core/misc.h"
#include "ui/fonts/TomThumb.h"
#include "ui/fonts/muMatrix8ptRegular.h"
#include "ui/fonts/symbols.h"
#include <stdlib.h>
#include <string.h>

static Cursor cursor;
static int16_t textClipLeft = 0, textClipRight = LCD_WIDTH;

void UI_ClearStatus(void) { FillRect(0, 0, LCD_WIDTH, 7, C_CLEAR); }
void UI_ClearScreen(void) {
  FillRect(0, 7, LCD_WIDTH, LCD_HEIGHT - 7, C_CLEAR);
}

void PutPixel(uint8_t x, uint8_t y, uint8_t fill) {
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT)
    return;
  uint8_t m = 1 << (y & 7), *p = &gFrameBuffer[y >> 3][x];
  *p = fill ? (fill & 2 ? *p ^ m : *p | m) : *p & ~m;
}

bool GetPixel(uint8_t x, uint8_t y) {
  return gFrameBuffer[y >> 3][x] & (1 << (y & 7));
}

static void DrawALine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      int16_t c) {
  int16_t s = abs(y1 - y0) > abs(x1 - x0);
  if (s) {
    SWAP(x0, y0);
    SWAP(x1, y1);
  }
  if (x0 > x1) {
    SWAP(x0, x1);
    SWAP(y0, y1);
  }

  int16_t dx = x1 - x0, dy = abs(y1 - y0), e = dx >> 1, ys = y0 < y1 ? 1 : -1;
  for (; x0 <= x1; x0++, e -= dy) {
    PutPixel(s ? y0 : x0, s ? x0 : y0, c);
    if (e < 0) {
      y0 += ys;
      e += dx;
    }
  }
}

void DrawVLine(int16_t x, int16_t y, int16_t h, Color c) {
  if (h)
    DrawALine(x, y, x, y + h - 1, c);
}

void DrawHLine(int16_t x, int16_t y, int16_t w, Color c) {
  if (w)
    DrawALine(x, y, x + w - 1, y, c);
}


void DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, Color c) {
  DrawHLine(x, y, w, c);
  DrawHLine(x, y + h - 1, w, c);
  DrawVLine(x, y, h, c);
  DrawVLine(x + w - 1, y, h, c);
}

void FillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color c) {
  for (int16_t i = x, e = x + w; i < e; i++)
    DrawVLine(i, y, h, c);
}

static void m_putchar(int16_t x, int16_t y, uint8_t c, Color col, uint8_t sx,
                      uint8_t sy, const GFXfont *f) {
  const GFXglyph *g = &f->glyph[c - f->first];
  const uint8_t *b = f->bitmap + g->bitmapOffset;
  uint8_t w = g->width, h = g->height, bits = 0, bit = 0;
  int8_t xo = g->xOffset, yo = g->yOffset;

  for (uint8_t yy = 0; yy < h; yy++) {
    for (uint8_t xx = 0; xx < w; xx++, bits <<= 1) {
      if (!(bit++ & 7))
        bits = *b++;
      if (bits & 0x80) {
        int16_t left = x + (xo + xx) * sx;
        // Scaled glyphs gain one pixel per dot so 2x strokes read as bold.
        int16_t right = left + sx + (sx > 1);
        if (left < textClipLeft) left = textClipLeft;
        if (right > textClipRight) right = textClipRight;
        if (right > left)
          FillRect(left, y + (yo + yy) * sy, right-left, sy, col);
      }
    }
  }
}

void charBounds(uint8_t c, int16_t *x, int16_t *y, int16_t *minx, int16_t *miny,
                int16_t *maxx, int16_t *maxy, uint8_t tsx, uint8_t tsy,
                bool wrap, const GFXfont *f) {
  if (c == '\n') {
    *x = 0;
    *y += tsy * f->yAdvance;
    return;
  }
  if (c == '\r' || c < f->first || c > f->last)
    return;

  const GFXglyph *g = &f->glyph[c - f->first];
  if (wrap && (*x + ((g->xOffset + g->width) * tsx) > LCD_WIDTH)) {
    *x = 0;
    *y += tsy * f->yAdvance;
  }

  int16_t x1 = *x + g->xOffset * tsx, y1 = *y + g->yOffset * tsy;
  int16_t x2 = x1 + g->width * tsx - 1, y2 = y1 + g->height * tsy - 1;
  if (x1 < *minx)
    *minx = x1;
  if (y1 < *miny)
    *miny = y1;
  if (x2 > *maxx)
    *maxx = x2;
  if (y2 > *maxy)
    *maxy = y2;
  *x += g->xAdvance * tsx;
}

static void getTextBounds(const char *s, int16_t x, int16_t y, int16_t *x1,
                          int16_t *y1, uint16_t *w, uint16_t *h,
                          const GFXfont *f) {
  int16_t minx = 0x7FFF, miny = 0x7FFF, maxx = -1, maxy = -1;
  for (; *s; s++)
    charBounds(*s, &x, &y, &minx, &miny, &maxx, &maxy, 1, 1, 0, f);
  *x1 = maxx >= minx ? minx : x;
  *y1 = maxy >= miny ? miny : y;
  *w = maxx >= minx ? maxx - minx + 1 : 0;
  *h = maxy >= miny ? maxy - miny + 1 : 0;
}

void write(uint8_t c, uint8_t tsx, uint8_t tsy, bool wrap, Color col,
           const GFXfont *f) {
  if (c == '\n') {
    cursor.x = 0;
    cursor.y += tsy * f->yAdvance;
    return;
  }
  if (c == '\r' || c < f->first || c > f->last)
    return;

  GFXglyph *g = &f->glyph[c - f->first];
  if (g->width && g->height) {
    if (wrap && (cursor.x + tsx * (g->xOffset + g->width) > LCD_WIDTH)) {
      cursor.x = 0;
      cursor.y += tsy * f->yAdvance;
    }
    m_putchar(cursor.x, cursor.y, c, col, tsx, tsy, f);
  }
  cursor.x += g->xAdvance * tsx;
}

static void printStr(const GFXfont *f, uint8_t x, uint8_t y, Color col,
                     TextPos pos, const char *text) {
  int16_t x1, y1;
  uint16_t w, h;
  getTextBounds(text, x, y, &x1, &y1, &w, &h, f);
  cursor.x = pos == POS_C ? x - (w >> 1) : pos == POS_R ? x - w : x;
  cursor.y = y;
  for (; *text; text++)
    write(*text, 1, 1, 1, col, f);
}

void PrintSmall(uint8_t x, uint8_t y, const char *text) {
  printStr(&TomThumb, x, y, C_FILL, POS_L, text);
}
void PrintSmallEx(uint8_t x, uint8_t y, TextPos p, Color c, const char *text) {
  printStr(&TomThumb, x, y, c, p, text);
}
void PrintMedium(uint8_t x, uint8_t y, const char *text) {
  printStr(&MuMatrix8ptRegular, x, y, C_FILL, POS_L, text);
}
void PrintMediumEx(uint8_t x, uint8_t y, TextPos p, Color c, const char *text) {
  printStr(&MuMatrix8ptRegular, x, y, c, p, text);
}
void PrintSymbolsEx(uint8_t x, uint8_t y, TextPos p, Color c, const char *text) {
  printStr(&Symbols, x, y, c, p, text);
}

static uint16_t textWidth(const char *text, const GFXfont *font) {
  uint16_t width = 0;
  if (!text) return 0;
  for (; *text; ++text) {
    uint8_t c = (uint8_t)*text;
    if (c < font->first || c > font->last) c = '?';
    width += font->glyph[c-font->first].xAdvance;
  }
  return width;
}

static void textWindow(uint8_t x, uint8_t baseline, uint8_t width, const char *text, int16_t offset, const GFXfont *font, uint8_t scale) {
  if (!text) return;
  textClipLeft = x;
  textClipRight = x+width;
  int16_t pen = x-offset;
  for (; *text; ++text) {
    uint8_t c = (uint8_t)*text;
    if (c < font->first || c > font->last) c = '?';
    m_putchar(pen, baseline, c, C_FILL, scale, scale, font);
    pen += font->glyph[c-font->first].xAdvance * scale;
    if (pen >= textClipRight) break;
  }
  textClipLeft = 0;
  textClipRight = LCD_WIDTH;
}

uint16_t UI_SymbolsTextWidth(const char *text) { return textWidth(text, &Symbols); }
uint16_t UI_SmallTextWidth(const char *text) { return textWidth(text, &TomThumb); }
uint16_t UI_MediumTextWidth(const char *text) { return textWidth(text, &MuMatrix8ptRegular); }
uint16_t UI_LargeTextWidth(const char *text) { return 2 * UI_MediumTextWidth(text); }
void UI_SmallTextWindow(uint8_t x, uint8_t baseline, uint8_t width, const char *text, int16_t offset) {
  textWindow(x, baseline, width, text, offset, &TomThumb, 1);
}
void UI_MediumTextWindow(uint8_t x, uint8_t baseline, uint8_t width, const char *text, int16_t offset) {
  textWindow(x, baseline, width, text, offset, &MuMatrix8ptRegular, 1);
}
void UI_LargeTextWindow(uint8_t x, uint8_t baseline, uint8_t width, const char *text, int16_t offset) {
  textWindow(x, baseline, width, text, offset, &MuMatrix8ptRegular, 2);
}

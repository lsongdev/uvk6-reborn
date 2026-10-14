#ifndef _GFXFONT_H_
#define _GFXFONT_H_

// to use fonts w/o cleanup
#define PROGMEM

#include <stdint.h>

// Three bytes per glyph. Bitmaps are stored back to back, each glyph taking
// ceil(width*height/8) bytes, so a glyph's offset is the sum of the sizes
// before it and is not stored. Field ranges cover every linked font
// (width/height/advance <= 31, xOffset -4..3, yOffset -16..15); an
// out-of-range value is a compile error via -Woverflow.
typedef struct __attribute__((packed)) { // Data stored PER GLYPH
  uint32_t width : 5, height : 5;          // Bitmap dimensions in pixels
  uint32_t xAdvance : 5;                   // Cursor advance (x axis)
  int32_t xOffset : 3, yOffset : 5;        // Cursor to upper-left corner
} GFXglyph;
_Static_assert(sizeof(GFXglyph) == 3, "Glyph metrics must stay bit-packed");

typedef struct {       // Data stored for FONT AS A WHOLE:
  uint8_t *bitmap;     // Glyph bitmaps, concatenated
  GFXglyph *glyph;     // Glyph array
  uint8_t first, last; // ASCII extents
  uint8_t yAdvance;    // Newline distance (y axis)
} GFXfont;

#endif // _GFXFONT_H_

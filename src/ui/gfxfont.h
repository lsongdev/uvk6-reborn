#ifndef _GFXFONT_H_
#define _GFXFONT_H_

// to use fonts w/o cleanup
#define PROGMEM

#include <stdint.h>

// Seven bytes per glyph; packing removes only tail padding, not pixels.
// Cortex-M0 code must access bitmapOffset through this packed type so GCC
// emits byte loads for glyphs at odd addresses.
typedef struct __attribute__((packed)) { // Data stored PER GLYPH
  uint16_t bitmapOffset;   // Pointer into GFXfont->bitmap
  uint8_t width, height;   // Bitmap dimensions in pixels
  uint8_t xAdvance;        // Distance to advance cursor (x axis)
  int8_t xOffset, yOffset; // Dist from cursor pos to UL corner
} GFXglyph;
_Static_assert(sizeof(GFXglyph) == 7, "Glyph tables must not contain padding");

typedef struct {       // Data stored for FONT AS A WHOLE:
  uint8_t *bitmap;     // Glyph bitmaps, concatenated
  GFXglyph *glyph;     // Glyph array
  uint8_t first, last; // ASCII extents
  uint8_t yAdvance;    // Newline distance (y axis)
} GFXfont;

#endif // _GFXFONT_H_

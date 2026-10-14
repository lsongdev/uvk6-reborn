// Glyph offsets are derived, so each bitmap array must be exactly the sum
// of its glyphs' packed sizes; a hand-edited font that drifts fails here.
#include "ui/fonts/TomThumb.h"
#include "ui/fonts/muMatrix8ptRegular.h"
#include "ui/fonts/symbols.h"
#include <assert.h>

static void packed(const GFXfont *font, unsigned bytes) {
  unsigned total = 0;
  for (unsigned i = 0; i <= (unsigned)(font->last - font->first); ++i)
    total += (font->glyph[i].width * font->glyph[i].height + 7) / 8;
  assert(total == bytes);
}

int main(void) {
  packed(&TomThumb, sizeof(TomThumbBitmaps));
  packed(&MuMatrix8ptRegular, sizeof(MuMatrix8ptRegularBitmaps));
  packed(&Symbols, sizeof(Symbols_Bitmaps));
  return 0;
}

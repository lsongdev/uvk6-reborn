#include "ui/components/radio-card.h"
#include "ui/components/signal.h"
#include "ui/components/frequency.h"
#include "ui/graphics.h"
#include "core/text.h"
#include <string.h>

// Body text ends at TEXT_RIGHT; the small column above the unit is always
// reserved and takes whatever does not fit, then the screen edge clips.
enum { BODY_X = 30, TEXT_RIGHT = 113, TAIL_X = 115, BAND_X = 14 };

typedef uint16_t (*TextWidth)(const char *text);

// Copies the longest prefix of text that fits in width; returns the rest.
static const char *fit(const char *text, TextWidth measure, uint16_t width,
                       char head[24]) {
  uint8_t n = 0;
  for (char glyph[2] = {0}; text[n] && n < 23; ++n) {
    glyph[0] = text[n];
    uint16_t w = measure(glyph);
    if (w > width) break;
    width -= w;
  }
  memcpy(head, text, n);
  head[n] = 0;
  return text + n;
}

// Compact step such as 12.5k or 1M; below 1 kHz plain Hz.
static void formatStep(uint32_t hz, char text[16]) {
  if (hz < 1000) {
    Text t = TEXT_Begin(text, 16);
    TEXT_Number(&t, hz, 1, 10);
    TEXT_Add(&t, "Hz");
    return;
  }
  const char *unit = UI_FormatFrequency(hz, text);
  size_t n = strlen(text);
  if (strchr(text, '.')) {
    while (text[n-1] == '0') text[--n] = 0;
    if (text[n-1] == '.') text[--n] = 0;
  }
  text[n] = unit[0] == 'M' ? 'M' : 'k';
  text[n+1] = 0;
}

static void renderActivity(uint8_t y, const UI_RadioCardView *view) {
  const char *upper, *lower;
  char step[16];
  switch (view->activity) {
  case UI_RADIO_CARD_RECEIVING:
    PrintSmall(2, y+14, "RX");
    if (view->signal_valid) UI_SignalIcon(2, y+25, view->signal_level);
    else PrintSmall(2, y+24, "--");
    return;
  case UI_RADIO_CARD_TRANSMITTING:
    upper = "TX"; lower = view->power_label ? view->power_label : "--"; break;
  case UI_RADIO_CARD_SEEKING:
    upper = "SEEK"; lower = ">"; break;
  case UI_RADIO_CARD_WATCHING:
    upper = "DW"; lower = "--"; break;
  case UI_RADIO_CARD_UNAVAILABLE:
    upper = "RX"; lower = "--"; break;
  case UI_RADIO_CARD_FAULT:
    upper = "FAULT"; lower = "RX"; break;
  default:
    formatStep(view->step_hz, step);
    upper = "STEP"; lower = step; break;
  }
  UI_SmallTextWindow(2, y+14, 26, upper, 0);
  UI_SmallTextWindow(2, y+24, 26, lower, 0);
}

static void renderHeader(uint8_t y, const UI_RadioCardView *view) {
  const char *mode = (unsigned)view->mode < RX_MODE_COUNT ? rxModeNames[view->mode] : "--";
  const char *text = view->header_message && *view->header_message ? view->header_message :
                     view->radio_text && *view->radio_text ? view->radio_text :
                     view->band_name ? view->band_name : "";
  uint8_t modeLeft = LCD_WIDTH-UI_SmallTextWidth(mode);
  if (view->selected) {
    DrawVLine(1, y+1, 5, C_FILL);
    DrawVLine(2, y+2, 3, C_FILL);
    PutPixel(3, y+3, C_FILL);
  }
  char id[2] = {view->id ? view->id : 'A', 0};
  PrintSmall(5, y+5, id);
  if (view->changed) PrintSmall(10, y+5, "*");
  UI_SmallTextWindow(BAND_X, y+5, modeLeft-3-BAND_X, text, 0);
  PrintSmallEx(LCD_WIDTH, y+5, POS_R, C_FILL, mode);
  FillRect(0, y, LCD_WIDTH, 8, C_INVERT);
}

void UI_RadioCardRender(uint8_t y, const UI_RadioCardView *view) {
  if (!view || y+UI_RADIO_CARD_HEIGHT > LCD_HEIGHT) return;
  FillRect(0, y, LCD_WIDTH, UI_RADIO_CARD_HEIGHT, C_CLEAR);
  renderHeader(y, view);
  renderActivity(y, view);
  char frequency[16], head[24];
  const char *unit = UI_FormatFrequency(view->frequency_hz, frequency);
  bool tuning = view->edit_text || view->activity == UI_RADIO_CARD_SEEKING;
  if (view->edit_text) {
    Text t = TEXT_Begin(frequency, sizeof(frequency));
    TEXT_Add(&t, view->edit_text);
    unit = "MHz";
  }
  if (!tuning && view->name && *view->name) {
    // Name over frequency, both medium and right-aligned to TEXT_RIGHT.
    const char *rest = fit(view->name, UI_MediumTextWidth, TEXT_RIGHT-BODY_X, head);
    uint16_t width = UI_MediumTextWidth(head);
    UI_MediumTextWindow(TEXT_RIGHT-width, y+16, width, head, 0);
    UI_SmallTextWindow(TAIL_X, y+16, LCD_WIDTH-TAIL_X, rest, 0);
    width = UI_MediumTextWidth(frequency);
    UI_MediumTextWindow(TEXT_RIGHT-width, y+26, width, frequency, 0);
    PrintSmallEx(LCD_WIDTH, y+26, POS_R, C_FILL, unit);
    return;
  }
  // Frequency alone: large digits; extra decimals or draft digits stack
  // small above the unit, keeping their end (the cursor) in view.
  const char *rest = fit(frequency, UI_LargeTextWidth, TEXT_RIGHT-BODY_X, head);
  uint16_t width = UI_LargeTextWidth(head), tail = UI_SmallTextWidth(rest);
  UI_LargeTextWindow(TEXT_RIGHT-width, y+22, width, head, 0);
  if (tail <= LCD_WIDTH-TAIL_X) UI_SmallTextWindow(LCD_WIDTH-tail, y+13, tail, rest, 0);
  else UI_SmallTextWindow(TAIL_X, y+13, LCD_WIDTH-TAIL_X, rest, tail-(LCD_WIDTH-TAIL_X));
  PrintSmallEx(LCD_WIDTH, y+22, POS_R, C_FILL, unit);
}

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

// The header frequency is shown while the body carries a name.
static void renderHeader(uint8_t y, const UI_RadioCardView *view, const char *frequency) {
  const char *mode = (unsigned)view->mode < RX_MODE_COUNT ? rxModeNames[view->mode] : "--";
  bool notice = view->header_message && *view->header_message;
  const char *text = notice ? view->header_message :
                     view->radio_text && *view->radio_text ? view->radio_text :
                     view->band_name ? view->band_name : "";
  uint8_t right = LCD_WIDTH-UI_SmallTextWidth(mode)-3;
  if (view->selected) {
    DrawVLine(1, y+1, 5, C_FILL);
    DrawVLine(2, y+2, 3, C_FILL);
    PutPixel(3, y+3, C_FILL);
  }
  char id[2] = {view->id ? view->id : 'A', 0};
  PrintSmall(5, y+5, id);
  if (view->changed) PrintSmall(10, y+5, "*");
  PrintSmallEx(LCD_WIDTH, y+5, POS_R, C_FILL, mode);
  // A notice is brief and takes the frequency's place until it expires.
  if (frequency && !notice) {
    PrintSmallEx(right, y+5, POS_R, C_FILL, frequency);
    right -= UI_SmallTextWidth(frequency)+3;
  }
  UI_SmallTextWindow(BAND_X, y+5, right > BAND_X ? right-BAND_X : 0, text, 0);
  FillRect(0, y, LCD_WIDTH, 8, C_INVERT);
}

void UI_RadioCardRender(uint8_t y, const UI_RadioCardView *view) {
  if (!view || y+UI_RADIO_CARD_HEIGHT > LCD_HEIGHT) return;
  FillRect(0, y, LCD_WIDTH, UI_RADIO_CARD_HEIGHT, C_CLEAR);
  char frequency[20], head[24];
  const char *unit = UI_FormatFrequency(view->frequency_hz, frequency);
  if (view->edit_text) {
    Text t = TEXT_Begin(frequency, sizeof(frequency));
    TEXT_Add(&t, view->edit_text);
    unit = "MHz";
  }
  bool tuning = view->edit_text || view->activity == UI_RADIO_CARD_SEEKING;
  bool named = !tuning && view->name && *view->name;
  if (named) { // Compact "438.500MHz" for the header.
    Text t = TEXT_Begin(frequency + strlen(frequency), sizeof(frequency) - strlen(frequency));
    TEXT_Add(&t, unit);
  }
  renderHeader(y, view, named ? frequency : NULL);
  renderActivity(y, view);
  // Large body text: the name, or else the frequency. What does not fit
  // continues small in the column above the unit, then the edge clips.
  const char *body = named ? view->name : frequency;
  const char *rest = fit(body, UI_LargeTextWidth, TEXT_RIGHT-BODY_X, head);
  uint16_t width = UI_LargeTextWidth(head), tail = UI_SmallTextWidth(rest);
  UI_LargeTextWindow(TEXT_RIGHT-width, y+22, width, head, 0);
  if (named) {
    UI_SmallTextWindow(TAIL_X, y+13, LCD_WIDTH-TAIL_X, rest, 0);
    return;
  }
  // Extra decimals right-align over the unit; a long draft keeps its end
  // (the cursor) in view.
  if (tail <= LCD_WIDTH-TAIL_X) UI_SmallTextWindow(LCD_WIDTH-tail, y+13, tail, rest, 0);
  else UI_SmallTextWindow(TAIL_X, y+13, LCD_WIDTH-TAIL_X, rest, tail-(LCD_WIDTH-TAIL_X));
  PrintSmallEx(LCD_WIDTH, y+22, POS_R, C_FILL, unit);
}

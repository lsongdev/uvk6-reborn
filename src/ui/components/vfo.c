#include "ui/components/vfo.h"
#include "ui/components/signal.h"
#include "ui/components/frequency.h"
#include "ui/graphics.h"
#include <string.h>

enum { BODY_X = 30, FREQUENCY_RIGHT = 110, BAND_X = 14 };

void UI_VfoShowFrequency(UI_VfoState *state, uint32_t now) {
  state->tuned_at = now;
  state->show_name = false;
  state->offset = 0;
}

static uint16_t scrollOffset(uint32_t now, uint32_t started, uint16_t overflow) {
  if (!overflow) return 0;
  uint32_t phase = (now-started)%(2u*UI_VFO_SCROLL_PAUSE_MS+overflow*UI_VFO_SCROLL_PIXEL_MS);
  return phase < UI_VFO_SCROLL_PAUSE_MS ? 0 : phase < UI_VFO_SCROLL_PAUSE_MS+overflow*UI_VFO_SCROLL_PIXEL_MS
                              ? (phase-UI_VFO_SCROLL_PAUSE_MS)/UI_VFO_SCROLL_PIXEL_MS : overflow;
}

bool UI_VfoUpdate(UI_VfoState *state, uint32_t now) {
  if (!state->initialized) return false;
  bool show = state->label[0] && (state->showing_message || (!state->tuning &&
              now-state->tuned_at >= UI_VFO_NAME_DELAY_MS));
  bool changed = show != state->show_name;
  if (show && !state->show_name) state->label_started = now;
  state->show_name = show;
  uint16_t offset = show ? scrollOffset(now, state->label_started, state->overflow) : 0;
  uint16_t bandOffset = scrollOffset(now, state->band_started, state->band_overflow);
  changed |= offset != state->offset || bandOffset != state->band_offset;
  state->offset = offset;
  state->band_offset = bandOffset;
  return changed;
}

static void formatStep(uint32_t hz, char text[24]) {
  if (hz < 1000) {
    snprintf(text, 24, "%uHz", (unsigned)hz);
    return;
  }
  char value[16];
  const char *unit = UI_FormatFrequency(hz, value);
  if (strchr(value, '.')) {
    size_t n = strlen(value);
    while (n && value[n-1] == '0') value[--n] = 0;
    if (n && value[n-1] == '.') value[--n] = 0;
  }
  snprintf(text, 24, "%s%c", value, unit[0] == 'M' ? 'M' : 'k');
}

static void renderActivity(uint8_t y, const UI_VfoView *view) {
  const char *upper, *lower;
  char step[24];
  switch (view->activity) {
  case UI_VFO_RECEIVING:
    PrintSmall(2, y+14, "RX");
    if (view->signal_valid) UI_SignalIcon(2, y+25, view->signal_level);
    else PrintSmall(2, y+24, "--");
    return;
  case UI_VFO_TRANSMITTING:
    upper = "TX"; lower = view->power_label ? view->power_label : "--"; break;
  case UI_VFO_SEEKING:
    upper = "SEEK"; lower = ">"; break;
  case UI_VFO_WATCHING:
    upper = "DW"; lower = "--"; break;
  case UI_VFO_UNAVAILABLE:
    upper = "RX"; lower = "--"; break;
  case UI_VFO_FAULT:
    upper = "FAULT"; lower = "RX"; break;
  default:
    formatStep(view->step_hz, step);
    upper = "STEP"; lower = step; break;
  }
  UI_SmallTextWindow(2, y+14, 26, upper, 0);
  UI_SmallTextWindow(2, y+24, 26, lower, 0);
}

void UI_VfoRender(uint8_t y, const UI_VfoView *view, UI_VfoState *state, uint32_t now) {
  if (!view || !state || y+UI_VFO_HEIGHT > LCD_HEIGHT) return;
  if (!state->initialized || state->frequency_hz != view->frequency_hz) {
    UI_VfoShowFrequency(state, now);
    state->initialized = true;
    state->frequency_hz = view->frequency_hz;
  }
  bool wasTuning = state->tuning;
  state->tuning = view->activity == UI_VFO_SEEKING;
  if (wasTuning && !state->tuning) UI_VfoShowFrequency(state, now);
  if (state->tuning) UI_VfoShowFrequency(state, now);
  char label[128], frequency[16];
  const char *name = view->name ? view->name : "";
  const char *rds = view->rds ? view->rds : "";
  state->showing_message = view->message && *view->message;
  if (state->showing_message) snprintf(label, sizeof(label), "%s", view->message);
  else snprintf(label, sizeof(label), "%s%s%s", name, *name && *rds ? " / " : "", rds);
  if (strcmp(label, state->label)) {
    strcpy(state->label, label);
    state->label_started = now;
    state->offset = 0;
  }
  uint16_t textWidth = UI_LargeBoldTextWidth(state->label);
  state->overflow = textWidth > LCD_WIDTH-BODY_X ? textWidth-(LCD_WIDTH-BODY_X) : 0;
  UI_VfoUpdate(state, now);
  const char *unit = UI_FormatFrequency(view->frequency_hz, frequency);
  const char *mode = (unsigned)view->mode < RX_MODE_COUNT ? rxModeNames[view->mode] : "--";
  uint8_t modeLeft = LCD_WIDTH-UI_SmallTextWidth(mode);
  char compact[24];
  snprintf(compact, sizeof(compact), "%s%s", frequency, unit);
  uint8_t bandRight = modeLeft-3;
  if (state->show_name) bandRight -= UI_SmallTextWidth(compact)+3;
  uint16_t bandWidth = bandRight > BAND_X ? bandRight-BAND_X : 0;
  const char *band = view->band_name ? view->band_name : "";
  if (strncmp(band, state->band, sizeof(state->band)-1) || bandWidth != state->band_width) {
    snprintf(state->band, sizeof(state->band), "%s", band);
    state->band_started = now;
    state->band_offset = 0;
    state->band_width = bandWidth;
  }
  uint16_t bandTextWidth = UI_SmallTextWidth(state->band);
  state->band_overflow = bandTextWidth > bandWidth ? bandTextWidth-bandWidth : 0;
  state->band_offset = scrollOffset(now, state->band_started, state->band_overflow);

  FillRect(0, y, LCD_WIDTH, UI_VFO_HEIGHT, C_CLEAR);
  if (view->selected) {
    DrawVLine(1, y+1, 5, C_FILL);
    DrawVLine(2, y+2, 3, C_FILL);
    PutPixel(3, y+3, C_FILL);
  }
  PrintSmall(5, y+5, "%c", view->id ? view->id : 'A');
  if (view->changed) PrintSmall(10, y+5, "*");
  UI_SmallTextWindow(BAND_X, y+5, bandWidth, state->band, state->band_offset);
  PrintSmallEx(LCD_WIDTH, y+5, POS_R, C_FILL, "%s", mode);
  if (state->show_name)
    PrintSmallEx(modeLeft-3, y+5, POS_R, C_FILL, "%s", compact);
  FillRect(0, y, LCD_WIDTH, 8, C_INVERT);
  renderActivity(y, view);
  if (state->show_name) {
    int16_t offset = state->overflow ? state->offset :
                     (int16_t)textWidth-(LCD_WIDTH-BODY_X);
    UI_LargeBoldTextWindow(BODY_X, y+22, LCD_WIDTH-BODY_X, state->label, offset);
  } else {
    char *tail = NULL;
    char *dot = strchr(frequency, '.');
    if (unit[0] == 'M' && dot && strlen(dot+1) > 3) {
      tail = dot+4;
      PrintSmallEx(LCD_WIDTH, y+17, POS_R, C_FILL, "%s", tail);
      *tail = 0;
    }
    PrintBiggestDigitsEx(FREQUENCY_RIGHT, y+24, POS_R, C_FILL, "%s", frequency);
    PrintSmallEx(LCD_WIDTH, y+24, POS_R, C_FILL, "%s", unit);
  }
}

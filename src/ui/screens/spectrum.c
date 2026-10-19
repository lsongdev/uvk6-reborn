#include "ui/screens/spectrum.h"
#include "radio/radio.h"
#include "core/text.h"
#include "ui/frequency.h"
#include "ui/graphics.h"
#include "ui/radio-card.h"
#include "ui/spectrum.h"

#define GRAPH_Y (8 + UI_RADIO_CARD_HEIGHT)

// Covered (settings) or closed: reception comes back; uncovered: sweep again.
static void enter(void) { RADIO_StartSpectrum(); }
static void leave(void) { RADIO_StopSpectrum(false); }

static void render(void) {
  const Spectrum *s = RADIO_Spectrum();
  if (!s) return;
  RadioSlotStatus status;
  RADIO_SlotStatus(RADIO_SelectedId(), &status);
  char span[28];
  Text t = TEXT_Begin(span, sizeof(span));
  TEXT_Add(&t, "Span ");
  UI_FrequencyText(SPECTRUM_POINTS * s->step_hz, t.at);
  UI_RadioCardView view = {
    .id = 'A' + RADIO_SelectedId(), .selected = true,
    .frequency_hz = status.frequency_hz, .step_hz = s->step_hz,
    .mode = status.slot.current.parameters.mode,
    .band_name = span, .header_message = RADIO_Notice(),
    .signal_valid = status.receiving, .signal_level = status.signal_level,
    .activity = status.receiving ? UI_RADIO_CARD_RECEIVING : UI_RADIO_CARD_IDLE,
  };
  UI_RadioCardRender(8, &view);
  UI_SpectrumRender(GRAPH_Y, LCD_HEIGHT - GRAPH_Y, s);
}

// Arrows move the cursor (panning at the edges), * and # widen and narrow
// the span, MENU tunes the slot to the cursor, EXIT goes back unchanged.
static bool key(KEY_Code_t key, Key_State_t state) {
  bool released = state == KEY_RELEASED;
  if ((key == KEY_UP || key == KEY_DOWN) && (released || state == KEY_LONG_PRESSED_CONT)) {
    RADIO_SpectrumMove(KEYBOARD_FrequencyIncreases(key));
    return true;
  }
  if (key == KEY_MENU && state == KEY_LONG_PRESSED) return false; // Settings.
  if (!released) return true;
  switch (key) {
  case KEY_STAR: RADIO_SpectrumZoom(true); break;
  case KEY_F: RADIO_SpectrumZoom(false); break;
  case KEY_MENU: RADIO_StopSpectrum(true); SCREEN_Pop(); break;
  case KEY_EXIT: SCREEN_Pop(); break;
  default: break;
  }
  return true;
}

const Screen spectrumScreen = {
  .title = "Spectrum", .enter = enter, .leave = leave, .render = render, .key = key,
};

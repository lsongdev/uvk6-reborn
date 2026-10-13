#ifndef UI_COMPONENT_VFO_H
#define UI_COMPONENT_VFO_H
#include "core/radio-model.h"
#define UI_VFO_HEIGHT 28
#define UI_VFO_NAME_DELAY_MS 1000u
#define UI_VFO_SCROLL_PAUSE_MS 1200u
#define UI_VFO_SCROLL_PIXEL_MS 150u
// Receiver state is explicit; a valid RSSI alone does not mean squelch is open.
typedef enum {
  UI_VFO_IDLE, UI_VFO_RECEIVING, UI_VFO_TRANSMITTING,
  UI_VFO_SEEKING, UI_VFO_WATCHING, UI_VFO_UNAVAILABLE, UI_VFO_FAULT
} UI_VfoActivity;
// Each visible VFO owns its view and animation state; no receiver globals.
typedef struct {
  uint32_t frequency_hz, step_hz;
  RxMode mode;
  UI_VfoActivity activity;
  const char *band_name, *name, *rds, *power_label;
  const char *edit_text; // Native MHz draft; owns the body while editing.
  const char *message; // Temporarily replaces frequency/name, using the same scrolling text renderer.
  char id;
  bool selected, changed, signal_valid;
  uint8_t signal_level; // Existing 0..14 relative/S mapping, rendered as 8 bars.
} UI_VfoView;
typedef struct {
  char label[128], band[64];
  uint32_t tuned_at, label_started, band_started, frequency_hz;
  uint16_t offset, overflow, band_offset, band_overflow, band_width;
  bool initialized, show_name, tuning, showing_message;
} UI_VfoState;
// Restart the one-second frequency hold even when retuning to the same Hz.
void UI_VfoShowFrequency(UI_VfoState *state, uint32_t now);
void UI_VfoRender(uint8_t y, const UI_VfoView *view, UI_VfoState *state, uint32_t now);
bool UI_VfoUpdate(UI_VfoState *state, uint32_t now);
#endif

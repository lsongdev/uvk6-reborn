#ifndef UI_COMPONENT_RADIO_CARD_H
#define UI_COMPONENT_RADIO_CARD_H
#include "core/radio-model.h"
#define UI_RADIO_CARD_HEIGHT 28
// Receiver state is explicit; a valid RSSI alone does not mean squelch is open.
typedef enum {
  UI_RADIO_CARD_IDLE, UI_RADIO_CARD_RECEIVING, UI_RADIO_CARD_TRANSMITTING,
  UI_RADIO_CARD_SEEKING, UI_RADIO_CARD_WATCHING, UI_RADIO_CARD_UNAVAILABLE, UI_RADIO_CARD_FAULT
} UI_RadioCardActivity;
// Everything one card shows; the card keeps no state and never animates.
typedef struct {
  uint32_t frequency_hz, step_hz;
  RxMode mode;
  UI_RadioCardActivity activity;
  const char *band_name, *power_label;
  const char *name;       // Channel name or RDS PS: shown above the frequency.
  const char *radio_text; // RDS RT: replaces the band in the header.
  const char *edit_text;  // Native MHz draft; owns the body while editing.
  const char *header_message; // Temporarily replaces band text in the black header.
  char id;
  bool selected, changed, signal_valid;
  uint8_t signal_level; // Existing 0..14 relative/S mapping, rendered as 8 bars.
} UI_RadioCardView;
void UI_RadioCardRender(uint8_t y, const UI_RadioCardView *view);
#endif

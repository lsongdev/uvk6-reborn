// Main screen: slot cards, status title and tune confirmation.
#include "apps/radio-internal.h"

// Owns transient strings until the card has been drawn.
typedef struct {
  UserChannel channel;
  char header[16];
} RadioViewStorage;

// Resolve application state here; the UI component only consumes display data.
static void buildCardView(uint8_t id, const char *inputText,
                          UI_RadioCardView *view, RadioViewStorage *storage) {
  bool selected = id == selectedSlot;
  const RadioSlot *stored = &gDeviceConfig.radio.slots[id];
  RadioSlot effective;
  UserChannel *channel = &storage->channel;
  *channel = (UserChannel){0};
  bool effectiveOk;
  if (selected) {
    effective = activeSlot;
    effectiveOk = activeSlot.source != RADIO_SOURCE_CHANNEL ||
                  loadChannelForSlot(id, channel);
  } else {
    effectiveOk = effectiveSlot(id, &effective, channel);
  }
  if (!effectiveOk) effective = *stored;
  const RadioSlot *config = &effective;
  const TxConfig *tx = &config->tx;
  const BandState *current = &config->current;
  const RadioParameters *p = &current->parameters;
  const RadioParameters *preset = &gDeviceConfig.radio.presets[config->active_band];
  bool channelMode = stored->source == RADIO_SOURCE_CHANNEL;
  bool modified = !channelMode && (p->mode != preset->mode || p->step_hz != preset->step_hz ||
    p->bandwidth_hz != preset->bandwidth_hz || p->squelch != preset->squelch ||
    p->rx_tone != preset->rx_tone);
  bool receiving = receivingSlot==id && RADIO_APP_isReceiving();
  bool textValid = labels[id].frequency_hz == current->frequency_hz;
  char *channelHeader = storage->header;
  const char *header = radioBands[config->active_band].name;
  const char *channelName = NULL;
  if (channelMode) {
    Text t = TEXT_Begin(channelHeader, sizeof(storage->header));
    TEXT_Add(&t, "CH ");
    TEXT_Number(&t, stored->channel_number, 3, 10);
    header = channelHeader;
    channelName = effectiveOk && channel->name[0] ? channel->name : channelHeader;
    if (!effectiveOk) { channelName = NULL; header = "Channel unavailable"; }
  }
  *view = (UI_RadioCardView){
    .id = 'A'+id, .selected = selected,
    .edit_text = selected && number.active && !inputTx ? inputText : NULL,
    .frequency_hz = selected && seek.active ? seek.cursor_hz : current->frequency_hz,
    .mode = p->mode, .band_name = header,
    .changed = modified, .step_hz = p->step_hz,
    .name = channelMode ? channelName : textValid ? labels[id].ps : NULL,
    .radio_text = textValid ? labels[id].text : NULL,
    .header_message = selected && !number.active ? (message ? message :
        watchStatus) : NULL,
    .signal_valid = receiving, .signal_level = receiving ? UI_SMeterLevel() : 0,
    .activity = id==txSlot && transmitter.mode==RADIO_TX ? UI_RADIO_CARD_TRANSMITTING :
      id==txSlot && transmitter.mode==RADIO_FAULT ? UI_RADIO_CARD_FAULT :
      selected && seek.active ? UI_RADIO_CARD_SEEKING :
      WATCH_Probe(&watcher)==id ? UI_RADIO_CARD_WATCHING :
      receiving && gReceiver.open ? UI_RADIO_CARD_RECEIVING :
      (channelMode && !effectiveOk) || (selected && !runtime.active) ? UI_RADIO_CARD_UNAVAILABLE : UI_RADIO_CARD_IDLE,
    .power_label = tx->power == TX_POWER_HIGH ? "HIGH" :
                   tx->power == TX_POWER_MEDIUM ? "MID" : "LOW",
  };
  // FC shows the latest measured carrier live where the frequency sits,
  // the detector range in the header and the scope in the mode's place.
  if (selected && capturing) {
    view->name=view->radio_text=NULL;
    view->header_message=capture.range==CAPTURE_RANGE_VHF ? "FC VHF" : "FC UHF";
    view->mode_text=capture.scope==CAPTURE_AUTO ? "Auto" : capture.scope==CAPTURE_VHF ? "VHF" : "UHF";
    if (capture.last_hz) view->frequency_hz=capture.last_hz;
    else view->edit_text="---";
  }
}

static void renderTitle(void) {
  char title[24];
  Text t = TEXT_Begin(title, sizeof(title));
  if (number.active) STATUSLINE_SetText(number.invalid ? "Invalid value" : inputTx ? "TX Frequency" : "RX Frequency");
  else if (inMenu || confirming || activeSlot.source == RADIO_SOURCE_CHANNEL ||
           gDeviceConfig.radio.slot_count > 1) {
    bool watchTitle = !inMenu && !confirming &&
                      activeSlot.source != RADIO_SOURCE_CHANNEL &&
                      gDeviceConfig.radio.slot_count > 1;
    const char *mode = inMenu || confirming ? "VFO" :
        activeSlot.source == RADIO_SOURCE_CHANNEL ? "MR" : "DW";
    TEXT_Add(&t, mode);
    TEXT_Char(&t, ' ');
    // While watching, the title names the slot actually heard.
    uint8_t heard = watcher.phase!=WATCH_OFF ? WATCH_Owner(&watcher) : receivingSlot;
    TEXT_Char(&t, watchTitle ? (heard<RADIO_SLOT_CAPACITY ? 'A'+heard : '-') : 'A'+selectedSlot);
    STATUSLINE_SetText(title);
  } else STATUSLINE_SetText("RADIO");
}

static void renderConfirmation(void) {
  char line[32];
  Text t = TEXT_Begin(line, sizeof(line));
  TEXT_Add(&t, "Apply ");
  TEXT_Add(&t, rxModeNames[pending.parameters.mode]);
  TEXT_Char(&t, '?');
  PrintMedium(2, 18, line);
  UI_FrequencyText(pending.frequency_hz, line);
  PrintMedium(2, 30, line);
  t = TEXT_Begin(line, sizeof(line));
  TEXT_Add(&t, "BW ");
  TEXT_Number(&t, pending.parameters.bandwidth_hz, 1, 10);
  TEXT_Add(&t, " Hz STEP ");
  TEXT_Number(&t, pending.parameters.step_hz, 1, 10);
  TEXT_Add(&t, " Hz");
  PrintSmall(2, 40, line);
  PrintSmall(2, 57, "MENU:OK  EXIT:Back");
}

void RADIO_APP_render(void) {
  renderTitle();
  if (CHANNEL_MANAGER_Render()) return;
  if (!message && (confirming || inMenu)) {
    if (confirming) renderConfirmation();
    else renderMenu();
    return;
  }

  char inputText[18];
  // The number formatter is only needed by an active RX frequency editor.
  if (number.active && !inputTx) NUMBER_Text(&number, inputText);
  uint8_t first = RADIO_STATE_VisibleFirst(&gDeviceConfig.radio);
  uint8_t visible = MIN(gDeviceConfig.radio.slot_count, RADIO_SLOT_VISIBLE);
  for (uint8_t row = 0; row < visible; ++row) {
    uint8_t id = first + row;
    RadioViewStorage storage;
    UI_RadioCardView view;
    buildCardView(id, inputText, &view, &storage);
    UI_RadioCardRender(8 + row * UI_RADIO_CARD_HEIGHT, &view);
  }
}

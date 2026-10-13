// Main screen: slot cards, status title and tune confirmation.
#include "apps/radio-internal.h"

UI_RadioCardState cards[RADIO_SLOT_CAPACITY];
void __attribute__((noinline)) showFrequency(void) {
  UI_RadioCardShowFrequency(&cards[selectedSlot], Now());
}

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
    snprintf(channelHeader, sizeof(storage->header), "CH %03u", stored->channel_number);
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
    .rds = textValid ? labels[id].text : NULL,
    .header_message = selected && !number.active ? (message ? message :
        gDeviceConfig.radio.watch_enabled ? watchStatus : NULL) : NULL,
    .signal_valid = receiving, .signal_level = receiving ? UI_SMeterLevel() : 0,
    .activity = selected && transmitter.mode==RADIO_TX ? UI_RADIO_CARD_TRANSMITTING :
      selected && transmitter.mode==RADIO_FAULT ? UI_RADIO_CARD_FAULT :
      selected && seek.active ? UI_RADIO_CARD_SEEKING :
      receivingSlot==id && WATCH_IsProbing(&watcher) ? UI_RADIO_CARD_WATCHING :
      receiving && gReceiver.open ? UI_RADIO_CARD_RECEIVING :
      (channelMode && !effectiveOk) || (selected && !runtime.active) ? UI_RADIO_CARD_UNAVAILABLE : UI_RADIO_CARD_IDLE,
    .power_label = tx->power == TX_POWER_HIGH ? "HIGH" :
                   tx->power == TX_POWER_MEDIUM ? "MID" : "LOW",
  };
  if (selected && capturePhase!=FC_OFF) {
    view->name=view->rds=NULL;
    view->header_message=capturePhase==FC_PREVIEW ? "FC Preview" : "FC detecting";
    if (capturePhase==FC_PREVIEW) {
      view->frequency_hz=capture.last_hz;
      view->mode=RX_NFM;
      view->step_hz=12500;
    }
  }
}

static void renderTitle(void) {
  if (capturePhase!=FC_OFF) STATUSLINE_SetText("FC %s %s",
      capture.scope==CAPTURE_AUTO ? "Auto" : capture.scope==CAPTURE_VHF ? "VHF" : "UHF",
      capturePhase==FC_PREVIEW ? "MENU:Use" : capture.range==CAPTURE_RANGE_VHF ? "VHF" : "UHF");
  else if (number.active) STATUSLINE_SetText(number.invalid ? "Invalid value" : inputTx ? "TX Frequency" : "RX Frequency");
  else if (inMenu || confirming || activeSlot.source == RADIO_SOURCE_CHANNEL ||
           (gDeviceConfig.radio.watch_enabled && gDeviceConfig.radio.slot_count > 1)) {
    bool watchTitle = !inMenu && !confirming &&
                      activeSlot.source != RADIO_SOURCE_CHANNEL &&
                      gDeviceConfig.radio.slot_count > 1;
    const char *mode = inMenu || confirming ? "VFO" :
        activeSlot.source == RADIO_SOURCE_CHANNEL ? "MR" : "DW";
    char id = watchTitle ? (receivingSlot<RADIO_SLOT_CAPACITY ?
              'A'+receivingSlot : '-') : 'A'+selectedSlot;
    STATUSLINE_SetText("%s %c", mode, id);
  } else STATUSLINE_SetText("RADIO");
}

static void renderConfirmation(void) {
  char frequencyText[16];
  const char *unit = UI_FormatFrequency(pending.frequency_hz, frequencyText);
  PrintMediumEx(2, 18, POS_L, C_FILL, "Apply %s?", rxModeNames[pending.parameters.mode]);
  PrintMediumEx(2, 30, POS_L, C_FILL, "%s %s", frequencyText, unit);
  PrintSmall(2, 40, "BW %u Hz STEP %u Hz", pending.parameters.bandwidth_hz, pending.parameters.step_hz);
  PrintSmall(2, 57, "MENU: apply  EXIT: cancel");
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
  uint32_t now = Now();
  uint8_t visible = MIN(gDeviceConfig.radio.slot_count, RADIO_SLOT_VISIBLE);
  for (uint8_t row = 0; row < visible; ++row) {
    uint8_t id = first + row;
    RadioViewStorage storage;
    UI_RadioCardView view;
    buildCardView(id, inputText, &view, &storage);
    UI_RadioCardRender(8 + row * UI_RADIO_CARD_HEIGHT, &view, &cards[id], now);
  }
}

// Radio controller: slots, VFO/MR resolution, the tune transaction, RDS and
// status for display. Features live in the other radio/*.c files.
#include "radio/radio-internal.h"

bool initialized;
uint8_t selectedSlot, receivingSlot = UINT8_MAX;
SlotLabel labels[RADIO_SLOT_CAPACITY];
RxRuntime runtime;
RdsReceiver rds;
RadioSlot activeSlot;
RxPlan pending;
bool confirming;
const char *message;
static uint32_t messageAt;
static uint32_t clockAt; // Uptime when the RDS minute began; 0: no clock.
static uint16_t clockMinute;
static UserChannel channelCache[RADIO_SLOT_CAPACITY];
static bool channelCacheValid[RADIO_SLOT_CAPACITY];
static uint8_t channelCacheNumber[RADIO_SLOT_CAPACITY];
static void (*redrawHook)(void);

void RADIO_SetRedrawHook(void (*redraw)(void)) { redrawHook = redraw; }

static bool loadChannelForSlot(uint8_t id, UserChannel *channel) {
  const RadioSlot *config = &gDeviceConfig.radio.slots[id];
  if (config->source != RADIO_SOURCE_CHANNEL || !config->channel_number)
    return false;
  if (!channelCacheValid[id] || channelCacheNumber[id] != config->channel_number) {
    if (!CHANNEL_STORE_IsOccupied(config->channel_number) ||
        !CHANNEL_STORE_Load(config->channel_number, &channelCache[id])) {
      channelCacheValid[id] = false;
      return false;
    }
    channelCacheValid[id] = true;
    channelCacheNumber[id] = config->channel_number;
  }
  *channel = channelCache[id];
  return true;
}

bool effectiveSlot(uint8_t id, RadioSlot *effective, UserChannel *channel) {
  if (id >= RADIO_SLOT_CAPACITY || !effective) return false;
  const RadioSlot *stored = &gDeviceConfig.radio.slots[id];
  if (stored->source == RADIO_SOURCE_VFO)
    return TUNING_Resolve(stored, NULL, effective);
  UserChannel loaded;
  if (!loadChannelForSlot(id, &loaded)) return false;
  if (!TUNING_Resolve(stored, &loaded, effective)) return false;
  if (channel) *channel = loaded;
  return true;
}

// These operations recur across FC, Seek, Watch, PTT and manual input.
void __attribute__((noinline)) resetRds(void) {
  RDS_ReceiverReset(&rds, Now());
}

void notify(const char *text) {
  message = text;
  messageAt = Now();
  gRedrawScreen = true;
}
void RADIO_Notify(const char *text) { notify(text); }
const char *RADIO_Notice(void) { return message; }
const char *RADIO_WatchStatus(void) { return watchStatus; }

bool RADIO_Clock(uint16_t *minute) {
  if (!clockAt) return false;
  *minute = (uint16_t)((clockMinute + (Now() - clockAt) / 60000u) % 1440u);
  return true;
}

// "Out of range" when no mode of this hardware receives the frequency at
// all, "RX unavailable" when only the chosen mode cannot.
static const char *unavailable(RxResult result, uint32_t frequency_hz) {
  if (result == RX_PATCH_MISSING) return "SSB patch missing";
  return RADIO_MODEL_Covers(frequency_hz, RECEIVER_Capabilities()) ? "RX unavailable" : "Out of range";
}

// The selected slot is now the one being heard.
static void tuned(void) {
  receivingSlot = selectedSlot;
  message = NULL;
  gRedrawScreen = true;
}

// RDS text from the previous frequency must not follow a retune.
static void resetLabel(uint32_t frequency_hz) {
  memset(&labels[selectedSlot], 0, sizeof(labels[selectedSlot]));
  labels[selectedSlot].frequency_hz = frequency_hz;
}

bool applyReceiver(void *unused, const RxPlan *plan) {
  (void)unused;
  resetRds();
  return RECEIVER_Apply(plan);
}

void stopReceiver(void *unused) {
  (void)unused;
  resetRds();
  RECEIVER_Mute();
  receivingSlot = UINT8_MAX;
}

bool apply(const RxPlan *plan) {
  (void)pauseWatch(); // Retuning owns the chips; watch resumes 2 s later.
  bool needsSsb = plan->parameters.mode == RX_LSB || plan->parameters.mode == RX_USB;
  bool wasSsb = runtime.active && (runtime.current.parameters.mode == RX_LSB ||
                                  runtime.current.parameters.mode == RX_USB);
  if (needsSsb && !wasSsb) { // Loading the SSB patch takes a while.
    notify("WAIT...");
    if (redrawHook) redrawHook();
  }
  RxTuneResult result = RX_RuntimeTune(&runtime, plan,
                                      applyReceiver, stopReceiver, NULL);
  if (result != RX_APPLIED) {
    notify(result == RX_RESTORED ? "Tune failed" : "RX error");
    return false;
  }
  if (activeSlot.source == RADIO_SOURCE_VFO &&
      !RADIO_SLOT_SetVfo(&activeSlot, plan->frequency_hz, plan->parameters)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    notify("RX error");
    return false;
  }
  if (activeSlot.source == RADIO_SOURCE_VFO) {
    // TX edits are committed independently from the receive draft.
    activeSlot.tx = gDeviceConfig.radio.slots[selectedSlot].tx;
    gDeviceConfig.radio.slots[selectedSlot] = activeSlot;
  }
  resetLabel(plan->frequency_hz);
  tuned();
  DEVICE_RequestSave();
  return true;
}

// Reverse: RX moves to the TX frequency and TX to the old RX, the shift
// flipping with it, so a repeater's input can be heard; again swaps back.
void RADIO_Reverse(void) {
  if (!requireVfoSource()) return;
  TxConfig tx = activeSlot.tx;
  uint32_t rx = activeSlot.current.frequency_hz;
  uint32_t target = TX_MODEL_Frequency(&tx, rx);
  if (tx.shift == TX_SHIFT_OFF || target == rx) { notify("TX Shift is Off"); return; }
  if (tx.shift == TX_SHIFT_PLUS) tx.shift = TX_SHIFT_MINUS;
  else if (tx.shift == TX_SHIFT_MINUS) tx.shift = TX_SHIFT_PLUS;
  else tx.split_hz = rx;
  RxPlan plan;
  RxResult result = TUNING_ExactPlan(target, activeSlot.current.parameters,
                                     RECEIVER_Capabilities(), &plan);
  if (result != RX_OK || !TX_MODEL_ConfigValid(&tx)) {
    notify(result != RX_OK ? unavailable(result, target) : "Invalid value");
    return;
  }
  TxConfig *stored = &gDeviceConfig.radio.slots[selectedSlot].tx;
  TxConfig previous = *stored;
  *stored = tx; // apply() takes the stored TX settings with the new RX.
  if (!apply(&plan)) *stored = previous;
}

// Plans a target for the selected slot; an approximation waits for
// RADIO_ConfirmTune instead of being applied.
void request(uint32_t target, RadioParameters values) {
  (void)pauseWatch();
  confirming = false;
  RxPlan plan;
  RxResult result = RADIO_MODEL_Plan(target, values, RECEIVER_Capabilities(), &plan);
  if (result == RX_OUT_OF_RANGE || result == RX_INVALID_PARAMETERS) {
    notify("Invalid value");
    return;
  }
  if (result != RX_OK) {
    // A gap has no receiver. Stop the old audio and retain a navigable target,
    // without storing the unsupported target as an applied EEPROM frequency.
    activeSlot.current.frequency_hz = target;
    activeSlot.current.parameters = values;
    activeSlot.active_band = RADIO_MODEL_BandAt(target);
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    notify(unavailable(result, target));
    return;
  }
  if (RADIO_MODEL_BandAt(plan.frequency_hz) != RADIO_MODEL_BandAt(target)) {
    notify("Band boundary");
    return;
  }
  if (plan.frequency_hz != target) {
    pending = plan;
    confirming = true;
    gRedrawScreen = true;
  } else {
    apply(&plan);
  }
}
const RxPlan *RADIO_PendingTune(void) { return confirming ? &pending : NULL; }
void RADIO_ConfirmTune(bool accept) {
  if (!confirming) return;
  confirming = false;
  if (accept) apply(&pending);
  gRedrawScreen = true;
}

static bool tuneDirect(const RxPlan *plan) {
  (void)pauseWatch();
  RxTuneResult result = RX_RuntimeTune(&runtime, plan,
                                       applyReceiver, stopReceiver, NULL);
  if (result != RX_APPLIED) {
    notify(result == RX_RESTORED ? "Tune failed" : "RX error");
    return false;
  }
  return true;
}

// Saved sources (MR, slot switch, VFO return) must tune exactly.
static bool tuneExact(uint32_t frequency_hz, RadioParameters parameters) {
  RxPlan plan;
  RxResult result = TUNING_ExactPlan(frequency_hz, parameters,
                                     RECEIVER_Capabilities(), &plan);
  if (result != RX_OK) {
    notify(unavailable(result, frequency_hz));
    return false;
  }
  return tuneDirect(&plan);
}

// Resolves and tunes the selected slot from its stored state; a slot whose
// channel is gone falls back to VFO.
static void loadSelected(void) {
  bool invalidSavedChannel = false;
  if (!effectiveSlot(selectedSlot, &activeSlot, NULL)) {
    gDeviceConfig.radio.slots[selectedSlot].source = RADIO_SOURCE_VFO;
    activeSlot = gDeviceConfig.radio.slots[selectedSlot];
    DEVICE_RequestSave();
    invalidSavedChannel = true;
  }
  receivingSlot = UINT8_MAX;
  request(activeSlot.current.frequency_hz, activeSlot.current.parameters);
  if (invalidSavedChannel) notify("Channel unavailable");
}

void RADIO_Init(void) {
  // The receiver owns chip reset, RF defaults and factory calibration.
  // It must run before loadSelected() probes capabilities or applies a plan.
  RECEIVER_Init();
  initialized = true;
  transmitter = (TxRuntime){0};
  seek.active = false;
  confirming = false;
  if (!RADIO_STATE_Valid(&gDeviceConfig.radio))
    RADIO_STATE_Default(&gDeviceConfig.radio);
  selectedSlot = gDeviceConfig.radio.selected_slot;
  for (uint8_t id = 0; id < RADIO_SLOT_CAPACITY; ++id) channelCacheValid[id] = false;
  loadSelected();
}

bool RADIO_IsReceiving(void) {
  return initialized && receivingSlot != UINT8_MAX && !WATCH_IsProbing(&watcher) &&
         runtime.active && !seek.active && !capturing && !toneScanning && !spectrumOn &&
         transmitter.mode == RADIO_RX;
}

uint8_t RADIO_Heard(void) {
  return watcher.phase != WATCH_OFF ? WATCH_Owner(&watcher) : receivingSlot;
}

static bool readRds(void *unused, RdsPacket *packet, bool clear_fifo) {
  (void)unused;
  return RX_DEVICE_ReadRDS(packet, clear_fifo);
}

void RADIO_Update(void) {
  if (!initialized) return; // Not yet started (e.g. the EEPROM reset page).
  uint32_t now = Now();
  RADIO_TxSafetyUpdate(); // Also permits deterministic host tests.
  if (message && now-messageAt >= 2500u) {
    message = NULL;
    gRedrawScreen = true;
  }
  for (uint8_t id = 0; id < RADIO_SLOT_CAPACITY; ++id)
    if (labels[id].rt_until && (int32_t)(now - labels[id].rt_until) >= 0) {
      labels[id].rt_until = 0;
      gRedrawScreen = true;
    }
  if (transmitter.mode != RADIO_RX)
    return;
  if (toneScanning) { toneScanUpdate(); return; }
  if (capturing) { captureUpdate(); return; }
  if (spectrumOn) { spectrumUpdate(); return; }
  seekUpdate();
  watchUpdate();
  uint8_t rdsSlot=receivingSlot;
  const RxPlan *rdsPlan=&runtime.current;
  bool rdsEnabled=RADIO_IsReceiving() && runtime.active &&
    rdsPlan->device==RX_DEVICE_SI && rdsPlan->parameters.mode==RX_WFM;
  // While watching, RDS follows the broadcast background, even when a BK
  // talk currently owns the speaker.
  if (watcher.phase!=WATCH_OFF) {
    const WatchTarget *bg=&watcher.background;
    rdsEnabled=watcher.background_ready && bg->plan.device==RX_DEVICE_SI &&
               bg->plan.parameters.mode==RX_WFM;
    rdsSlot=bg->id;rdsPlan=&bg->plan;
  }
  bool textExpired = rdsEnabled && now-rds.last_group >= 15000u;
  if (RDS_ReceiverUpdate(&rds, now, rdsEnabled, readRds, NULL) || textExpired) {
    if (rdsSlot < RADIO_SLOT_CAPACITY && rdsEnabled) {
      SlotLabel *label = &labels[rdsSlot];
      // A changed RadioText is shown for a while, then the header returns
      // to band and frequency until the text changes again.
      if (rds.decoder.text[0] && memcmp(label->text, rds.decoder.text, sizeof(label->text)))
        label->rt_until = (now + RADIO_RT_SHOW_MS) | 1u;
      label->frequency_hz = rdsPlan->frequency_hz;
      memcpy(label->ps, rds.decoder.ps, sizeof(rds.decoder.ps));
      memcpy(label->text, rds.decoder.text, sizeof(rds.decoder.text));
    }
    gRedrawScreen = true;
  }
  if (rds.decoder.clock_new) {
    rds.decoder.clock_new = false;
    clockMinute = rds.decoder.clock;
    clockAt = now | 1u;
    gRedrawScreen = true;
  }
}

// --- Intents on the selected slot -----------------------------------------

bool requireVfoSource(void) {
  if (activeSlot.source == RADIO_SOURCE_VFO) return true;
  notify("VFO only");
  return false;
}

void RADIO_TuneTo(uint32_t target) {
  if (!requireVfoSource()) return;
  uint8_t band = RADIO_MODEL_BandAt(target);
  if (band == UINT8_MAX) {
    notify("Out of range");
    return;
  }
  RadioParameters values = band == activeSlot.active_band ? activeSlot.current.parameters
                                                          : gDeviceConfig.radio.presets[band];
  request(target, values);
}

static void tuneChannel(uint16_t number, bool enterMode) {
  UserChannel channel;
  if (!CHANNEL_STORE_Load(number, &channel)) {
    notify("Channel unavailable");
    return;
  }
  if (!tuneExact(channel.frequency_hz, channel.rx)) return;
  channelCache[selectedSlot] = channel;
  channelCacheValid[selectedSlot] = true;
  channelCacheNumber[selectedSlot] = (uint8_t)number;
  RadioSlot *stored = &gDeviceConfig.radio.slots[selectedSlot];
  bool selectionChanged = stored->channel_number != number;
  stored->channel_number = (uint8_t)number;
  if (enterMode) stored->source = RADIO_SOURCE_CHANNEL;
  activeSlot = *stored;
  activeSlot.active_band = RADIO_MODEL_BandAt(channel.frequency_hz);
  activeSlot.current = (BandState){channel.frequency_hz, channel.rx};
  activeSlot.tx = channel.tx;
  resetLabel(channel.frequency_hz);
  tuned();
  if (enterMode || selectionChanged) DEVICE_RequestSave();
}

void RADIO_TuneChannel(uint16_t number) { tuneChannel(number, true); }

void RADIO_Step(bool up) {
  if (activeSlot.source == RADIO_SOURCE_CHANNEL) {
    uint16_t number = gDeviceConfig.radio.slots[selectedSlot].channel_number;
    uint16_t target;
    if (!number || !CHANNEL_STORE_Next(number, up, &target)) {
      notify("No saved channels");
      return;
    }
    tuneChannel(target, false);
    return;
  }
  uint32_t target;
  RadioParameters params;
  if (TUNING_StepVfo(&activeSlot, gDeviceConfig.radio.presets, up, &target, &params))
    request(target, params);
}

void RADIO_SwitchSource(void) {
  RadioSlot *stored = &gDeviceConfig.radio.slots[selectedSlot];
  if (stored->source == RADIO_SOURCE_CHANNEL) {
    if (!tuneExact(stored->current.frequency_hz, stored->current.parameters)) return;
    stored->source = RADIO_SOURCE_VFO;
    activeSlot = *stored;
    tuned();
    DEVICE_RequestSave();
    return;
  }
  uint16_t number = stored->channel_number;
  if (!CHANNEL_STORE_IsOccupied(number)) {
    number = 0;
    for (uint16_t candidate = 1; candidate <= USER_CHANNEL_COUNT; ++candidate)
      if (CHANNEL_STORE_IsOccupied(candidate)) { number = candidate; break; }
  }
  if (!number) {
    notify(CHANNEL_STORE_Available() ? "No saved channels" : "No channel memory");
    return;
  }
  tuneChannel(number, true);
}

static void selectSlot(uint8_t target) {
  RadioSlot next;
  if (!effectiveSlot(target, &next, NULL)) {
    notify("Channel unavailable");
    return;
  }
  if (!tuneExact(next.current.frequency_hz, next.current.parameters)) return;
  selectedSlot = target;
  activeSlot = next;
  tuned();
  gDeviceConfig.radio.selected_slot = target;
  DEVICE_RequestSave();
}
void RADIO_SwitchSlot(void) { selectSlot(RADIO_STATE_NextSlot(&gDeviceConfig.radio)); }

void RADIO_NextBand(void) {
  if (!requireVfoSource()) return;
  ReceiverCapabilities capabilities = RECEIVER_Capabilities();
  uint8_t band = RADIO_MODEL_NextBand(activeSlot.active_band, capabilities);
  uint32_t frequency = activeSlot.bands[band].frequency_hz;
  // A remembered frequency this hardware cannot receive moves into the
  // nearest receivable part of the band (UV-K1 SW starts at 18 MHz).
  RadioSpan span;
  if (RADIO_MODEL_BandSpan(band, frequency, capabilities, &span))
    frequency = frequency < span.start_hz ? span.start_hz
              : frequency >= span.end_hz ? span.end_hz - 1u : frequency;
  request(frequency, gDeviceConfig.radio.presets[band]);
}

void RADIO_NextMode(void) { RADIO_AdjustRx(RADIO_FIELD_MODE, true); }

void RADIO_AdjustRx(RadioParameterField field, bool up) {
  if (!requireVfoSource()) return;
  RadioParameters candidate = activeSlot.current.parameters;
  if (field == RADIO_FIELD_RX_TONE && candidate.mode != RX_NFM) {
    notify("NFM only");
    return;
  }
  if (RADIO_MODEL_Adjust(activeSlot.current.frequency_hz, &candidate,
                         RECEIVER_Capabilities(), field, up))
    request(activeSlot.current.frequency_hz, candidate);
  else if (field == RADIO_FIELD_BANDWIDTH && candidate.mode == RX_WFM)
    notify("Fixed");
  else if (field == RADIO_FIELD_MODE)
    notify("No option");
}

// Back to the band preset; frequency and TX stay.
void RADIO_ResetToPreset(void) {
  if (!requireVfoSource()) return;
  request(activeSlot.current.frequency_hz, gDeviceConfig.radio.presets[activeSlot.active_band]);
  notify("Defaults restored");
}

const RadioSlot *RADIO_Selected(void) { return &activeSlot; }
uint8_t RADIO_SelectedId(void) { return selectedSlot; }

// --- Status for display ---------------------------------------------------

void RADIO_SlotStatus(uint8_t id, RadioSlotStatus *status) {
  *status = (RadioSlotStatus){0};
  if (id >= RADIO_SLOT_CAPACITY) return;
  const RadioSlot *stored = &gDeviceConfig.radio.slots[id];
  bool selected = id == selectedSlot;
  UserChannel channel = {0};
  bool ok;
  if (selected) {
    status->slot = activeSlot;
    ok = activeSlot.source != RADIO_SOURCE_CHANNEL || loadChannelForSlot(id, &channel);
  } else {
    ok = effectiveSlot(id, &status->slot, &channel);
  }
  if (!ok) status->slot = *stored;
  const BandState *current = &status->slot.current;
  const RadioParameters *p = &current->parameters;
  const RadioParameters *preset = &gDeviceConfig.radio.presets[status->slot.active_band];
  bool channelMode = stored->source == RADIO_SOURCE_CHANNEL;
  status->channel_missing = channelMode && !ok;
  status->modified = !channelMode && (p->mode != preset->mode || p->step_hz != preset->step_hz ||
    p->bandwidth_hz != preset->bandwidth_hz || p->squelch != preset->squelch ||
    p->rx_tone != preset->rx_tone);
  status->frequency_hz = selected && seek.active ? seek.cursor_hz : current->frequency_hz;
  status->receiving = receivingSlot == id && RADIO_IsReceiving();
  if (status->receiving) status->signal_level = RX_DEVICE_SignalLevel(&gReceiver.plan, gReceiver.rssi);
  const SlotLabel *label = &labels[id];
  bool textValid = label->frequency_hz == current->frequency_hz;
  const char *name = channelMode ? (ok ? channel.name : "") : textValid ? label->ps : "";
  Text t = TEXT_Begin(status->name, sizeof(status->name));
  TEXT_Add(&t, name);
  status->radio_text = textValid && label->rt_until ? label->text : NULL;
  status->activity =
      id == txSlot && (transmitter.mode == RADIO_TX || transmitter.mode == RADIO_TX_TAIL) ? RADIO_ACTIVITY_TRANSMITTING :
      id == txSlot && transmitter.mode == RADIO_FAULT ? RADIO_ACTIVITY_FAULT :
      selected && seek.active ? RADIO_ACTIVITY_SEEKING :
      status->receiving && gReceiver.open ? RADIO_ACTIVITY_RECEIVING :
      status->channel_missing || (selected && !runtime.active) ? RADIO_ACTIVITY_UNAVAILABLE :
      RADIO_ACTIVITY_IDLE;
  status->fc_running = selected && capturing;
  if (status->fc_running) {
    status->capture_hz = capture.last_hz;
    status->capture_scope = capture.scope;
    status->capture_range = capture.range;
  }
  const Spectrum *sweep = selected ? RADIO_Spectrum() : NULL;
  if (sweep) { // The card follows the spectrum cursor.
    status->frequency_hz = SPECTRUM_Frequency(sweep, sweep->cursor);
    status->name[0] = '\0';
    status->radio_text = NULL;
    uint8_t level = sweep->level[sweep->cursor];
    RxPlan bk = {.device = RX_DEVICE_BK, .frequency_hz = status->frequency_hz};
    status->receiving = level != 0;
    status->signal_level = level ? RX_DEVICE_SignalLevel(&bk, level * 2u) : 0;
    status->activity = RADIO_ACTIVITY_IDLE;
  }
}

// --- Channels and settings hooks ------------------------------------------

void RADIO_ChannelDraft(UserChannel *channel) {
  *channel = (UserChannel){
    .frequency_hz = activeSlot.current.frequency_hz,
    .rx = activeSlot.current.parameters,
    .tx = activeSlot.tx,
  };
  // A broadcast's RDS station name (PS) is the suggested name.
  const SlotLabel *label = &labels[selectedSlot];
  if (label->frequency_hz == channel->frequency_hz && label->ps[0]) {
    Text t = TEXT_Begin(channel->name, sizeof(channel->name));
    TEXT_Add(&t, label->ps);
    for (size_t n = strlen(channel->name); n && channel->name[n-1] == ' '; --n)
      channel->name[n-1] = '\0'; // PS is space-padded to 8 characters.
  }
}

uint16_t RADIO_ChannelNumber(void) {
  const RadioSlot *stored = &gDeviceConfig.radio.slots[selectedSlot];
  return stored->source == RADIO_SOURCE_CHANNEL ? stored->channel_number : 0;
}

void RADIO_ChannelSaved(uint16_t number, const UserChannel *channel) {
  gDeviceConfig.radio.slots[selectedSlot].channel_number = (uint8_t)number;
  activeSlot.channel_number = (uint8_t)number;
  for (uint8_t i = 0; i < RADIO_SLOT_CAPACITY; ++i) {
    if (gDeviceConfig.radio.slots[i].channel_number == number) {
      channelCache[i] = *channel;
      channelCacheNumber[i] = (uint8_t)number;
      channelCacheValid[i] = true;
    }
  }
  DEVICE_RequestSave();
}

// A hidden selection moves back to A.
void RADIO_SlotsChanged(void) {
  (void)pauseWatch();
  if (selectedSlot < gDeviceConfig.radio.slot_count) return;
  selectedSlot = gDeviceConfig.radio.selected_slot = 0;
  loadSelected();
}

// Drop cached channels; slots whose channel is gone go back to VFO, and the
// selected one is retuned.
void RADIO_ChannelsChanged(void) {
  bool selectedLost = false;
  for (uint8_t id = 0; id < RADIO_SLOT_CAPACITY; ++id) {
    RadioSlot *slot = &gDeviceConfig.radio.slots[id];
    channelCacheValid[id] = false;
    if (slot->source == RADIO_SOURCE_CHANNEL && !CHANNEL_STORE_IsOccupied(slot->channel_number)) {
      slot->source = RADIO_SOURCE_VFO;
      slot->channel_number = 0;
      selectedLost |= id == selectedSlot;
      DEVICE_RequestSave();
    }
  }
  (void)pauseWatch(); // Watch plans may name a changed channel.
  if (selectedLost) loadSelected();
}

// Reapply only the active receive plan. Global settings never rewrite slots
// or presets, and the receive runtime remains responsible for recovery.
bool RADIO_ApplyRxSettings(void) {
  if (toneScanning) RADIO_StopToneScan(false);
  if (capturing) cancelCapture(NULL);
  if (!initialized) return true;
  RxPlan plan;
  if (runtime.active) plan = runtime.current;
  else if (RADIO_MODEL_Plan(activeSlot.current.frequency_hz, activeSlot.current.parameters,
      RECEIVER_Capabilities(), &plan) != RX_OK) return false;
  if (!tuneDirect(&plan)) return false;
  receivingSlot = selectedSlot;
  return true;
}

// Leaving the main screen ends what its keys drive; reception and watch go on.
void RADIO_CancelInteractive(void) {
  confirming = false;
  if (toneScanning) RADIO_StopToneScan(false);
  if (capturing) cancelCapture(NULL);
  RADIO_AbortTx();
  if (seek.active) finishSeek(false, 0, NULL);
}

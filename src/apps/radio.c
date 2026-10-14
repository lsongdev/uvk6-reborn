// Radio app controller: slots, VFO/MR resolution, the tune transaction and
// key dispatch. Features live in radio-*.c; see radio-internal.h.
#include "apps/radio-internal.h"

static bool initialized;
uint8_t selectedSlot, receivingSlot = UINT8_MAX, inputSlot;
NumberInput number;
bool inputTx;
SlotLabel labels[RADIO_SLOT_CAPACITY];
RxRuntime runtime;
RdsReceiver rds;
bool inMenu;
bool confirming;
static UserChannel channelCache[RADIO_SLOT_CAPACITY];
static bool channelCacheValid[RADIO_SLOT_CAPACITY];
static uint8_t channelCacheNumber[RADIO_SLOT_CAPACITY];
RadioSlot activeSlot; // Selected slot's effective source; MR never writes VFO memories.
RxPlan pending;
const char *message;
static uint32_t messageAt;
bool functionPending;
static uint32_t functionAt;

bool loadChannelForSlot(uint8_t id, UserChannel *channel) {
  if (id >= RADIO_SLOT_CAPACITY || !channel) return false;
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

bool effectiveSlot(uint8_t id, RadioSlot *effective,
                          UserChannel *channel) {
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
bool RADIO_APP_isShortcutPending(void) { return functionPending; }

void RADIO_APP_cancelShortcut(void) {
  if (functionPending) {
    functionPending = false;
    gRedrawScreen = true;
  }
}

// These operations recur across FC, Seek, Watch, PTT and manual input.
// Keep their event ordering identical while sharing one implementation.
void __attribute__((noinline)) resetRds(void) {
  RDS_ReceiverReset(&rds, Now());
}

// Leaving the main screen (settings, band editor, text input) ends what its
// keys drive, but reception and watch keep running in the background.
void RADIO_APP_deinit(void) {
  number.active = false;
  inMenu = confirming = false;
  if (capturing) cancelCapture(NULL);
  RADIO_APP_PttRelease();
  RADIO_APP_cancelShortcut();
  if (seek.active)
    finishSeek(false, 0, NULL);
}

static void expireShortcut(uint32_t now) {
  if (functionPending && now-functionAt >= 3000u)
    RADIO_APP_cancelShortcut();
}

void notify(const char *text) {
  message = text;
  messageAt = Now();
  gRedrawScreen = true;
}

static void channelSaved(uint8_t id, uint16_t number,
                         const UserChannel *channel) {
  if (id >= RADIO_SLOT_CAPACITY || !channel) return;
  gDeviceConfig.radio.slots[id].channel_number = (uint8_t)number;
  if (id == selectedSlot) activeSlot.channel_number = (uint8_t)number;
  for (uint8_t i = 0; i < RADIO_SLOT_CAPACITY; ++i) {
    if (gDeviceConfig.radio.slots[i].channel_number == number) {
      channelCache[i] = *channel;
      channelCacheNumber[i] = (uint8_t)number;
      channelCacheValid[i] = true;
    }
  }
  DEVICE_RequestSave();
}

static const char *unavailable(RxResult result) {
  return result == RX_PATCH_MISSING ? "SSB patch missing" : "RX unavailable";
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
  if (needsSsb && !wasSsb) {
    notify("WAIT...");
    RADIO_APP_render();
    DISPLAY_Blit();
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

void request(uint32_t target, RadioParameters values) {
  (void)pauseWatch();
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
    notify(unavailable(result));
    return;
  }
  if (RADIO_MODEL_BandAt(plan.frequency_hz) != RADIO_MODEL_BandAt(target)) {
    notify("Band boundary");
    return;
  }
  if (plan.frequency_hz != target) {
    pending = plan;
    confirming = true;
    inMenu = false;
    gRedrawScreen = true;
  } else {
    apply(&plan);
  }
}

static void tuneTo(uint32_t target) {
  if (inputSlot != selectedSlot) return;
  uint8_t band = RADIO_MODEL_BandAt(target);
  if (band == UINT8_MAX) {
    notify("Out of range");
    return;
  }
  RadioParameters values = band == activeSlot.active_band ? activeSlot.current.parameters
                                                     : gDeviceConfig.radio.presets[band];
  request(target, values);
}

// RX keypad entry and the TX Offset / TX Freq menu edit share one input
// lifecycle. TX edits the repeater offset for +/-, the frequency for Split.
void beginFrequencyInput(bool tx) {
  inputSlot = selectedSlot;
  inputTx = tx;
  message = NULL;
  const TxConfig *t = &activeSlot.tx;
  bool split = t->shift == TX_SHIFT_SPLIT;
  uint32_t initial = !tx ? 0 : !split ? t->offset_hz :
      t->split_hz ? t->split_hz : activeSlot.current.frequency_hz;
  NUMBER_Begin(&number, tx ? 0u : RADIO_MIN_HZ,
               !tx ? RADIO_MAX_HZ : split ? 1300000000u : TX_OFFSET_MAX_HZ,
               1000000u, initial, tx);
}

void RADIO_APP_ChannelsChanged(void) {
  for (uint8_t id = 0; id < RADIO_SLOT_CAPACITY; ++id) {
    RadioSlot *slot = &gDeviceConfig.radio.slots[id];
    channelCacheValid[id] = false;
    if (slot->source == RADIO_SOURCE_CHANNEL && !CHANNEL_STORE_IsOccupied(slot->channel_number)) {
      slot->source = RADIO_SOURCE_VFO;
      slot->channel_number = 0;
      DEVICE_RequestSave();
    }
  }
  (void)pauseWatch(); // Watch plans may name a changed channel.
}

void RADIO_APP_init(void) {
  RADIO_APP_cancelShortcut();
  // Returning from another screen keeps reception as it is, unless the
  // selection or the selected slot's source changed meanwhile.
  if (initialized && selectedSlot==gDeviceConfig.radio.selected_slot &&
      selectedSlot<gDeviceConfig.radio.slot_count &&
      activeSlot.source==gDeviceConfig.radio.slots[selectedSlot].source) return;
  initialized = true;
  transmitter = (TxRuntime){0};
  CHANNEL_MANAGER_Init(notify, channelSaved);
  seek.active = false;
  if (!RADIO_STATE_Valid(&gDeviceConfig.radio))
    RADIO_STATE_Default(&gDeviceConfig.radio);
  selectedSlot = gDeviceConfig.radio.selected_slot;
  bool invalidSavedChannel = false;
  for (uint8_t id = 0; id < RADIO_SLOT_CAPACITY; ++id) channelCacheValid[id] = false;
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

bool RADIO_APP_isReceiving(void) {
  return initialized && receivingSlot != UINT8_MAX && !WATCH_IsProbing(&watcher) && runtime.active && !seek.active && !capturing &&
         transmitter.mode == RADIO_RX;
}

static bool readRds(void *unused, RdsPacket *packet, bool clear_fifo) {
  (void)unused;
  return RX_DEVICE_ReadRDS(packet, clear_fifo);
}

void RADIO_APP_update(void) {
  if (!initialized) return; // Not yet started (e.g. the EEPROM reset page).
  uint32_t now = Now();
  expireShortcut(now);
  RADIO_APP_TxSafetyUpdate(); // Also permit deterministic host/UI unit tests.
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
  if (capturing) { captureUpdate(); return; }
  seekUpdate();
  watchUpdate();
  uint8_t rdsSlot=receivingSlot;
  const RxPlan *rdsPlan=&runtime.current;
  bool rdsEnabled=RADIO_APP_isReceiving() && runtime.active &&
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
      // A changed RadioText is shown in full for a while, then the header
      // returns to band and frequency until the text changes again.
      if (rds.decoder.text[0] && memcmp(label->text, rds.decoder.text, sizeof(label->text)))
        label->rt_until = (now + RADIO_RT_SHOW_MS) | 1u;
      label->frequency_hz = rdsPlan->frequency_hz;
      memcpy(label->ps, rds.decoder.ps, sizeof(rds.decoder.ps));
      memcpy(label->text, rds.decoder.text, sizeof(rds.decoder.text));
    }
    gRedrawScreen = true;
  }
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
    notify(unavailable(result));
    return false;
  }
  return tuneDirect(&plan);
}

// Reapply only the active receive plan. Global settings never rewrite VFOs
// or presets, and the receive runtime remains responsible for recovery.
bool RADIO_APP_ApplyRxSettings(void) {
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
  if (enterMode) {
    stored->source = RADIO_SOURCE_CHANNEL;
  }
  activeSlot = *stored;
  activeSlot.active_band = RADIO_MODEL_BandAt(channel.frequency_hz);
  activeSlot.current = (BandState){channel.frequency_hz, channel.rx};
  activeSlot.tx = channel.tx;
  resetLabel(channel.frequency_hz);
  tuned();
  if (enterMode || selectionChanged) DEVICE_RequestSave();
}

static void nextChannel(bool up) {
  uint16_t number = gDeviceConfig.radio.slots[selectedSlot].channel_number;
  uint16_t target;
  if (!number || !CHANNEL_STORE_Next(number, up, &target)) {
    notify("No saved channels");
    return;
  }
  tuneChannel(target, false);
}

static void switchSource(void) {
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

static void switchSlot(void) {
  uint8_t target = RADIO_STATE_NextSlot(&gDeviceConfig.radio);
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

// F-prefix and side-key shortcuts must enforce the same VFO-only rule.
static bool requireVfoSource(void) {
  if (activeSlot.source == RADIO_SOURCE_VFO) return true;
  notify("VFO only");
  return false;
}

static void nextBand(void) {
  if (!requireVfoSource()) return;
  uint8_t band = RADIO_MODEL_NextBand(activeSlot.active_band);
  const BandState *saved = &activeSlot.bands[band];
  request(saved->frequency_hz, gDeviceConfig.radio.presets[band]);
}

void nextMode(void) {
  if (!requireVfoSource()) return;
  RadioParameters candidate = activeSlot.current.parameters;
  if (RADIO_MODEL_Adjust(activeSlot.current.frequency_hz, &candidate,
                         RECEIVER_Capabilities(), RADIO_FIELD_MODE, true))
    request(activeSlot.current.frequency_hz, candidate);
  else
    notify("No option");
}

static void frequencyInputKey(KEY_Code_t key, Key_State_t state) {
  uint32_t value;
  if (inputSlot != selectedSlot) { number.active = false; return; }
  NumberInputResult result = NUMBER_Key(&number, key, state, &value);
  if (result == NUMBER_ACCEPTED) {
    if (inputTx) {
      if (!setTxValue(value)) { number.active = true; number.invalid = true; }
    } else tuneTo(value);
  }
  if (result != NUMBER_EDITING && !number.active) delayWatch();
  gRedrawScreen = true;
}

static void shortcutKey(KEY_Code_t key) {
  RADIO_APP_cancelShortcut();
  switch (key) {
  case KEY_1: nextBand(); break;
  case KEY_0: nextMode(); break;
  case KEY_2: switchSlot(); break;
  case KEY_3: switchSource(); break;
  case KEY_4: startCapture(); break;
  case KEY_6: {
    static char powerText[20]; // Outlives the call: notify keeps the pointer.
    if (changeTxPower(true)) {
      Text t = TEXT_Begin(powerText, sizeof(powerText));
      TEXT_Add(&t, "TX Power: ");
      TEXT_Add(&t, txPowerNames[activeSlot.tx.power]);
      notify(powerText);
    }
    break;
  }
  case KEY_STAR: startSeek(); break;
  default:
    if (key != KEY_F && key != KEY_EXIT) notify("Not supported");
    break;
  }
}

bool RADIO_APP_key(KEY_Code_t key, Key_State_t state) {
  expireShortcut(Now());
  if (capturing) {
    captureKey(key, state);
    return true;
  }
  if (number.active) {
    frequencyInputKey(key, state);
    return true;
  }
  if (seek.active) {
    if (state == KEY_RELEASED && (key == KEY_EXIT || key == KEY_MENU))
      finishSeek(false, 0, "Seek cancelled");
    return true; // Search temporarily owns all controls except cancel.
  }
  if (key == KEY_PTT) {
    pttKey(state);
    return true;
  }
  if (txBusy())
    return true;
  if (CHANNEL_MANAGER_HandleKey(key, state)) {
    gRedrawScreen = true;
    return true;
  }
  if (confirming) {
    if (state == KEY_RELEASED) {
      if (key == KEY_MENU) {
        confirming = false;
        apply(&pending);
      } else if (key == KEY_EXIT) {
        confirming = false;
      }
    }
    return true;
  }
  if (inMenu)
    return menuKey(key, state);
  // The function key is a sequential, one-shot prefix on the main screen.
  // Consume the target so it cannot also enter frequency input or a menu.
  if (functionPending) {
    if (key == KEY_MENU && state == KEY_LONG_PRESSED) {
      RADIO_APP_cancelShortcut();
      return false; // Keep the global settings shortcut available.
    }
    if (state == KEY_RELEASED) shortcutKey(key);
    return true;
  }
  if ((key == KEY_UP || key == KEY_DOWN) &&
      (state == KEY_RELEASED || state == KEY_LONG_PRESSED_CONT)) {
    if (activeSlot.source == RADIO_SOURCE_CHANNEL) {
      nextChannel(key == KEY_UP);
      return true;
    }
    uint32_t target;
    RadioParameters params;
    if (TUNING_StepVfo(&activeSlot, gDeviceConfig.radio.presets, key == KEY_UP,
                       &target, &params))
      request(target, params);
    return true;
  }
  if (state != KEY_RELEASED)
    return false;
  if (key == KEY_F) {
    functionPending = true;
    functionAt = Now();
    gRedrawScreen = true;
  } else if (key == KEY_SIDE1) {
    nextBand();
  } else if (key == KEY_SIDE2) {
    nextMode();
  }
  else if (key == KEY_MENU)
    inMenu = true;
  else if (key == KEY_EXIT)
    ; // Consumed: the main screen has nothing to close.
  else if (key <= KEY_9) {
    if (activeSlot.source == RADIO_SOURCE_CHANNEL) {
      notify("VFO only");
      return true;
    }
    beginFrequencyInput(false);
    uint32_t unused;
    NUMBER_Key(&number, key, state, &unused);
  } else {
    return false;
  }
  return true;
}

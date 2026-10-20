// PTT, busy lockout, talkback, TX safety deadlines and per-slot TX settings.
#include "radio/radio-internal.h"

TxRuntime transmitter;
uint8_t txSlot;
static TxConfig replyTx;
static const TxConfig *txConfig(void) {
  return txSlot==selectedSlot ? &activeSlot.tx : &replyTx;
}
static void startTransmit(void);
static bool pttPending;
static uint32_t pttCheckAt;
// PTT owns the radio: transmitting, checking busy, or waiting for release.
bool RADIO_TxBusy(void) {
  return pttPending || transmitter.mode != RADIO_RX || transmitter.wait_release;
}

static void txMute(void *unused) {
  (void)unused;
  RECEIVER_Mute();
  receivingSlot = UINT8_MAX;
  resetRds();
}
static bool txPrepare(void *unused, const TxPlan *plan) {
  (void)unused; return TX_DEVICE_Prepare(plan);
}
static bool txEnable(void *unused) {
  (void)unused; return TX_DEVICE_Enable();
}
static void txDisable(void *unused) {
  (void)unused; TX_DEVICE_Disable();
}
static bool txRestore(void *unused, const RxPlan *plan) {
  (void)unused;
  if (!RECEIVER_Apply(plan)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    return false;
  }
  receivingSlot = txSlot;
  resetRds();
  return true;
}
static bool txTail(void *unused) {
  (void)unused; return TX_DEVICE_BeginTail();
}
static const TxOperations txOperations = {
  .mute_rx = txMute, .prepare = txPrepare, .enable = txEnable,
  .disable = txDisable, .restore_rx = txRestore, .begin_tail = txTail
};

void RADIO_AbortTx(void) {
  pttPending = false;
  TX_RuntimeAbort(&transmitter, &txOperations);
}
void RADIO_PttRelease(void) {
  pttPending = false;
  if ((transmitter.mode == RADIO_TX || transmitter.mode == RADIO_TX_TAIL) &&
      !TX_DEVICE_VoltageValid()) RADIO_AbortTx();
  else TX_RuntimeRelease(&transmitter, Now(), &txOperations);
}

void RADIO_TxSafetyUpdate(void) {
  bool onAir = transmitter.mode == RADIO_TX || transmitter.mode == RADIO_TX_TAIL;
  if (onAir && !TX_DEVICE_VoltageValid()) {
    RADIO_AbortTx();
    notify("TX: bad battery");
    return;
  }
  TxRuntimeEvent event = TX_RuntimeUpdate(&transmitter, Now(), &txOperations);
  if (event != TX_EVENT_NONE) {
    gRedrawScreen = true;
    if (transmitter.mode == RADIO_FAULT) notify("RX error");
    else if (event == TX_EVENT_TIMEOUT) notify("TX timeout");
  }
  // Physical release starts a tail once; subsequent polls only tick its deadline.
  if ((pttPending || transmitter.mode == RADIO_TX || transmitter.wait_release) &&
      !TX_DEVICE_PttPressed()) {
    RADIO_PttRelease();
    gRedrawScreen = true;
  }
  if (pttPending && (int32_t)(Now()-pttCheckAt) >= 0) {
    pttPending = false;
    bool busy = false;
    if (!runtime.active || receivingSlot != txSlot ||
        !RX_DEVICE_WatchSignal(&runtime.current, &busy)) {
      transmitter.wait_release = true;
      notify("TX busy/locked");
    } else if (busy) {
      transmitter.wait_release = true;
      notify("TX: RX Busy");
    } else startTransmit();
  }
}

// Selected slot TX settings: validated, stored and saved; VFO only.
bool RADIO_SetTx(const TxConfig *tx) {
  if (!requireVfoSource()) return false;
  if (!TX_MODEL_ConfigValid(tx)) { notify("Invalid value"); return false; }
  activeSlot.tx = gDeviceConfig.radio.slots[selectedSlot].tx = *tx;
  DEVICE_RequestSave();
  return true;
}

bool RADIO_CycleTxPower(bool up) {
  TxConfig tx = activeSlot.tx;
  tx.power = (TxPower)((tx.power + (up ? 1u : 2u)) % 3u);
  return RADIO_SetTx(&tx);
}

static bool txReady(void) {
  return !confirming && runtime.active && gDeviceConfigReady &&
      !(gDeviceConfig.preferences.keylock && gDeviceConfig.preferences.ptt_lock);
}

static void startTransmit(void) {
  if (!txReady() || transmitter.mode != RADIO_RX) {
    notify("TX busy/locked");
    return;
  }
  const TxConfig *tx = txConfig();
  TxCapabilities capability = TX_DEVICE_Capabilities(TX_MODEL_Frequency(tx, runtime.current.frequency_hz));
  TxPlan planned;
  TxResult result = TX_MODEL_Plan(&runtime.current, tx,
                                 &capability, &planned);
  // Target chip coverage and calibration are checked independently of RX mode.
  if (result == TX_OK && !TX_DEVICE_VoltageValid()) {
    notify("TX: bad battery");
    return;
  }
  if (result == TX_OK &&
      !TX_RuntimePress(&transmitter, &runtime.current, &planned,
                       gDeviceConfig.preferences.tx_timeout_seconds,
                       Now(), &txOperations))
    notify("TX error");
  else if (result == TX_DISABLED)
    notify("TX disabled");
  else if (result == TX_UNSUPPORTED_MODE)
    notify("TX: NFM only");
  else if (result == TX_UNVERIFIED_HARDWARE)
    notify("TX HW locked");
  else if (result == TX_UNCALIBRATED)
    notify("TX uncalibrated");
  else if (result == TX_OUT_OF_BAND)
    notify("TX out of band");
  else if (result != TX_OK)
    notify("TX not allowed");
}

void RADIO_PttPress(void) {
  // A re-press during the tail never starts a new transmission implicitly.
  if (transmitter.mode == RADIO_TX_TAIL) {
    transmitter.wait_release = true;
    return;
  }
  if (toneScanning || pttPending || transmitter.wait_release || transmitter.mode != RADIO_RX)
    return;
  // Talkback: during a watched talk or its reply window PTT answers that
  // slot with its own TX settings; otherwise it transmits on the selection.
  WatchTarget reply;
  RadioSlot effective;
  bool talkback = WATCH_Talk(&watcher, &reply) && reply.id != selectedSlot &&
                  effectiveSlot(reply.id, &effective, NULL);
  if (!(talkback ? pauseWatchTo(&reply) : pauseWatch())) return;
  txSlot = talkback ? reply.id : selectedSlot;
  if (talkback) replyTx = effective.tx;
  if (!txReady()) {
    notify("TX busy/locked");
    return;
  }
  if (gDeviceConfig.preferences.tx_busy_lockout) {
    // Restore/tune first, then sample the selected receiver after settling.
    // Keep this nonblocking so PTT release can cancel before PA preparation.
    pttPending = true;
    pttCheckAt = Now()+(runtime.current.device == RX_DEVICE_BK ? 180u : 350u);
  } else {
    startTransmit();
  }
}

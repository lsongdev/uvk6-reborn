// PTT, busy lockout, TX safety deadlines and per-slot TX settings.
#include "apps/radio-internal.h"

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
bool txBusy(void) {
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
static const TxOperations txOperations = {
  .mute_rx = txMute, .prepare = txPrepare, .enable = txEnable,
  .disable = txDisable, .restore_rx = txRestore
};

void RADIO_APP_PttRelease(void) {
  pttPending = false;
  TX_RuntimeRelease(&transmitter, &txOperations);
}

void RADIO_APP_TxSafetyUpdate(void) {
  // Check the physical button, not just the queued keyboard release event.
  if ((pttPending || transmitter.mode == RADIO_TX) && !TX_DEVICE_PttPressed()) {
    RADIO_APP_PttRelease();
    gRedrawScreen = true;
    return;
  }
  if (transmitter.mode == RADIO_TX && !TX_DEVICE_VoltageValid()) {
    RADIO_APP_PttRelease();
    transmitter.wait_release = true;
    notify("TX: bad battery");
    return;
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
      notify("Channel busy");
    } else {
      startTransmit();
    }
  }
  // TOT is a system-level safety deadline, not a screen refresh feature.
  if (TX_RuntimeUpdate(&transmitter, Now(), &txOperations))
    notify(transmitter.mode == RADIO_FAULT ? "RX error" : "TX timeout");
}

// Accepts the TX Offset (+/-) or TX Freq (Split) edit; zero clears it.
bool setTxValue(uint32_t value) {
  TxConfig tx = activeSlot.tx;
  if (tx.shift == TX_SHIFT_SPLIT) tx.split_hz = value; else tx.offset_hz = value;
  if (inputSlot != selectedSlot || !TX_MODEL_ConfigValid(&tx)) return false;
  activeSlot.tx = gDeviceConfig.radio.slots[selectedSlot].tx = tx;
  DEVICE_RequestSave();
  notify("Saved");
  return true;
}

const char *const txPowerNames[3] = {"Low", "Medium", "High"};

bool changeTxPower(bool up) {
  if (activeSlot.source == RADIO_SOURCE_CHANNEL) {
    notify("VFO only");
    return false;
  }
  TxConfig *tx = &gDeviceConfig.radio.slots[selectedSlot].tx;
  tx->power = (tx->power + (up ? 1u : 2u)) % 3u;
  activeSlot.tx.power = tx->power;
  DEVICE_RequestSave();
  return true;
}

static bool txReady(void) {
  return !inMenu && !confirming && runtime.active && gDeviceConfigReady &&
      !(gDeviceConfig.preferences.keylock && gDeviceConfig.preferences.ptt_lock);
}

static void startTransmit(void) {
  if (!txReady() || transmitter.mode != RADIO_RX) {
    notify("TX busy/locked");
    return;
  }
  const TxConfig *tx = txConfig();
  TxCapabilities capability = TX_DEVICE_Capabilities(TX_MODEL_Frequency(tx, runtime.current.frequency_hz));
  TxPolicy policy = TX_DEVICE_Policy();
  TxPlan planned;
  TxResult result = TX_MODEL_Plan(&runtime.current, tx,
                                 &capability, &policy, &planned);
  // The board policy and target-frequency calibration authorize TX independently of RX.
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

void pttKey(Key_State_t state) {
  RADIO_APP_cancelShortcut();
  if (state == KEY_RELEASED) {
    RADIO_APP_PttRelease();
    return;
  }
  if (state != KEY_PRESSED || pttPending || transmitter.wait_release ||
      transmitter.mode != RADIO_RX)
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
  if (CHANNEL_MANAGER_Active() || !txReady()) {
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

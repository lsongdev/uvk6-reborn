#include "apps/vfo.h"
#include "hal/display.h"
#include "external/printf/printf.h"
#include "ui/menu.h"
#include "core/radio.h"
#include "core/device-config.h"
#include "core/rx-runtime.h"
#include "core/watch.h"
#include "core/tx-model.h"
#include "core/tx-runtime.h"
#include "hal/tx-device.h"
#include "core/settings.h"
#include "core/seek.h"
#include "core/rds-receiver.h"
#include "hal/rx-device.h"
#include "hal/scheduler.h"
#include "ui/components/signal.h"
#include "ui/components/frequency.h"
#include "ui/components/vfo.h"
#include "ui/graphics.h"
#include "ui/statusline.h"
#include "apps/apps.h"
#include "apps/finput.h"
#include <string.h>

static bool initialized;
static UI_VfoState cards[RADIO_VFO_CAPACITY];
static uint8_t selectedVfo, receivingVfo = UINT8_MAX, inputVfo;
static struct { uint32_t frequency_hz; char ps[9], text[65]; } labels[RADIO_VFO_CAPACITY];
static RxRuntime runtime;
static WatchRuntime watcher;
static uint32_t watchResumeAt;
static bool watchDelay;
static const char *watchStatus;
static bool pauseWatch(void);
static void watchUpdate(void);
static TxRuntime transmitter;
static RdsReceiver rds;
static bool changed;
static bool inMenu;
static bool confirming;
static uint8_t menuIndex;
enum { MENU_RX_WATCH = 9, MENU_CHANNEL_NAME, MENU_CHANNEL_SAVE, MENU_ITEM_COUNT };
static VfoConfig user; // Current UI state, committed only on successful RX.
static RxPlan pending;
static const char *message;
static uint32_t messageAt;
static bool functionPending;
static uint32_t functionAt;

// Search never mutates user/EEPROM until one final station is accepted.
typedef enum { SEEK_STEP, SEEK_SETTLE, SEEK_VERIFY,
               SEEK_NATIVE_START, SEEK_NATIVE_POLL, SEEK_WRAP_SETTLE } SeekPhase;
static Seek seek;
static SeekPhase seekPhase;
static RxPlan seekOrigin;
static bool seekNative;
static uint32_t seekAt, seekDeadline;
bool VFO_isShortcutPending(void) { return functionPending; }

void VFO_cancelShortcut(void) {
  if (functionPending) {
    functionPending = false;
    gRedrawScreen = true;
  }
}

static void finishSeek(bool found, uint32_t frequency_hz, const char *status);

static void stopReceiver(void *unused);
static void txMute(void *unused) {
  (void)unused;
  RADIO_StopReception();
  receivingVfo = UINT8_MAX;
  RDS_ReceiverReset(&rds, Now());
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
  if (!RADIO_ApplyRxPlan(ctx, plan)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    return false;
  }
  receivingVfo = selectedVfo;
  RDS_ReceiverReset(&rds, Now());
  return true;
}
static const TxOperations txOperations = {
  .mute_rx = txMute, .prepare = txPrepare, .enable = txEnable,
  .disable = txDisable, .restore_rx = txRestore
};

void VFO_PttRelease(void) {
  TX_RuntimeRelease(&transmitter, &txOperations);
}

void VFO_deinit(void) {
  VFO_PttRelease();
  VFO_cancelShortcut();
  if (seek.active)
    finishSeek(false, 0, NULL);
  (void)pauseWatch();
}

static void expireShortcut(void) {
  if (functionPending && Now()-functionAt >= 3000)
    VFO_cancelShortcut();
}

static void notify(const char *text) {
  message = text;
  messageAt = Now();
  gRedrawScreen = true;
}

static bool applyReceiver(void *unused, const RxPlan *plan) {
  (void)unused;
  RDS_ReceiverReset(&rds, Now());
  return RADIO_ApplyRxPlan(ctx, plan);
}

static void stopReceiver(void *unused) {
  (void)unused;
  RDS_ReceiverReset(&rds, Now());
  RADIO_StopReception();
  receivingVfo = UINT8_MAX;
}

static void apply(const RxPlan *plan) {
  bool needsSsb = plan->parameters.mode == RX_LSB || plan->parameters.mode == RX_USB;
  bool wasSsb = runtime.active && (runtime.current.parameters.mode == RX_LSB ||
                                  runtime.current.parameters.mode == RX_USB);
  if (needsSsb && !wasSsb) {
    FillRect(0, LCD_YCENTER-4, LCD_WIDTH, 9, C_FILL);
    PrintMediumBoldEx(LCD_XCENTER, LCD_YCENTER+3, POS_C, C_INVERT, "WAIT...");
    DISPLAY_Blit();
  }
  RxTuneResult result = RX_RuntimeTune(&runtime, plan,
                                      applyReceiver, stopReceiver, NULL);
  if (result != RX_APPLIED) {
    notify(result == RX_RESTORED ? "Tune failed" : "RX error");
    return;
  }
  if (!RADIO_VFO_Set(&user, plan->frequency_hz, plan->parameters)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    notify("State error");
    return;
  }
  // TX edits are committed independently from the receive draft.
  user.tx = gDeviceConfig.radio.vfos[selectedVfo].tx;
  user.watch = gDeviceConfig.radio.vfos[selectedVfo].watch;
  gDeviceConfig.radio.vfos[selectedVfo] = user;
  receivingVfo = selectedVfo;
  memset(&labels[selectedVfo], 0, sizeof(labels[selectedVfo]));
  labels[selectedVfo].frequency_hz = plan->frequency_hz;
  DEVICE_RequestSave();
  const RadioParameters *preset = &gDeviceConfig.radio.presets[user.active_band];
  changed = user.current.parameters.mode != preset->mode || user.current.parameters.step_hz != preset->step_hz ||
            user.current.parameters.bandwidth_hz != preset->bandwidth_hz || user.current.parameters.squelch != preset->squelch || user.current.parameters.rx_tone != preset->rx_tone;
  message = NULL;
  UI_VfoShowFrequency(&cards[selectedVfo], Now());
}

// Forward-only seek owns temporary RF tuning; RxRuntime retains the last
// committed plan so cancel/exhaustion never writes the EEPROM.
static void finishSeek(bool found, uint32_t frequency_hz, const char *status) {
  // SEEK_Next/SEEK_Wrap may already have marked exhaustion.
  if (seekNative)
    RX_DEVICE_SeekCancel(seekOrigin.device);
  SEEK_Stop(&seek);
  RDS_ReceiverReset(&rds, Now());
  gRedrawScreen = true;

  if (found) {
    RxPlan selected;
    if (RADIO_MODEL_Plan(frequency_hz, seekOrigin.parameters,
                         RADIO_GetReceiverCapabilities(), &selected) == RX_OK &&
        selected.device == seekOrigin.device &&
        SEEK_Accept(&(Seek){.active=true, .origin_hz=seek.origin_hz,
                             .lower_hz=seek.lower_hz,.end_hz=seek.end_hz,
                             .wrapped=seek.wrapped}, selected.frequency_hz)) {
      apply(&selected); // The only place a found station is saved.
      return;
    }
    status = "Seek error";
  }
  // Restoring directly does not update RadioUserState or request a save.
  if (!RADIO_ApplyRxPlan(ctx, &seekOrigin)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    notify("RX error");
  } else if (status) {
    notify(status);
  }
}

static void startSeek(void) {
  if (!runtime.active || user.active_band >= radioBandCount) {
    notify("RX unavailable");
    return;
  }
  if (user.current.parameters.mode == RX_LSB ||
      user.current.parameters.mode == RX_USB) {
    notify("SSB seek off");
    return;
  }
  seekOrigin = runtime.current;
  if (!SEEK_Start(&seek, &radioBands[user.active_band],
                  seekOrigin.frequency_hz, seekOrigin.parameters.step_hz)) {
    notify("Seek unavailable");
    return;
  }
  seekNative = RX_DEVICE_SeekNative(&seekOrigin);
  seekPhase = seekNative ? SEEK_NATIVE_START : SEEK_STEP;
  seekAt = Now();
  seekDeadline = Now() + 15000;
  RADIO_StopReception();
  RDS_ReceiverReset(&rds, Now());
  message = NULL;
  gRedrawScreen = true;
}

static void seekUpdate(void) {
  if (!seek.active) return;
  uint32_t now = Now();
  if (seekPhase == SEEK_STEP) {
    uint32_t next;
    if (!SEEK_Next(&seek, &next)) {
      finishSeek(false, 0, "No station");
      return;
    }
    RxPlan candidate;
    RadioParameters params = seekOrigin.parameters;
    if (params.squelch < 4 && seekOrigin.device == RX_DEVICE_BK)
      params.squelch = 4; // Monitor/SQ0 must not match every noise carrier.
    if (RADIO_MODEL_Plan(next, params, RADIO_GetReceiverCapabilities(),
                         &candidate) != RX_OK ||
        candidate.device != seekOrigin.device ||
        candidate.frequency_hz < seek.lower_hz ||
        candidate.frequency_hz >= seek.end_hz)
      return;
    if (!RADIO_ApplyRxPlan(ctx, &candidate)) {
      finishSeek(false, 0, "Seek error");
      return;
    }
    seekAt = now;
    seekPhase = SEEK_SETTLE;
  } else if (seekPhase == SEEK_SETTLE || seekPhase == SEEK_VERIFY ||
             seekPhase == SEEK_WRAP_SETTLE) {
    uint32_t elapsed = now - seekAt;
    uint32_t wait = seekPhase == SEEK_VERIFY ? 80u : 180u;
    if (elapsed < wait) return;
    bool detected;
    if (!RX_DEVICE_SeekSignal(seekOrigin.device, &detected)) {
      finishSeek(false, 0, "Seek error");
      return;
    }
    if (detected && seekPhase == SEEK_SETTLE) {
      seekPhase = SEEK_VERIFY;
      seekAt = now;
    } else if (detected) {
      finishSeek(true, seek.cursor_hz, NULL);
    } else if (seekPhase == SEEK_WRAP_SETTLE) {
      seekPhase = SEEK_NATIVE_START;
    } else {
      seekPhase = SEEK_STEP;
    }
  } else if (seekPhase == SEEK_NATIVE_START) {
    // The SI hardware cannot search over arbitrary VFO modes or spacings.
    uint32_t end = seek.wrapped ? seek.origin_hz : seek.end_hz-1u;
    if (!RX_DEVICE_SeekStart(&seekOrigin, seek.lower_hz, end)) {
      finishSeek(false, 0, "Seek error");
      return;
    }
    seekDeadline = now + 15000u;
    seekAt = now;
    seekPhase = SEEK_NATIVE_POLL;
  } else if (seekPhase == SEEK_NATIVE_POLL) {
    if ((int32_t)(now-seekDeadline) >= 0) {
      finishSeek(false, 0, "Seek timeout");
      return;
    }
    if (now-seekAt < 90u) return;
    seekAt = now;
    uint32_t frequency = 0;
    RxSeekResult result = RX_DEVICE_SeekPoll(seekOrigin.device, &frequency);
    if (result == RX_SEEK_BUSY) return;
    if (result == RX_SEEK_ERROR) {
      finishSeek(false, 0, "Seek error");
      return;
    }
    if (result == RX_SEEK_FOUND && SEEK_Accept(&seek, frequency)) {
      finishSeek(true, frequency, NULL);
      return;
    }
    // A complete hardware segment ended; the application owns the one wrap.
    if (seek.wrapped || !SEEK_Wrap(&seek)) {
      finishSeek(false, 0, "No station");
      return;
    }
    RX_DEVICE_SeekCancel(seekOrigin.device);
    RxPlan first;
    if (RADIO_MODEL_Plan(seek.lower_hz, seekOrigin.parameters,
                         RADIO_GetReceiverCapabilities(), &first) != RX_OK ||
        first.device != seekOrigin.device ||
        !RADIO_ApplyRxPlan(ctx, &first)) {
      finishSeek(false, 0, "Seek error");
      return;
    }
    seek.cursor_hz = first.frequency_hz;
    seekAt = now;
    seekPhase = SEEK_WRAP_SETTLE; // Evaluate the first channel once.
  }
  gRedrawScreen = true;
}

static void request(uint32_t target, RadioParameters values) {
  RxPlan plan;
  RxResult result = RADIO_MODEL_Plan(target, values, RADIO_GetReceiverCapabilities(), &plan);
  if (result == RX_OUT_OF_RANGE || result == RX_INVALID_PARAMETERS) {
    notify("Invalid params");
    return;
  }
  if (result != RX_OK) {
    // A gap has no receiver. Stop the old audio and retain a navigable target,
    // without storing the unsupported target as an applied EEPROM frequency.
    user.current.frequency_hz = target;
    user.current.parameters = values;
    user.active_band = RADIO_MODEL_BandAt(target);
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    notify(result == RX_PATCH_MISSING ? "SSB patch missing" : "RX unavailable");
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

static void tuneTo(uint32_t value, uint32_t unused) {
  (void)unused;
  if (inputVfo != selectedVfo) return;
  uint32_t target = value*10u; // Legacy input/storage boundary is 10 Hz.
  uint8_t band = RADIO_MODEL_BandAt(target);
  if (band == UINT8_MAX) {
    notify("Out of range");
    return;
  }
  RadioParameters values = band == user.active_band ? user.current.parameters
                                                     : gDeviceConfig.radio.presets[band];
  request(target, values);
}

void VFO_init(void) {
  VFO_cancelShortcut();
  if (initialized && selectedVfo==gDeviceConfig.radio.selected_vfo &&
      selectedVfo<gDeviceConfig.radio.vfo_count) return;
  initialized = true;
  transmitter = (TxRuntime){0};
  seek.active = false;
  if (!RADIO_STATE_Valid(&gDeviceConfig.radio))
    RADIO_STATE_Default(&gDeviceConfig.radio);
  selectedVfo = gDeviceConfig.radio.selected_vfo;
  user = gDeviceConfig.radio.vfos[selectedVfo];
  receivingVfo = UINT8_MAX;
  request(user.current.frequency_hz, user.current.parameters);
}

bool VFO_isReceiving(void) {
  return initialized && receivingVfo != UINT8_MAX && !WATCH_IsProbing(&watcher) && runtime.active && !seek.active &&
         transmitter.mode == RADIO_RX;
}

static bool readRds(void *unused, RdsPacket *packet, bool clear_fifo) {
  (void)unused;
  return RX_DEVICE_ReadRDS(packet, clear_fifo);
}

void VFO_TxSafetyUpdate(void) {
  // TOT is a system-level safety deadline, not a screen refresh feature.
  if (TX_RuntimeUpdate(&transmitter, Now(), &txOperations))
    notify(transmitter.mode == RADIO_FAULT ? "RX restore error" : "TX timeout");
}

void VFO_update(void) {
  expireShortcut();
  VFO_TxSafetyUpdate(); // Also permit deterministic host/UI unit tests.
  if (message || (!inMenu && !confirming))
    for (uint8_t id=0; id<RADIO_VFO_CAPACITY; ++id)
      if (UI_VfoUpdate(&cards[id], Now())) gRedrawScreen = true;
  const UI_VfoState *card=&cards[selectedVfo];
  uint32_t duration=2500u;
  if (card->showing_message && card->overflow)
    duration=2u*UI_VFO_SCROLL_PAUSE_MS+card->overflow*UI_VFO_SCROLL_PIXEL_MS;
  if (message && Now()-messageAt >= duration) {
    message = NULL;
    gRedrawScreen = true;
  }
  if (transmitter.mode != RADIO_RX)
    return;
  seekUpdate();
  watchUpdate();
  bool rdsEnabled = VFO_isReceiving() && runtime.active && runtime.current.device == RX_DEVICE_SI &&
                    runtime.current.parameters.mode == RX_WFM;
  bool textExpired = rdsEnabled && Now()-rds.last_group >= 15000u;
  if (RDS_ReceiverUpdate(&rds, Now(), rdsEnabled, readRds, NULL) || textExpired) {
    if (receivingVfo < RADIO_VFO_CAPACITY && rdsEnabled) {
      labels[receivingVfo].frequency_hz = runtime.current.frequency_hz;
      memcpy(labels[receivingVfo].ps, rds.decoder.ps, sizeof(rds.decoder.ps));
      memcpy(labels[receivingVfo].text, rds.decoder.text, sizeof(rds.decoder.text));
    }
    gRedrawScreen = true;
  }
}

static bool restorePrimary(void) {
  RxPlan plan;
  if (RADIO_MODEL_Plan(user.current.frequency_hz,user.current.parameters,
      RADIO_GetReceiverCapabilities(),&plan)!=RX_OK ||
      plan.frequency_hz!=user.current.frequency_hz) {
    RX_RuntimeStop(&runtime,stopReceiver,NULL); notify("RX unavailable"); return false;
  }
  if (runtime.active && receivingVfo==selectedVfo && WATCH_SameReceiver(&runtime.current,&plan))
    return true;
  if (RX_RuntimeTune(&runtime,&plan,applyReceiver,stopReceiver,NULL)!=RX_APPLIED) {
    // A restored background watch plan is never a PTT/primary substitute.
    RX_RuntimeStop(&runtime,stopReceiver,NULL); notify("RX restore error"); return false;
  }
  receivingVfo=selectedVfo; gRedrawScreen=true;
  return true;
}
static bool pauseWatch(void) {
  bool wasWatching=watcher.phase!=WATCH_OFF;
  WATCH_Stop(&watcher);
  watchDelay=true; watchResumeAt=Now()+2000u;
  return !wasWatching || restorePrimary();
}
static bool watchTune(void *unused, const WatchTarget *target) {
  (void)unused;
  RxTuneResult result=RX_RuntimeTune(&runtime,&target->plan,applyReceiver,stopReceiver,NULL);
  if (result!=RX_APPLIED) return false;
  receivingVfo=target->id; gRedrawScreen=true;
  return true;
}
static bool watchSignal(void *unused, const RxPlan *plan, bool *found) {
  (void)unused; return RX_DEVICE_WatchSignal(plan,found);
}
static void watchAudio(void *unused, bool open) {
  (void)unused;
  if (!vfo || vfo->is_open==open) return;
  vfo->is_open=open; vfo->msm.open=open;
  RADIO_SetAudioOpen(open); gRedrawScreen=true;
}
static uint32_t watchNow(void *unused) { (void)unused; return Now(); }
static const WatchOperations watchOperations={
  .tune=watchTune,.signal=watchSignal,.audio=watchAudio,.now=watchNow
};
static void watchUpdate(void) {
  if (inMenu || confirming || seek.active || functionPending ||
      transmitter.mode!=RADIO_RX || transmitter.wait_release) return;
  if (!gDeviceConfig.radio.watch_enabled) {
    watchStatus=NULL;
    if (watcher.phase!=WATCH_OFF) (void)pauseWatch();
    return;
  }
  if (watchDelay) {
    if ((int32_t)(Now()-watchResumeAt)<0) return;
    watchDelay=false;
  }
  if (watcher.phase==WATCH_OFF) {
    if (!runtime.active) return;
    WatchTarget targets[RADIO_WATCH_LIMIT];
    uint8_t count=0;
    watchStatus="DW: select 2";
    for (uint8_t id=0; id<gDeviceConfig.radio.vfo_count && count<RADIO_WATCH_LIMIT; ++id) {
      const VfoConfig *v=&gDeviceConfig.radio.vfos[id];
      if (!v->watch) continue;
      RxPlan plan;
      RxResult result=RADIO_MODEL_Plan(v->current.frequency_hz,v->current.parameters,
          RADIO_GetReceiverCapabilities(),&plan);
      if (result!=RX_OK || plan.frequency_hz!=v->current.frequency_hz) {
        watchStatus="DW: unavailable"; continue;
      }
      if (!WATCH_Eligible(&plan)) { watchStatus="DW: SSB off"; continue; }
      targets[count++]=(WatchTarget){id,plan};
    }
    if (!WATCH_Start(&watcher,targets,count)) {
      if (count==2) watchStatus="DW: same channel";
      watchDelay=true; watchResumeAt=Now()+WATCH_RETRY_MS; return;
    }
  }
  watchStatus=NULL;
  bool wasRetrying=watcher.phase==WATCH_RETRY;
  WATCH_Update(&watcher,Now(),&watchOperations);
  // If all candidates fail, listen to the primary during bounded retry delay.
  if (watcher.phase==WATCH_RETRY && !wasRetrying) (void)restorePrimary();
}

static void switchVfo(void) {
  uint8_t target = RADIO_STATE_NextVfo(&gDeviceConfig.radio);
  const VfoConfig *next = &gDeviceConfig.radio.vfos[target];
  RxPlan plan;
  RxResult planned = RADIO_MODEL_Plan(next->current.frequency_hz,
      next->current.parameters, RADIO_GetReceiverCapabilities(), &plan);
  if (planned != RX_OK || plan.frequency_hz != next->current.frequency_hz) {
    notify(planned == RX_PATCH_MISSING ? "SSB patch missing" : "RX unavailable");
    return;
  }
  RxTuneResult result = RX_RuntimeTune(&runtime, &plan, applyReceiver, stopReceiver, NULL);
  if (result != RX_APPLIED) {
    notify(result == RX_RESTORED ? "Tune failed" : "RX error");
    return;
  }
  selectedVfo = target;
  receivingVfo = target;
  user = *next;
  gDeviceConfig.radio.selected_vfo = target;
  DEVICE_RequestSave();
  message = NULL;
  UI_VfoShowFrequency(&cards[selectedVfo], Now());
  gRedrawScreen = true;
}

static void nextBand(void) {
  uint8_t band = RADIO_MODEL_NextBand(user.active_band);
  const BandState *saved = &user.bands[band];
  request(saved->frequency_hz, gDeviceConfig.radio.presets[band]);
}

static void nextMode(void) {
  RadioParameters candidate = user.current.parameters;
  if (RADIO_MODEL_Adjust(user.current.frequency_hz, &candidate,
                         RADIO_GetReceiverCapabilities(), RADIO_FIELD_MODE, true))
    request(user.current.frequency_hz, candidate);
  else
    notify("No other mode");
}

static void setTxFrequency(uint32_t value, uint32_t unused) {
  (void)unused;
  if (inputVfo != selectedVfo) return;
  if (value > UINT32_MAX/10u || !TX_MODEL_FrequencyValid(value*10u)) {
    inMenu = false;
    notify("Invalid TX freq");
    return;
  }
  gDeviceConfig.radio.vfos[selectedVfo].tx.frequency_hz = value*10u;
  DEVICE_RequestSave();
  notify("TX freq saved");
}

static void changeValue(bool up) {
  if (menuIndex==MENU_RX_WATCH) {
    bool enabled=!gDeviceConfig.radio.vfos[selectedVfo].watch;
    if (!RADIO_STATE_SetWatch(&gDeviceConfig.radio,selectedVfo,enabled))
      notify("Watch limit: 2");
    else DEVICE_RequestSave();
    return;
  }
  if (menuIndex == MENU_CHANNEL_NAME || menuIndex == MENU_CHANNEL_SAVE) {
    notify("Not implemented");
    return;
  }
  if (menuIndex == 0) {
    nextMode();
    return;
  }
  if (menuIndex == 7) {
    if (!up) {
      gDeviceConfig.radio.vfos[selectedVfo].tx.frequency_hz = 0;
      DEVICE_RequestSave();
    } else {
      gFInputValue1 = (gDeviceConfig.radio.vfos[selectedVfo].tx.frequency_hz ? gDeviceConfig.radio.vfos[selectedVfo].tx.frequency_hz : user.current.frequency_hz)/10u;
      gFInputValue2 = 0;
      inputVfo = selectedVfo;
      gFInputCallback = setTxFrequency;
      FINPUT_setup(0, 130000000u, UNIT_MHZ, false);
      APPS_run(APP_FINPUT);
      STATUSLINE_SetText("TX Frequency");
    }
    return;
  }
  if (menuIndex == 8) {
    gDeviceConfig.radio.vfos[selectedVfo].tx.tone = TONE_Next(gDeviceConfig.radio.vfos[selectedVfo].tx.tone, up);
    DEVICE_RequestSave();
    return;
  }
  if (menuIndex == 4 && user.current.parameters.mode != RX_NFM) {
    notify("Tone: NFM only");
    return;
  }
  if (menuIndex == 5) {
    gDeviceConfig.radio.vfos[selectedVfo].tx.enabled = !gDeviceConfig.radio.vfos[selectedVfo].tx.enabled;
    DEVICE_RequestSave();
    return;
  }
  if (menuIndex == 6) {
    gDeviceConfig.radio.vfos[selectedVfo].tx.power = (gDeviceConfig.radio.vfos[selectedVfo].tx.power +
                              (up ? 1u : 2u)) % 3u;
    DEVICE_RequestSave();
    return;
  }
  RadioParameters candidate = user.current.parameters;
  if (RADIO_MODEL_Adjust(user.current.frequency_hz, &candidate,
                         RADIO_GetReceiverCapabilities(), (RadioParameterField)menuIndex, up))
    request(user.current.frequency_hz, candidate);
  else if (menuIndex == 2 && candidate.mode == RX_WFM)
    notify("Fixed bandwidth");
}

bool VFO_key(KEY_Code_t key, Key_State_t state) {
  expireShortcut();
  if (seek.active) {
    if (state == KEY_RELEASED && (key == KEY_EXIT || key == KEY_MENU))
      finishSeek(false, 0, "Seek cancelled");
    return true; // Search temporarily owns all controls except cancel.
  }
  if (key == KEY_PTT) {
    VFO_cancelShortcut();
    if (state == KEY_RELEASED) {
      VFO_PttRelease();
      return true;
    }
    if (state != KEY_PRESSED || transmitter.wait_release ||
        transmitter.mode != RADIO_RX)
      return true;
    if (!pauseWatch()) return true;
    if (inMenu || confirming || !runtime.active || !gDeviceConfigReady ||
        gSettings.pttLock) {
      notify("TX busy/locked");
      return true;
    }
    if (gDeviceConfig.preferences.tx_busy_lockout &&
        vfo && vfo->is_open) {
      notify("Channel busy");
      return true;
    }
    TxCapabilities capability = TX_DEVICE_Capabilities();
    TxPolicy policy = TX_DEVICE_Policy();
    TxPlan planned;
    TxResult result = TX_MODEL_Plan(&runtime.current, &gDeviceConfig.radio.vfos[selectedVfo].tx,
                                   &capability, &policy, &planned);
    // The production TX HAL deliberately provides no verified capabilities.
    // Receiver support can never be re-used as transmit authorization.
    if (result == TX_OK &&
        !TX_RuntimePress(&transmitter, &runtime.current, &planned,
                         gDeviceConfig.preferences.tx_timeout_seconds,
                         Now(), &txOperations))
      notify("TX hardware error");
    else if (result == TX_DISABLED)
      notify("TX disabled");
    else if (result == TX_UNSUPPORTED_MODE)
      notify("TX mode not supported");
    else if (result == TX_UNVERIFIED_HARDWARE)
      notify("TX HW locked");
    else if (result != TX_OK)
      notify("TX not allowed");
    return true;
  }
  if (transmitter.mode != RADIO_RX || transmitter.wait_release)
    return true;
  if ((state==KEY_RELEASED || state==KEY_LONG_PRESSED || state==KEY_LONG_PRESSED_CONT) &&
      !pauseWatch()) return true;
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
  if (inMenu) {
    if (key == KEY_MENU && state == KEY_LONG_PRESSED) {
      inMenu = false;
      return false; // System opens global settings.
    }
    if (state == KEY_RELEASED) {
      if (key == KEY_EXIT)
        inMenu = false;
      else if (key == KEY_UP || key == KEY_DOWN)
        menuIndex = key == KEY_DOWN ? (menuIndex+1)%MENU_ITEM_COUNT
                                   : (menuIndex+MENU_ITEM_COUNT-1)%MENU_ITEM_COUNT;
      else if (key == KEY_STAR || key == KEY_F || key == KEY_MENU)
        changeValue(key != KEY_F);
    }
    return true;
  }
  // The function key is a sequential, one-shot prefix on the main screen.
  // Consume the target so it cannot also enter frequency input or a menu.
  if (functionPending) {
    if (key == KEY_MENU && state == KEY_LONG_PRESSED) {
      VFO_cancelShortcut();
      return false; // Keep the global settings shortcut available.
    }
    if (state == KEY_RELEASED) {
      VFO_cancelShortcut();
      if (key == KEY_1)
        nextBand();
      else if (key == KEY_0)
        nextMode();
      else if (key == KEY_2)
        switchVfo();
      else if (key == KEY_STAR)
        startSeek();
      else if (key != KEY_F && key != KEY_EXIT)
        notify("Not supported");
    }
    return true;
  }
  if ((key == KEY_UP || key == KEY_DOWN) &&
      (state == KEY_RELEASED || state == KEY_LONG_PRESSED_CONT)) {
    uint32_t target = RADIO_MODEL_Step(user.current.frequency_hz, user.current.parameters.step_hz, key == KEY_UP);
    uint8_t band = RADIO_MODEL_BandAt(target);
    if (band != UINT8_MAX)
      request(target, band == user.active_band ? user.current.parameters
                                               : gDeviceConfig.radio.presets[band]);
    return true;
  }
  if (state != KEY_RELEASED)
    return false;
  if (key == KEY_F) {
    functionPending = true;
    functionAt = Now();
    gRedrawScreen = true;
  } else if (key == KEY_SIDE1)
    nextBand();
  else if (key == KEY_SIDE2)
    nextMode();
  else if (key == KEY_MENU)
    inMenu = true;
  else if (key == KEY_EXIT)
    gMonitorMode = false;
  else if (key <= KEY_9) {
    gFInputValue1 = gFInputValue2 = 0;
    inputVfo = selectedVfo;
    gFInputCallback = tuneTo;
    FINPUT_setup(RADIO_MIN_HZ/10, RADIO_MAX_HZ/10, UNIT_MHZ, false);
    APPS_run(APP_FINPUT);
    APPS_key(key, state);
  } else {
    return false;
  }
  return true;
}

void VFO_render(void) {
  char frequencyText[16];
  if (inMenu || confirming) STATUSLINE_SetText("VFO %c", 'A'+selectedVfo);
  else if (gDeviceConfig.radio.watch_enabled)
    STATUSLINE_SetText("DW %c", receivingVfo<RADIO_VFO_CAPACITY ? 'A'+receivingVfo : '-');
  else STATUSLINE_SetText("RADIO");
  if (confirming && !message) {
    const char *unit = UI_FormatFrequency(pending.frequency_hz, frequencyText);
    PrintMediumEx(2, 18, POS_L, C_FILL, "Apply %s?", rxModeNames[pending.parameters.mode]);
    PrintMediumEx(2, 30, POS_L, C_FILL, "%s %s", frequencyText, unit);
    PrintSmall(2, 40, "BW %u Hz STEP %u Hz", pending.parameters.bandwidth_hz, pending.parameters.step_hz);
    PrintSmall(2, 57, "MENU: apply  EXIT: cancel");
    return;
  }
  if (inMenu && !message) {
    static const char *const names[] = {
      "RX Mode", "RX Step", "RX Bandwidth", "RX Squelch", "RX Tone", "TX Enable", "TX Power", "TX Frequency", "TX Tone", "RX Watch", "Name", "Save"
    };
    _Static_assert(sizeof(names)/sizeof(names[0]) == MENU_ITEM_COUNT,
                   "Every VFO menu item needs a label");
    uint8_t first = menuIndex < 5 ? 0 : menuIndex-4;
    for (uint8_t i = first; i < MENU_ITEM_COUNT && i < first+5; ++i) {
      uint8_t y = 17+(i-first)*11;
      PrintMediumEx(2, y, POS_L, C_FILL, "%s", names[i]);
      if (i == 0)
        PrintSmallEx(124, y, POS_R, C_FILL, "%s", rxModeNames[user.current.parameters.mode]);
      else if (i == 1) {
        const char *unit = UI_FormatFrequency(user.current.parameters.step_hz, frequencyText);
        PrintSmallEx(124, y, POS_R, C_FILL, "%s %s", frequencyText, unit);
      }
      else if (i == 2) {
        if (user.current.parameters.mode == RX_WFM)
          PrintSmallEx(124, y, POS_R, C_FILL, "Fixed");
        else {
          const char *unit = UI_FormatFrequency(user.current.parameters.bandwidth_hz, frequencyText);
          PrintSmallEx(124, y, POS_R, C_FILL, "%s %s", frequencyText, unit);
        }
      } else if (i == 3)
        PrintSmallEx(124, y, POS_R, C_FILL, "%u", user.current.parameters.squelch);
      else if (i == 4 || i == 8) {
        char toneText[24];
        if (i == 4 && user.current.parameters.mode != RX_NFM)
          snprintf(toneText,sizeof(toneText),"NFM only");
        else
          TONE_Format(i == 4 ? user.current.parameters.rx_tone : gDeviceConfig.radio.vfos[selectedVfo].tx.tone, toneText);
        PrintSmallEx(124, y, POS_R, C_FILL, "%s", toneText);
      }
      else if (i == 5)
        PrintSmallEx(124, y, POS_R, C_FILL, "%s", gDeviceConfig.radio.vfos[selectedVfo].tx.enabled ? "On" : "Off");
      else if (i == 7) {
        if (!gDeviceConfig.radio.vfos[selectedVfo].tx.frequency_hz) PrintSmallEx(124,y,POS_R,C_FILL,"Same");
        else {
          const char *unit = UI_FormatFrequency(gDeviceConfig.radio.vfos[selectedVfo].tx.frequency_hz,frequencyText);
          PrintSmallEx(124,y,POS_R,C_FILL,"%s %s",frequencyText,unit);
        }
      } else if (i == 6) {
        static const char *const powers[] = {"Low", "Medium", "High"};
        PrintSmallEx(124, y, POS_R, C_FILL, "%s",
                     powers[gDeviceConfig.radio.vfos[selectedVfo].tx.power <= TX_POWER_HIGH ?
                            gDeviceConfig.radio.vfos[selectedVfo].tx.power : 0]);
      } else if (i==MENU_RX_WATCH) {
        PrintSmallEx(124,y,POS_R,C_FILL,"%s",gDeviceConfig.radio.vfos[selectedVfo].watch ? "On" : "Off");
      } else {
        PrintSmallEx(124,y,POS_R,C_FILL,"Not implemented");
      }
      if (i == menuIndex)
        FillRect(0, y-7, 128, 10, C_INVERT);
    }
    return;
  }
  uint8_t first=RADIO_STATE_VisibleFirst(&gDeviceConfig.radio);
  for (uint8_t row=0; row<RADIO_VFO_VISIBLE; ++row) {
    uint8_t id=first+row;
    bool selected = id == selectedVfo;
    const TxConfig *tx = &gDeviceConfig.radio.vfos[id].tx;
    const VfoConfig *config = selected ? &user : &gDeviceConfig.radio.vfos[id];
    const BandState *current = &config->current;
    const RadioParameters *p = &current->parameters;
    const RadioParameters *preset = &gDeviceConfig.radio.presets[config->active_band];
    bool modified = p->mode != preset->mode || p->step_hz != preset->step_hz ||
      p->bandwidth_hz != preset->bandwidth_hz || p->squelch != preset->squelch ||
      p->rx_tone != preset->rx_tone;
    bool receiving = receivingVfo==id && VFO_isReceiving();
    bool textValid = labels[id].frequency_hz == current->frequency_hz;
    UI_VfoView view = {
      .id = 'A'+id, .selected = selected,
      .frequency_hz = selected && seek.active ? seek.cursor_hz : current->frequency_hz,
      .mode = p->mode, .band_name = radioBands[config->active_band].name,
      .changed = modified, .step_hz = p->step_hz,
      .name = textValid ? labels[id].ps : NULL,
      .rds = textValid ? labels[id].text : NULL,
      .message = selected ? (message ? message :
          gDeviceConfig.radio.watch_enabled ? watchStatus : NULL) : NULL,
      .signal_valid = receiving, .signal_level = receiving ? UI_SMeterLevel() : 0,
      .activity = selected && transmitter.mode==RADIO_TX ? UI_VFO_TRANSMITTING :
        selected && transmitter.mode==RADIO_FAULT ? UI_VFO_FAULT :
        selected && seek.active ? UI_VFO_SEEKING :
        receivingVfo==id && WATCH_IsProbing(&watcher) ? UI_VFO_WATCHING :
        receiving && vfo->is_open ? UI_VFO_RECEIVING :
        selected && !runtime.active ? UI_VFO_UNAVAILABLE : UI_VFO_IDLE,
      .power_label = tx->power == TX_POWER_HIGH ? "HIGH" :
                     tx->power == TX_POWER_MEDIUM ? "MID" : "LOW",
    };
    UI_VfoRender(8+row*UI_VFO_HEIGHT, &view, &cards[id], Now());
  }
}

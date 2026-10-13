#include "vfo/vfo.h"
#include "hal/display.h"
#include "external/printf/printf.h"
#include "ui/menu.h"
#include "core/radio.h"
#include "core/device-config.h"
#include "core/channel-store.h"
#include "core/channel-model.h"
#include "core/rx-runtime.h"
#include "core/watch.h"
#include "core/tx-model.h"
#include "core/tx-runtime.h"
#include "hal/tx-device.h"
#include "core/settings.h"
#include "core/seek.h"
#include "core/capture.h"
#include "core/rds-receiver.h"
#include "hal/rx-device.h"
#include "hal/scheduler.h"
#include "ui/components/signal.h"
#include "ui/components/frequency.h"
#include "ui/components/vfo.h"
#include "ui/graphics.h"
#include "ui/statusline.h"
#include "apps/apps.h"
#include "ui/number-input.h"
#include "vfo/channel-manager.h"
#include <string.h>

static bool initialized;
static UI_VfoState cards[RADIO_VFO_CAPACITY];
static uint8_t selectedVfo, receivingVfo = UINT8_MAX, inputVfo;
static NumberInput number;
static bool inputTx;
static struct { uint32_t frequency_hz; char ps[9], text[65]; } labels[RADIO_VFO_CAPACITY];
static RxRuntime runtime;
static WatchRuntime watcher;
static uint32_t watchResumeAt;
static bool watchDelay;
static const char *watchStatus;
static bool pauseWatch(void);
static void watchUpdate(void);
static TxRuntime transmitter;
static bool pttPending;
static uint32_t pttCheckAt;
static void startTransmit(void);
static RdsReceiver rds;
static bool changed;
static bool inMenu;
static bool confirming;
static UserChannel channelCache[RADIO_VFO_CAPACITY];
static bool channelCacheValid[RADIO_VFO_CAPACITY];
static uint8_t channelCacheNumber[RADIO_VFO_CAPACITY];
static uint8_t menuIndex;
enum { MENU_RX_WATCH = 9, MENU_CHANNEL_NAME, MENU_CHANNEL_SAVE, MENU_ITEM_COUNT };
static VfoConfig user; // Current UI state, committed only on successful RX.
static RxPlan pending;
static const char *message;
static uint32_t messageAt;
static bool functionPending;
static uint32_t functionAt;
static void nextChannel(bool up);

static bool loadChannelForVfo(uint8_t id, UserChannel *channel) {
  if (id >= RADIO_VFO_CAPACITY || !channel) return false;
  const VfoConfig *config = &gDeviceConfig.radio.vfos[id];
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

static bool effectiveVfo(uint8_t id, VfoConfig *effective,
                         UserChannel *channel) {
  if (id >= RADIO_VFO_CAPACITY || !effective) return false;
  *effective = gDeviceConfig.radio.vfos[id];
  if (effective->source == RADIO_SOURCE_VFO) return true;
  UserChannel loaded;
  if (!loadChannelForVfo(id, &loaded)) return false;
  effective->active_band = RADIO_MODEL_BandAt(loaded.frequency_hz);
  if (effective->active_band == UINT8_MAX) return false;
  effective->current = (BandState){loaded.frequency_hz, loaded.rx};
  effective->tx = loaded.tx;
  if (channel) *channel = loaded;
  return true;
}

// Search never mutates user/EEPROM until one final station is accepted.
typedef enum { SEEK_STEP, SEEK_SETTLE, SEEK_VERIFY,
               SEEK_NATIVE_START, SEEK_NATIVE_POLL, SEEK_WRAP_SETTLE } SeekPhase;
static Seek seek;
static SeekPhase seekPhase;
static RxPlan seekOrigin;
static bool seekNative;
static CaptureRuntime capture;
static enum { FC_OFF, FC_DETECT, FC_PREVIEW } capturePhase;
static uint32_t capturePollAt;
static void cancelCapture(const char *status);
static void captureUpdate(void);
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
  pttPending = false;
  TX_RuntimeRelease(&transmitter, &txOperations);
}

void VFO_deinit(void) {
  number.active = false;
  if (capturePhase!=FC_OFF) cancelCapture(NULL);
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

static void channelSaved(uint8_t id, uint16_t number,
                         const UserChannel *channel) {
  if (id >= RADIO_VFO_CAPACITY || !channel) return;
  gDeviceConfig.radio.vfos[id].channel_number = (uint8_t)number;
  if (id == selectedVfo) user.channel_number = (uint8_t)number;
  for (uint8_t i = 0; i < RADIO_VFO_CAPACITY; ++i) {
    if (gDeviceConfig.radio.vfos[i].channel_number == number) {
      channelCache[i] = *channel;
      channelCacheNumber[i] = (uint8_t)number;
      channelCacheValid[i] = true;
    }
  }
  DEVICE_RequestSave();
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

static bool apply(const RxPlan *plan) {
  bool needsSsb = plan->parameters.mode == RX_LSB || plan->parameters.mode == RX_USB;
  bool wasSsb = runtime.active && (runtime.current.parameters.mode == RX_LSB ||
                                  runtime.current.parameters.mode == RX_USB);
  if (needsSsb && !wasSsb) {
    notify("WAIT...");
    VFO_render();
    DISPLAY_Blit();
  }
  RxTuneResult result = RX_RuntimeTune(&runtime, plan,
                                      applyReceiver, stopReceiver, NULL);
  if (result != RX_APPLIED) {
    notify(result == RX_RESTORED ? "Tune failed" : "RX error");
    return false;
  }
  if (user.source == RADIO_SOURCE_VFO &&
      !RADIO_VFO_Set(&user, plan->frequency_hz, plan->parameters)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    notify("State error");
    return false;
  }
  if (user.source == RADIO_SOURCE_VFO) {
    // TX edits are committed independently from the receive draft.
    user.tx = gDeviceConfig.radio.vfos[selectedVfo].tx;
    user.watch = gDeviceConfig.radio.vfos[selectedVfo].watch;
    gDeviceConfig.radio.vfos[selectedVfo] = user;
  }
  receivingVfo = selectedVfo;
  memset(&labels[selectedVfo], 0, sizeof(labels[selectedVfo]));
  labels[selectedVfo].frequency_hz = plan->frequency_hz;
  DEVICE_RequestSave();
  const RadioParameters *preset = &gDeviceConfig.radio.presets[user.active_band];
  changed = user.current.parameters.mode != preset->mode || user.current.parameters.step_hz != preset->step_hz ||
            user.current.parameters.bandwidth_hz != preset->bandwidth_hz || user.current.parameters.squelch != preset->squelch || user.current.parameters.rx_tone != preset->rx_tone;
  message = NULL;
  UI_VfoShowFrequency(&cards[selectedVfo], Now());
  return true;
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
  if (user.source == RADIO_SOURCE_CHANNEL) {
    notify("Seek is VFO only");
    return;
  }
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

// Runtime keeps the committed origin throughout detection and temporary preview.
static bool capturePlan(uint32_t hz, RxPlan *plan) {
  return RADIO_MODEL_Plan(hz, (RadioParameters){RX_NFM,12500,12000,4},
      RADIO_GetReceiverCapabilities(), plan)==RX_OK &&
      plan->device==RX_DEVICE_BK && plan->frequency_hz==hz;
}

static void cancelCapture(const char *status) {
  RX_DEVICE_CaptureStop();
  capturePhase=FC_OFF;
  if (!applyReceiver(NULL, &runtime.current)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    status="RX error";
  } else {
    runtime.active=true;
    receivingVfo=selectedVfo;
  }
  watchDelay=true; watchResumeAt=Now()+2000u;
  UI_VfoShowFrequency(&cards[selectedVfo], Now());
  message=NULL;
  if (status) notify(status);
  gRedrawScreen=true;
}

static void captureAttempt(void) {
  RADIO_StopReception();
  receivingVfo=UINT8_MAX;
  RDS_ReceiverReset(&rds, Now());
  RxDevice previous=ctx->radio_type==RADIO_BK4819 ? RX_DEVICE_BK :
    ctx->radio_type==RADIO_SI4732 ? RX_DEVICE_SI : RX_DEVICE_BROADCAST;
  RX_DEVICE_CaptureStart(capture.range, previous);
  capturePhase=FC_DETECT;
  capturePollAt=Now();
  message=NULL;
  gRedrawScreen=true;
}

static void startCapture(void) {
  if (user.source!=RADIO_SOURCE_VFO) { notify("FC: VFO only"); return; }
  if (!runtime.active || !pauseWatch()) return;
  CAPTURE_Start(&capture, CAPTURE_AUTO, Now());
  captureAttempt();
}

static void captureUpdate(void) {
  if (capturePhase!=FC_DETECT || Now()-capturePollAt<20u) return;
  capturePollAt=Now();
  uint32_t hz=0;
  CaptureSample sample=RX_DEVICE_CapturePoll(capture.range, &hz);
  RxPlan plan;
  if (sample==CAPTURE_SAMPLE && !capturePlan(hz, &plan)) sample=CAPTURE_INVALID;
  CaptureAction action=CAPTURE_Update(&capture, Now(), sample, hz);
  if (action==CAPTURE_TIMEOUT) cancelCapture("FC timeout");
  else if (action==CAPTURE_RESTART) captureAttempt();
  else if (action==CAPTURE_FOUND) {
    RX_DEVICE_CaptureStop();
    if (!applyReceiver(NULL, &plan)) { cancelCapture("Tune failed"); return; }
    capturePhase=FC_PREVIEW;
    receivingVfo=selectedVfo;
    UI_VfoShowFrequency(&cards[selectedVfo], Now());
    gRedrawScreen=true;
  }
}

static void acceptCapture(void) {
  RxPlan plan;
  RX_DEVICE_CaptureStop();
  capturePhase=FC_OFF;
  if (!capturePlan(capture.last_hz, &plan) || !apply(&plan)) {
    cancelCapture("Tune failed"); return;
  }
  // apply has queued a deferred save; include these changes in that one save.
  user.tx.enabled=false;
  user.channel_number=0;
  gDeviceConfig.radio.vfos[selectedVfo]=user;
  CHANNEL_MANAGER_LoadName(selectedVfo, 0);
  watchDelay=true; watchResumeAt=Now()+2000u;
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

static void tuneTo(uint32_t target) {
  if (inputVfo != selectedVfo) return;
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
  CHANNEL_MANAGER_Init(notify, channelSaved);
  seek.active = false;
  if (!RADIO_STATE_Valid(&gDeviceConfig.radio))
    RADIO_STATE_Default(&gDeviceConfig.radio);
  selectedVfo = gDeviceConfig.radio.selected_vfo;
  bool invalidSavedChannel = false;
  for (uint8_t id = 0; id < RADIO_VFO_CAPACITY; ++id) {
    channelCacheValid[id] = false;
    CHANNEL_MANAGER_LoadName(id, gDeviceConfig.radio.vfos[id].channel_number);
  }
  if (!effectiveVfo(selectedVfo, &user, NULL)) {
    gDeviceConfig.radio.vfos[selectedVfo].source = RADIO_SOURCE_VFO;
    user = gDeviceConfig.radio.vfos[selectedVfo];
    DEVICE_RequestSave();
    invalidSavedChannel = true;
  }
  receivingVfo = UINT8_MAX;
  request(user.current.frequency_hz, user.current.parameters);
  if (invalidSavedChannel) notify("Channel unavailable");
}

bool VFO_isReceiving(void) {
  return initialized && receivingVfo != UINT8_MAX && !WATCH_IsProbing(&watcher) && runtime.active && !seek.active && capturePhase!=FC_DETECT &&
         transmitter.mode == RADIO_RX;
}

static bool readRds(void *unused, RdsPacket *packet, bool clear_fifo) {
  (void)unused;
  return RX_DEVICE_ReadRDS(packet, clear_fifo);
}

void VFO_TxSafetyUpdate(void) {
  // Check the physical button, not just the queued keyboard release event.
  if ((pttPending || transmitter.mode == RADIO_TX) && !TX_DEVICE_PttPressed()) {
    VFO_PttRelease();
    gRedrawScreen = true;
    return;
  }
  if (transmitter.mode == RADIO_TX && !TX_DEVICE_VoltageValid()) {
    VFO_PttRelease();
    transmitter.wait_release = true;
    notify("TX battery voltage");
    return;
  }

  if (pttPending && (int32_t)(Now()-pttCheckAt) >= 0) {
    pttPending = false;
    bool busy = false;
    if (!runtime.active || receivingVfo != selectedVfo ||
        !RX_DEVICE_WatchSignal(&runtime.current, &busy)) {
      transmitter.wait_release = true;
      notify("Busy check failed");
    } else if (busy) {
      transmitter.wait_release = true;
      notify("Channel busy");
    } else {
      startTransmit();
    }
  }
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
  uint16_t overflow = card->band_overflow;
  if (overflow)
    duration=2u*UI_VFO_SCROLL_PAUSE_MS+overflow*UI_VFO_SCROLL_PIXEL_MS;
  if (message && Now()-messageAt >= duration) {
    message = NULL;
    gRedrawScreen = true;
  }
  if (transmitter.mode != RADIO_RX)
    return;
  if (capturePhase!=FC_OFF) { captureUpdate(); return; }
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
  if (capturePhase!=FC_OFF || number.active || CHANNEL_MANAGER_Active() || inMenu || confirming || seek.active || functionPending ||
      transmitter.mode!=RADIO_RX || transmitter.wait_release || pttPending) return;
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
      const VfoConfig *stored=&gDeviceConfig.radio.vfos[id];
      VfoConfig effective;
      if (!stored->watch) continue;
      if (!effectiveVfo(id, &effective, NULL)) {
        watchStatus="DW: unavailable"; continue;
      }
      RxPlan plan;
      RxResult result=RADIO_MODEL_Plan(effective.current.frequency_hz,effective.current.parameters,
          RADIO_GetReceiverCapabilities(),&plan);
      if (result!=RX_OK || plan.frequency_hz!=effective.current.frequency_hz) {
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

static bool tuneDirect(const RxPlan *plan) {
  RxTuneResult result = RX_RuntimeTune(&runtime, plan,
                                       applyReceiver, stopReceiver, NULL);
  if (result != RX_APPLIED) {
    notify(result == RX_RESTORED ? "Tune failed" : "RX error");
    return false;
  }
  return true;
}

// Reapply only the active receive plan. Global settings never rewrite VFOs
// or presets, and the receive runtime remains responsible for recovery.
bool VFO_ApplyRxSettings(void) {
  if (capturePhase!=FC_OFF) cancelCapture(NULL);
  if (!initialized) return true;
  RxPlan plan;
  if (runtime.active) plan = runtime.current;
  else if (RADIO_MODEL_Plan(user.current.frequency_hz, user.current.parameters,
      RADIO_GetReceiverCapabilities(), &plan) != RX_OK) return false;
  if (!tuneDirect(&plan)) return false;
  receivingVfo = selectedVfo;
  return true;
}

static void tuneChannel(uint16_t number, bool enterMode) {
  UserChannel channel;
  if (!CHANNEL_STORE_Load(number, &channel)) {
    notify("Channel unavailable");
    return;
  }
  RxPlan plan;
  RxResult result = RADIO_MODEL_Plan(channel.frequency_hz, channel.rx,
      RADIO_GetReceiverCapabilities(), &plan);
  if (result != RX_OK || plan.frequency_hz != channel.frequency_hz) {
    notify(result == RX_PATCH_MISSING ? "SSB patch missing" : "RX unavailable");
    return;
  }
  if (!tuneDirect(&plan)) return;
  channelCache[selectedVfo] = channel;
  channelCacheValid[selectedVfo] = true;
  channelCacheNumber[selectedVfo] = (uint8_t)number;
  VfoConfig *stored = &gDeviceConfig.radio.vfos[selectedVfo];
  bool selectionChanged = stored->channel_number != number;
  stored->channel_number = (uint8_t)number;
  if (enterMode) {
    stored->source = RADIO_SOURCE_CHANNEL;
  }
  user = *stored;
  user.active_band = RADIO_MODEL_BandAt(channel.frequency_hz);
  user.current = (BandState){channel.frequency_hz, channel.rx};
  user.tx = channel.tx;
  receivingVfo = selectedVfo;
  memset(&labels[selectedVfo], 0, sizeof(labels[selectedVfo]));
  labels[selectedVfo].frequency_hz = channel.frequency_hz;
  changed = false;
  message = NULL;
  UI_VfoShowFrequency(&cards[selectedVfo], Now());
  if (enterMode || selectionChanged) DEVICE_RequestSave();
  CHANNEL_MANAGER_LoadName(selectedVfo, number);
  gRedrawScreen = true;
}

static void nextChannel(bool up) {
  uint16_t number = gDeviceConfig.radio.vfos[selectedVfo].channel_number;
  uint16_t target;
  if (!number || !CHANNEL_STORE_Next(number, up, &target)) {
    notify("No saved channels");
    return;
  }
  tuneChannel(target, false);
}

static void switchSource(void) {
  VfoConfig *stored = &gDeviceConfig.radio.vfos[selectedVfo];
  if (stored->source == RADIO_SOURCE_CHANNEL) {
    RxPlan plan;
    RxResult result = RADIO_MODEL_Plan(stored->current.frequency_hz,
        stored->current.parameters, RADIO_GetReceiverCapabilities(), &plan);
    if (result != RX_OK || plan.frequency_hz != stored->current.frequency_hz) {
      notify(result == RX_PATCH_MISSING ? "SSB patch missing" : "RX unavailable");
      return;
    }
    if (!tuneDirect(&plan)) return;
    stored->source = RADIO_SOURCE_VFO;
    user = *stored;
    receivingVfo = selectedVfo;
    DEVICE_RequestSave();
    message = NULL;
    UI_VfoShowFrequency(&cards[selectedVfo], Now());
    gRedrawScreen = true;
    return;
  }
  uint16_t number = stored->channel_number;
  if (!CHANNEL_STORE_IsOccupied(number)) {
    number = 0;
    for (uint16_t candidate = 1; candidate <= USER_CHANNEL_COUNT; ++candidate)
      if (CHANNEL_STORE_IsOccupied(candidate)) { number = candidate; break; }
  }
  if (!number) {
    notify(CHANNEL_STORE_Available() ? "No saved channels" : "Channel store unavailable");
    return;
  }
  tuneChannel(number, true);
}

static void switchVfo(void) {
  uint8_t target = RADIO_STATE_NextVfo(&gDeviceConfig.radio);
  VfoConfig next;
  if (!effectiveVfo(target, &next, NULL)) {
    notify("Channel unavailable");
    return;
  }
  RxPlan plan;
  RxResult planned = RADIO_MODEL_Plan(next.current.frequency_hz,
      next.current.parameters, RADIO_GetReceiverCapabilities(), &plan);
  if (planned != RX_OK || plan.frequency_hz != next.current.frequency_hz) {
    notify(planned == RX_PATCH_MISSING ? "SSB patch missing" : "RX unavailable");
    return;
  }
  if (!tuneDirect(&plan)) return;
  selectedVfo = target;
  receivingVfo = target;
  user = next;
  CHANNEL_MANAGER_LoadName(selectedVfo, user.channel_number);
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

static bool setTxFrequency(uint32_t value) {
  if (inputVfo != selectedVfo || !TX_MODEL_FrequencyValid(value)) return false;
  gDeviceConfig.radio.vfos[selectedVfo].tx.frequency_hz = value;
  user.tx.frequency_hz = value;
  DEVICE_RequestSave();
  notify("TX freq saved");
  return true;
}

static bool changeTxPower(bool up) {
  if (user.source == RADIO_SOURCE_CHANNEL) {
    notify("Channel is read only");
    return false;
  }
  TxConfig *tx = &gDeviceConfig.radio.vfos[selectedVfo].tx;
  tx->power = (tx->power + (up ? 1u : 2u)) % 3u;
  user.tx.power = tx->power;
  DEVICE_RequestSave();
  return true;
}

static void changeValue(bool up) {
  if (menuIndex == MENU_CHANNEL_NAME) {
    CHANNEL_MANAGER_StartName(selectedVfo,
        gDeviceConfig.radio.vfos[selectedVfo].channel_number);
    return;
  }
  if (menuIndex == MENU_CHANNEL_SAVE) {
    UserChannel channel = {
      .frequency_hz = user.current.frequency_hz,
      .rx = user.current.parameters,
      .tx = user.tx,
    };
    CHANNEL_MANAGER_StartSave(selectedVfo,
        gDeviceConfig.radio.vfos[selectedVfo].source == RADIO_SOURCE_CHANNEL
            ? gDeviceConfig.radio.vfos[selectedVfo].channel_number : 0,
        &channel);
    return;
  }
  if (gDeviceConfig.radio.vfos[selectedVfo].source == RADIO_SOURCE_CHANNEL &&
      menuIndex != MENU_RX_WATCH) {
    notify("Channel is read only");
    return;
  }
  if (menuIndex==MENU_RX_WATCH) {
    bool enabled=!gDeviceConfig.radio.vfos[selectedVfo].watch;
    if (!RADIO_STATE_SetWatch(&gDeviceConfig.radio,selectedVfo,enabled))
      notify("Watch limit: 2");
    else DEVICE_RequestSave();
    return;
  }
  if (menuIndex == 0) {
    nextMode();
    return;
  }
  if (menuIndex == 7) {
    if (!up) {
      gDeviceConfig.radio.vfos[selectedVfo].tx.frequency_hz = 0;
      user.tx.frequency_hz = 0;
      DEVICE_RequestSave();
    } else {
      inputVfo = selectedVfo;
      inputTx = true;
      message = NULL;
      NUMBER_Begin(&number, 0, 1300000000u, 1000000u,
                   user.tx.frequency_hz ? user.tx.frequency_hz : user.current.frequency_hz, true);
    }
    return;
  }
  if (menuIndex == 8) {
    gDeviceConfig.radio.vfos[selectedVfo].tx.tone = TONE_Next(gDeviceConfig.radio.vfos[selectedVfo].tx.tone, up);
    user.tx.tone = gDeviceConfig.radio.vfos[selectedVfo].tx.tone;
    DEVICE_RequestSave();
    return;
  }
  if (menuIndex == 4 && user.current.parameters.mode != RX_NFM) {
    notify("Tone: NFM only");
    return;
  }
  if (menuIndex == 5) {
    gDeviceConfig.radio.vfos[selectedVfo].tx.enabled = !gDeviceConfig.radio.vfos[selectedVfo].tx.enabled;
    user.tx.enabled = gDeviceConfig.radio.vfos[selectedVfo].tx.enabled;
    DEVICE_RequestSave();
    return;
  }
  if (menuIndex == 6) {
    changeTxPower(up);
    return;
  }
  RadioParameters candidate = user.current.parameters;
  if (RADIO_MODEL_Adjust(user.current.frequency_hz, &candidate,
                         RADIO_GetReceiverCapabilities(), (RadioParameterField)menuIndex, up))
    request(user.current.frequency_hz, candidate);
  else if (menuIndex == 2 && candidate.mode == RX_WFM)
    notify("Fixed bandwidth");
}

static void startTransmit(void) {
  if (inMenu || confirming || !runtime.active || !gDeviceConfigReady ||
      (gDeviceConfig.preferences.keylock && gDeviceConfig.preferences.ptt_lock) || transmitter.mode != RADIO_RX) {
    notify("TX busy/locked");
    return;
  }
  TxCapabilities capability = TX_DEVICE_Capabilities(user.tx.frequency_hz ? user.tx.frequency_hz : runtime.current.frequency_hz);
  TxPolicy policy = TX_DEVICE_Policy();
  TxPlan planned;
  TxResult result = TX_MODEL_Plan(&runtime.current, &user.tx,
                                 &capability, &policy, &planned);
  // The board policy and target-frequency calibration authorize TX independently of RX.
  if (result == TX_OK && !TX_DEVICE_VoltageValid()) {
    notify("TX battery voltage");
    return;
  }
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
  else if (result == TX_UNCALIBRATED)
    notify("TX calibration missing");
  else if (result == TX_OUT_OF_BAND)
    notify("TX out of band");
  else if (result != TX_OK)
    notify("TX not allowed");
}

bool VFO_key(KEY_Code_t key, Key_State_t state) {
  expireShortcut();
  if (capturePhase!=FC_OFF) {
    if (key==KEY_PTT && state==KEY_RELEASED) VFO_PttRelease();
    if (state==KEY_RELEASED) {
      if (key==KEY_EXIT || (key==KEY_MENU && capturePhase==FC_DETECT))
        cancelCapture(NULL);
      else if (key==KEY_MENU) acceptCapture();
      else if (key==KEY_SIDE1 || key==KEY_SIDE2) {
        RX_DEVICE_CaptureStop();
        CAPTURE_Start(&capture, (capture.scope+1u)%3u, Now());
        captureAttempt();
      }
    }
    return true;
  }
  if (number.active) {
    uint32_t value;
    if (inputVfo != selectedVfo) { number.active = false; return true; }
    NumberInputResult result = NUMBER_Key(&number, key, state, &value);
    if (result == NUMBER_ACCEPTED) {
      if (inputTx) {
        if (!setTxFrequency(value)) { number.active = true; number.invalid = true; }
      } else tuneTo(value);
    }
    if (result != NUMBER_EDITING && !number.active) {
      watchDelay = true; watchResumeAt = Now()+2000u;
      UI_VfoShowFrequency(&cards[selectedVfo], Now());
    }
    gRedrawScreen = true;
    return true;
  }
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
    if (state != KEY_PRESSED || pttPending || transmitter.wait_release ||
        transmitter.mode != RADIO_RX)
      return true;
    if (!pauseWatch()) return true;
    if (CHANNEL_MANAGER_Active() || inMenu || confirming || !runtime.active || !gDeviceConfigReady ||
        (gDeviceConfig.preferences.keylock && gDeviceConfig.preferences.ptt_lock)) {
      notify("TX busy/locked");
      return true;
    }
    if (gDeviceConfig.preferences.tx_busy_lockout) {
      // Restore/tune first, then sample the selected receiver after settling.
      // Keep this nonblocking so PTT release can cancel before PA preparation.
      pttPending = true;
      pttCheckAt = Now()+(runtime.current.device == RX_DEVICE_BK ? 180u : 350u);
    } else {
      startTransmit();
    }
    return true;
  }
  if (pttPending || transmitter.mode != RADIO_RX || transmitter.wait_release)
    return true;
  if ((state==KEY_RELEASED || state==KEY_LONG_PRESSED || state==KEY_LONG_PRESSED_CONT) &&
      !pauseWatch()) return true;
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
      else if (key == KEY_3)
        switchSource();
      else if (key == KEY_4)
        startCapture();
      else if (key == KEY_6) {
        static const char *const messages[] = {"TX Power: Low", "TX Power: Medium", "TX Power: High"};
        if (changeTxPower(true)) notify(messages[user.tx.power]);
      }
      else if ((key == KEY_1 || key == KEY_0) &&
               user.source == RADIO_SOURCE_CHANNEL)
        notify("Channel mode");
      else if (key == KEY_STAR)
        startSeek();
      else if (key != KEY_F && key != KEY_EXIT)
        notify("Not supported");
    }
    return true;
  }
  if ((key == KEY_UP || key == KEY_DOWN) &&
      (state == KEY_RELEASED || state == KEY_LONG_PRESSED_CONT)) {
    if (user.source == RADIO_SOURCE_CHANNEL) {
      nextChannel(key == KEY_UP);
      return true;
    }
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
  } else if (key == KEY_SIDE1) {
    if (user.source == RADIO_SOURCE_CHANNEL) notify("Channel mode");
    else nextBand();
  } else if (key == KEY_SIDE2) {
    if (user.source == RADIO_SOURCE_CHANNEL) notify("Channel mode");
    else nextMode();
  }
  else if (key == KEY_MENU)
    inMenu = true;
  else if (key == KEY_EXIT)
    gMonitorMode = false;
  else if (key <= KEY_9) {
    if (user.source == RADIO_SOURCE_CHANNEL) {
      notify("Switch to VFO first");
      return true;
    }
    inputVfo = selectedVfo;
    inputTx = false;
    message = NULL;
    NUMBER_Begin(&number, RADIO_MIN_HZ, RADIO_MAX_HZ, 1000000u, 0, false);
    uint32_t unused;
    NUMBER_Key(&number, key, state, &unused);
  } else {
    return false;
  }
  return true;
}

static const char *menuValue(uint8_t item, char out[24]) {
  const RadioParameters *rx = &user.current.parameters;
  char frequency[16];
  uint32_t hz;
  switch (item) {
  case 0: return rxModeNames[rx->mode];
  case 1: hz = rx->step_hz; break;
  case 2:
    if (rx->mode == RX_WFM) return "Fixed";
    hz = rx->bandwidth_hz; break;
  case 3: snprintf(out,24,"%u",rx->squelch); return out;
  case 4:
    if (rx->mode != RX_NFM) return "NFM only";
    TONE_Format(rx->rx_tone,out); return out;
  case 5: return user.tx.enabled ? "On" : "Off";
  case 6: {
    static const char *const powers[] = {"Low", "Medium", "High"};
    return powers[user.tx.power <= TX_POWER_HIGH ? user.tx.power : 0];
  }
  case 7:
    if (number.active && inputTx) { NUMBER_Text(&number,out); return out; }
    if (!user.tx.frequency_hz) return "Same";
    hz = user.tx.frequency_hz; break;
  case 8: TONE_Format(user.tx.tone,out); return out;
  case MENU_RX_WATCH: return gDeviceConfig.radio.vfos[selectedVfo].watch ? "On" : "Off";
  case MENU_CHANNEL_NAME:
    return CHANNEL_MANAGER_Name(selectedVfo)[0] ? CHANNEL_MANAGER_Name(selectedVfo) : "(none)";
  case MENU_CHANNEL_SAVE: return "Choose CH";
  default: return "";
  }
  const char *unit = UI_FormatFrequency(hz,frequency);
  snprintf(out,24,"%s %s",frequency,unit);
  return out;
}

// Owns transient strings until the card has been drawn.
typedef struct {
  UserChannel channel;
  char header[16];
} VfoViewStorage;

// Resolve application state here; the UI component only consumes display data.
static void buildCardView(uint8_t id, const char *inputText,
                          UI_VfoView *view, VfoViewStorage *storage) {
  bool selected = id == selectedVfo;
  const VfoConfig *stored = &gDeviceConfig.radio.vfos[id];
  VfoConfig effective;
  UserChannel *channel = &storage->channel;
  *channel = (UserChannel){0};
  bool effectiveOk;
  if (selected) {
    effective = user;
    effectiveOk = user.source != RADIO_SOURCE_CHANNEL ||
                  loadChannelForVfo(id, channel);
  } else {
    effectiveOk = effectiveVfo(id, &effective, channel);
  }
  if (!effectiveOk) effective = *stored;
  const VfoConfig *config = &effective;
  const TxConfig *tx = &config->tx;
  const BandState *current = &config->current;
  const RadioParameters *p = &current->parameters;
  const RadioParameters *preset = &gDeviceConfig.radio.presets[config->active_band];
  bool channelMode = stored->source == RADIO_SOURCE_CHANNEL;
  bool modified = !channelMode && (p->mode != preset->mode || p->step_hz != preset->step_hz ||
    p->bandwidth_hz != preset->bandwidth_hz || p->squelch != preset->squelch ||
    p->rx_tone != preset->rx_tone);
  bool receiving = receivingVfo==id && VFO_isReceiving();
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
  *view = (UI_VfoView){
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
    .activity = selected && transmitter.mode==RADIO_TX ? UI_VFO_TRANSMITTING :
      selected && transmitter.mode==RADIO_FAULT ? UI_VFO_FAULT :
      selected && seek.active ? UI_VFO_SEEKING :
      receivingVfo==id && WATCH_IsProbing(&watcher) ? UI_VFO_WATCHING :
      receiving && vfo->is_open ? UI_VFO_RECEIVING :
      (channelMode && !effectiveOk) || (selected && !runtime.active) ? UI_VFO_UNAVAILABLE : UI_VFO_IDLE,
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
  else if (inMenu || confirming) STATUSLINE_SetText("VFO %c", 'A'+selectedVfo);
  else if (user.source == RADIO_SOURCE_CHANNEL) STATUSLINE_SetText("MR %c", 'A'+selectedVfo);
  else if (gDeviceConfig.radio.watch_enabled)
    STATUSLINE_SetText("DW %c", receivingVfo<RADIO_VFO_CAPACITY ? 'A'+receivingVfo : '-');
  else STATUSLINE_SetText("RADIO");
}

static void renderConfirmation(void) {
  char frequencyText[16];
  const char *unit = UI_FormatFrequency(pending.frequency_hz, frequencyText);
  PrintMediumEx(2, 18, POS_L, C_FILL, "Apply %s?", rxModeNames[pending.parameters.mode]);
  PrintMediumEx(2, 30, POS_L, C_FILL, "%s %s", frequencyText, unit);
  PrintSmall(2, 40, "BW %u Hz STEP %u Hz", pending.parameters.bandwidth_hz, pending.parameters.step_hz);
  PrintSmall(2, 57, "MENU: apply  EXIT: cancel");
}

static void renderMenu(void) {
  static const char *const names[] = {
    "RX Mode", "RX Step", "RX Bandwidth", "RX Squelch", "RX Tone", "TX Enable", "TX Power", "TX Frequency", "TX Tone", "RX Watch", "Name", "Save"
  };
  _Static_assert(sizeof(names)/sizeof(names[0]) == MENU_ITEM_COUNT,
                 "Every VFO menu item needs a label");
  uint8_t first = menuIndex < 5 ? 0 : menuIndex-4;
  for (uint8_t i = first; i < MENU_ITEM_COUNT && i < first+5; ++i) {
    uint8_t y = 17+(i-first)*11;
    if (i == 7 && number.active) PrintSmallEx(2, y, POS_L, C_FILL, "TX Freq");
    else PrintMediumEx(2, y, POS_L, C_FILL, "%s", names[i]);
    char value[24];
    PrintSmallEx(124, y, POS_R, C_FILL, "%s", menuValue(i, value));
    if (i == menuIndex)
      FillRect(0, y-7, 128, 10, C_INVERT);
  }
}

void VFO_render(void) {
  renderTitle();
  if (CHANNEL_MANAGER_Render()) return;
  if (confirming && !message) {
    renderConfirmation();
    return;
  }
  if (inMenu && !message) {
    renderMenu();
    return;
  }

  char inputText[18];
  NUMBER_Text(&number, inputText);
  uint8_t first = RADIO_STATE_VisibleFirst(&gDeviceConfig.radio);
  uint32_t now = Now();
  for (uint8_t row = 0; row < RADIO_VFO_VISIBLE; ++row) {
    uint8_t id = first + row;
    VfoViewStorage storage;
    UI_VfoView view;
    buildCardView(id, inputText, &view, &storage);
    UI_VfoRender(8 + row * UI_VFO_HEIGHT, &view, &cards[id], now);
  }
}

#include "apps/vfo1.h"
#include "hal/display.h"
#include "external/printf/printf.h"
#include "ui/menu.h"
#include "core/radio.h"
#include "core/device-config.h"
#include "core/rx-runtime.h"
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
static UI_VfoState card;
static RxRuntime runtime;
static TxRuntime transmitter;
static RdsReceiver rds;
static bool changed;
static bool inMenu;
static bool confirming;
static uint8_t menuIndex;
enum { MENU_CHANNEL_NAME = 9, MENU_CHANNEL_SAVE, MENU_ITEM_COUNT };
static RadioUserState user; // Current UI state, committed only on successful RX.
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
bool VFO1_isShortcutPending(void) { return functionPending; }

void VFO1_cancelShortcut(void) {
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
  RDS_ReceiverReset(&rds, Now());
  return true;
}
static const TxOperations txOperations = {
  .mute_rx = txMute, .prepare = txPrepare, .enable = txEnable,
  .disable = txDisable, .restore_rx = txRestore
};

void VFO1_PttRelease(void) {
  TX_RuntimeRelease(&transmitter, &txOperations);
}

void VFO1_deinit(void) {
  VFO1_PttRelease();
  VFO1_cancelShortcut();
  if (seek.active)
    finishSeek(false, 0, NULL);
}

static void expireShortcut(void) {
  if (functionPending && Now()-functionAt >= 3000)
    VFO1_cancelShortcut();
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
  for (uint8_t i = 0; i < radioBandCount; ++i)
    user.presets[i] = gDeviceConfig.radio.presets[i];
  if (!RADIO_STATE_Set(&user, plan->frequency_hz, plan->parameters)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    notify("State error");
    return;
  }
  gDeviceConfig.radio = user;
  DEVICE_RequestSave();
  const RadioParameters *preset = &gDeviceConfig.radio.presets[user.active_band];
  changed = user.current.parameters.mode != preset->mode || user.current.parameters.step_hz != preset->step_hz ||
            user.current.parameters.bandwidth_hz != preset->bandwidth_hz || user.current.parameters.squelch != preset->squelch || user.current.parameters.rx_tone != preset->rx_tone;
  message = NULL;
  UI_VfoShowFrequency(&card, Now());
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

void VFO1_init(void) {
  VFO1_cancelShortcut();
  if (initialized)
    return;
  initialized = true;
  transmitter = (TxRuntime){0};
  seek.active = false;
  user = gDeviceConfig.radio;
  if (!RADIO_STATE_Valid(&user))
    RADIO_STATE_Default(&user);
  request(user.current.frequency_hz, user.current.parameters);
}

bool VFO1_isReceiving(void) {
  return initialized && runtime.active && !seek.active &&
         transmitter.mode == RADIO_RX;
}

static bool readRds(void *unused, RdsPacket *packet, bool clear_fifo) {
  (void)unused;
  return RX_DEVICE_ReadRDS(packet, clear_fifo);
}

void VFO1_TxSafetyUpdate(void) {
  // TOT is a system-level safety deadline, not a screen refresh feature.
  if (TX_RuntimeUpdate(&transmitter, Now(), &txOperations))
    notify(transmitter.mode == RADIO_FAULT ? "RX restore error" : "TX timeout");
}

void VFO1_update(void) {
  expireShortcut();
  VFO1_TxSafetyUpdate(); // Also permit deterministic host/UI unit tests.
  if (transmitter.mode != RADIO_RX)
    return;
  seekUpdate();
  bool rdsEnabled = !seek.active && runtime.active && runtime.current.device == RX_DEVICE_SI &&
                    runtime.current.parameters.mode == RX_WFM;
  if (RDS_ReceiverUpdate(&rds, Now(), rdsEnabled, readRds, NULL))
    gRedrawScreen = true;
  if (!inMenu && !confirming && UI_VfoUpdate(&card, Now()))
    gRedrawScreen = true;
  if (message && Now()-messageAt >= 2500) {
    message = NULL;
    gRedrawScreen = true;
  }
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
  if (value > UINT32_MAX/10u || !TX_MODEL_FrequencyValid(value*10u)) {
    inMenu = false;
    notify("Invalid TX freq");
    return;
  }
  gDeviceConfig.tx.frequency_hz = value*10u;
  DEVICE_RequestSave();
  notify("TX freq saved");
}

static void changeValue(bool up) {
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
      gDeviceConfig.tx.frequency_hz = 0;
      DEVICE_RequestSave();
    } else {
      gFInputValue1 = (gDeviceConfig.tx.frequency_hz ? gDeviceConfig.tx.frequency_hz : user.current.frequency_hz)/10u;
      gFInputValue2 = 0;
      gFInputCallback = setTxFrequency;
      FINPUT_setup(0, 130000000u, UNIT_MHZ, false);
      APPS_run(APP_FINPUT);
      STATUSLINE_SetText("TX Frequency");
    }
    return;
  }
  if (menuIndex == 8) {
    gDeviceConfig.tx.tone = TONE_Next(gDeviceConfig.tx.tone, up);
    DEVICE_RequestSave();
    return;
  }
  if (menuIndex == 4 && user.current.parameters.mode != RX_NFM) {
    notify("Tone: NFM only");
    return;
  }
  if (menuIndex == 5) {
    gDeviceConfig.tx.enabled = !gDeviceConfig.tx.enabled;
    DEVICE_RequestSave();
    return;
  }
  if (menuIndex == 6) {
    gDeviceConfig.tx.power = (gDeviceConfig.tx.power +
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

bool VFO1_key(KEY_Code_t key, Key_State_t state) {
  expireShortcut();
  if (seek.active) {
    if (state == KEY_RELEASED && (key == KEY_EXIT || key == KEY_MENU))
      finishSeek(false, 0, "Seek cancelled");
    return true; // Search temporarily owns all controls except cancel.
  }
  if (key == KEY_PTT) {
    VFO1_cancelShortcut();
    if (state == KEY_RELEASED) {
      VFO1_PttRelease();
      return true;
    }
    if (state != KEY_PRESSED || transmitter.wait_release ||
        transmitter.mode != RADIO_RX)
      return true;
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
    TxResult result = TX_MODEL_Plan(&runtime.current, &gDeviceConfig.tx,
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
      VFO1_cancelShortcut();
      return false; // Keep the global settings shortcut available.
    }
    if (state == KEY_RELEASED) {
      VFO1_cancelShortcut();
      if (key == KEY_1)
        nextBand();
      else if (key == KEY_0)
        nextMode();
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
    gFInputCallback = tuneTo;
    FINPUT_setup(RADIO_MIN_HZ/10, RADIO_MAX_HZ/10, UNIT_MHZ, false);
    APPS_run(APP_FINPUT);
    APPS_key(key, state);
  } else {
    return false;
  }
  return true;
}

void VFO1_render(void) {
  char frequencyText[16];
  STATUSLINE_SetText("RADIO");
  if (confirming) {
    const char *unit = UI_FormatFrequency(pending.frequency_hz, frequencyText);
    PrintMediumEx(2, 18, POS_L, C_FILL, "Apply %s?", rxModeNames[pending.parameters.mode]);
    PrintMediumEx(2, 30, POS_L, C_FILL, "%s %s", frequencyText, unit);
    PrintSmall(2, 40, "BW %u Hz STEP %u Hz", pending.parameters.bandwidth_hz, pending.parameters.step_hz);
    PrintSmall(2, 57, "MENU: apply  EXIT: cancel");
    return;
  }
  if (inMenu) {
    static const char *const names[] = {
      "RX Mode", "RX Step", "RX Bandwidth", "RX Squelch", "RX Tone", "TX Enable", "TX Power", "TX Frequency", "TX Tone", "Name", "Save"
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
          TONE_Format(i == 4 ? user.current.parameters.rx_tone : gDeviceConfig.tx.tone, toneText);
        PrintSmallEx(124, y, POS_R, C_FILL, "%s", toneText);
      }
      else if (i == 5)
        PrintSmallEx(124, y, POS_R, C_FILL, "%s", gDeviceConfig.tx.enabled ? "On" : "Off");
      else if (i == 7) {
        if (!gDeviceConfig.tx.frequency_hz) PrintSmallEx(124,y,POS_R,C_FILL,"Same");
        else {
          const char *unit = UI_FormatFrequency(gDeviceConfig.tx.frequency_hz,frequencyText);
          PrintSmallEx(124,y,POS_R,C_FILL,"%s %s",frequencyText,unit);
        }
      } else if (i == 6) {
        static const char *const powers[] = {"Low", "Medium", "High"};
        PrintSmallEx(124, y, POS_R, C_FILL, "%s",
                     powers[gDeviceConfig.tx.power <= TX_POWER_HIGH ?
                            gDeviceConfig.tx.power : 0]);
      } else {
        PrintSmallEx(124,y,POS_R,C_FILL,"Not implemented");
      }
      if (i == menuIndex)
        FillRect(0, y-7, 128, 10, C_INVERT);
    }
    return;
  }
  const RadioParameters *preset = &gDeviceConfig.radio.presets[user.active_band];
  changed = user.current.parameters.mode != preset->mode || user.current.parameters.step_hz != preset->step_hz ||
            user.current.parameters.bandwidth_hz != preset->bandwidth_hz || user.current.parameters.squelch != preset->squelch || user.current.parameters.rx_tone != preset->rx_tone;
  uint32_t frequency = seek.active ? seek.cursor_hz
                                   : user.current.frequency_hz;
  UI_VfoView view = {
    .id = 'A', .selected = true,
    .frequency_hz = frequency, .mode = user.current.parameters.mode,
    .band_name = radioBands[user.active_band].name, .changed = changed,
    .step_hz = user.current.parameters.step_hz,
    .name = rds.decoder.ps, .rds = rds.decoder.text,
    .signal_valid = runtime.active && !seek.active && transmitter.mode == RADIO_RX,
    .signal_level = runtime.active && !seek.active ? UI_SMeterLevel() : 0,
    .activity = transmitter.mode == RADIO_TX ? UI_VFO_TRANSMITTING :
                transmitter.mode == RADIO_FAULT ? UI_VFO_FAULT :
                seek.active ? UI_VFO_SEEKING : !runtime.active ? UI_VFO_UNAVAILABLE :
                vfo->is_open ? UI_VFO_RECEIVING : UI_VFO_IDLE,
    .power_label = gDeviceConfig.tx.power == TX_POWER_HIGH ? "HIGH" :
                   gDeviceConfig.tx.power == TX_POWER_MEDIUM ? "MID" : "LOW",
  };
  UI_VfoRender(8, &view, &card, Now());
  if (message) PrintSmall(0, 43, "%s", message);
}

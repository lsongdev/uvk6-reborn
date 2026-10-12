#include "apps/vfo1.h"
#include "hal/display.h"
#include "external/printf/printf.h"
#include "ui/menu.h"
#include "core/radio.h"
#include "core/device-config.h"
#include "core/rx-runtime.h"
#include "hal/scheduler.h"
#include "ui/components/signal.h"
#include "ui/components/frequency.h"
#include "ui/components/vfo.h"
#include "ui/graphics.h"
#include "ui/statusline.h"
#include "apps/apps.h"
#include "apps/finput.h"
#include <string.h>

static const char *shortBandName(const char *name) {
  if (!strcmp(name, "FM Radio")) return "FM";
  if (!strcmp(name, "Airband")) return "AIR";
  if (!strcmp(name, "70cm")) return "70c";
  return name;
}

static bool initialized;
static UI_VfoState card;
static RxRuntime runtime;
static bool changed;
static bool inMenu;
static bool confirming;
static uint8_t menuIndex;
static RadioUserState user; // Current UI state, committed only on successful RX.
static RxPlan pending;
static const char *message;
static uint32_t messageAt;

static void notify(const char *text) {
  message = text;
  messageAt = Now();
  gRedrawScreen = true;
}

static bool applyReceiver(void *unused, const RxPlan *plan) {
  (void)unused;
  return RADIO_ApplyRxPlan(ctx, plan);
}

static void stopReceiver(void *unused) {
  (void)unused;
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
            user.current.parameters.bandwidth_hz != preset->bandwidth_hz || user.current.parameters.squelch != preset->squelch;
  message = NULL;
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
  if (initialized)
    return;
  initialized = true;
  user = gDeviceConfig.radio;
  if (!RADIO_STATE_Valid(&user))
    RADIO_STATE_Default(&user);
  request(user.current.frequency_hz, user.current.parameters);
}

bool VFO1_isReceiving(void) { return initialized && runtime.active; }

void VFO1_update(void) {
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

static void changeValue(bool up) {
  if (menuIndex == 0) {
    nextMode();
    return;
  }
  if (menuIndex == 4) {
    gMonitorMode = !gMonitorMode;
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
  if (key == KEY_PTT) {
    if (state == KEY_RELEASED)
      RADIO_ToggleTX(ctx, false);
    else if (!inMenu && !confirming && runtime.active)
      RADIO_ToggleTX(ctx, true);
    return true;
  }
  if (ctx->tx_state.is_active)
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
        menuIndex = key == KEY_DOWN ? (menuIndex+1)%5 : (menuIndex+4)%5;
      else if (key == KEY_STAR || key == KEY_F || key == KEY_MENU)
        changeValue(key != KEY_F);
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
  if (key == KEY_SIDE1)
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
    static const char *const names[] = {"Mode", "Step", "BW", "SQ", "Monitor"};
    for (uint8_t i = 0; i < 5; ++i) {
      uint8_t y = 17+i*10;
      PrintMediumEx(2, y, POS_L, C_FILL, "%s", names[i]);
      if (i == 0)
        PrintSmallEx(124, y, POS_R, C_FILL, "%s", rxModeNames[user.current.parameters.mode]);
      else if (i == 1)
        PrintSmallEx(124, y, POS_R, C_FILL, "%u Hz", user.current.parameters.step_hz);
      else if (i == 2) {
        if (user.current.parameters.mode == RX_WFM)
          PrintSmallEx(124, y, POS_R, C_FILL, "Fixed");
        else
          PrintSmallEx(124, y, POS_R, C_FILL, "%u Hz", user.current.parameters.bandwidth_hz);
      } else if (i == 3)
        PrintSmallEx(124, y, POS_R, C_FILL, "%u", user.current.parameters.squelch);
      else
        PrintSmallEx(124, y, POS_R, C_FILL, "%s", gMonitorMode ? "On" : "Off");
      if (i == menuIndex)
        FillRect(0, y-7, 128, 10, C_INVERT);
    }
    return;
  }
  const RadioParameters *preset = &gDeviceConfig.radio.presets[user.active_band];
  changed = user.current.parameters.mode != preset->mode || user.current.parameters.step_hz != preset->step_hz ||
            user.current.parameters.bandwidth_hz != preset->bandwidth_hz || user.current.parameters.squelch != preset->squelch;
  uint32_t frequency = ctx->tx_state.is_active ? RADIO_GetParam(ctx, PARAM_TX_FREQUENCY_FACT)*10u : user.current.frequency_hz;
  UI_VfoView view = {
    .id = 'A', .selected = true,
    .frequency_hz = frequency, .mode = user.current.parameters.mode,
    .band_short = shortBandName(radioBands[user.active_band].name), .changed = changed,
    .signal_valid = runtime.active,
    .signal_level = runtime.active ? UI_SMeterLevel() : 0,
    .status = ctx->tx_state.is_active ? "TX" : !runtime.active ? "RX --"
            : ctx->tx_state.last_error != TX_UNKNOWN ? "TX OFF" : NULL,
  };
  UI_VfoRender(8, &view, &card, Now());
  if (message) PrintSmall(0, 43, "%s", message);
}

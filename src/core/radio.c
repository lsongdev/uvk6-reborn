#include "hal/led.h"
#include "core/radio.h"
#include "hal/rx-device.h"
#include "hal/board.h"
#include "core/dcs.h"
#include "hal/audio.h"
#include "hal/backlight.h"
#include "drivers/bk1080/bk1080.h"
#include "drivers/bk4819/bk4819-regs.h"
#include "drivers/bk4819/bk4819.h"
#include "drivers/si473x/si473x.h"
#include "hal/display.h"
#include "hal/system.h"
#include "hal/uart.h"
#include "external/printf/printf.h"
#include "core/bands.h"
#include "core/battery.h"
#include "core/channels.h"
#include "core/lootlist.h"
#include "core/measurements.h"
#include "core/misc.h"
#include "hal/scheduler.h"
#include "core/settings.h"
#include <stdint.h>
#include <string.h>

bool gShowAllRSSI = false;
bool gMonitorMode = false;

RadioState *gRadioState;
ExtendedVFOContext *vfo;
VFOContext *ctx;

const char *RADIO_NAMES[3] = {
    [RADIO_BK4819] = "BK4819",
    [RADIO_BK1080] = "BK1080",
    [RADIO_SI4732] = "SI4732",
};


static void toggleBK4819(bool on) {
  Log("[RADIO] BK4819 audio=%u", on);
  if (on) {
    BK4819_ToggleAFDAC(true);
    BK4819_ToggleAFBit(true);
    SYS_DelayMs(8);
    AUDIO_ToggleSpeaker(true);
  } else {
    AUDIO_ToggleSpeaker(false);
    SYS_DelayMs(8);
    BK4819_ToggleAFDAC(false);
    BK4819_ToggleAFBit(false);
  }
}


static void toggleBK1080SI4732(bool on) {
  Log("[RADIO] BK1080/SI audio=%u", on);
  if (on) {
    SYS_DelayMs(8);
    AUDIO_ToggleSpeaker(true);
  } else {
    AUDIO_ToggleSpeaker(false);
    SYS_DelayMs(8);
  }
}

// Output routing is independent from receiver tuning and power-up.
void RADIO_SetAudioOpen(bool open) {
  if (!vfo || !ctx)
    return;
  if (ctx->radio_type == RADIO_BK4819)
    toggleBK4819(open);
  else
    toggleBK1080SI4732(open);

  LED_SetGreen(open);
  if (open && gSettings.backlightOnSquelch != BL_SQL_OFF)
    BACKLIGHT_On();
  else if (!open && gSettings.backlightOnSquelch == BL_SQL_OPEN)
    BACKLIGHT_Toggle(false);
}

void RADIO_StopReception(void) {
  if (!vfo || !ctx)
    return;
  // Leave the RF chip configured for recovery, but never play stale audio.
  // The next successful tune/squelch update will explicitly reopen it.
  vfo->is_open = false;
  vfo->msm.open = false;
  RADIO_SetAudioOpen(false);
}

// Legacy receiver selection is used only when the active chip changes.

uint16_t RADIO_GetRSSI(const VFOContext *ctx) {
  switch (ctx->radio_type) {
  case RADIO_BK4819:
    return BK4819_GetRSSI();
  case RADIO_BK1080:
    return BK1080_GetRSSI();
  case RADIO_SI4732:
    return RSQ_GET() ? rsqStatus.resp.RSSI : 0;
  default:
    return 0;
  }
}

uint8_t RADIO_GetSNR(const VFOContext *ctx) {
  switch (ctx->radio_type) {
  case RADIO_BK4819:
    return ConvertDomain(BK4819_GetSNR(), 24, 170, 0, 30);
  case RADIO_BK1080:
    return BK1080_GetSNR();
  case RADIO_SI4732:
    return rsqStatus.resp.SNR; // Same sample as the preceding RSSI read.
  default:
    return 0;
  }
}

uint8_t RADIO_GetNoise(const VFOContext *ctx) {
  return ctx->radio_type == RADIO_BK4819 ? BK4819_GetNoise() : 0;
}

uint8_t RADIO_GetGlitch(const VFOContext *ctx) {
  return ctx->radio_type == RADIO_BK4819 ? BK4819_GetGlitch() : 0;
}


static void updateContext(void) {
  vfo = RADIO_GetCurrentVFO(gRadioState);
  ctx = vfo ? &vfo->context : NULL;
}

void RADIO_InitState(RadioState *state, uint8_t num_vfos) {
  Log("[RADIO] InitState");
  (void)num_vfos;
  memset(state, 0, sizeof(*state));
  state->num_vfos = 1; // Single EEPROM-backed legacy receiver.
  state->active_vfo_index = 0;
  state->vfos[0].mode = MODE_VFO;
  state->vfos[0].is_active = true;
  updateContext();
}

bool RADIO_CheckSquelch(VFOContext *ctx) {

  if (gMonitorMode) {
    return true;
  }
  if (ctx->radio_type == RADIO_BK4819) {
    return BK4819_IsSquelchOpen();
  }

  return ctx->squelch.value == 0 || RADIO_GetSNR(ctx) > ctx->squelch.value;
}

static void RADIO_UpdateMeasurement(ExtendedVFOContext *vfo) {
  // Log("Update MSM");
  VFOContext *ctx = &vfo->context;
  vfo->msm.f = ctx->frequency;
  vfo->msm.rssi = RADIO_GetRSSI(ctx);
  vfo->msm.noise = RADIO_GetNoise(ctx);
  vfo->msm.glitch = RADIO_GetGlitch(ctx);
  vfo->msm.snr = RADIO_GetSNR(ctx);
  vfo->msm.open = RADIO_CheckSquelch(ctx);
}

void RADIO_UpdateSquelch(RadioState *state) {
  /* if (ctx->tx_state.is_active) {
    vfo->is_open = vfo->msm.open = false;
    RADIO_SwitchAudioToVFO(state, state->active_vfo_index);
    return;
  } */
  RADIO_UpdateMeasurement(&state->vfos[state->active_vfo_index]);
  if (vfo->msm.open != vfo->is_open) {
    vfo->is_open = vfo->msm.open;
    RADIO_SetAudioOpen(vfo->is_open); // Squelch must not retune or repower RX.
    gRedrawScreen = true;
  }
}
void RADIO_LoadVFOs(RadioState *state) {
  // Initialize hardware once. VFO1 then applies the validated UVR2 plan.
  // No synthetic legacy channel or second parameter replay.
  BK4819_Init();
  BK4819_RX_TurnOn();
  (void)BK1080_Init(0, false);

  ExtendedVFOContext *receiver = &state->vfos[0];
  VFOContext *context = &receiver->context;
  *context = (VFOContext){
      .frequency = 14550000, .radio_type = RADIO_BK4819,
      .modulation = MOD_FM, .bandwidth = BK4819_FILTER_BW_12k,
      .gain = AUTO_GAIN_INDEX, .volume = 100,
      .filter = FILTER_AUTO, .preciseFChange = true,
      .step = STEP_12_5kHz
  };
  context->tx_state.last_error = TX_UNKNOWN;
  receiver->vfo_ch_index = UINT16_MAX;
  receiver->mode = MODE_VFO;
  receiver->is_active = true;
  receiver->is_open = false;
  state->num_vfos = 1;
  state->active_vfo_index = 0;
  updateContext();
}

uint8_t RADIO_GetCurrentVFONumber(const RadioState *state) {
  /* for (uint8_t i = 0; i < state->num_vfos; i++) {
    if (state->vfos[i].is_active) {
      return i;
    }
  }
  return 0xFF; // Ошибка - активный VFO не найден */
  return state && state->num_vfos ? 0 : UINT8_MAX;
}

/**
 * @brief Получает указатель на текущий активный VFO
 * @param state Указатель на состояние радио
 * @return Указатель на ExtendedVFOContext или NULL если ошибка
 */
ExtendedVFOContext *RADIO_GetCurrentVFO(RadioState *state) {
  uint8_t current = RADIO_GetCurrentVFONumber(state);
  return (current != 0xFF) ? &state->vfos[current] : NULL;
}

/**
 * @brief Получает константный указатель на текущий активный VFO
 * @param state Указатель на состояние радио
 * @return Константный указатель на ExtendedVFOContext или NULL если ошибка
 */
const ExtendedVFOContext *RADIO_GetCurrentVFOConst(const RadioState *state) {
  uint8_t current = RADIO_GetCurrentVFONumber(state);
  return (current != 0xFF) ? &state->vfos[current] : NULL;
}

ReceiverCapabilities RADIO_GetReceiverCapabilities(void) {
  static bool detected;
  static ReceiverCapabilities capabilities;
  if (!detected) {
    capabilities.broadcast = BK1080_Detect();
    // Probe each chip through its own driver. A failed BK read is not SI.
    capabilities.si = !capabilities.broadcast && SI47XX_Detect();
    capabilities.ssb_patch = capabilities.si && SETTINGS_IsPatchPresent();
    detected = true;
  }
  return capabilities;
}

// Hardware is configured only through RX_DEVICE_Apply. The temporary
// VFOContext mirrors successfully applied plans for measurements and UI.
bool RADIO_ApplyRxPlan(VFOContext *context, const RxPlan *plan) {
  if (!plan || !gRadioState || context != &gRadioState->vfos[0].context ||
      context->tx_state.is_active ||
      (unsigned)plan->parameters.mode >= RX_MODE_COUNT)
    return false;

  RxPlan resolved;
  if (RADIO_MODEL_Plan(plan->frequency_hz, plan->parameters,
                       RADIO_GetReceiverCapabilities(), &resolved) != RX_OK ||
      resolved.device != plan->device ||
      resolved.frequency_hz != plan->frequency_hz)
    return false;

  Radio next = plan->device == RX_DEVICE_BK ? RADIO_BK4819 :
               plan->device == RX_DEVICE_SI ? RADIO_SI4732 : RADIO_BK1080;
  RxDevice old = context->radio_type == RADIO_BK4819 ? RX_DEVICE_BK :
                 context->radio_type == RADIO_SI4732 ? RX_DEVICE_SI :
                 RX_DEVICE_BROADCAST;
  const bool ssb = plan->parameters.mode == RX_LSB ||
                   plan->parameters.mode == RX_USB;
  uint16_t bandwidth = 0;
  if (plan->parameters.mode != RX_WFM &&
      !(plan->device == RX_DEVICE_BK
          ? BK4819_BandwidthFromHz(plan->parameters.bandwidth_hz, &bandwidth)
          : SI47XX_BandwidthFromHz(plan->parameters.bandwidth_hz, ssb, &bandwidth)))
    return false;

  RADIO_StopReception();
  // On failure the previously committed context stays intact. rx-runtime
  // may restore its last plan, otherwise reception remains safely muted.
  if (!RX_DEVICE_Apply(plan, old))
    return false;

  static const ModulationType modes[RX_MODE_COUNT] = {
      MOD_FM, MOD_WFM, MOD_AM, MOD_LSB, MOD_USB
  };
  context->radio_type = next;
  context->modulation = modes[plan->parameters.mode];
  context->frequency = plan->frequency_hz / 10u;
  context->bandwidth = bandwidth;
  context->squelch.value = plan->parameters.squelch;
  context->gain = next == RADIO_BK4819 ? AUTO_GAIN_INDEX : 0;
  context->volume = 100;
  context->tx_allowed = false;
  context->tx_state.offsetDirection = OFFSET_NONE;
  context->preciseFChange = true;
  context->filter = FILTER_AUTO;
  context->save_to_eeprom = false;
  // Only legacy diagnostic/readback code still uses this indexed step.
  for (uint8_t n = 0; n < ARRAY_SIZE(StepFrequencyTable); ++n)
    if (StepFrequencyTable[n] * 10u == plan->parameters.step_hz) {
      context->step = n;
      break;
    }
  memset(context->dirty, 0, sizeof(context->dirty));
  gRedrawScreen = true;
  return true;
}

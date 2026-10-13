#include "core/device-config.h"
#include "hal/led.h"
#include "core/radio.h"
#include "hal/rx-device.h"
#include "hal/board.h"
#include "core/dcs.h"
#include "hal/audio.h"
#include "hal/tx-device.h"
#include "hal/backlight.h"
#include "drivers/bk1080/bk1080.h"
#include "drivers/bk4819/bk4819-regs.h"
#include "drivers/bk4819/bk4819.h"
#include "drivers/si473x/si473x.h"
#include "hal/display.h"
#include "hal/system.h"
#include "hal/uart.h"
#include "external/printf/printf.h"
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
RadioState *vfo;
RadioReceiver *ctx;

// Both receiver families share the speaker mute/settle sequence.
// The BK4819 additionally gates its DAC and audio path.
static void toggleAudio(bool on, bool bk) {
  Log("[RADIO] audio=%u bk=%u", on, bk);
  if (on) {
    if (bk) {
      BK4819_ToggleAFDAC(true);
      BK4819_ToggleAFBit(true);
    }
    SYS_DelayMs(8);
    AUDIO_ToggleSpeaker(true);
  } else {
    AUDIO_ToggleSpeaker(false);
    SYS_DelayMs(8);
    if (bk) {
      BK4819_ToggleAFDAC(false);
      BK4819_ToggleAFBit(false);
    }
  }
}

// Output routing is independent from receiver tuning and power-up.
void RADIO_SetAudioOpen(bool open) {
  if (!vfo || !ctx)
    return;
  toggleAudio(open, ctx->radio_type == RADIO_BK4819);

  LED_SetGreen(open);
  BACKLIGHT_SetReception(open);
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

uint16_t RADIO_GetRSSI(const RadioReceiver *ctx) {
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

uint8_t RADIO_GetSNR(const RadioReceiver *ctx) {
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

uint8_t RADIO_GetNoise(const RadioReceiver *ctx) {
  return ctx->radio_type == RADIO_BK4819 ? BK4819_GetNoise() : 0;
}

uint8_t RADIO_GetGlitch(const RadioReceiver *ctx) {
  return ctx->radio_type == RADIO_BK4819 ? BK4819_GetGlitch() : 0;
}


void RADIO_InitState(RadioState *state) {
  if (!state) return;
  memset(state, 0, sizeof(*state));
  gRadioState = vfo = state;
  ctx = &state->context;
  ctx->radio_type = RADIO_BK4819;
  ctx->frequency = 14550000;
  ctx->modulation = MOD_FM;
  ctx->bandwidth = BK4819_FILTER_BW_12k;
}

bool RADIO_CheckSquelch(RadioReceiver *ctx) {

  if (gMonitorMode) {
    return true;
  }
  if (ctx->radio_type == RADIO_BK4819) {
    bool open = BK4819_IsSquelchOpen();
    bool tone = !ctx->rx_tone || BK4819_CssIsDetected(TONE_Type(ctx->rx_tone) != CODE_TYPE_CONTINUOUS_TONE);
    return open && tone;
  }

  return ctx->squelch == 0 || RADIO_GetSNR(ctx) > ctx->squelch;
}

void RADIO_Measure(RadioState *state) {
  RadioReceiver *receiver = &state->context;
  state->msm.f = receiver->frequency;
  bool valid = true;
  if (receiver->radio_type == RADIO_SI4732) {
    valid = RSQ_GET();
    state->msm.rssi = valid ? rsqStatus.resp.RSSI : 0;
    state->msm.snr = valid ? rsqStatus.resp.SNR : 0;
  } else {
    state->msm.rssi = RADIO_GetRSSI(receiver);
    state->msm.snr = RADIO_GetSNR(receiver);
  }
  state->msm.noise = RADIO_GetNoise(receiver);
  state->msm.glitch = RADIO_GetGlitch(receiver);
  state->msm.open = valid && RADIO_CheckSquelch(receiver);
}
void RADIO_UpdateSquelch(RadioState *state) {
  if (!state) return;
  RADIO_Measure(state);
  if (state->msm.open != state->is_open) {
    state->is_open = state->msm.open;
    RADIO_SetAudioOpen(state->is_open);
    gRedrawScreen = true;
  }
}
void RADIO_InitHardware(RadioState *state) {
  if (!state || state != gRadioState) return;
  BK4819_Init();
  TX_DEVICE_Disable(); // Explicitly keep PA off on startup.
  BK4819_RX_TurnOn();
  (void)BK1080_Init(0, false);
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

static void adoptReceiver(const RxPlan *plan, uint16_t bandwidth) {
  static const ModulationType modes[RX_MODE_COUNT] = {
      MOD_FM, MOD_WFM, MOD_AM, MOD_LSB, MOD_USB
  };
  ctx->radio_type = plan->device==RX_DEVICE_BK ? RADIO_BK4819 :
                    plan->device==RX_DEVICE_SI ? RADIO_SI4732 : RADIO_BK1080;
  ctx->modulation = modes[plan->parameters.mode];
  ctx->frequency = plan->frequency_hz / 10u;
  ctx->bandwidth = bandwidth;
  ctx->squelch = plan->parameters.squelch;
  ctx->rx_tone = plan->parameters.rx_tone;
  // Samples belong to a tuned RF plan; never carry RSSI across VFO handoffs.
  gRadioState->msm = (Measurement){0};
  gRedrawScreen = true;
}

bool RADIO_SelectPreparedRxPlan(const RxPlan *plan, uint8_t devices, bool open) {
  if (!plan || !gRadioState || (unsigned)plan->parameters.mode>=RX_MODE_COUNT) return false;
  uint16_t bandwidth=0;
  bool ssb=plan->parameters.mode==RX_LSB || plan->parameters.mode==RX_USB;
  if (plan->parameters.mode!=RX_WFM &&
      !(plan->device==RX_DEVICE_BK ? BK4819_BandwidthFromHz(plan->parameters.bandwidth_hz,&bandwidth) :
        SI47XX_BandwidthFromHz(plan->parameters.bandwidth_hz,ssb,&bandwidth))) return false;
  if (!RX_DEVICE_SelectAudio(devices,plan->device,open)) return false;
  adoptReceiver(plan,bandwidth);
  vfo->is_open=vfo->msm.open=open;
  LED_SetGreen(open);BACKLIGHT_SetReception(open);
  return true;
}

// Hardware is configured only through RX_DEVICE_Apply. The temporary
// RadioReceiver mirrors successfully applied plans for measurements and UI.
bool RADIO_ApplyRxPlan(RadioReceiver *context, const RxPlan *plan) {
  if (!plan || !gRadioState || context != &gRadioState->context ||
      (unsigned)plan->parameters.mode >= RX_MODE_COUNT)
    return false;

  RxPlan resolved;
  if (RADIO_MODEL_Plan(plan->frequency_hz, plan->parameters,
                       RADIO_GetReceiverCapabilities(), &resolved) != RX_OK ||
      resolved.device != plan->device ||
      resolved.frequency_hz != plan->frequency_hz)
    return false;

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

  adoptReceiver(plan,bandwidth);
  return true;
}

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

#define RADIO_SAVE_DELAY_MS 1000

#define DEBUG_PARAMS 1

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

const char *FILTER_NAMES[4] = {
    [FILTER_VHF] = "VHF",
    [FILTER_UHF] = "UHF",
    [FILTER_OFF] = "Off",
    [FILTER_AUTO] = "Auto",
};

const char *PARAM_NAMES[] = {
    [PARAM_FREQUENCY] = "f",                 //
    [PARAM_STEP] = "Step",                   //
    [PARAM_POWER] = "Power",                 //
    [PARAM_TX_OFFSET] = "TX offset",         //
    [PARAM_TX_OFFSET_DIR] = "TX offset dir", //
    [PARAM_MODULATION] = "Mod",              //
    [PARAM_SQUELCH_VALUE] = "SQ",            //
    [PARAM_SQUELCH_TYPE] = "SQ type",        //
    [PARAM_VOLUME] = "Volume",               //
    [PARAM_GAIN] = "ATT",                    //
    [PARAM_BANDWIDTH] = "BW",                //
    [PARAM_TX_STATE] = "TX state",           //
    [PARAM_RADIO] = "Radio",                 //
    [PARAM_RX_CODE] = "RX code",             //
    [PARAM_TX_CODE] = "TX code",             //
    [PARAM_RSSI] = "RSSI",                   //
    [PARAM_GLITCH] = "Glitch",               //
    [PARAM_NOISE] = "Noise",                 //
    [PARAM_SNR] = "SNR",                     //
    [PARAM_PRECISE_F_CHANGE] = "Precise f",  //

    [PARAM_TX_POWER] = "TX Power",
    [PARAM_TX_POWER_AMPLIFIER] = "TX PA",
    [PARAM_TX_FREQUENCY] = "TX f",
    [PARAM_TX_FREQUENCY_FACT] = "TX f fact",

    [PARAM_AFC] = "AFC",         //
    [PARAM_AFC_SPD] = "AFC SPD", //
    [PARAM_DEV] = "DEV",         //
    [PARAM_MIC] = "MIC",         //
    [PARAM_XTAL] = "XTAL",       //
    [PARAM_FILTER] = "Filter",   //
};

const char *TX_STATE_NAMES[7] = {
    [TX_UNKNOWN] = "TX Off",              //
    [TX_ON] = "TX On",                    //
    [TX_VOL_HIGH] = "CHARGING",           //
    [TX_BAT_LOW] = "BAT LOW",             //
    [TX_DISABLED] = "DISABLED",           //
    [TX_DISABLED_UPCONVERTER] = "UPCONV", //
    [TX_POW_OVERDRIVE] = "HIGH POW",      //
};

const char *MOD_NAMES_BK4819[8] = {
    [MOD_FM] = "FM",   //
    [MOD_AM] = "AM",   //
    [MOD_LSB] = "LSB", //
    [MOD_USB] = "USB", //
    [MOD_BYP] = "BYP", //
    [MOD_RAW] = "RAW", //
    [MOD_WFM] = "WFM", //
};

const char *MOD_NAMES_SI47XX[8] = {
    [SI47XX_AM] = "AM",
    [SI47XX_FM] = "FM",
    [SI47XX_LSB] = "LSB",
    [SI47XX_USB] = "USB",
};

const char *BW_NAMES_BK4819[10] = {
    [BK4819_FILTER_BW_6k] = "U6K",   //
    [BK4819_FILTER_BW_7k] = "U7K",   //
    [BK4819_FILTER_BW_9k] = "N9k",   //
    [BK4819_FILTER_BW_10k] = "N10k", //
    [BK4819_FILTER_BW_12k] = "W12k", //
    [BK4819_FILTER_BW_14k] = "W14k", //
    [BK4819_FILTER_BW_17k] = "W17k", //
    [BK4819_FILTER_BW_20k] = "W20k", //
    [BK4819_FILTER_BW_23k] = "W23k", //
    [BK4819_FILTER_BW_26k] = "W26k", //
};

const char *BW_NAMES_SI47XX[7] = {
    [SI47XX_BW_1_8_kHz] = "1.8k", //
    [SI47XX_BW_1_kHz] = "1k",     //
    [SI47XX_BW_2_kHz] = "2k",     //
    [SI47XX_BW_2_5_kHz] = "2.5k", //
    [SI47XX_BW_3_kHz] = "3k",     //
    [SI47XX_BW_4_kHz] = "4k",     //
    [SI47XX_BW_6_kHz] = "6k",     //
};

const char *BW_NAMES_SI47XX_SSB[6] = {
    [SI47XX_SSB_BW_0_5_kHz] = "0.5k", //
    [SI47XX_SSB_BW_1_0_kHz] = "1.0k", //
    [SI47XX_SSB_BW_1_2_kHz] = "1.2k", //
    [SI47XX_SSB_BW_2_2_kHz] = "2.2k", //
    [SI47XX_SSB_BW_3_kHz] = "3k",     //
    [SI47XX_SSB_BW_4_kHz] = "4k",     //
};

const char *FLT_BOUND_NAMES[2] = {"240MHz", "280MHz"};

const char *SQ_TYPE_NAMES[4] = {"RNG", "RG", "RN", "R"};

const uint16_t StepFrequencyTable[15] = {
    2,   5,   50,  100,

    250, 500, 625, 833, 900, 1000, 1250, 2500, 5000, 10000, 50000,
};

// Диапазоны для BK4819
static const FreqBand bk4819_bands[] = {
    {
        .min_freq = BK4819_F_MIN,
        .max_freq = BK4819_F_MAX,
        .num_available_mods = 4,
        .num_available_bandwidths = 10,
        .available_mods = {MOD_FM, MOD_AM, MOD_LSB, MOD_USB},
        .available_bandwidths =
            {
                BK4819_FILTER_BW_6k,  //  "U 6K"
                BK4819_FILTER_BW_7k,  //  "U 7K",
                BK4819_FILTER_BW_9k,  //  "N 9k",
                BK4819_FILTER_BW_10k, //  "N 10k",
                BK4819_FILTER_BW_12k, //  "W 12k",
                BK4819_FILTER_BW_14k, //  "W 14k",
                BK4819_FILTER_BW_17k, //  "W 17k",
                BK4819_FILTER_BW_20k, //  "W 20k",
                BK4819_FILTER_BW_23k, //  "W 23k",
                BK4819_FILTER_BW_26k, //  "W 26k",
            },
    },
};

// Диапазоны для SI4732
static const FreqBand si4732_bands[] = {
    {
        .min_freq = SI47XX_F_MIN,
        .max_freq = SI47XX_F_MAX,
        .num_available_mods = 3,
        .num_available_bandwidths = 7,
        .available_mods = {SI47XX_AM, SI47XX_LSB, SI47XX_USB},
        .available_bandwidths =
            {
                SI47XX_BW_1_kHz,
                SI47XX_BW_1_8_kHz,
                SI47XX_BW_2_kHz,
                SI47XX_BW_2_5_kHz,
                SI47XX_BW_3_kHz,
                SI47XX_BW_4_kHz,
                SI47XX_BW_6_kHz,
            },
    },
    {
        .min_freq = SI47XX_F_MIN,
        .max_freq = SI47XX_F_MAX,
        .num_available_mods = 3,
        .num_available_bandwidths = 6,
        .available_mods = {SI47XX_AM, SI47XX_LSB, SI47XX_USB},
        .available_bandwidths =
            {
                SI47XX_SSB_BW_0_5_kHz,
                SI47XX_SSB_BW_1_0_kHz,
                SI47XX_SSB_BW_1_2_kHz,
                SI47XX_SSB_BW_2_2_kHz,
                SI47XX_SSB_BW_3_kHz,
                SI47XX_SSB_BW_4_kHz,
            },
    },
    {
        .min_freq = SI47XX_FM_F_MIN,
        .max_freq = SI47XX_FM_F_MAX,
        .num_available_mods = 2,
        .num_available_bandwidths = 0,
        .available_mods = {MOD_FM, MOD_WFM},
        .available_bandwidths = {},
    },
};

static const FreqBand bk1080_band = {
    .min_freq = BK1080_F_MIN,
    .max_freq = BK1080_F_MAX,
    .num_available_mods = 1,
    .available_mods = {MOD_FM},
};

static bool RADIO_HasSi() { return RADIO_GetReceiverCapabilities().si; }

static uint32_t getRealTxFreq(const VFOContext *ctx) {
  switch (ctx->tx_state.offsetDirection) {
  case OFFSET_PLUS:
    return ctx->frequency + ctx->tx_state.frequency;
  case OFFSET_MINUS:
    return ctx->frequency - ctx->tx_state.frequency;
  case OFFSET_FREQ:
    return ctx->tx_state.frequency;
  default:
    break;
  }
  return ctx->frequency;
}

static void enableCxCSS(VFOContext *ctx) {
  switch (ctx->tx_state.code.type) {
  case CODE_TYPE_CONTINUOUS_TONE:
    BK4819_SetCTCSSFrequency(CTCSS_Options[ctx->tx_state.code.value]);
    break;
  case CODE_TYPE_DIGITAL:
  case CODE_TYPE_REVERSE_DIGITAL:
    BK4819_SetCDCSSCodeWord(DCS_GetGolayCodeWord(ctx->tx_state.code.type,
                                                 ctx->tx_state.code.value));
    break;
  default:
    BK4819_ExitSubAu();
    break;
  }
}

static void setupToneDetection(VFOContext *ctx) {
  BK4819_WriteRegister(BK4819_REG_7E, 0x302E); // DC flt BW 0=BYP
  uint16_t InterruptMask = BK4819_REG_3F_CxCSS_TAIL;
  if (gSettings.dtmfdecode) {
    BK4819_EnableDTMF();
    InterruptMask |= BK4819_REG_3F_DTMF_5TONE_FOUND;
  } else {
    BK4819_DisableDTMF();
  }
  switch (ctx->code.type) {
  case CODE_TYPE_DIGITAL:
  case CODE_TYPE_REVERSE_DIGITAL:
    // Log("DCS on");
    BK4819_SetCDCSSCodeWord(
        DCS_GetGolayCodeWord(ctx->code.type, ctx->code.value));
    InterruptMask |= BK4819_REG_3F_CDCSS_FOUND | BK4819_REG_3F_CDCSS_LOST;
    break;
  case CODE_TYPE_CONTINUOUS_TONE:
    // Log("CTCSS on");
    BK4819_SetCTCSSFrequency(CTCSS_Options[ctx->code.value]);
    InterruptMask |= BK4819_REG_3F_CTCSS_FOUND | BK4819_REG_3F_CTCSS_LOST;
    break;
  default:
    // Log("STE on");
    BK4819_SetCTCSSFrequency(670);
    BK4819_SetTailDetection(550);
    break;
  }
  BK4819_WriteRegister(BK4819_REG_3F, InterruptMask);
}

static void sendEOT() {
  BK4819_ExitSubAu();
  switch (gSettings.roger) {
  case 1:
    BK4819_PlayRogerTiny();
    break;
  default:
    break;
  }
  if (gSettings.ste) {
    SYS_DelayMs(50);
    BK4819_GenTail(4);
    BK4819_WriteRegister(BK4819_REG_51, 0x9033);
    SYS_DelayMs(200);
  }
  BK4819_ExitSubAu();
}

// RX-only baseline: legacy EEPROM allowTx cannot enable the PA.
static TXStatus checkTX(VFOContext *context) {
  (void)context;
  return TX_DISABLED;
}

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

static void rxTurnOff(Radio r) {
  Log("RX OFF %s", RADIO_NAMES[r]);
  switch (r) {
  case RADIO_BK4819:
    BK4819_Idle();
    break;
  case RADIO_BK1080:
    BK1080_Mute(true);
    break;
  case RADIO_SI4732:
    if (isSi4732On) {
      if (gSettings.si4732PowerOff) {
        SI47XX_PowerDown();
      } else {
        SI47XX_SetVolume(0);
      }
    }
    break;
  default:
    break;
  }
}

static void rxTurnOn(const VFOContext *ctx) {
  switch (ctx->radio_type) {
  case RADIO_BK4819:
    BK4819_RX_TurnOn();
    break;
  case RADIO_BK1080:
    BK4819_Idle();
    BK1080_Mute(false);
    BK1080_Init(ctx->frequency, true);
    break;
  case RADIO_SI4732:
    BK4819_Idle();
    SI47XX_SwitchMode(ctx->modulation == MOD_WFM ? SI47XX_FM
                                               : (SI47XX_MODE)ctx->modulation);
    SI47XX_SetVolume(ConvertDomain(ctx->volume, 0, 100, 0, 63));
    break;
  default:
    break;
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
void RADIO_SwitchAudioToVFO(RadioState *state, uint8_t vfo_index) {
  if (vfo_index >= state->num_vfos)
    return;

  Log("SW AUD");

  const ExtendedVFOContext *vfo = &state->vfos[vfo_index];

  switch (vfo->context.radio_type) {
  case RADIO_BK4819:
    rxTurnOff(RADIO_HasSi() ? RADIO_SI4732 : RADIO_BK1080);
    toggleBK1080SI4732(false);
    break;
  case RADIO_SI4732:
    toggleBK4819(false);
    rxTurnOff(RADIO_BK4819);
    break;
  case RADIO_BK1080:
    toggleBK4819(false);
    rxTurnOff(RADIO_BK4819);
    break;
  default:
    break;
  }

  rxTurnOn(&vfo->context);

  RADIO_SetAudioOpen(vfo->is_open);

  // Устанавливаем громкость для выбранного VFO
  // AUDIO_SetVolume(vfo->context.volume);
}

static bool setParamBK4819(VFOContext *ctx, ParamType p) {
  switch (p) {
  case PARAM_GAIN:
    BK4819_SetAGC(ctx->modulation != MOD_AM, ctx->gain);
    return true;
  case PARAM_BANDWIDTH:
    BK4819_SetFilterBandwidth(ctx->bandwidth);
    return true;
  case PARAM_SQUELCH_VALUE:
    BK4819_Squelch(ctx->squelch.value, gSettings.sqlOpenTime,
                   gSettings.sqlCloseTime);
    return true;
  case PARAM_SQUELCH_TYPE:
    BK4819_SquelchType(ctx->squelch.type);
    return true;
  case PARAM_MODULATION:
    BK4819_SetModulation(ctx->modulation);
    return true;
  case PARAM_FREQUENCY:
    if (ctx->filter == FILTER_AUTO) {
      Filter filter = (ctx->frequency < SETTINGS_GetFilterBound()) ? FILTER_VHF
                                                                   : FILTER_UHF;
      BK4819_SelectFilterEx(filter);
    }
    BK4819_TuneTo(ctx->frequency,
                  ctx->preciseFChange); // TODO: check if SetFreq needed
    return true;
  /* case PARAM_TX_POWER_AMPLIFIER:
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE,
                         ctx->tx_state.pa_enabled);
    return true;
  case PARAM_TX_POWER:
    BK4819_SetupPowerAmplifier(ctx->tx_state.power_level,
                               ctx->tx_state.frequency);
    return true; */
  case PARAM_AFC:
    BK4819_SetAFC(ctx->afc);
    return true;
  case PARAM_AFC_SPD:
    BK4819_SetAFCSpeed(ctx->afc_speed);
    return true;
  case PARAM_XTAL:
    BK4819_XtalSet(ctx->xtal);
    return true;
  case PARAM_FILTER: {
    Filter filter = ctx->filter;
    if (ctx->filter == FILTER_AUTO) {
      filter = (ctx->frequency < SETTINGS_GetFilterBound()) ? FILTER_VHF
                                                            : FILTER_UHF;
    }
    BK4819_SelectFilterEx(filter);
  }
    return true;
  case PARAM_MIC:
    BK4819_SetRegValue(RS_MIC, ctx->mic);
    return true;
  case PARAM_DEV:
    BK4819_SetRegValue(RS_DEV, ctx->dev);
    return true;
  case PARAM_VOLUME:
    /* BK4819_SetRegValue(RS_AF_DAC_GAIN,
                       ConvertDomain(ctx->volume, 0, 100, 0, 15)); */
    return true;
  case PARAM_RADIO:
  case PARAM_PRECISE_F_CHANGE:
  case PARAM_STEP:
  case PARAM_POWER:
  case PARAM_TX_OFFSET:
  case PARAM_TX_OFFSET_DIR:
  case PARAM_TX_STATE:
  case PARAM_TX_FREQUENCY:
  case PARAM_TX_FREQUENCY_FACT:
  case PARAM_TX_POWER:
  case PARAM_TX_POWER_AMPLIFIER:
  case PARAM_RX_CODE:
  case PARAM_TX_CODE:
  case PARAM_RSSI:
  case PARAM_NOISE:
  case PARAM_GLITCH:
  case PARAM_SNR:
  case PARAM_COUNT:
    break;
  }
  return false;
}

static bool setParamSI4732(VFOContext *ctx, ParamType p) {
  switch (p) {
  case PARAM_FREQUENCY:
    SI47XX_TuneTo(ctx->frequency);
    return true;
  case PARAM_MODULATION:
    SI47XX_SwitchMode(ctx->modulation == MOD_WFM ? SI47XX_FM
                                               : (SI47XX_MODE)ctx->modulation);
    return true;
  case PARAM_GAIN:
    SI47XX_SetAutomaticGainControl(ctx->gain == 0,
                                   ctx->gain == 0 ? 0 : ctx->gain);
    return true;
  case PARAM_BANDWIDTH:
    if (ctx->modulation == MOD_FM || ctx->modulation == MOD_WFM)
      return true; // AM/SSB bandwidth properties do not apply to FM.
    if (RADIO_IsSSB(ctx)) {
      SI47XX_SetSsbBandwidth(ctx->bandwidth);
    } else {
      SI47XX_SetBandwidth(ctx->bandwidth, true);
    }
    return true;
  case PARAM_VOLUME:
    SI47XX_SetVolume(ConvertDomain(ctx->volume, 0, 100, 0, 63));
    return true;
  case PARAM_RADIO:
  case PARAM_PRECISE_F_CHANGE:
  case PARAM_STEP:
  case PARAM_POWER:
  case PARAM_SQUELCH_TYPE:
  case PARAM_SQUELCH_VALUE:
  case PARAM_TX_OFFSET:
  case PARAM_TX_OFFSET_DIR:
  case PARAM_TX_STATE:
  case PARAM_TX_FREQUENCY:
  case PARAM_TX_FREQUENCY_FACT:
  case PARAM_TX_POWER:
  case PARAM_TX_POWER_AMPLIFIER:
  case PARAM_RX_CODE:
  case PARAM_TX_CODE:
  case PARAM_AFC:
  case PARAM_AFC_SPD:
  case PARAM_DEV:
  case PARAM_MIC:
  case PARAM_XTAL:
  case PARAM_FILTER:
  case PARAM_RSSI:
  case PARAM_NOISE:
  case PARAM_GLITCH:
  case PARAM_SNR:
  case PARAM_COUNT:
    break;
  }
  return false;
}

static bool setParamBK1080(VFOContext *ctx, ParamType p) {
  if (p == PARAM_FREQUENCY) {
    BK1080_SetFrequency(ctx->frequency);
    return true;
  }
  return false;
}

uint16_t RADIO_GetRSSI(const VFOContext *ctx) {
  switch (ctx->radio_type) {
  case RADIO_BK4819:
    return BK4819_GetRSSI();
  case RADIO_BK1080:
    return BK1080_GetRSSI();
  case RADIO_SI4732:
    RSQ_GET();
    return rsqStatus.resp.RSSI;
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

static void updateContext() {
  vfo = RADIO_GetCurrentVFO(gRadioState);
  ctx = &vfo->context;
}

// Select a capable receiver before validating a requested RX frequency.
// Broadcast FM needs the wideband receiver, even where BK4819 overlaps.
static bool selectReceiverForFrequency(VFOContext *ctx, uint32_t frequency) {
  Radio receiver;
  if (frequency >= BK1080_F_MIN && frequency <= BK1080_F_MAX &&
      ctx->modulation == MOD_FM) {
    receiver = RADIO_HasSi() ? RADIO_SI4732 : RADIO_BK1080;
  } else if (ctx->radio_type == RADIO_SI4732 &&
             frequency >= SI47XX_F_MIN && frequency <= SI47XX_F_MAX) {
    receiver = RADIO_SI4732;
  } else if (frequency >= BK4819_F_MIN && frequency <= BK4819_F_MAX) {
    receiver = RADIO_BK4819;
  } else if (frequency >= SI47XX_F_MIN && frequency <= SI47XX_F_MAX &&
             RADIO_HasSi()) {
    receiver = RADIO_SI4732;
  } else {
    return false;
  }

  if (ctx->radio_type != receiver) {
    ctx->radio_type = receiver;
    // Gain indices belong to the receiver, not to the shared VFO.
    ctx->gain = receiver == RADIO_BK4819 ? AUTO_GAIN_INDEX : 0;
    for (uint8_t i = 0; i < PARAM_COUNT; ++i)
      ctx->dirty[i] = true;
  }
  return true;
}

static void RADIO_UpdateCurrentBand(VFOContext *ctx);

static void RADIO_ApplyCorrections(VFOContext *ctx, bool save_to_eeprom) {
  const FreqBand *band = ctx->current_band;
  if (!band)
    return; // Нет band — ничего не корректируем

  // Корректировка частоты (если вне диапазона)
  if (!RADIO_IsParamValid(ctx, PARAM_FREQUENCY, ctx->frequency)) {
    LogC(LOG_C_YELLOW,
         "[RADIO] CORRECT: Frequency %u out of band, adjusting to nearest "
         "boundary",
         ctx->frequency);

    if (selectReceiverForFrequency(ctx, ctx->frequency)) {
      RADIO_UpdateCurrentBand(ctx);
      band = ctx->current_band;
    }

    if (ctx->frequency < band->min_freq) {
      LogC(LOG_C_BRIGHT_YELLOW, "[RADIO] CORRECT F=%u ~ %u (%u..%u)",
           ctx->frequency, band->min_freq, band->max_freq);
      ctx->frequency = band->min_freq;
    } else if (ctx->frequency > band->max_freq) {
      LogC(LOG_C_BRIGHT_YELLOW, "[RADIO] CORRECT F=%u ~ %u (%u..%u)",
           ctx->frequency, band->min_freq, band->max_freq);
      ctx->frequency = band->max_freq;
    }
    ctx->dirty[PARAM_FREQUENCY] = true; // Помечаем как dirty для применения
    if (save_to_eeprom) {
      ctx->save_to_eeprom = true;
      ctx->last_save_time = Now();
    }
  }

  // Корректировка модуляции (если не доступна)
  if (!RADIO_IsParamValid(ctx, PARAM_MODULATION, ctx->modulation)) {
    if (band->num_available_mods > 0) {
      uint32_t default_mod = band->available_mods[0];
      LogC(
          LOG_C_YELLOW,
          "[RADIO] CORRECT: Modulation %u invalid for band, setting to %u (%s)",
          ctx->modulation, default_mod,
          RADIO_GetParamValueString(
              ctx, PARAM_MODULATION)); // Используем новую мод для строки
      ctx->modulation = default_mod;
      ctx->dirty[PARAM_MODULATION] = true;
      if (save_to_eeprom) {
        ctx->save_to_eeprom = true;
        ctx->last_save_time = Now();
      }
    }
  }

  // Корректировка bandwidth (если не доступна)
  if (!RADIO_IsParamValid(ctx, PARAM_BANDWIDTH, ctx->bandwidth)) {
    if (band->num_available_bandwidths > 0) {
      uint32_t default_bw = band->available_bandwidths[0];
      LogC(LOG_C_YELLOW,
           "[RADIO] CORRECT: Bandwidth %u invalid for band, setting to %u (%s)",
           ctx->bandwidth, default_bw,
           RADIO_GetParamValueString(ctx, PARAM_BANDWIDTH));
      ctx->bandwidth = default_bw;
      ctx->dirty[PARAM_BANDWIDTH] = true;
      if (save_to_eeprom) {
        ctx->save_to_eeprom = true;
        ctx->last_save_time = Now();
      }
    }
  }

  // Можно добавить корректировки для других params, если нужно (e.g., gain,
  // step) Например, для gain: if (!RADIO_IsParamValid(ctx, PARAM_GAIN,
  // ctx->gain)) { ... set default ... }
}

static void RADIO_UpdateCurrentBand(VFOContext *ctx) {
  const FreqBand *band = NULL;

  switch (ctx->radio_type) {
  case RADIO_BK4819:
    LogC(LOG_C_BRIGHT_YELLOW, "[RADIO] BAND: BK4819");
    band = &bk4819_bands[0];
    break;
  case RADIO_SI4732:
    // Выбираем диапазон на основе частоты и модуляции
    if (ctx->frequency >= SI47XX_FM_F_MIN &&
        ctx->frequency <= SI47XX_FM_F_MAX) {
      LogC(LOG_C_BRIGHT_YELLOW, "[RADIO] BAND: SI FM");
      band = &si4732_bands[2]; // FM
    } else if (ctx->modulation == MOD_LSB || ctx->modulation == MOD_USB) {
      LogC(LOG_C_BRIGHT_YELLOW, "[RADIO] BAND: SI SSB");
      band = &si4732_bands[1]; // SSB
    } else {
      LogC(LOG_C_BG_BRIGHT_YELLOW, "[RADIO] BAND: SI AM");
      band = &si4732_bands[0]; // AM
    }
    break;
  case RADIO_BK1080:
    LogC(LOG_C_BRIGHT_YELLOW, "[RADIO] BAND: BK1080 FM");
    band = &bk1080_band;
    break;
  default:
    break;
  }

  ctx->current_band = band;

  // === Автоматически пересчитываем индексы ===
  ctx->modulation_index = 0;
  for (uint8_t i = 0; i < band->num_available_mods; i++) {
    if (band->available_mods[i] == ctx->modulation) {
      ctx->modulation_index = i;
      break;
    }
  }

  ctx->bandwidth_index = 0;
  for (uint8_t i = 0; i < band->num_available_bandwidths; i++) {
    if (band->available_bandwidths[i] == ctx->bandwidth) {
      ctx->bandwidth_index = i;
      break;
    }
  }

  // Если не нашли — принудительно установим 0 (первый в списке)
  if (ctx->modulation_index >= band->num_available_mods &&
      band->num_available_mods > 0) {
    ctx->modulation_index = 0;
    ctx->modulation = band->available_mods[0];
    ctx->dirty[PARAM_MODULATION] = true;
  }
  if (ctx->bandwidth_index >= band->num_available_bandwidths &&
      band->num_available_bandwidths > 0) {
    ctx->bandwidth_index = 0;
    ctx->bandwidth = band->available_bandwidths[0];
    ctx->dirty[PARAM_BANDWIDTH] = true;
  }
}

// Инициализация VFO
void RADIO_Init(VFOContext *ctx, Radio radio_type) {
  memset(ctx, 0, sizeof(VFOContext));
  ctx->radio_type = radio_type;

  // Установка диапазона по умолчанию
  switch (radio_type) {
  case RADIO_BK4819:
    ctx->current_band = &bk4819_bands[0];
    ctx->frequency = 14550000; // 145.5 МГц (диапазон FM)
    ctx->gain = AUTO_GAIN_INDEX;
    // ctx->afc_speed = BK4819_GetAFCSpeed();

    /* ctx->xtal = BK4819_GetRegValue(RS_XTAL_MODE);
    ctx->afc = BK4819_GetAFC();
    ctx->dev = BK4819_GetRegValue(RS_DEV);
    ctx->mic = BK4819_GetRegValue(RS_MIC);
    */

    break;
  case RADIO_SI4732:
    ctx->current_band = &si4732_bands[0];
    ctx->frequency = 710000; // 7.1 МГц (диапазон AM)
    break;
  default:
    break;
  }
  /* RADIO_UpdateCurrentBand(ctx);
  RADIO_ApplyCorrections(ctx, false); */
}

// Проверка параметра для текущего диапазона
bool RADIO_IsParamValid(VFOContext *ctx, ParamType param, uint32_t value) {
  const FreqBand *band = ctx->current_band;
  if (!band)
    return false;

  switch (param) {
  case PARAM_FREQUENCY:
  case PARAM_TX_FREQUENCY:
    return (value >= band->min_freq && value <= band->max_freq);

  case PARAM_MODULATION:
    for (uint8_t i = 0; i < band->num_available_mods; i++) {
      if (band->available_mods[i] == value)
        return true;
    }
    return false;

  case PARAM_BANDWIDTH:
    for (uint8_t i = 0; i < band->num_available_bandwidths; i++) {
      if (band->available_bandwidths[i] == value)
        return true;
    }
    return false;

  case PARAM_GAIN:
    if (ctx->radio_type == RADIO_BK4819) {
      return value < ARRAY_SIZE(GAIN_TABLE); // Оставляем как есть (индекс)
    }
    if (ctx->radio_type == RADIO_SI4732) {
      return value <= 27; // 0..26 + auto
    }
    return false;

  case PARAM_POWER:
    return value <= TX_POW_HIGH;

  default:
    return true;
  }
}

// Установка параметра (всегда uint32_t!)
void RADIO_SetParam(VFOContext *ctx, ParamType param, uint32_t value,
                    bool save_to_eeprom) {
  if (param == PARAM_FREQUENCY) {
    if (!selectReceiverForFrequency(ctx, value)) {
      LogC(LOG_C_RED, "[RADIO] Unsupported frequency %u", value);
      return;
    }
    // The SI band depends on the requested frequency, so refresh it after
    // assigning the frequency below, rather than checking its old band.
  } else if (!RADIO_IsParamValid(ctx, param, value)) {
    LogC(LOG_C_RED, "[RADIO] ERR: %-12s -> %u", PARAM_NAMES[param], value);
    return;
  }

  uint32_t old_value = RADIO_GetParam(ctx, param);

  switch (param) {

  case PARAM_MODULATION:
    ctx->modulation = (ModulationType)value;
    if (ctx->current_band) {
      ctx->modulation_index = 0;
      for (uint8_t i = 0; i < ctx->current_band->num_available_mods; i++) {
        if (ctx->current_band->available_mods[i] == value) {
          ctx->modulation_index = i;
          break;
        }
      }
    }
    ctx->dirty[PARAM_MODULATION] = true;
    break;

  case PARAM_BANDWIDTH:
    ctx->bandwidth = (uint16_t)value;
    if (ctx->current_band) {
      ctx->bandwidth_index = 0;
      for (uint8_t i = 0; i < ctx->current_band->num_available_bandwidths;
           i++) {
        if (ctx->current_band->available_bandwidths[i] == value) {
          ctx->bandwidth_index = i;
          break;
        }
      }
    }
    ctx->dirty[PARAM_BANDWIDTH] = true;
    break;
  case PARAM_RX_CODE:
    ctx->code.value = value;
    break;
  case PARAM_TX_CODE:
    ctx->tx_state.code.value = value;
    break;

  case PARAM_PRECISE_F_CHANGE:
    ctx->preciseFChange = value;
    break;
  case PARAM_STEP:
    ctx->step = (Step)value;
    break;
  case PARAM_TX_FREQUENCY:
  case PARAM_TX_OFFSET:
    ctx->tx_state.frequency = value;
    break;
  case PARAM_TX_OFFSET_DIR:
    ctx->tx_state.offsetDirection = value;
    break;
  case PARAM_POWER: {
    ctx->power = value;
    ctx->tx_state.power_level =
        BANDS_CalculateOutputPower(ctx->power, ctx->tx_state.frequency);

    ctx->tx_state.pa_enabled = true;
    ctx->dirty[PARAM_TX_POWER] = true;
    ctx->dirty[PARAM_TX_POWER_AMPLIFIER] = true;
  } break;
  case PARAM_TX_POWER:
    ctx->tx_state.power_level = value;
    break;
  case PARAM_TX_POWER_AMPLIFIER:
    ctx->tx_state.pa_enabled = value;
    break;

  case PARAM_FREQUENCY:
    ctx->frequency = value;
    break;
  /* case PARAM_MODULATION:
    ctx->modulation = (ModulationType)value;
    break;
  case PARAM_BANDWIDTH:
    ctx->bandwidth = (uint16_t)value;
    break; */
  case PARAM_VOLUME:
    ctx->volume = (uint8_t)value;
    break;
  case PARAM_GAIN:
    ctx->gain = value;
    break;
  case PARAM_AFC:
    ctx->afc = value;
    break;
  case PARAM_AFC_SPD:
    ctx->afc_speed = value;
    break;
  case PARAM_DEV:
    ctx->dev = value;
    break;
  case PARAM_MIC:
    ctx->mic = value;
    break;
  case PARAM_XTAL:
    ctx->xtal = (XtalMode)value;
    break;
  case PARAM_FILTER:
    ctx->filter = (Filter)value;
    break;
  case PARAM_SQUELCH_VALUE:
    ctx->squelch.value = value;
    break;
  case PARAM_SQUELCH_TYPE:
    ctx->squelch.type = value;
    break;
  case PARAM_RADIO:
    ctx->radio_type = value;
    // RADIO_UpdateCurrentBand(ctx);
    for (uint8_t i = 0; i < PARAM_COUNT; ++i) {
      ctx->dirty[i] = true;
    }
    break;
  case PARAM_TX_FREQUENCY_FACT:
  case PARAM_TX_STATE:
  case PARAM_RSSI:
  case PARAM_NOISE:
  case PARAM_GLITCH:
  case PARAM_SNR:
  case PARAM_COUNT:
    return;
  }

  if (param == PARAM_FREQUENCY || param == PARAM_BANDWIDTH ||
      param == PARAM_MODULATION || param == PARAM_RADIO) {
    RADIO_UpdateCurrentBand(ctx);
    RADIO_ApplyCorrections(ctx, save_to_eeprom);
  }

#ifdef DEBUG_PARAMS
  LogC(LOG_C_WHITE, "[SET] %-12s -> %s%s", PARAM_NAMES[param],
       RADIO_GetParamValueString(ctx, param), save_to_eeprom ? " [W]" : "");
#endif /* ifdef DEBUG_PARAMS */

  // TODO: make dirty only when changed.
  // but, potential BUG: param not applied when 0
  ctx->dirty[param] = true;

  // Если значение изменилось и требуется сохранение - устанавливаем флаг
  if (save_to_eeprom && (old_value != value)) {
    LogC(LOG_C_BRIGHT_YELLOW, "[RADIO] SAVE %s", PARAM_NAMES[param]);
    ctx->save_to_eeprom = true;
    ctx->last_save_time = Now();
  }
}

uint32_t RADIO_GetParam(const VFOContext *ctx, ParamType param) {
  switch (param) {
  case PARAM_RX_CODE:
    return ctx->code.value;
  case PARAM_TX_CODE:
    return ctx->tx_state.code.value;
  case PARAM_TX_OFFSET:
    return ctx->tx_state.frequency;
  case PARAM_TX_OFFSET_DIR:
    return ctx->tx_state.offsetDirection;
  case PARAM_TX_FREQUENCY:
    return ctx->tx_state.frequency;
  case PARAM_TX_FREQUENCY_FACT:
    return getRealTxFreq(ctx);
  case PARAM_TX_POWER_AMPLIFIER:
    return ctx->tx_state.pa_enabled;
  case PARAM_TX_POWER:
    return ctx->tx_state.power_level;
  case PARAM_TX_STATE:
    return ctx->tx_state.is_active;
  case PARAM_RSSI:
    return vfo->msm.rssi;
  case PARAM_NOISE:
    return vfo->msm.noise;
  case PARAM_GLITCH:
    return vfo->msm.glitch;
  case PARAM_SNR:
    return vfo->msm.snr;
  case PARAM_FREQUENCY:
    return ctx->frequency;
  case PARAM_PRECISE_F_CHANGE:
    return ctx->preciseFChange;
  case PARAM_MODULATION:
    return ctx->modulation;
  case PARAM_BANDWIDTH:
    return ctx->bandwidth;
  case PARAM_STEP:
    return ctx->step;
  case PARAM_VOLUME:
    return ctx->volume;
  case PARAM_GAIN:
    return ctx->gain;
  case PARAM_SQUELCH_TYPE:
    return ctx->squelch.type;
  case PARAM_SQUELCH_VALUE:
    return ctx->squelch.value;
  case PARAM_RADIO:
    return ctx->radio_type;
  case PARAM_POWER:
    return ctx->power;
  case PARAM_AFC:
    return ctx->afc;
  case PARAM_AFC_SPD:
    return ctx->afc_speed;
  case PARAM_XTAL:
    return ctx->xtal;
  case PARAM_FILTER:
    return ctx->filter;
  case PARAM_MIC:
    return ctx->mic;
  case PARAM_DEV:
    return ctx->dev;
  case PARAM_COUNT:
    break;
  }
  return 0;
}

bool RADIO_AdjustParam(VFOContext *ctx, ParamType param, uint32_t inc,
                       bool save_to_eeprom) {
  const FreqBand *band = ctx->current_band;
  if (!band) {
    return false;
  }

  uint32_t mi = 0, ma = UINT32_MAX, v = RADIO_GetParam(ctx, param);

  switch (param) {

  case PARAM_MODULATION:
    ma = band->num_available_mods;
    if (ma == 0)
      return false;
    // Работаем с индексом!
    ctx->modulation_index = AdjustU(ctx->modulation_index, 0, ma, inc);
    RADIO_SetParam(ctx, param, band->available_mods[ctx->modulation_index],
                   save_to_eeprom);
    RADIO_ApplySettings(ctx);
    return true;

  case PARAM_BANDWIDTH:
    ma = band->num_available_bandwidths;
    if (ma == 0)
      return false;
    ctx->bandwidth_index = AdjustU(ctx->bandwidth_index, 0, ma, inc);
    RADIO_SetParam(ctx, param, band->available_bandwidths[ctx->bandwidth_index],
                   save_to_eeprom);
    RADIO_ApplySettings(ctx);
    return true;

  case PARAM_FREQUENCY:
    mi = band->min_freq;
    ma = band->max_freq;
    break;
  case PARAM_TX_OFFSET:
  case PARAM_TX_FREQUENCY:
    mi = band->min_freq;
    ma = band->max_freq;
    break;
  case PARAM_POWER:
    ma = 4;
    break;
  case PARAM_TX_POWER:
    ma = 255;
    break;
  case PARAM_TX_OFFSET_DIR:
    ma = OFFSET_FREQ + 1;
    break;
  case PARAM_TX_POWER_AMPLIFIER:
    ma = 2;
    break;
  /* case PARAM_MODULATION:
    ma = band->num_available_mods;
    break;
  case PARAM_BANDWIDTH:
    ma = band->num_available_bandwidths;
    break; */
  case PARAM_STEP:
    ma = STEP_COUNT;
    break;
  case PARAM_SQUELCH_VALUE:
    ma = 11;
    break;
  case PARAM_SQUELCH_TYPE:
    ma = 4;
    break;
  case PARAM_AFC:
    ma = 9;
    break;
  case PARAM_AFC_SPD:
    ma = 64;
    break;
  case PARAM_DEV:
    ma = 2550;
    break;
  case PARAM_MIC:
    ma = 16;
    break;
  case PARAM_XTAL:
    ma = XTAL_3_38_4M + 1;
    break;
  case PARAM_FILTER:
    ma = FILTER_AUTO + 1;
    break;
  case PARAM_GAIN:
    if (ctx->radio_type == RADIO_BK4819) {
      ma = ARRAY_SIZE(GAIN_TABLE);
    } else if (ctx->radio_type == RADIO_SI4732) {
      ma = 28;
    } else {
      ma = 0;
    }
    break;
  case PARAM_RADIO:
    ma = 3;
    break;
  default:
    LogC(LOG_C_RED, "[RADIO] ERR: range %s", PARAM_NAMES[param]);
    return false;
  }
  RADIO_SetParam(ctx, param, AdjustU(v, mi, ma, inc), save_to_eeprom);
  RADIO_ApplySettings(ctx);
  return true;
}

bool RADIO_IncDecParam(VFOContext *ctx, ParamType param, bool inc,
                       bool save_to_eeprom) {
  uint32_t v = 1;
  if (param == PARAM_FREQUENCY) {
    v = StepFrequencyTable[ctx->step];
  }
  if (param == PARAM_RADIO) {
    v = RADIO_GetParam(ctx, PARAM_RADIO);
    v = v == RADIO_BK4819 ? (RADIO_HasSi() ? RADIO_SI4732 : RADIO_BK1080)
                          : RADIO_BK4819;
    RADIO_SetParam(ctx, param, v, save_to_eeprom);
    return true;
  }
  if (param == PARAM_DEV) {
    v = 10;
  }
  return RADIO_AdjustParam(ctx, param, inc ? v : -v, save_to_eeprom);
}

typedef bool (*spf_ptr)(VFOContext *, ParamType);

static spf_ptr setParamForRadio[] = {
    [RADIO_BK4819] = &setParamBK4819,
    [RADIO_BK1080] = &setParamBK1080,
    [RADIO_SI4732] = &setParamSI4732,
};

// Применение настроек
void RADIO_ApplySettings(VFOContext *ctx) {
  const bool receiverChanged = ctx->dirty[PARAM_RADIO];
  if (receiverChanged) {
    LogC(LOG_C_BRIGHT_MAGENTA, "[RADIO] =%s",
         RADIO_GetParamValueString(ctx, PARAM_RADIO));
    ctx->dirty[PARAM_RADIO] = false;

    if (ctx->radio_type != RADIO_BK4819) {
      // printf("RADIO IS BC\n");
      rxTurnOn(ctx);
    }
  }

  const bool needSetupToneDetection =
      (ctx->dirty[PARAM_RX_CODE] || ctx->dirty[PARAM_TX_CODE] ||
       ctx->dirty[PARAM_TX_STATE]) &&
      ctx->radio_type == RADIO_BK4819;

  for (uint8_t p = 0; p < PARAM_COUNT; ++p) {
    if (!ctx->dirty[p]) {
      continue;
    }

    switch (p) {
    case PARAM_STEP:
    case PARAM_POWER:
    case PARAM_TX_FREQUENCY:
    case PARAM_TX_OFFSET:
    case PARAM_TX_OFFSET_DIR:
    case PARAM_TX_STATE:
    case PARAM_TX_CODE:
    case PARAM_RX_CODE:
    case PARAM_RSSI:
    case PARAM_NOISE:
    case PARAM_GLITCH:
    case PARAM_SNR:
    case PARAM_PRECISE_F_CHANGE:
      ctx->dirty[p] = false;
      continue;
    }

    if (!setParamForRadio[ctx->radio_type](ctx, p)) {
#ifdef DEBUG_PARAMS
      LogC(LOG_C_YELLOW, "[W] Param %s not set for %s", PARAM_NAMES[p],
           RADIO_NAMES[ctx->radio_type]);
#endif
      continue;
    }
    ctx->dirty[p] = false;
#ifdef DEBUG_PARAMS
    LogC(LOG_C_BRIGHT_WHITE, "[SET] %-12s -> %s", PARAM_NAMES[p],
         RADIO_GetParamValueString(ctx, p));
#endif /* ifdef DEBUG_PARAMS */
  }

  if (needSetupToneDetection) {
    setupToneDetection(ctx);
  }
  // Squelch may remain open across a receiver change. Route audio even if
  // its boolean state did not change, after the new receiver is configured.
  if (receiverChanged && gRadioState &&
      ctx == &gRadioState->vfos[gRadioState->active_vfo_index].context) {
    RADIO_SwitchAudioToVFO(gRadioState, gRadioState->active_vfo_index);
  }
}

// Начать передачу
bool RADIO_StartTX(VFOContext *ctx) {
  TXStatus status = checkTX(ctx);
  if (status != TX_ON) {
    ctx->tx_state.last_error = status;
    return false;
  }
  ctx->tx_state.last_error = TX_UNKNOWN;
  if (ctx->tx_state.is_active) {
    return true;
  }

  uint8_t power = ctx->tx_state.power_level;

  // HACK
  vfo->is_open = false;
  RADIO_StopReception();

  BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);

  uint32_t txF = getRealTxFreq(ctx);
  BK4819_SelectFilter(txF);
  BK4819_TuneTo(txF, true);

  LED_SetRed(gSettings.brightness > 1);
  BK4819_PrepareTransmit();

  SYS_DelayMs(10);
  BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, ctx->tx_state.pa_enabled);
  SYS_DelayMs(5);
  BK4819_SetupPowerAmplifier(power, txF);
  SYS_DelayMs(10);

  enableCxCSS(ctx);
  ctx->tx_state.is_active = true;
  return true;
}

// Завершить передачу
void RADIO_StopTX(VFOContext *ctx) {
  ctx->tx_state.last_error = TX_UNKNOWN;
  if (!ctx->tx_state.is_active) {
    return;
  }

  BK4819_ExitDTMF_TX(true); // also prepares to tx ste

  sendEOT();

  ctx->tx_state.is_active = false;
  LED_SetRed(false);
  BK4819_TurnsOffTones_TurnsOnRX();

  BK4819_SetupPowerAmplifier(0, 0);
  BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
  BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);

  setupToneDetection(ctx);
  BK4819_SelectFilter(ctx->frequency);
  BK4819_TuneTo(ctx->frequency, true);
}

void RADIO_ToggleTX(VFOContext *ctx, bool on) {
  if (on) {
    RADIO_StartTX(ctx);
  } else {
    RADIO_StopTX(ctx);
  }
}

bool RADIO_IsSSB(const VFOContext *ctx) {
  return ctx->modulation == MOD_LSB || ctx->modulation == MOD_USB;
}

// Initialize radio state
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
  if (ctx->tx_state.is_active) {
    return false;
  }
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
  RADIO_UpdateCurrentBand(context);
}

const char *RADIO_GetParamValueString(const VFOContext *ctx, ParamType param) {
  static char buf[16] = "unk";
  uint32_t v = RADIO_GetParam(ctx, param);
  switch (param) {
  case PARAM_RSSI:
    sprintf(buf, "%+ddB", Rssi2DBm(v));
    break;
  case PARAM_MODULATION:
    if (ctx->radio_type == RADIO_BK4819) {
      return MOD_NAMES_BK4819[ctx->modulation];
    }
    if (ctx->radio_type == RADIO_SI4732) {
      return ctx->modulation == MOD_WFM ? "WFM" : MOD_NAMES_SI47XX[ctx->modulation];
    }
    return "WFM";
  case PARAM_TX_STATE:
    return TX_STATE_NAMES[ctx->tx_state.last_error];
  case PARAM_BANDWIDTH:
    if (ctx->radio_type == RADIO_BK4819) {
      return BW_NAMES_BK4819[v];
    }
    if (ctx->radio_type == RADIO_SI4732) {
      if (RADIO_IsSSB(ctx)) {
        return BW_NAMES_SI47XX_SSB[v];
      }
      return BW_NAMES_SI47XX[v];
    }
    return "?(WIP)";
  case PARAM_STEP:
    sprintf(buf, "%d.%02d", StepFrequencyTable[v] / KHZ,
            StepFrequencyTable[v] % KHZ);
    break;
  case PARAM_FREQUENCY:
  case PARAM_TX_OFFSET:
  case PARAM_TX_FREQUENCY:
  case PARAM_TX_FREQUENCY_FACT:
    mhzToS(buf, v);
    break;
  case PARAM_RADIO:
    sprintf(buf, "%s", RADIO_NAMES[ctx->radio_type]);
    break;
  case PARAM_TX_OFFSET_DIR:
    sprintf(buf, "%s", TX_OFFSET_NAMES[ctx->radio_type]);
    break;
  case PARAM_GAIN:
    if (ctx->radio_type == RADIO_BK4819) {
      bkAttToS(buf, v);
      break;
    } else if (ctx->radio_type == RADIO_SI4732) {
      sprintf(buf, v == 0 ? "Auto" : "%u", v - 1);
      break;
    }
    sprintf(buf, "Auto");
    break;

  case PARAM_RX_CODE:
    PrintRTXCode(buf, ctx->code.type, ctx->code.value);
    break;

  case PARAM_TX_CODE:
    PrintRTXCode(buf, ctx->tx_state.code.type, ctx->tx_state.code.value);
    break;

  case PARAM_POWER:
    return TX_POWER_NAMES[ctx->power];
  case PARAM_FILTER:
    return FILTER_NAMES[ctx->filter];
  case PARAM_NOISE:
  case PARAM_GLITCH:
  case PARAM_SNR:
  case PARAM_TX_POWER:
  case PARAM_TX_POWER_AMPLIFIER:
  case PARAM_AFC:
  case PARAM_AFC_SPD:
  case PARAM_DEV:
  case PARAM_MIC:
  case PARAM_XTAL:
  case PARAM_SQUELCH_VALUE:
  case PARAM_PRECISE_F_CHANGE:
  case PARAM_COUNT:
    sprintf(buf, "%u", v);
    break;
  case PARAM_VOLUME:
    sprintf(buf, "%u%", v);
    break;
  case PARAM_SQUELCH_TYPE:
    sprintf(buf, "%s", SQ_TYPE_NAMES[ctx->squelch.type]);
    break;
  }
  return buf;
}

/**
 * @brief Получает номер текущего активного VFO
 * @param state Указатель на состояние радио
 * @return Номер VFO (0..MAX_VFOS-1) или 0xFF если не найдено
 */
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

void RADIO_FastSquelchUpdate() { vfo->is_open = RADIO_CheckSquelch(ctx); }

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

RadioParameters RADIO_ReadParameters(const VFOContext *context) {
  RxMode mode = RX_NFM;
  if (context->modulation == MOD_WFM || context->radio_type == RADIO_BK1080 ||
      (context->radio_type == RADIO_SI4732 && context->modulation == MOD_FM))
    mode = RX_WFM;
  else if (context->modulation == MOD_AM)
    mode = RX_AM;
  else if (context->modulation == MOD_LSB)
    mode = RX_LSB;
  else if (context->modulation == MOD_USB)
    mode = RX_USB;
  uint32_t bandwidth = mode == RX_WFM ? 0
      : context->radio_type == RADIO_BK4819 ? BK4819_BandwidthToHz(context->bandwidth)
      : SI47XX_BandwidthToHz(context->bandwidth, mode == RX_LSB || mode == RX_USB);
  uint32_t step = context->step < ARRAY_SIZE(StepFrequencyTable)
                      ? StepFrequencyTable[context->step]*10u : 0;
  return (RadioParameters){mode, step, bandwidth, context->squelch.value, false};
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
  RADIO_UpdateCurrentBand(context);
  gRedrawScreen = true;
  return true;
}

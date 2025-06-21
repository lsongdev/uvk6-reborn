#include "radio.h"
#include "board.h"
#include "dcs.h"
#include "driver/audio.h"
#include "driver/bk1080.h"
#include "driver/bk4819.h"
#include "driver/si473x.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "external/printf/printf.h"
#include "helper/battery.h"
#include "helper/channels.h"
#include "helper/lootlist.h"
#include "helper/measurements.h"
#include "misc.h"
#include "scheduler.h"
#include <stdint.h>
#include <string.h>

bool gShowAllRSSI;
bool gMonitorMode;

Measurement gLoot;

const char *PARAM_NAMES[] = {
    [PARAM_FREQUENCY] = "f",         //
    [PARAM_STEP] = "Step",           //
    [PARAM_POWER] = "Power",         //
    [PARAM_TX_OFFSET] = "TX offset", //
    [PARAM_MODULATION] = "Mod",      //
    [PARAM_SQUELCH] = "SQL",         //
    [PARAM_VOLUME] = "Volume",       //
    [PARAM_BANDWIDTH] = "BW",        //
    [PARAM_TX_STATE] = "TX state",   //
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

const char *BW_NAMES_SI47XX[] = {
    [SI47XX_BW_1_8_kHz] = "1.8k", //
    [SI47XX_BW_1_kHz] = "1k",     //
    [SI47XX_BW_2_kHz] = "2k",     //
    [SI47XX_BW_2_5_kHz] = "2.5k", //
    [SI47XX_BW_3_kHz] = "3k",     //
    [SI47XX_BW_4_kHz] = "4k",     //
    [SI47XX_BW_6_kHz] = "6k",     //
};

const char *BW_NAMES_SI47XX_SSB[] = {
    [SI47XX_SSB_BW_0_5_kHz] = "0.5k", //
    [SI47XX_SSB_BW_1_0_kHz] = "1.0k", //
    [SI47XX_SSB_BW_1_2_kHz] = "1.2k", //
    [SI47XX_SSB_BW_2_2_kHz] = "2.2k", //
    [SI47XX_SSB_BW_3_kHz] = "3k",     //
    [SI47XX_SSB_BW_4_kHz] = "4k",     //
};

const uint16_t StepFrequencyTable[15] = {
    2,   5,   50,  100,

    250, 500, 625, 833, 900, 1000, 1250, 2500, 5000, 10000, 50000,
};

// Диапазоны для BK4819
static const FreqBand bk4819_bands[] = {
    {
        .min_freq = BK4819_F_MIN,
        .max_freq = BK4819_F_MAX,
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
                BK4819_FILTER_BW_26k, //	"W 26k",
            },
    },
    // ... другие диапазоны
};

// Диапазоны для SI4732
static const FreqBand si4732_bands[] = {
    {
        .min_freq = SI47XX_F_MIN,
        .max_freq = SI47XX_F_MAX,
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
        .available_mods = {SI47XX_LSB, SI47XX_USB},
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
        .available_mods = {SI47XX_FM},
        .available_bandwidths = {},
    },
};

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

static TXStatus checkTX(VFOContext *ctx) {
  if (gSettings.upconverter) {
    return TX_DISABLED_UPCONVERTER;
  }

  if (ctx->radio_type != RADIO_BK4819) {
    return TX_DISABLED;
  }

  /* Band txBand = BANDS_ByFrequency(txF);

  if (!txBand.allowTx && !(RADIO_IsChMode() && radio->allowTx)) {
    return TX_DISABLED;
  } */

  if (gBatteryPercent == 0) {
    return TX_BAT_LOW;
  }
  if (gChargingWithTypeC || gBatteryVoltage > 880) {
    return TX_VOL_HIGH;
  }
  return TX_ON;
}

static void toggleBK4819(bool on) {
  // Log("Toggle bk4819 audio %u", on);
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
  // Log("Toggle bk1080si audio %u", on);
  if (on) {
    SYS_DelayMs(8);
    AUDIO_ToggleSpeaker(true);
  } else {
    AUDIO_ToggleSpeaker(false);
    SYS_DelayMs(8);
  }
}

// Внутренняя функция переключения аудио на конкретный VFO
static void RADIO_SwitchAudioToVFO(RadioState *state, uint8_t vfo_index) {
  if (vfo_index >= state->num_vfos)
    return;

  const ExtendedVFOContext *vfo = &state->vfos[vfo_index];

  // Реализация зависит от вашего аппаратного обеспечения
  switch (vfo->context.radio_type) {
  case RADIO_BK4819:
    toggleBK1080SI4732(false);
    toggleBK4819(true);
    break;
  case RADIO_SI4732:
  case RADIO_BK1080:
    toggleBK4819(false);
    toggleBK1080SI4732(true);
    break;
  default:
    break;
  }

  // Устанавливаем громкость для выбранного VFO
  // AUDIO_SetVolume(vfo->context.volume);
}

// Инициализация VFO
void RADIO_Init(VFOContext *ctx, Radio radio_type) {
  memset(ctx, 0, sizeof(VFOContext));
  ctx->radio_type = radio_type;

  // Установка диапазона по умолчанию
  switch (radio_type) {
  case RADIO_BK4819:
    ctx->current_band = &bk4819_bands[0];
    ctx->frequency = 145500000; // 145.5 МГц (диапазон FM)
    break;
  case RADIO_SI4732:
    ctx->current_band = &si4732_bands[0];
    ctx->frequency = 7100000; // 7.1 МГц (диапазон AM)
    break;
  default:
    break;
  }
}

// Проверка параметра для текущего диапазона
bool RADIO_IsParamValid(VFOContext *ctx, ParamType param, uint32_t value) {
  const FreqBand *band = ctx->current_band;
  if (!band)
    return false;

  switch (param) {
  case PARAM_FREQUENCY:
    return (value >= band->min_freq && value <= band->max_freq);
  case PARAM_MODULATION:
    for (size_t i = 0; i < sizeof(band->available_mods); i++) {
      if (band->available_mods[i] == (ModulationType)value)
        return true;
    }
    return false;
  case PARAM_BANDWIDTH:
    for (size_t i = 0; i < sizeof(band->available_bandwidths); i++) {
      if (band->available_bandwidths[i] == (uint16_t)value)
        return true;
    }
    return false;
  default:
    return true; // Остальные параметры не зависят от диапазона
  }
}

// Установка параметра (всегда uint32_t!)
void RADIO_SetParam(VFOContext *ctx, ParamType param, uint32_t value,
                    bool save_to_eeprom) {
  if (!RADIO_IsParamValid(ctx, param, value))
    return;

  uint32_t old_value = RADIO_GetParam(ctx, param);

  switch (param) {
  case PARAM_FREQUENCY:
    ctx->frequency = value;
    break;
  case PARAM_MODULATION:
    ctx->modulation = (ModulationType)value;
    break;
  case PARAM_BANDWIDTH:
    ctx->bandwidth = (uint16_t)value;
    break;
  case PARAM_VOLUME:
    ctx->volume = (uint8_t)value;
    break;
  default:
    return;
  }
  ctx->dirty[param] = true;

  // Если значение изменилось и требуется сохранение - устанавливаем флаг
  if (save_to_eeprom && (old_value != value)) {
    ctx->save_to_eeprom = true;
    ctx->last_save_time = Now();
  }
}

uint32_t RADIO_GetParam(VFOContext *ctx, ParamType param) {
  switch (param) {
  case PARAM_FREQUENCY:
    return ctx->frequency;
  case PARAM_MODULATION:
    return ctx->modulation;
  case PARAM_BANDWIDTH:
    return ctx->bandwidth;
  case PARAM_VOLUME:
    return ctx->volume;
  default:
    return 0;
  }
  ctx->dirty[param] = true;
}

bool RADIO_AdjustParam(VFOContext *ctx, ParamType param, uint32_t inc,
                       bool save_to_eeprom) {
  uint32_t mi, ma, v = RADIO_GetParam(ctx, param);
  const FreqBand *band = ctx->current_band;
  if (!band)
    return false;

  switch (param) {
  case PARAM_FREQUENCY:
    mi = band->min_freq;
    ma = band->max_freq;
    break;
  case PARAM_MODULATION:
    v = band->available_mods[0];
    for (size_t i = 0; i < ARRAY_SIZE(band->available_mods); i++) {
      if (band->available_mods[i] == v) {
        v = AdjustU(i, 0, ARRAY_SIZE(band->available_mods), inc);
      }
    }
    RADIO_SetParam(ctx, param, v, save_to_eeprom);
    return true;
  case PARAM_BANDWIDTH:
    v = band->available_bandwidths[0];
    for (size_t i = 0; i < ARRAY_SIZE(band->available_bandwidths); i++) {
      if (band->available_bandwidths[i] == v) {
        v = AdjustU(i, 0, ARRAY_SIZE(band->available_bandwidths), inc);
      }
    }
    RADIO_SetParam(ctx, param, v, save_to_eeprom);
    return true;
  default:
    return false;
  }
  RADIO_SetParam(ctx, param, AdjustU(v, mi, ma, inc), save_to_eeprom);
  return true;
}

bool RADIO_IncDecParam(VFOContext *ctx, ParamType param, bool inc,
                       bool save_to_eeprom) {
  uint32_t v = 1;
  if (param == PARAM_FREQUENCY) {
    v = StepFrequencyTable[ctx->step];
  }
  return RADIO_AdjustParam(ctx, param, inc ? v : -v, save_to_eeprom);
}

// Применение настроек
void RADIO_ApplySettings(VFOContext *ctx) {
  switch (ctx->radio_type) {
  case RADIO_BK4819:
    if (ctx->dirty[PARAM_FREQUENCY]) {
      BK4819_SetFrequency(ctx->frequency);
      ctx->dirty[PARAM_FREQUENCY] = false;
    }
    if (ctx->dirty[PARAM_MODULATION]) {
      BK4819_SetModulation(ctx->modulation);
      ctx->dirty[PARAM_MODULATION] = false;
    }
    break;

  case RADIO_SI4732:
    if (ctx->dirty[PARAM_FREQUENCY]) {
      SI47XX_TuneTo(ctx->frequency);
      ctx->dirty[PARAM_FREQUENCY] = false;
    }
    break;
  case RADIO_BK1080:
    break;
  }
}

// Начать передачу
bool RADIO_StartTX(VFOContext *ctx) {
  TXStatus status = checkTX(ctx);
  if (status != TX_ON) {
    ctx->tx_state.last_error = status;
    return false;
  }
  if (ctx->tx_state.is_active)
    return true;

  uint8_t power = ctx->tx_state.power_level;

  BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);

  BK4819_TuneTo(ctx->tx_state.frequency, true);

  BOARD_ToggleRed(gSettings.brightness > 1);
  BK4819_PrepareTransmit();

  SYS_DelayMs(10);
  BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, ctx->tx_state.pa_enabled);
  SYS_DelayMs(5);
  BK4819_SetupPowerAmplifier(power, ctx->tx_state.frequency);
  SYS_DelayMs(10);

  enableCxCSS(ctx);
  ctx->tx_state.is_active = true;
  return true;
}

// Завершить передачу
void RADIO_StopTX(VFOContext *ctx) {
  if (!ctx->tx_state.is_active)
    return;

  BK4819_ExitDTMF_TX(true); // also prepares to tx ste

  sendEOT();

  ctx->tx_state.is_active = false;
  BOARD_ToggleRed(false);
  BK4819_TurnsOffTones_TurnsOnRX();

  BK4819_SetupPowerAmplifier(0, 0);
  BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
  BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);

  setupToneDetection(ctx);
  BK4819_TuneTo(ctx->frequency, true);
}

void RADIO_ToggleTX(VFOContext *ctx, bool on) {
  if (on) {
    RADIO_StartTX(ctx);
  } else {
    RADIO_StopTX(ctx);
  }
}

bool RADIO_IsSSB(VFOContext *ctx) {
  return ctx->modulation == MOD_LSB || ctx->modulation == MOD_USB;
}

// Initialize radio state
void RADIO_InitState(RadioState *state, uint8_t num_vfos) {
  memset(state, 0, sizeof(RadioState));
  state->num_vfos = (num_vfos > MAX_VFOS) ? MAX_VFOS : num_vfos;

  for (uint8_t i = 0; i < state->num_vfos; i++) {
    RADIO_Init(&state->vfos[i].context, RADIO_BK4819); // Default to BK4819
    state->vfos[i].mode = MODE_VFO;
    state->vfos[i].channel_index = 0;
    state->vfos[i].is_active = (i == 0); // First VFO is active by default
  }

  // Initialize multiwatch
  state->multiwatch.active_vfo_index = 0;
  state->multiwatch.num_vfos = state->num_vfos;
  state->multiwatch.scan_interval_ms = 100; // 1 second scan interval
  state->multiwatch.last_scan_time = 0;
  state->multiwatch.enabled = false;
}

// Функция проверки необходимости сохранения и собственно сохранения
void RADIO_CheckAndSaveVFO(RadioState *state, uint8_t vfo_index) {
  if (vfo_index >= state->num_vfos)
    return;

  VFOContext *ctx = &state->vfos[vfo_index].context;
  uint16_t num = state->vfosChannels[vfo_index];

  if (ctx->save_to_eeprom && (Now() - ctx->last_save_time >= 1000)) {
    VFO ch;

    RADIO_SaveVFOToStorage(state, vfo_index, &ch);
    CHANNELS_Save(num, &ch);

    ctx->save_to_eeprom = false;
  }
}

// Switch to a different VFO
bool RADIO_SwitchVFO(RadioState *state, uint8_t vfo_index) {
  if (vfo_index >= state->num_vfos)
    return false;

  RADIO_CheckAndSaveVFO(state, state->multiwatch.active_vfo_index);

  // Deactivate current VFO
  state->vfos[state->multiwatch.active_vfo_index].is_active = false;

  // Activate new VFO
  state->multiwatch.active_vfo_index = vfo_index;
  state->vfos[vfo_index].is_active = true;

  // Apply settings for the new VFO
  RADIO_ApplySettings(&state->vfos[vfo_index].context);

  return true;
}

// Load VFO settings from EEPROM storage
void RADIO_LoadVFOFromStorage(RadioState *state, uint8_t vfo_index,
                              const VFO *storage) {
  if (vfo_index >= state->num_vfos)
    return;

  ExtendedVFOContext *vfo = &state->vfos[vfo_index];
  vfo->mode = storage->isChMode;

  // Set basic parameters
  RADIO_SetParam(&vfo->context, PARAM_FREQUENCY, storage->rxF, false);
  RADIO_SetParam(&vfo->context, PARAM_MODULATION, storage->modulation, false);
  // RADIO_SetParam(&vfo->context, PARAM_VOLUME, storage->volume, false);
  RADIO_SetParam(&vfo->context, PARAM_BANDWIDTH, storage->bw, false);

  vfo->context.code = storage->code.rx;
  vfo->context.tx_state.code = storage->code.tx;

  // Handle channel mode
  if (vfo->mode == MODE_CHANNEL) {
    vfo->channel_index = storage->channel;
    // Optionally validate the channel here
  }

  RADIO_ApplySettings(&vfo->context);
}

// Save VFO settings to EEPROM storage
void RADIO_SaveVFOToStorage(const RadioState *state, uint8_t vfo_index,
                            VFO *storage) {
  if (vfo_index >= state->num_vfos)
    return;

  const ExtendedVFOContext *vfo = &state->vfos[vfo_index];

  storage->isChMode = vfo->mode;
  storage->rxF = vfo->context.frequency;
  storage->modulation = vfo->context.modulation;
  // storage->volume = vfo->context.volume;
  storage->bw = vfo->context.bandwidth;
  storage->channel = vfo->channel_index;
  storage->code.rx = vfo->context.code;
  storage->code.tx = vfo->context.tx_state.code;
}

// Load channel into VFO
void RADIO_LoadChannelToVFO(RadioState *state, uint8_t vfo_index,
                            uint16_t channel_index) {
  if (vfo_index >= state->num_vfos)
    return;

  ExtendedVFOContext *vfo = &state->vfos[vfo_index];
  CH channel;
  CHANNELS_Load(channel_index, &channel);

  vfo->mode = MODE_CHANNEL;
  vfo->channel_index = channel_index;

  // Set parameters from channel
  RADIO_SetParam(&vfo->context, PARAM_FREQUENCY, channel.rxF, false);
  RADIO_SetParam(&vfo->context, PARAM_MODULATION, channel.modulation, false);
  RADIO_SetParam(&vfo->context, PARAM_BANDWIDTH, channel.bw, false);

  vfo->context.code = channel.code.rx;
  vfo->context.tx_state.code = channel.code.tx;

  RADIO_ApplySettings(&vfo->context);
}

/**
 * @brief Переключает режим работы VFO между частотным и канальным
 * @param state Указатель на состояние радио
 * @param vfo_index Индекс VFO (0..MAX_VFOS-1)
 * @return true если переключение успешно, false при ошибке
 */
bool RADIO_ToggleVFOMode(RadioState *state, uint8_t vfo_index) {
  // Проверка допустимости индекса VFO
  if (vfo_index >= state->num_vfos) {
    return false;
  }

  ExtendedVFOContext *vfo = &state->vfos[vfo_index];
  VFOContext *ctx = &vfo->context;

  // Определяем новый режим (инвертируем текущий)
  VFOMode new_mode = (vfo->mode == MODE_VFO) ? MODE_CHANNEL : MODE_VFO;

  if (new_mode == MODE_CHANNEL) { // Если переключаемся в канальный режим

    // TODO: set next channel if invalid
    RADIO_SaveVFOToStorage(state, vfo_index, vfo);
    RADIO_LoadChannelToVFO(state, vfo_index, vfo->channel_index);
  } else { // Если переключаемся в частотный режим
    RADIO_LoadVFOFromStorage(state, vfo_index, vfo);
  }

  // Помечаем для сохранения в EEPROM
  ctx->save_to_eeprom = true;
  ctx->last_save_time = Now();

  gRedrawScreen = true;

  return true;
}

// Toggle multiwatch on/off
void RADIO_ToggleMultiwatch(RadioState *state, bool enable) {
  state->multiwatch.enabled = enable;
  if (!enable) {
    // Return to the primary VFO when disabling multiwatch
    RADIO_SwitchVFO(state, 0);
  }
}

// Check if a VFO is a broadcast receiver
static bool isBroadcastReceiver(const VFOContext *ctx) {
  return (ctx->radio_type == RADIO_SI4732 || ctx->radio_type == RADIO_BK1080);
}

uint8_t RADIO_GetSNR(VFOContext *ctx) {
  switch (ctx->radio_type) {
  case RADIO_BK4819:
    return ConvertDomain(BK4819_GetSNR(), 24, 170, 0, 30);
  case RADIO_BK1080:
    return gShowAllRSSI ? BK1080_GetSNR() : 0;
  case RADIO_SI4732:
    if (gShowAllRSSI) {
      RSQ_GET();
      return rsqStatus.resp.SNR;
    }
    return 0;
  default:
    return 0;
  }
}

bool RADIO_CheckSquelch(VFOContext *ctx) {
  if (gMonitorMode) {
    return true;
  }
  if (ctx->radio_type == RADIO_BK4819) {
    return BK4819_IsSquelchOpen();
  }

  return gShowAllRSSI ? RADIO_GetSNR(ctx) > ctx->squelch.value : true;
}

// Update multiwatch state (should be called periodically)
void RADIO_UpdateMultiwatch(RadioState *state) {
  if (!state->multiwatch.enabled)
    return;

  uint32_t current_time = Now();
  if (current_time - state->multiwatch.last_scan_time <
      state->multiwatch.scan_interval_ms) {
    return;
  }

  state->multiwatch.last_scan_time = current_time;

  // Обновляем маршрутизацию аудио
  RADIO_UpdateAudioRouting(state);

  // Get current active VFO
  uint8_t current_vfo = state->multiwatch.active_vfo_index;
  const ExtendedVFOContext *active_vfo = &state->vfos[current_vfo];

  // If current VFO is a broadcast receiver, we can switch away from it
  if (isBroadcastReceiver(&active_vfo->context)) {
    // Check other VFOs for activity
    for (uint8_t i = 0; i < state->num_vfos; i++) {
      if (i == current_vfo)
        continue;

      ExtendedVFOContext *vfo = &state->vfos[i];

      // Skip broadcast receivers in multiwatch
      if (isBroadcastReceiver(&vfo->context))
        continue;

      // Check for activity (implementation depends on your hardware)
      bool has_activity = RADIO_CheckSquelch(&vfo->context);

      if (has_activity) {
        // Switch to this VFO
        RADIO_SwitchVFO(state, i);
        return;
      }
    }
  }

  // If we're on a non-broadcast VFO, check if we should return to broadcast
  if (!isBroadcastReceiver(&active_vfo->context)) {
    bool still_active = RADIO_CheckSquelch(&active_vfo->context);

    if (!still_active) {
      // Return to the first broadcast receiver we find
      for (uint8_t i = 0; i < state->num_vfos; i++) {
        ExtendedVFOContext *vfo = &state->vfos[i];
        if (isBroadcastReceiver(&vfo->context)) {
          RADIO_SwitchVFO(state, i);
          return;
        }
      }
    }
  }
}

void RADIO_LoadVFOs(RadioState *state) {
  uint8_t vfoIdx = 0;
  for (uint16_t i = 0; i < CHANNELS_GetCountMax(); ++i) {
    CHMeta meta = CHANNELS_GetMeta(i);

    bool isOurType = (TYPE_FILTER_VFO & (1 << meta.type)) != 0;
    if (!isOurType) {
      continue;
    }

    MR ch;
    CHANNELS_Load(i, &ch);
    if (ch.isChMode) {
      RADIO_LoadChannelToVFO(state, vfoIdx, ch.channel);
    } else {
      RADIO_LoadVFOFromStorage(state, vfoIdx, &ch);
    }
    vfoIdx++;
  }
}

// Включаем/выключаем маршрутизацию аудио
void RADIO_EnableAudioRouting(RadioState *state, bool enable) {
  state->audio_routing_enabled = enable;
  if (!enable) {
    // При выключении возвращаем аудио на активный VFO
    RADIO_SwitchAudioToVFO(state, state->multiwatch.active_vfo_index);
  }
}

// Обновление маршрутизации аудио
void RADIO_UpdateAudioRouting(RadioState *state) {
  if (!state->audio_routing_enabled)
    return;

  // Если текущий VFO - вещательный приемник, оставляем аудио на нем
  if (isBroadcastReceiver(
          &state->vfos[state->multiwatch.active_vfo_index].context)) {
    return;
  }

  // Проверяем активность на других VFO
  for (uint8_t i = 0; i < state->num_vfos; i++) {
    if (i == state->multiwatch.active_vfo_index)
      continue;

    ExtendedVFOContext *vfo = &state->vfos[i];

    // Для вещательных приемников не переключаем аудио автоматически
    if (isBroadcastReceiver(&vfo->context))
      continue;

    bool has_activity = RADIO_CheckSquelch(&vfo->context);

    if (has_activity) {
      // Если нашли активность и это не текущий VFO - переключаем аудио
      if (state->last_active_vfo != i) {
        RADIO_SwitchAudioToVFO(state, i);
        state->last_active_vfo = i;
      }
      return;
    }
  }

  // Если активность не обнаружена - возвращаем аудио на основной VFO
  if (state->last_active_vfo != state->multiwatch.active_vfo_index) {
    RADIO_SwitchAudioToVFO(state, state->multiwatch.active_vfo_index);
    state->last_active_vfo = state->multiwatch.active_vfo_index;
  }
}

static const char *RADIO_GetModulationName(VFOContext *ctx) {
  if (ctx->radio_type == RADIO_BK4819) {
    return MOD_NAMES_BK4819[ctx->modulation];
  }
  if (ctx->radio_type == RADIO_SI4732) {
    return MOD_NAMES_SI47XX[ctx->modulation];
  }
  return "WFM";
}

const char *RADIO_GetParamValueString(VFOContext *ctx, ParamType param) {
  static char buf[16];
  uint32_t v = RADIO_GetParam(ctx, param);
  switch (param) {
  case PARAM_MODULATION:
    return RADIO_GetModulationName(ctx);
  case PARAM_TX_STATE:
    return TX_STATE_NAMES[ctx->tx_state.last_error];
  case PARAM_BANDWIDTH:
    if (ctx->radio_type == RADIO_BK4819) {
      return BW_NAMES_BK4819[ctx->bandwidth];
    }
    if (ctx->radio_type == RADIO_SI4732) {
      if (RADIO_IsSSB(ctx)) {
        return BW_NAMES_SI47XX_SSB[ctx->bandwidth];
      }
      return BW_NAMES_SI47XX[ctx->bandwidth];
    }
    return "?(WIP)";
  case PARAM_STEP:
    snprintf(buf, 15, "%d.%02d", StepFrequencyTable[ctx->step] / KHZ,
             StepFrequencyTable[ctx->step] % KHZ);
    break;
  case PARAM_FREQUENCY:
    snprintf(buf, 15, "%u.%05u", ctx->frequency / MHZ, ctx->frequency % MHZ);
    break;
  case PARAM_POWER:
    return TX_POWER_NAMES[ctx->power];
  }
  return buf;
}

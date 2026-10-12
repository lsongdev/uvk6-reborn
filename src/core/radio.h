#ifndef RADIO_H
#define RADIO_H

#include "drivers/bk4819/bk4819.h"
#include "core/radio-model.h"
#include "core/channels.h"
#include "core/lootlist.h"
#include <stdbool.h>
#include <stdint.h>

#define SQL_DELAY 50
#define GARBAGE_FREQUENCY_MOD (13 * MHZ)

#define MAX_VFOS 1

extern const char *PARAM_NAMES[];
extern const char *TX_STATE_NAMES[7];
extern const char *FLT_BOUND_NAMES[2];
extern const char *BW_NAMES_BK4819[10];
extern const char *BW_NAMES_SI47XX[7];
extern const char *BW_NAMES_SI47XX_SSB[6];
extern const char *SQ_TYPE_NAMES[4];
extern const char *MOD_NAMES_BK4819[8];
extern const char *RADIO_NAMES[3];

extern const uint16_t StepFrequencyTable[15];

extern bool gShowAllRSSI;
extern bool gMonitorMode;

// Параметры
typedef const enum {
  PARAM_RADIO,
  PARAM_PRECISE_F_CHANGE,
  PARAM_STEP,
  PARAM_POWER,
  PARAM_MODULATION,
  PARAM_SQUELCH_TYPE,
  PARAM_SQUELCH_VALUE,
  PARAM_GAIN,
  PARAM_VOLUME,
  PARAM_BANDWIDTH,

  PARAM_TX_OFFSET,
  PARAM_TX_OFFSET_DIR,
  PARAM_TX_STATE,
  PARAM_TX_FREQUENCY,
  PARAM_TX_FREQUENCY_FACT,
  PARAM_TX_POWER,
  PARAM_TX_POWER_AMPLIFIER,

  PARAM_RX_CODE,
  PARAM_TX_CODE,

  PARAM_AFC,
  PARAM_AFC_SPD,
  PARAM_DEV,
  PARAM_MIC,
  PARAM_XTAL,
  PARAM_FILTER,

  PARAM_RSSI,
  PARAM_NOISE,
  PARAM_GLITCH,
  PARAM_SNR,

  // IMPORTANT FOR SI47xx (know modulation before f set)
  PARAM_FREQUENCY,

  PARAM_COUNT,
} ParamType;

typedef enum {
  TX_UNKNOWN,
  TX_ON,
  TX_VOL_HIGH,
  TX_BAT_LOW,
  TX_DISABLED,
  TX_DISABLED_UPCONVERTER,
  TX_POW_OVERDRIVE,
} TXStatus;

// Диапазон частот и его параметры
typedef struct {
  uint32_t min_freq;
  uint32_t max_freq;
  uint16_t available_bandwidths[10]; // Доступные полосы (кГц)
  uint8_t available_mods[5];         // Доступные модуляции
  uint8_t num_available_mods;
  uint8_t num_available_bandwidths;
} FreqBand;

// Контекст VFO
typedef struct {

  struct {
    uint32_t frequency; // Частота передачи (может отличаться от RX)
    uint8_t power_level; // Уровень мощности
    TXStatus last_error;
    ModulationType modulation; // Модуляция TX
    Code code;
    OffsetDirection offsetDirection;
    bool dirty;     // Флаг изменения параметров TX
    bool is_active; // true, если идёт передача
    bool pa_enabled;
  } tx_state;

  char name[10];
  bool dirty[PARAM_COUNT]; // Флаги изменений

  const FreqBand *current_band; // Активный диапазон
  uint32_t last_save_time; // Время последнего сохранения
  uint32_t frequency;      // Текущая частота
  uint16_t bandwidth;      // Полоса пропускания
  uint16_t dev;
  uint8_t modulation_index;
  uint8_t bandwidth_index;
  uint8_t volume; // Громкость
  uint8_t gain;
  uint8_t afc;
  uint8_t afc_speed;
  uint8_t mic;
  Code code;
  Step step;
  Radio radio_type;
  ModulationType modulation; // Текущая модуляция
  XtalMode xtal;
  Filter filter;
  TXOutputPower power;
  Squelch squelch;
  bool preciseFChange;
  bool fixed_bounds;
  bool tx_allowed;

  bool save_to_eeprom; // Флаг необходимости сохранения в EEPROM
} VFOContext;

// Channel/VFO mode
typedef enum { MODE_VFO, MODE_CHANNEL } VFOMode;

// Extended VFO context
typedef struct {
  uint32_t last_activity_time; // for multiwatch
  uint16_t channel_index;      // Channel index if in channel mode
  uint16_t vfo_ch_index;       // MR index of VFO
  Measurement msm;             // TODO: implement
  VFOContext context;          // Existing VFO context
  VFOMode mode;                // VFO or channel mode
  bool is_active;              // Whether this is the active VFO
  bool is_open;
} ExtendedVFOContext;

// Single receiver, temporarily using the old VFO storage structure.
typedef struct {
  ExtendedVFOContext vfos[MAX_VFOS];
  uint8_t num_vfos;
  uint8_t active_vfo_index;
} RadioState;

// Single receiver. The persisted radio state is defined by radio-state.h.
void RADIO_InitState(RadioState *state, uint8_t num_vfos);
ReceiverCapabilities RADIO_GetReceiverCapabilities(void);
bool RADIO_ApplyRxPlan(VFOContext *context, const RxPlan *plan);
RadioParameters RADIO_ReadParameters(const VFOContext *context);

// Инициализация
void RADIO_Init(VFOContext *ctx, Radio radio_type);

// Установка параметра
void RADIO_SetParam(VFOContext *ctx, ParamType param, uint32_t value,
                    bool save_to_eeprom);


// Применение настроек
void RADIO_ApplySettings(VFOContext *ctx);

// Проверка поддержки параметра в текущем диапазоне
bool RADIO_IsParamValid(VFOContext *ctx, ParamType param, uint32_t value);

uint32_t RADIO_GetParam(const VFOContext *ctx, ParamType param);
bool RADIO_AdjustParam(VFOContext *ctx, ParamType param, uint32_t inc,
                       bool save_to_eeprom);
bool RADIO_IncDecParam(VFOContext *ctx, ParamType param, bool inc,
                       bool save_to_eeprom);
void RADIO_LoadVFOs(RadioState *state);

void RADIO_ToggleTX(VFOContext *ctx, bool on);
bool RADIO_IsSSB(const VFOContext *ctx);
const char *RADIO_GetParamValueString(const VFOContext *ctx, ParamType param);

uint8_t RADIO_GetCurrentVFONumber(const RadioState *state);
ExtendedVFOContext *RADIO_GetCurrentVFO(RadioState *state);
const ExtendedVFOContext *RADIO_GetCurrentVFOConst(const RadioState *state);

void RADIO_SwitchAudioToVFO(RadioState *state, uint8_t vfo_index);
// Output-only operations: squelch and unsupported tuning must not reinitialize RX.
void RADIO_SetAudioOpen(bool open);
void RADIO_StopReception(void);
void RADIO_UpdateSquelch(RadioState *state);

uint16_t RADIO_GetRSSI(const VFOContext *ctx);
uint8_t RADIO_GetSNR(const VFOContext *ctx);
uint8_t RADIO_GetNoise(const VFOContext *ctx);
uint8_t RADIO_GetGlitch(const VFOContext *ctx);

void RADIO_FastSquelchUpdate();
void RADIO_SlowRSSIUpdate();

extern RadioState *gRadioState;
extern ExtendedVFOContext *vfo;
extern VFOContext *ctx;

#endif // RADIO_H

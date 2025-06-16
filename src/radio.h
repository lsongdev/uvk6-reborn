#ifndef RADIO_H
#define RADIO_H

#include "driver/bk4819.h"
#include "helper/channels.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_VFOS 16
extern bool gShowAllRSSI;
extern bool gMonitorMode;

// Параметры
typedef enum {
  PARAM_FREQUENCY,
  PARAM_MODULATION,
  PARAM_SQUELCH,
  PARAM_VOLUME,
  PARAM_BANDWIDTH,

  PARAM_COUNT
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
  uint8_t available_mods[5];         // Доступные модуляции
  uint16_t available_bandwidths[10]; // Доступные полосы (кГц)
} FreqBand;

// Контекст VFO
typedef struct {
  Radio radio_type;
  uint32_t frequency;           // Текущая частота
  ModulationType modulation;    // Текущая модуляция
  uint8_t volume;               // Громкость
  uint16_t bandwidth;           // Полоса пропускания
  const FreqBand *current_band; // Активный диапазон
  bool dirty[PARAM_COUNT];      // Флаги изменений
  Code code;
  Squelch squelch;
  struct {
    bool is_active; // true, если идёт передача
    TXStatus last_error;
    uint32_t frequency; // Частота передачи (может отличаться от RX)
    ModulationType modulation; // Модуляция TX
    uint8_t power_level;       // Уровень мощности
    bool dirty; // Флаг изменения параметров TX
    Code code;
    bool pa_enabled;
  } tx_state;
} VFOContext;

// Channel/VFO mode
typedef enum { MODE_VFO, MODE_CHANNEL } VFOMode;

// Multiwatch context
typedef struct {
  uint8_t active_vfo_index;  // Currently active VFO
  uint8_t num_vfos;          // Number of VFOs in use
  uint32_t scan_interval_ms; // How often to check other VFOs
  uint32_t last_scan_time;   // Last scan time
  bool enabled;              // Whether multiwatch is enabled
} MultiwatchContext;

// Extended VFO context
typedef struct {
  VFOContext context;     // Existing VFO context
  VFOMode mode;           // VFO or channel mode
  uint16_t channel_index; // Channel index if in channel mode
  bool is_active;         // Whether this is the active VFO
} ExtendedVFOContext;

// Global radio state
typedef struct {
  ExtendedVFOContext vfos[MAX_VFOS]; // Array of VFOs
  MultiwatchContext multiwatch;
  uint8_t num_vfos; // Number of configured VFOs
  bool audio_routing_enabled; // Флаг управления аудио маршрутизацией
  uint8_t last_active_vfo; // Последний активный VFO с активностью
} RadioState;

// New functions for multi-VFO and multiwatch support
void RADIO_InitState(RadioState *state, uint8_t num_vfos);
bool RADIO_SwitchVFO(RadioState *state, uint8_t vfo_index);
void RADIO_LoadVFOFromStorage(RadioState *state, uint8_t vfo_index,
                              const VFO *storage);
void RADIO_SaveVFOToStorage(const RadioState *state, uint8_t vfo_index,
                            VFO *storage);
void RADIO_LoadChannelToVFO(RadioState *state, uint8_t vfo_index,
                            uint16_t channel_index);
void RADIO_ToggleMultiwatch(RadioState *state, bool enable);
void RADIO_UpdateMultiwatch(RadioState *state);

// Инициализация
void RADIO_Init(VFOContext *ctx, Radio radio_type);

// Установка параметра
void RADIO_SetParam(VFOContext *ctx, ParamType param, uint32_t value);

// Применение настроек
void RADIO_ApplySettings(VFOContext *ctx);

// Проверка поддержки параметра в текущем диапазоне
bool RADIO_IsParamValid(VFOContext *ctx, ParamType param, uint32_t value);

uint32_t RADIO_GetParam(VFOContext *ctx, ParamType param);
bool RADIO_AdjustParam(VFOContext *ctx, ParamType param, uint32_t inc);
bool RADIO_IncDecParam(VFOContext *ctx, ParamType param, bool inc);
void RADIO_LoadVFOs(RadioState *state);

void RADIO_EnableAudioRouting(RadioState *state, bool enable);
void RADIO_UpdateAudioRouting(RadioState *state);

#endif // RADIO_H

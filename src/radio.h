#ifndef RADIO_H
#define RADIO_H

#include "driver/bk4819.h"
#include "helper/channels.h"
#include <stdbool.h>
#include <stdint.h>

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

#endif // RADIO_H

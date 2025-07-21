#include "scan.h"
#include "../apps/apps.h"
#include "../driver/st7565.h"
#include "../driver/system.h"
#include "../driver/systick.h"
#include "../radio.h"
#include "../scheduler.h"
#include "../ui/spectrum.h"
#include "bands.h"

// =============================
// Состояние сканирования
// =============================
typedef struct {
  uint32_t scanDelayUs; // Задержка измерения (микросек)
  uint16_t squelchLevel; // Текущий уровень шумоподавления
  bool thinking;         // Думоем
  bool wasThinkingEarlier; // Флаг для корректировки squelch
  bool lastListenState;    // Последнее состояние squelch
  bool isMultiband;        // Мультидиапазонный режим
  uint32_t stayAtTimeout; // Таймаут удержания на частоте
  uint32_t scanListenTimeout; // Таймаут прослушивания
  uint32_t scanCycles; // Количество циклов сканирования
  uint32_t lastCpsTime;    // Последнее время замера CPS
  uint32_t lastRenderTime; // Последнее время отрисовки
} ScanState;

static ScanState scan = {
    .scanDelayUs = 1200,
    .squelchLevel = 0,
    .thinking = false,
    .wasThinkingEarlier = false,
    .lastListenState = false,
    .isMultiband = false,
    .stayAtTimeout = 0,
    .scanListenTimeout = 0,
    .scanCycles = 0,
    .lastCpsTime = 0,
    .lastRenderTime = 0,
};

// =============================
// Вспомогательные функции
// =============================
static inline VFOContext *GetVFOContext() {
  return &RADIO_GetCurrentVFO(&gRadioState)->context;
}

static inline ExtendedVFOContext *GetVFO() {
  return RADIO_GetCurrentVFO(&gRadioState);
}

static uint16_t MeasureSignal(uint32_t frequency, bool precise) {
  VFOContext *ctx = GetVFOContext();
  RADIO_SetParam(ctx, PARAM_PRECISE_F_CHANGE, precise, false);
  RADIO_SetParam(ctx, PARAM_FREQUENCY, frequency, false);
  RADIO_ApplySettings(ctx);
  SYSTICK_DelayUs(scan.scanDelayUs);
  return RADIO_GetParam(ctx, PARAM_RSSI);
}

static void ApplyBandSettings() {
  VFOContext *ctx = GetVFOContext();
  gLoot.f = gCurrentBand.rxF;
  RADIO_SetParam(ctx, PARAM_STEP, gCurrentBand.step, false);
  RADIO_ApplySettings(ctx);
  SP_Init(&gCurrentBand);
}

static void NextFrequency() {
  VFOContext *ctx = GetVFOContext();
  uint32_t step = StepFrequencyTable[RADIO_GetParam(ctx, PARAM_STEP)];
  gLoot.f += step;
  RADIO_GetCurrentVFO(&gRadioState)->is_open = false;
  RADIO_SwitchAudioToVFO(&gRadioState, gRadioState.active_vfo_index);

  if (gLoot.f > gCurrentBand.txF) {
    if (scan.isMultiband) {
      BANDS_SelectBandRelativeByScanlist(true);
      ApplyBandSettings();
    }
    gLoot.f = gCurrentBand.rxF;
    gRedrawScreen = true;
  }

  LOOT_Replace(&gLoot, gLoot.f);
  SetTimeout(&scan.scanListenTimeout, 0);
  SetTimeout(&scan.stayAtTimeout, 0);
  scan.scanCycles++;
}

static void NextWithTimeout() {
  ExtendedVFOContext *vfo = GetVFO();
  if (scan.lastListenState != vfo->is_open) {
    scan.lastListenState = vfo->is_open;

    if (vfo->is_open) {
      SetTimeout(&scan.scanListenTimeout,
                 SCAN_TIMEOUTS[gSettings.sqOpenedTimeout]);
      SetTimeout(&scan.stayAtTimeout, UINT32_MAX);
    } else {
      SetTimeout(&scan.stayAtTimeout, SCAN_TIMEOUTS[gSettings.sqClosedTimeout]);
    }
  }

  if ((CheckTimeout(&scan.scanListenTimeout) && vfo->is_open) ||
      CheckTimeout(&scan.stayAtTimeout)) {
    NextFrequency();
  }
}

// =============================
// API функций
// =============================
uint32_t SCAN_GetCps() {
  uint32_t cps = scan.scanCycles * 1000 / (Now() - scan.lastCpsTime);
  scan.lastCpsTime = Now();
  scan.scanCycles = 0;
  return cps;
}

void SCAN_setBand(Band b) {
  gCurrentBand = b;
  ApplyBandSettings();
}

void SCAN_setStartF(uint32_t f) {
  gCurrentBand.rxF = f;
  ApplyBandSettings();
}

void SCAN_setEndF(uint32_t f) {
  gCurrentBand.txF = f;
  ApplyBandSettings();
}

void SCAN_Next(bool up) { NextFrequency(); }

void SCAN_Init(bool multiband) {
  scan.isMultiband = multiband;
  gLoot.snr = 0;
  scan.lastCpsTime = Now();
  scan.scanCycles = 0;
  ApplyBandSettings();
}

// =============================
// Обработка сканирования
// =============================
static void HandleAnalyserMode() {
  gLoot.rssi = MeasureSignal(gLoot.f, false);
  SP_AddPoint(&gLoot);
  if (Now() - scan.lastRenderTime > 500) {
    gRedrawScreen = true;
    scan.lastRenderTime = Now();
  }
  NextFrequency();
}

static void UpdateSquelchAndRssi(bool isAnalyserMode) {
  ExtendedVFOContext *vfo = GetVFO();
  gLoot.rssi = MeasureSignal(gLoot.f, !isAnalyserMode);

  if (!scan.squelchLevel && gLoot.rssi) {
    scan.squelchLevel = gLoot.rssi - 1;
  }

  if (scan.squelchLevel > gLoot.rssi) {
    uint16_t perc = (scan.squelchLevel - gLoot.rssi) * 100 /
                    ((scan.squelchLevel + gLoot.rssi) / 2);
    if (perc >= 25) {
      scan.squelchLevel = gLoot.rssi - 1;
    }
  }

  gLoot.open = gLoot.rssi >= scan.squelchLevel;
  SP_AddPoint(&gLoot);

  if (gSettings.skipGarbageFrequencies &&
      (RADIO_GetParam(&vfo->context, PARAM_FREQUENCY) % 1300000 == 0)) {
    gLoot.open = false;
  }
}

void SCAN_Check(bool isAnalyserMode) {
  RADIO_UpdateMultiwatch(&gRadioState);
  RADIO_CheckAndSaveVFO(&gRadioState);

  ExtendedVFOContext *vfo = GetVFO();

  if (isAnalyserMode) {
    HandleAnalyserMode();
    return;
  }

  if (gLoot.open) {
    RADIO_UpdateSquelch(&gRadioState);
    gLoot.open = vfo->is_open;
    gRedrawScreen = true;
  } else {
    UpdateSquelchAndRssi(isAnalyserMode);
  }

  if (gLoot.open && !vfo->is_open) {
    scan.thinking = true;
    scan.wasThinkingEarlier = true;
    SYS_DelayMs(SQL_DELAY);
    RADIO_UpdateSquelch(&gRadioState);
    gLoot.open = vfo->is_open;
    scan.thinking = false;
    gRedrawScreen = true;
    if (!gLoot.open) {
      scan.squelchLevel++;
    }
  }

  LOOT_Update(&gLoot);

  if (vfo->is_open && !gLoot.open) {
    scan.squelchLevel = SP_GetNoiseFloor();
  }

  if (gLoot.open) {
    gRedrawScreen = true;
  }

  static uint8_t stepsPassed;
  if (!gLoot.open) {
    if (stepsPassed++ > 64) {
      stepsPassed = 0;
      gRedrawScreen = true;
      if (!scan.wasThinkingEarlier) {
        scan.squelchLevel--;
      }
      scan.wasThinkingEarlier = false;
    }
  }

  NextWithTimeout();
}

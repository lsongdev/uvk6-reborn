#include "scan.h"
#include "../apps/apps.h"
#include "../driver/st7565.h"
#include "../driver/system.h"
#include "../driver/systick.h"
#include "../radio.h"
#include "../scheduler.h"
#include "../ui/spectrum.h"
#include "bands.h"

uint32_t delay = 1200;
uint16_t sqLevel = 0;

static bool thinking = false;
static bool wasThinkingEarlier = false;

static uint32_t stay_at_timeout;
static uint32_t scan_listen_timeout;

static bool lastListenState = false;
static bool isMultiband = false;

static uint32_t scanCycles = 0;
static uint32_t lastCpsTime = 0;

static uint16_t measure(uint32_t f, bool precise) {
  VFOContext *ctx = &RADIO_GetCurrentVFO(&gRadioState)->context;
  RADIO_SetParam(ctx, PARAM_PRECISE_F_CHANGE, precise, false);
  RADIO_SetParam(ctx, PARAM_FREQUENCY, f, false);
  SYSTICK_DelayUs(delay);
  return RADIO_GetParam(ctx, PARAM_RSSI);
}

static void onNewBand() {
  VFOContext *ctx = &RADIO_GetCurrentVFO(&gRadioState)->context;
  gLoot.f = gCurrentBand.rxF;
  // RADIO_SetParam(ctx, PARAM_FREQUENCY, gCurrentBand.rxF, false);
  RADIO_SetParam(ctx, PARAM_STEP, gCurrentBand.step, false);
  SP_Init(&gCurrentBand);
}

uint32_t SCAN_GetCps() {
  uint32_t cps = scanCycles * 1000 / (Now() - lastCpsTime);
  lastCpsTime = Now();
  scanCycles = 0;
  return cps;
}

void SCAN_setBand(Band b) {
  gCurrentBand = b;
  onNewBand();
}

void SCAN_setStartF(uint32_t f) {
  gCurrentBand.rxF = f;
  onNewBand();
}

void SCAN_setEndF(uint32_t f) {
  gCurrentBand.txF = f;
  onNewBand();
}

static void next() {
  VFOContext *ctx = &RADIO_GetCurrentVFO(&gRadioState)->context;
  uint32_t step = StepFrequencyTable[RADIO_GetParam(ctx, PARAM_STEP)];
  gLoot.f += step;
  // RADIO_AdjustParam(ctx, PARAM_FREQUENCY, step, false);

  if (gLoot.f > gCurrentBand.txF) {
    if (isMultiband) {
      BANDS_SelectBandRelativeByScanlist(true);
      onNewBand();
    }
    gLoot.f = gCurrentBand.rxF;
    gRedrawScreen = true;
  }

  LOOT_Replace(&gLoot, gLoot.f);
  SetTimeout(&scan_listen_timeout, 0);
  SetTimeout(&stay_at_timeout, 0);
  scanCycles++;
}

static void nextWithTimeout() {
  ExtendedVFOContext *vfo = RADIO_GetCurrentVFO(&gRadioState);
  if (lastListenState != vfo->is_open) {
    lastListenState = vfo->is_open;

    if (vfo->is_open) {
      SetTimeout(&scan_listen_timeout,
                 SCAN_TIMEOUTS[gSettings.sqOpenedTimeout]);
      SetTimeout(&stay_at_timeout, UINT32_MAX);
    } else {
      SetTimeout(&stay_at_timeout, SCAN_TIMEOUTS[gSettings.sqClosedTimeout]);
    }
  }

  if (CheckTimeout(&scan_listen_timeout) && vfo->is_open) {
    next();
    return;
  }

  if (CheckTimeout(&stay_at_timeout)) {
    next();
    return;
  }
}

void SCAN_Next(bool up) { next(); }

void SCAN_Init(bool multiband) {
  isMultiband = multiband;
  gLoot.snr = 0;

  lastCpsTime = Now();
  scanCycles = 0;

  onNewBand();
}

static uint32_t lastRender;

void SCAN_Check(bool isAnalyserMode) {
  RADIO_UpdateMultiwatch(&gRadioState);
  RADIO_CheckAndSaveVFO(&gRadioState);

  ExtendedVFOContext *vfo = RADIO_GetCurrentVFO(&gRadioState);
  VFOContext *ctx = &vfo->context;
  if (isAnalyserMode) {
    // gLoot.f = RADIO_GetParam(ctx, PARAM_FREQUENCY);
    gLoot.rssi = measure(gLoot.f, !isAnalyserMode);
    SP_AddPoint(&gLoot);
    if (Now() - lastRender > 500) {
      gRedrawScreen = true;
      lastRender = Now();
    }
    next();
    return;
  }

  if (gLoot.open) {
    // gLoot.open = RADIO_IsSquelchOpen();
    RADIO_UpdateSquelch(&gRadioState);
    gRedrawScreen = true;
  } else {
    // gLoot.f = RADIO_GetParam(ctx, PARAM_FREQUENCY);
    gLoot.rssi = measure(gLoot.f, !isAnalyserMode);

    if (!sqLevel && gLoot.rssi) {
      sqLevel = gLoot.rssi - 1;
    }

    if (sqLevel > gLoot.rssi) {
      uint16_t perc =
          (sqLevel - gLoot.rssi) * 100 / ((sqLevel + gLoot.rssi) / 2);
      if (perc >= 25) {
        sqLevel = gLoot.rssi - 1;
      }
    }

    gLoot.open = gLoot.rssi >= sqLevel;

    SP_AddPoint(&gLoot);
  }

  if (gSettings.skipGarbageFrequencies &&
      (RADIO_GetParam(ctx, PARAM_FREQUENCY) % 1300000 == 0)) {
    gLoot.open = false;
  }

  // really good level?
  if (gLoot.open && !vfo->is_open) {
    thinking = true;
    wasThinkingEarlier = true;
    SYS_DelayMs(SQL_DELAY);
    gLoot.open = vfo->is_open;
    thinking = false;
    gRedrawScreen = true;
    if (!gLoot.open) {
      sqLevel++;
    }
  }

  LOOT_Update(&gLoot);

  // reset sql to noise floor when sql closed to check next freq better
  if (vfo->is_open && !gLoot.open) {
    sqLevel = SP_GetNoiseFloor();
  }
  RADIO_SwitchAudioToVFO(&gRadioState, gRadioState.active_vfo_index);

  if (gLoot.open) {
    gRedrawScreen = true;
  }

  static uint8_t stepsPassed;

  if (!gLoot.open) {
    if (stepsPassed++ > 64) {
      stepsPassed = 0;
      gRedrawScreen = true;
      if (!wasThinkingEarlier) {
        sqLevel--;
      }
      wasThinkingEarlier = false;
    }
  }

  nextWithTimeout();
}

#include "devices/tx-device.h"
#include "store/preferences.h"
#include "store/calibration.h"
#include "hal/storage.h"
#include "devices/audio.h"
#include "devices/led.h"
#include "hal/gpio.h"
#include "devices/rx-device.h"
#include "hal/system.h"
#include "hal/board.h"
#include "drivers/bk48x9/bk48x9.h"
#include "store/device-config.h"
#include "devices/battery.h"
#include "core/dcs.h"

// Set by the Makefile (ENABLE_TX); host tests and bare compiles stay RX-only.
#ifndef UVK6_ENABLE_TX
#define UVK6_ENABLE_TX 0
#endif

#if UVK6_ENABLE_TX
// Read only the board's factory PA records. Never synthesize a calibration
// from another board's defaults or apply reference firmware power boosts.
static bool prepared;
static uint8_t paBias;
static uint32_t txFrequency;
static RadioTone txTone;

static __attribute__((noinline)) bool readCalibration(uint32_t hz, uint8_t values[9],
                                                      BoardTxCalibration *record) {
  return BOARD_TxCalibration(hz, record) &&
    STORAGE_Read(STORAGE_CALIBRATION, record->offset, values, CALIBRATION_PA_BYTES);
}
static uint8_t calibrationMask(const uint8_t values[9]) {
  uint8_t mask = 0;
  for (uint8_t power=0; power<3; ++power) {
    const uint8_t *v = values + power * 3;
    if (v[0] && v[0]!=255 && v[1] && v[1]!=255 && v[2] && v[2]!=255)
      mask |= 1u << power;
  }
  return mask;
}
// Measured: sharing this across Settings and PTT saves 128 B with release LTO.
__attribute__((noinline)) TxCapabilities TX_DEVICE_Capabilities(uint32_t hz) {
  uint8_t values[9]; BoardTxCalibration record;
  return (TxCapabilities){.available=true,
    .ranges=BK48X9_FREQUENCY_RANGES,
    .power_mask=readCalibration(hz,values,&record) ? calibrationMask(values) : 0};
}
#else
TxCapabilities TX_DEVICE_Capabilities(uint32_t hz) {
  (void)hz;
  return (TxCapabilities){0};
}
#endif

bool TX_DEVICE_PttPressed(void) {
  return !GPIO_Read(GPIO_PTT);
}
bool TX_DEVICE_VoltageValid(void) {
  // Battery voltage is in 10 mV units; unknown readings also fail closed.
  return gBatteryVoltage >= 600u && gBatteryVoltage <= 900u;
}
void TX_DEVICE_Disable(void) {
  // PA gate is always the first hardware action, including repeated releases.
  BK48X9_ToggleGpioOut(BK48X9_GPIO1_PIN29_PA_ENABLE, false);
  BK48X9_WriteRegister(BK48X9_REG_36, 0);
  LED_SetRed(false);
#if UVK6_ENABLE_TX
  if (prepared) {
    BK48X9_EnterTxMute();
    BK48X9_Idle();
    BK48X9_ExitSubAu();
    RX_DEVICE_InvalidateBk();
  }
  prepared = false;
#endif
}
#if UVK6_ENABLE_TX
static uint8_t interpolate(uint8_t a, uint8_t b, uint32_t hz,
                           uint32_t low, uint32_t high) {
  if (hz <= low) return a;
  if (hz >= high) return b;
  uint32_t k = (hz-low)/10u, n = (high-low)/10u; // Descending curves allowed.
  return b >= a ? a + (b-a)*k/n : a - (a-b)*k/n;
}
bool TX_DEVICE_Prepare(const TxPlan *plan) {
  TX_DEVICE_Disable();
  if (!plan || !TONE_Valid(plan->tone) || (unsigned)plan->power > TX_POWER_HIGH ||
      !RADIO_MODEL_InRfRange(plan->frequency_hz,BK48X9_FREQUENCY_RANGES) ||
      !TX_DEVICE_PttPressed() || !TX_DEVICE_VoltageValid()) return false;
  uint8_t values[9]; BoardTxCalibration record;
  if (!readCalibration(plan->frequency_hz,values,&record) ||
      !(calibrationMask(values) & (1u << plan->power))) return false;
  const uint8_t *anchors = values + plan->power * 3;
  paBias = plan->frequency_hz <= record.middle_hz ?
    interpolate(anchors[0],anchors[1],plan->frequency_hz,record.lower_hz,record.middle_hz) :
    interpolate(anchors[1],anchors[2],plan->frequency_hz,record.middle_hz,record.upper_hz);
  // TX owns the antenna. A muted BK1080 may still be receiving; park all
  // receivers successfully before touching the shared RF path or voice DSP.
  if (!RX_DEVICE_StopAll()) return false;
  txTone = plan->tone;
  txFrequency = plan->frequency_hz / 10u;
  prepared = true; // All subsequent exit paths must stop DSP and invalidate RX.
  AUDIO_ToggleSpeaker(false);
  LED_SetGreen(false);
  BK48X9_ToggleGpioOut(BK48X9_GPIO0_PIN28_RX_ENABLE, false);
  RX_DEVICE_InvalidateBk();
  BK48X9_ConfigureVoiceTx(txFrequency, gDeviceConfig.preferences.mic,
                         gDeviceConfig.preferences.deviation * 10u, PREFERENCES_FilterBound());
  if (!plan->tone) BK48X9_ExitSubAu();
  else if (TONE_Type(plan->tone) == CODE_TYPE_CONTINUOUS_TONE)
    BK48X9_SetCTCSSFrequency(CTCSS_Options[TONE_Code(plan->tone)]);
  else BK48X9_SetCDCSSCodeWord(DCS_GetGolayCodeWord(TONE_Type(plan->tone),TONE_Code(plan->tone)));
  return true;
}
bool TX_DEVICE_Enable(void) {
  if (!prepared || !TX_DEVICE_PttPressed() || !TX_DEVICE_VoltageValid()) {
    TX_DEVICE_Disable(); return false;
  }
  BK48X9_PrepareTransmit(); // Voice DSP includes the microphone ADC (0xC1FE).
  SYS_DelayMs(10);
  BK48X9_SetupPowerAmplifier(paBias,txFrequency);
  if (!TX_DEVICE_PttPressed() || !TX_DEVICE_VoltageValid()) {
    TX_DEVICE_Disable(); return false;
  }
  BK48X9_ToggleGpioOut(BK48X9_GPIO1_PIN29_PA_ENABLE, true); // Last RF enable.
  LED_SetRed(true);
  return true; // Register commands sent; this is not an RF measurement.
}

bool TX_DEVICE_BeginTail(void) {
  if (!prepared || !txTone || !TX_DEVICE_VoltageValid()) return false;
  BK48X9_StartTxTail(TONE_Type(txTone) != CODE_TYPE_CONTINUOUS_TONE);
  return true;
}

#else
bool TX_DEVICE_BeginTail(void) { return false; }
bool TX_DEVICE_Prepare(const TxPlan *plan) {
  (void)plan;
  TX_DEVICE_Disable();
  return false;
}
bool TX_DEVICE_Enable(void) {
  TX_DEVICE_Disable();
  return false;
}
#endif

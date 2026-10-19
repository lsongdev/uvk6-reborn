#include "hal/tx-device.h"
#include "hal/eeprom.h"
#include "hal/eeprom-layout.h"
#include "hal/audio.h"
#include "hal/led.h"
#include "hal/gpio.h"
#include "hal/rx-device.h"
#include "hal/system.h"
#include "drivers/bk4819/bk4819.h"
#include "core/device-config.h"
#include "core/battery.h"
#include "core/dcs.h"

// Set by the Makefile (ENABLE_TX); host tests and bare compiles stay RX-only.
#ifndef UVK6_ENABLE_TX
#define UVK6_ENABLE_TX 0
#endif

#if UVK6_ENABLE_TX
// Factory calibration layout from DualTachyon's UV-K5 radio.c/frequencies.c.
// Seven 16-byte band records from PA_CAL_BASE, three anchors per
// Low/Medium/High level; tools/resources.py writes them. Our board policy
// uses records 2 and 5 only.
static bool prepared;
static uint8_t paBias;
static uint32_t txFrequency;

static __attribute__((noinline)) bool readCalibration(uint32_t hz, uint8_t values[9], uint32_t *low,
                            uint32_t *middle, uint32_t *high) {
  uint8_t band;
  if (hz >= 136000000u && hz < 174000000u) {
    band = 2; *low = 136000000u; *middle = 150000000u; *high = 173999990u;
  } else if (hz >= 400000000u && hz < 470000000u) {
    band = 5; *low = 400000000u; *middle = 435000000u; *high = 469999990u;
  } else return false;
  return !(hz % 10u) && EEPROM_ReadBuffer(PA_CAL_BASE + band * PA_CAL_STRIDE, values, PA_CAL_BYTES) == 0;
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
  uint8_t values[9]; uint32_t low, middle, high;
  return (TxCapabilities){.available=true,
    .power_mask=readCalibration(hz,values,&low,&middle,&high) ? calibrationMask(values) : 0};
}
TxPolicy TX_DEVICE_Policy(void) {
  return (TxPolicy){.authorized=true, .count=2,
    .ranges={{136000000u,174000000u},{400000000u,470000000u}}};
}
#else
TxCapabilities TX_DEVICE_Capabilities(uint32_t hz) {
  (void)hz;
  return (TxCapabilities){0};
}
TxPolicy TX_DEVICE_Policy(void) { return (TxPolicy){0}; }
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
  BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
  BK4819_WriteRegister(BK4819_REG_36, 0);
  LED_SetRed(false);
#if UVK6_ENABLE_TX
  if (prepared) {
    BK4819_EnterTxMute();
    BK4819_Idle();
    BK4819_ExitSubAu();
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
      !TX_DEVICE_PttPressed() || !TX_DEVICE_VoltageValid()) return false;
  uint8_t values[9]; uint32_t low, middle, high;
  if (!readCalibration(plan->frequency_hz,values,&low,&middle,&high) ||
      !(calibrationMask(values) & (1u << plan->power))) return false;
  const uint8_t *anchors = values + plan->power * 3;
  paBias = plan->frequency_hz <= middle ?
    interpolate(anchors[0],anchors[1],plan->frequency_hz,low,middle) :
    interpolate(anchors[1],anchors[2],plan->frequency_hz,middle,high);
  txFrequency = plan->frequency_hz / 10u;
  prepared = true; // All subsequent exit paths must stop DSP and invalidate RX.
  AUDIO_ToggleSpeaker(false);
  LED_SetGreen(false);
  BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);
  RX_DEVICE_InvalidateBk();
  BK4819_ConfigureVoiceTx(txFrequency, gDeviceConfig.preferences.mic,
                         gDeviceConfig.preferences.deviation * 10u);
  if (!plan->tone) BK4819_ExitSubAu();
  else if (TONE_Type(plan->tone) == CODE_TYPE_CONTINUOUS_TONE)
    BK4819_SetCTCSSFrequency(CTCSS_Options[TONE_Code(plan->tone)]);
  else BK4819_SetCDCSSCodeWord(DCS_GetGolayCodeWord(TONE_Type(plan->tone),TONE_Code(plan->tone)));
  return true;
}
bool TX_DEVICE_Enable(void) {
  if (!prepared || !TX_DEVICE_PttPressed() || !TX_DEVICE_VoltageValid()) return false;
  BK4819_PrepareTransmit(); // Voice DSP includes the microphone ADC (0xC1FE).
  SYS_DelayMs(10);
  BK4819_SetupPowerAmplifier(paBias,txFrequency);
  if (!TX_DEVICE_PttPressed() || !TX_DEVICE_VoltageValid()) {
    TX_DEVICE_Disable(); return false;
  }
  BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, true); // Last RF enable.
  LED_SetRed(true);
  return true; // Register commands sent; this is not an RF measurement.
}

#else
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

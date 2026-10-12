#include "hal/rx-device.h"
#include "hal/bk1080.h"
#include "hal/bk4819.h"
#include "hal/si473x.h"
#include "core/settings.h"

// Silence the previous RF chip when moving to another receiver. The audio
// output is already muted by RADIO_StopReception() before this entry point.
static void leave(RxDevice device) {
  switch (device) {
  case RX_DEVICE_BK:
    BK4819_Idle();
    break;
  case RX_DEVICE_SI:
    if (gSettings.si4732PowerOff)
      SI47XX_PowerDown();
    else
      (void)SI47XX_SetVolume(0);
    break;
  case RX_DEVICE_BROADCAST:
    BK1080_Mute(true);
    break;
  }
}

bool RX_DEVICE_Apply(const RxPlan *plan, RxDevice previous) {
  if (!plan || plan->frequency_hz < RADIO_MIN_HZ ||
      plan->frequency_hz > RADIO_MAX_HZ ||
      plan->frequency_hz % 10u || plan->parameters.tx_enabled ||
      (unsigned)plan->parameters.mode >= RX_MODE_COUNT)
    return false;

  const uint32_t frequency = plan->frequency_hz / 10u; // Drivers use 10 Hz.
  const RxMode mode = plan->parameters.mode;
  uint16_t bandwidth = 0;

  switch (plan->device) {
  case RX_DEVICE_BK:
    if (mode != RX_NFM && mode != RX_AM)
      return false;
    if (!BK4819_BandwidthFromHz(plan->parameters.bandwidth_hz, &bandwidth))
      return false;
    break;
  case RX_DEVICE_SI:
    if (mode == RX_NFM || (mode == RX_WFM &&
          (plan->frequency_hz < 64000000u || plan->frequency_hz > 108000000u)) ||
        (mode != RX_WFM && plan->frequency_hz > 30000000u))
      return false;
    if (mode != RX_WFM &&
        !SI47XX_BandwidthFromHz(plan->parameters.bandwidth_hz,
                               mode == RX_USB || mode == RX_LSB, &bandwidth))
      return false;
    break;
  case RX_DEVICE_BROADCAST:
    if (mode != RX_WFM || plan->frequency_hz < 64000000u ||
        plan->frequency_hz > 108000000u)
      return false;
    break;
  default:
    return false;
  }

  // Do not disturb the previous receiver for invalid mode/bandwidth requests.
  if (previous != plan->device)
    leave(previous);

  switch (plan->device) {
  case RX_DEVICE_BK:
    BK4819_RX_TurnOn();
    BK4819_SetModulation(mode == RX_NFM ? MOD_FM : MOD_AM);
    BK4819_SetFilterBandwidth(bandwidth);
    BK4819_SetAGC(mode == RX_NFM, AUTO_GAIN_INDEX);
    BK4819_SetAFC(8);
    BK4819_SetAFCSpeed(63);
    BK4819_Squelch(plan->parameters.squelch, gSettings.sqlOpenTime,
                   gSettings.sqlCloseTime);
    BK4819_SelectFilterEx(frequency < SETTINGS_GetFilterBound()
                            ? FILTER_VHF : FILTER_UHF);
    BK4819_TuneTo(frequency, true);
    return true; // BK serial register writes lack an ACK/failure API.
  case RX_DEVICE_SI: {
    SI47XX_MODE siMode = mode == RX_WFM ? SI47XX_FM :
                         mode == RX_AM ? SI47XX_AM :
                         mode == RX_LSB ? SI47XX_LSB : SI47XX_USB;
    bool ready = SI47XX_SwitchMode(siMode) && SI47XX_TuneTo(frequency);
    if (ready && mode != RX_WFM)
      ready = (mode == RX_LSB || mode == RX_USB)
                  ? SI47XX_SetSsbBandwidth(bandwidth)
                  : SI47XX_SetBandwidth(bandwidth, true);
    if (ready)
      ready = SI47XX_SetVolume(63);
    // Previous plan may be restored by rx-runtime; don't leave an aborted
    // SI plan audible while the other receiver is being configured.
    if (!ready)
      (void)SI47XX_SetVolume(0);
    return ready;
  }
  case RX_DEVICE_BROADCAST:
    if (!BK1080_Init(frequency, true)) {
      (void)BK1080_Mute(true);
      return false;
    }
    // An I2C NACK on unmute is not a successful reception.
    if (!BK1080_Mute(false)) {
      (void)BK1080_Mute(true);
      return false;
    }
    return true;
  }
  return false;
}

#include "hal/rx-device.h"
#include "drivers/bk1080/bk1080.h"
#include "drivers/bk4819/bk4819.h"
#include "drivers/si473x/si473x.h"
#include "core/settings.h"
#include <string.h>

// BK4819 has no write acknowledgement. Cache only the last *requested*
// configuration to skip redundant setup on same-chip frequency stepping.
// Invalidate before any full reconfiguration, including failed SI handoffs.
static bool bkCached;
static RxPlan bkPrevious;
static uint8_t bkSqlOpenTime, bkSqlCloseTime;

bool RX_DEVICE_ReadRDS(RdsPacket *packet, bool clear_fifo) {
  SI47XX_RdsStatus status;
  if (!packet || !SI47XX_ReadRDS(&status, clear_fifo)) return false;
  memcpy(packet->group.blocks, status.blocks, sizeof(status.blocks));
  memcpy(packet->group.errors, status.errors, sizeof(status.errors));
  packet->fifo_used = status.fifo_used;
  packet->synchronized = status.synchronized;
  packet->overflow = status.overflow;
  return true;
}

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
      plan->frequency_hz % 10u ||
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

  // A frequency-only BK step does not need to repeat AGC/squelch/
  // bandwidth setup. SQL timing changes invalidate the shortcut.
  bool fastBk = bkCached && previous == RX_DEVICE_BK &&
                plan->device == RX_DEVICE_BK &&
                bkPrevious.parameters.mode == mode &&
                bkPrevious.parameters.bandwidth_hz == plan->parameters.bandwidth_hz &&
                bkPrevious.parameters.squelch == plan->parameters.squelch &&
                bkSqlOpenTime == gSettings.sqlOpenTime &&
                bkSqlCloseTime == gSettings.sqlCloseTime;
  uint32_t oldBkFrequency = bkPrevious.frequency_hz;
  bkCached = false;
  if (previous != plan->device)
    leave(previous);

  switch (plan->device) {
  case RX_DEVICE_BK:
    if (!fastBk) {
      BK4819_RX_TurnOn();
      BK4819_SetModulation(mode == RX_NFM ? MOD_FM : MOD_AM);
      BK4819_SetFilterBandwidth(bandwidth);
      BK4819_SetAGC(mode == RX_NFM, AUTO_GAIN_INDEX);
      BK4819_SetAFC(8);
      BK4819_SetAFCSpeed(63);
      BK4819_Squelch(plan->parameters.squelch, gSettings.sqlOpenTime,
                     gSettings.sqlCloseTime);
    }
    if (!fastBk || ((oldBkFrequency / 10u < SETTINGS_GetFilterBound()) !=
                    (frequency < SETTINGS_GetFilterBound())))
      BK4819_SelectFilterEx(frequency < SETTINGS_GetFilterBound()
                              ? FILTER_VHF : FILTER_UHF);
    if (!fastBk || oldBkFrequency != plan->frequency_hz)
      BK4819_TuneTo(frequency, true);
    bkPrevious = *plan;
    bkSqlOpenTime = gSettings.sqlOpenTime;
    bkSqlCloseTime = gSettings.sqlCloseTime;
    bkCached = true;
    return true; // BK serial writes have no acknowledgement.
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

bool RX_DEVICE_SeekNative(const RxPlan *plan) {
  if (!plan || plan->device != RX_DEVICE_SI)
    return false;
  if (plan->parameters.mode == RX_WFM)
    return plan->parameters.step_hz == 50000 ||
           plan->parameters.step_hz == 100000 ||
           plan->parameters.step_hz == 200000;
  return plan->parameters.mode == RX_AM &&
         plan->parameters.step_hz >= 1000 &&
         plan->parameters.step_hz <= 10000 &&
         plan->parameters.step_hz % 1000 == 0;
}

bool RX_DEVICE_SeekStart(const RxPlan *plan, uint32_t lower_hz,
                         uint32_t upper_hz) {
  // There is no assumption that a Seek is a completed user tune.
  if (!RX_DEVICE_SeekNative(plan))
    return false;
  uint32_t unit = plan->parameters.mode == RX_WFM ? 10000u : 1000u;
  uint32_t bottom = ((lower_hz + unit-1)/unit)*unit;
  uint32_t top = (upper_hz / unit) * unit;
  if (top <= bottom || top >= 0x10000u * unit)
    return false;
  return SI47XX_SeekStart(bottom, top, plan->parameters.step_hz);
}

RxSeekResult RX_DEVICE_SeekPoll(RxDevice device, uint32_t *frequency_hz) {
  if (device != RX_DEVICE_SI)
    return RX_SEEK_ERROR;
  switch (SI47XX_SeekPoll(frequency_hz)) {
  case SI_SEEK_BUSY: return RX_SEEK_BUSY;
  case SI_SEEK_FOUND: return RX_SEEK_FOUND;
  case SI_SEEK_LIMIT: return RX_SEEK_LIMIT;
  default: return RX_SEEK_ERROR;
  }
}

void RX_DEVICE_SeekCancel(RxDevice device) {
  if (device == RX_DEVICE_SI)
    SI47XX_SeekCancel();
}

bool RX_DEVICE_SeekSignal(RxDevice device, bool *found) {
  if (!found) return false;
  switch (device) {
  case RX_DEVICE_BK:
    // The caller applies a temporary squelch >= 4 even if monitoring uses 0.
    *found = BK4819_IsSquelchOpen();
    return true;
  case RX_DEVICE_SI:
    if (!RSQ_GET()) return false;
    *found = rsqStatus.resp.RSSI >= 16 && rsqStatus.resp.SNR >= 7;
    return true;
  case RX_DEVICE_BROADCAST: {
    // BK1080's 64-76 and 76-108 MHz channel banks are discontinuous.
    // Frequency stepping handles the split without unsafe native seek wraps.
    uint16_t rssi = BK1080_ReadRegister(BK1080_REG_10);
    uint16_t snr = BK1080_ReadRegister(BK1080_REG_07);
    if (rssi == UINT16_MAX || snr == UINT16_MAX) return false;
    *found = (rssi & 0xffu) >= 24u && (snr & 0x0fu) >= 5u;
    return true;
  }
  }
  return false;
}

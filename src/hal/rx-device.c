#include "core/device-config.h"
#include "hal/rx-device.h"
#include "hal/audio.h"
#include "hal/system.h"
#include "drivers/bk1080/bk1080.h"
#include "drivers/bk4819/bk4819.h"
#include "drivers/si473x/si473x.h"
#include "core/settings.h"
#include "core/measurements.h"
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
// output is already muted by RECEIVER_Mute() before this entry point.
static void leave(RxDevice device) {
  switch (device) {
  case RX_DEVICE_BK:
    BK4819_Idle();
    break;
  case RX_DEVICE_SI:
    if (gDeviceConfig.preferences.si4732_power_off)
      SI47XX_PowerDown();
    else
      (void)SI47XX_SetVolume(0);
    break;
  case RX_DEVICE_BROADCAST:
    BK1080_Mute(true);
    break;
  }
}

void RX_DEVICE_InvalidateBk(void) {
  bkCached=false;
  BK4819_InvalidateRxMode();
}
void RX_DEVICE_CaptureStop(void) {
  BK4819_StopScan();
  RX_DEVICE_InvalidateBk();
}

void RX_DEVICE_CaptureStart(CaptureRange range, RxDevice previous) {
  leave(previous);
  RX_DEVICE_CaptureStop();
  // A previous SI/BK1080 reception may have left BK asleep. Establish an
  // RX-only baseline before enabling its autonomous frequency detector.
  BK4819_RX_TurnOn();
  BK4819_SetRxMode(false);
  BK4819_ExitSubAu();
  BK4819_SetAGC(AUTO_GAIN_INDEX);
  BK4819_SelectFilterEx(range==CAPTURE_RANGE_VHF ? FILTER_VHF : FILTER_UHF);
  BK4819_EnableFrequencyScanEx(F_SC_T_0_2s);
  // RX turn-on enables the AF DAC; FC never plays BK audio, and a broadcast
  // chip may be using the speaker meanwhile.
  BK4819_SetAF(BK4819_AF_MUTE);
  BK4819_ToggleAFDAC(false);
}

CaptureSample RX_DEVICE_CapturePoll(CaptureRange range, uint32_t *hz) {
  uint32_t raw;
  if (!BK4819_GetFrequencyScanResult(&raw)) return CAPTURE_BUSY;
  if (!hz || raw>RADIO_MAX_HZ/10u) return CAPTURE_INVALID;
  *hz=raw*10u;
  if (*hz<RADIO_MIN_HZ ||
      ((*hz/10u < SETTINGS_GetFilterBound()) != (range==CAPTURE_RANGE_VHF)))
    return CAPTURE_INVALID;
  return CAPTURE_SAMPLE;
}

#if UVK6_ENABLE_SPECTRUM
static bool spectrumVhf;
void RX_DEVICE_SpectrumStart(RxDevice previous) {
  leave(previous);
  RX_DEVICE_InvalidateBk();
  uint16_t bandwidth = 0;
  (void)BK4819_BandwidthFromHz(12000, &bandwidth);
  BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);
  BK4819_RX_TurnOn();
  BK4819_SetRxMode(false);
  BK4819_ExitSubAu();
  BK4819_SetFilterBandwidth(bandwidth);
  BK4819_SetAGC(AUTO_GAIN_INDEX);
  spectrumVhf = true;
  BK4819_SelectFilterEx(FILTER_VHF);
  BK4819_SetAF(BK4819_AF_MUTE);
  BK4819_ToggleAFDAC(false);
}
void RX_DEVICE_SpectrumTune(uint32_t frequency_hz) {
  uint32_t frequency = frequency_hz / 10u;
  bool vhf = frequency < SETTINGS_GetFilterBound();
  if (vhf != spectrumVhf) {
    BK4819_SelectFilterEx(vhf ? FILTER_VHF : FILTER_UHF);
    spectrumVhf = vhf;
  }
  BK4819_TuneTo(frequency, true);
}
uint16_t RX_DEVICE_SpectrumRssi(void) { return BK4819_GetRSSI(); }
void RX_DEVICE_SpectrumStop(void) { RX_DEVICE_InvalidateBk(); }
#endif

static bool applyPlan(const RxPlan *plan, RxDevice previous, bool audible) {
  if (!plan || plan->frequency_hz < RADIO_MIN_HZ ||
      plan->frequency_hz > RADIO_MAX_HZ ||
      plan->frequency_hz % 10u ||
      (unsigned)plan->parameters.mode >= RX_MODE_COUNT ||
      !TONE_Valid(plan->parameters.rx_tone) ||
      (plan->parameters.rx_tone && (plan->device != RX_DEVICE_BK || plan->parameters.mode != RX_NFM)))
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
                bkPrevious.parameters.rx_tone == plan->parameters.rx_tone &&
                bkSqlOpenTime == gDeviceConfig.preferences.sql_open_time &&
                bkSqlCloseTime == gDeviceConfig.preferences.sql_close_time;
  uint32_t oldBkFrequency = bkPrevious.frequency_hz;
  bkCached = false;
  if (previous != plan->device)
    leave(previous);

  switch (plan->device) {
  case RX_DEVICE_BK:
    if (!fastBk) {
      BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);
      BK4819_RX_TurnOn();
      BK4819_SetRxMode(mode == RX_AM);
      BK4819_SetFilterBandwidth(bandwidth);
      BK4819_SetAGC(AUTO_GAIN_INDEX);
      BK4819_SetAFC(8);
      BK4819_SetAFCSpeed(63);
      BK4819_Squelch(plan->parameters.squelch, gDeviceConfig.preferences.sql_open_time,
                     gDeviceConfig.preferences.sql_close_time);
    }
    if (!fastBk || ((oldBkFrequency / 10u < SETTINGS_GetFilterBound()) !=
                    (frequency < SETTINGS_GetFilterBound())))
      BK4819_SelectFilterEx(frequency < SETTINGS_GetFilterBound()
                              ? FILTER_VHF : FILTER_UHF);
    if (!fastBk || oldBkFrequency != plan->frequency_hz) {
      RadioTone tone = plan->parameters.rx_tone;
      if (!tone) {
        BK4819_ExitSubAu();
        BK4819_WriteRegister(BK4819_REG_3F, 0);
      } else {
        DCS_CodeType_t type = TONE_Type(tone);
        if (type == CODE_TYPE_CONTINUOUS_TONE)
          BK4819_SetCTCSSFrequency(CTCSS_Options[TONE_Code(tone)]);
        else
          BK4819_SetCDCSSCodeWord(DCS_GetGolayCodeWord(type, TONE_Code(tone)));
        BK4819_CssResetDetection(type != CODE_TYPE_CONTINUOUS_TONE);
      }
    }
    if (!fastBk || oldBkFrequency != plan->frequency_hz)
      BK4819_TuneTo(frequency, true);
    bkPrevious = *plan;
    bkSqlOpenTime = gDeviceConfig.preferences.sql_open_time;
    bkSqlCloseTime = gDeviceConfig.preferences.sql_close_time;
    bkCached = true;
    if (!audible) {
      BK4819_SetAF(BK4819_AF_MUTE);
      BK4819_ToggleAFDAC(false);
    }
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
      ready = SI47XX_SetVolume(audible ? 63 : 0);
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
    if (!BK1080_Mute(!audible)) {
      (void)BK1080_Mute(true);
      return false;
    }
    return true;
  }
  return false;
}

bool RX_DEVICE_Apply(const RxPlan *plan, RxDevice previous) {
  return applyPlan(plan,previous,true);
}
bool RX_DEVICE_ApplyConcurrent(const RxPlan *plan) {
  return plan && applyPlan(plan,plan->device,false);
}
void RX_DEVICE_StopChip(RxDevice device) {
  if (device==RX_DEVICE_BK) RX_DEVICE_InvalidateBk();
  leave(device);
}
void RX_DEVICE_SetAudio(RxDevice device, bool open) {
  bool bk = device == RX_DEVICE_BK;
  if (open) {
    if (bk) {
      BK4819_ToggleAFDAC(true);
      BK4819_ToggleAFBit(true);
    }
    SYS_DelayMs(8);
    AUDIO_ToggleSpeaker(true);
  } else {
    AUDIO_ToggleSpeaker(false);
    SYS_DelayMs(8);
    if (bk) {
      BK4819_ToggleAFDAC(false);
      BK4819_ToggleAFBit(false);
    }
  }
}

bool RX_DEVICE_Sample(const RxPlan *plan, uint16_t *rssi, bool *open) {
  *rssi = 0;
  *open = false;
  uint8_t snr;
  switch (plan->device) {
  case RX_DEVICE_BK: {
    *rssi = BK4819_GetRSSI();
    RadioTone tone = plan->parameters.rx_tone;
    *open = BK4819_IsSquelchOpen() &&
            (!tone || BK4819_CssIsDetected(TONE_Type(tone) != CODE_TYPE_CONTINUOUS_TONE));
    return true;
  }
  case RX_DEVICE_SI:
    if (!RSQ_GET()) return false;
    *rssi = rsqStatus.resp.RSSI;
    snr = rsqStatus.resp.SNR;
    break;
  case RX_DEVICE_BROADCAST:
    *rssi = BK1080_GetRSSI();
    snr = BK1080_GetSNR();
    break;
  default:
    return false;
  }
  *open = !plan->parameters.squelch || snr > plan->parameters.squelch;
  return true;
}

bool RX_DEVICE_SelectAudio(uint8_t devices, RxDevice selected, bool open) {
  // Break before make: never mix audio from two chips. Receiver DSP stays on.
  AUDIO_ToggleSpeaker(false);
  bool ok=true;
  if (devices&(1u<<RX_DEVICE_BK)) {
    BK4819_SetAF(BK4819_AF_MUTE);
    BK4819_ToggleAFDAC(false);
  }
  if (devices&(1u<<RX_DEVICE_SI)) ok=SI47XX_SetVolume(0) && ok;
  if (devices&(1u<<RX_DEVICE_BROADCAST)) ok=BK1080_Mute(true) && ok;
  if (!ok || !open) return ok;
  if (!(devices&(1u<<selected))) return false;
  switch(selected) {
  case RX_DEVICE_BK:
    BK4819_SetAF(BK4819_AF_FM);BK4819_ToggleAFDAC(true);break;
  case RX_DEVICE_SI: ok=SI47XX_SetVolume(63);break;
  case RX_DEVICE_BROADCAST: ok=BK1080_Mute(false);break;
  default: return false;
  }
  if (ok) AUDIO_ToggleSpeaker(true);
  return ok;
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

// Watch detection is independent from SQ0/Monitor audio. A noise floor prevents
// an always-open receiver monopolizing the pair; thresholds need RF validation.
bool RX_DEVICE_WatchSignal(const RxPlan *plan, bool *found) {
  if (!plan || !found) return false;
  *found=false;
  if (plan->device==RX_DEVICE_BK) {
    uint8_t level=plan->parameters.squelch;
    SQL floor=GetSql(level<4 ? 4 : level);
    bool carrier=BK4819_GetRSSI()>=floor.ro && BK4819_GetNoise()<=floor.no &&
                 BK4819_GetGlitch()<=floor.go;
    RadioTone tone=plan->parameters.rx_tone;
    bool matched=!tone || BK4819_CssIsDetected(TONE_Type(tone)!=CODE_TYPE_CONTINUOUS_TONE);
    *found=carrier && matched && (!level || BK4819_IsSquelchOpen());
    return true;
  }
  if (plan->device==RX_DEVICE_SI) {
    if (!RSQ_GET()) return false;
    uint8_t snr=plan->parameters.squelch<7 ? 7 : plan->parameters.squelch+1u;
    *found=rsqStatus.resp.RSSI>=16 && rsqStatus.resp.SNR>=snr;
    return true;
  }
  if (plan->device==RX_DEVICE_BROADCAST) {
    uint16_t rssi=BK1080_ReadRegister(BK1080_REG_10);
    uint16_t snr=BK1080_ReadRegister(BK1080_REG_07);
    if (rssi==UINT16_MAX || snr==UINT16_MAX) return false;
    uint8_t minimum=plan->parameters.squelch<5 ? 5 : plan->parameters.squelch;
    *found=(rssi&0xffu)>=24 && (snr&0xfu)>=minimum;
    return true;
  }
  return false;
}

uint8_t RX_DEVICE_SignalLevel(const RxPlan *plan, uint16_t rssi) {
  enum { SEGMENTS = 14 };
  uint8_t level = 0;
  if (plan->device == RX_DEVICE_BK) {
    if (rssi) {
      int16_t dBm = Rssi2DBm(rssi) + BK4819_GetAttenuation();
      level = DBm2S(dBm, plan->frequency_hz >= 300000000u);
    }
  } else {
    level = ConvertDomain(rssi, 0, 75, 0, SEGMENTS);
  }
  return level > SEGMENTS ? SEGMENTS : level;
}

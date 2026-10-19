#include "store/device-config.h"
#include "devices/rx-device.h"
#include "devices/audio.h"
#include "hal/system.h"
#include "hal/board.h"
#include "hal/features.h"
#include "drivers/bk1080/bk1080.h"
#include "drivers/bk48x9/bk48x9.h"
#include "drivers/si473x/si473x.h"
#include "store/preferences.h"
#include "core/measurements.h"
#include "devices/tx-device.h"
#include "store/resource.h"
#include "store/calibration.h"
#include "hal/storage.h"
#include <string.h>

// BK4819 has no write acknowledgement. Cache only the last *requested*
// configuration to skip redundant setup on same-chip frequency stepping.
// Invalidate before any full reconfiguration, including failed SI handoffs.
static bool bkCached;
static RxPlan bkPrevious;
static uint8_t bkSqlOpenTime, bkSqlCloseTime;
// Paths requested active, including a partially prepared path after an error.
// The audio mirror is separate: muting never releases a receiver's RF path.
static uint8_t runningDevices;
static RxDevice audioDevice;
static bool audioOpen;

ReceiverCapabilities RX_DEVICE_Capabilities(void) {
  static bool detected;
  static ReceiverCapabilities capabilities;
  if (!detected) {
    BK48X9Capabilities bk=BK48X9_GetCapabilities();
    capabilities.bk_am=bk.am;
    capabilities.bk_bandwidth_mask=bk.bandwidth_mask;
    capabilities.si=UVK6_ENABLE_SI4732;
    capabilities.broadcast=!UVK6_ENABLE_SI4732;
    capabilities.ssb_patch=UVK6_ENABLE_SI4732 && RESOURCE_SsbPatchPresent();
    if (UVK6_ENABLE_SI4732 && capabilities.ssb_patch)
      SI47XX_SetPatchSource(RESOURCE_SsbPatchRead,RESOURCE_SSB_PATCH_BYTES);
    detected = true;
  }
  return capabilities;
}

void RX_DEVICE_Init(void) {
  const ConfigPreferences *p = &gDeviceConfig.preferences;
  BK48X9_Init(p->mic, p->deviation);
  BoardRxCalibration calibration;
  if (BOARD_RxCalibration(&calibration))
    BK48X9_SetRxCalibration(calibration.trim,calibration.volume,calibration.dac);
  TX_DEVICE_Disable(); // Explicitly keep PA off on startup.
  BK48X9_RX_TurnOn();
  runningDevices |= 1u << RX_DEVICE_BK;
  if (!UVK6_ENABLE_SI4732) (void)BK1080_Standby();
}

bool RX_DEVICE_SiActive(void) { return UVK6_ENABLE_SI4732 && isSi4732On; }

bool RX_DEVICE_CanRunTogether(RxDevice a, RxDevice b) {
  if (a == b) return false;
  bool shared = (a == RX_DEVICE_BK && b == RX_DEVICE_BROADCAST) ||
                (b == RX_DEVICE_BK && a == RX_DEVICE_BROADCAST);
  return !shared || !BOARD_BroadcastUsesBkVhfPath();
}

bool RX_DEVICE_ReadRDS(RdsPacket *packet, bool clear_fifo) {
  SI47XX_RdsStatus status;
  if (!UVK6_ENABLE_SI4732 || !packet || !SI47XX_ReadRDS(&status, clear_fifo)) return false;
  memcpy(packet->group.blocks, status.blocks, sizeof(status.blocks));
  memcpy(packet->group.errors, status.errors, sizeof(status.errors));
  packet->fifo_used = status.fifo_used;
  packet->synchronized = status.synchronized;
  packet->overflow = status.overflow;
  return true;
}

// Silence the previous RF chip when moving to another receiver. The audio
// output is already muted by RECEIVER_Mute() before this entry point.
// BK4829 squelch levels come from the factory calibration like the
// reference firmware: from offset 0x60 below 174 MHz, else 0x00; six 16-byte rows
// (RSSI open, RSSI close, noise open, noise close, glitch close, glitch
// open), byte n for level n (1-9). The generic formula suits the BK4819
// scale only. Unreadable or erased calibration falls back to the formula.
static SQL squelchFor(uint8_t level,uint32_t hz) {
  BoardSquelchCalibration calibrated;
  if (!BOARD_SquelchCalibration(hz,level,&calibrated)) return GetSql(level);
  return (SQL){.ro=calibrated.rssi_open,.rc=calibrated.rssi_close,
    .no=calibrated.noise_open,.nc=calibrated.noise_close,
    .go=calibrated.glitch_open,.gc=calibrated.glitch_close};
}

bool RX_DEVICE_StopChip(RxDevice device) {
  if (!(runningDevices & (1u << device))) return true;
  bool ok = true;
  if (audioOpen && audioDevice == device) {
    AUDIO_ToggleSpeaker(false);
    audioOpen = false;
  }
  switch (device) {
  case RX_DEVICE_BK:
    RX_DEVICE_InvalidateBk();
    BK48X9_Idle();
    break;
  case RX_DEVICE_SI:
    if (!UVK6_ENABLE_SI4732) return false;
    if (gDeviceConfig.preferences.si4732_power_off) SI47XX_PowerDown();
    else ok = SI47XX_SetVolume(0);
    break;
  case RX_DEVICE_BROADCAST:
    if (UVK6_ENABLE_SI4732) return false;
    ok = BK1080_Standby();
    break;
  }
  if (ok) runningDevices &= ~(1u << device);
  return ok;
}

void RX_DEVICE_InvalidateBk(void) {
  bkCached=false;
  BK48X9_InvalidateRxMode();
}
bool RX_DEVICE_StopAll(void) {
  AUDIO_ToggleSpeaker(false);
  audioOpen = false;
  bool ok = true;
  for (uint8_t d=0; d<=RX_DEVICE_BROADCAST; ++d)
    if (!RX_DEVICE_StopChip((RxDevice)d)) ok = false;
  return ok;
}
void RX_DEVICE_CaptureStop(void) {
  BK48X9_StopScan();
  RX_DEVICE_InvalidateBk();
  runningDevices &= ~(1u << RX_DEVICE_BK);
}

bool RX_DEVICE_CaptureStart(CaptureRange range) {
  for (uint8_t d=0; d<=RX_DEVICE_BROADCAST; ++d)
    if (!RX_DEVICE_CanRunTogether(RX_DEVICE_BK,(RxDevice)d) &&
        !RX_DEVICE_StopChip((RxDevice)d)) return false;
  bool restore = audioOpen;
  RxDevice previousAudio = audioDevice;
  AUDIO_ToggleSpeaker(false);
  audioOpen = false;
  RX_DEVICE_CaptureStop();
  runningDevices |= 1u << RX_DEVICE_BK;
  // A previous SI/BK1080 reception may have left BK asleep. Establish an
  // RX-only baseline before enabling its autonomous frequency detector.
  BK48X9_RX_TurnOn();
  BK48X9_SetRxMode(false);
  BK48X9_ExitSubAu();
  BK48X9_SetAGC(AUTO_GAIN_INDEX);
  BK48X9_SelectFilterEx(range==CAPTURE_RANGE_VHF ? FILTER_VHF : FILTER_UHF);
  BK48X9_EnableFrequencyScanEx(F_SC_T_0_2s);
  // RX turn-on enables the AF DAC; FC never plays BK audio, and a broadcast
  // chip may be using the speaker meanwhile.
  BK48X9_SetAF(BK48X9_AF_MUTE);
  BK48X9_ToggleAFDAC(false);
  return !restore || RX_DEVICE_RouteAudio(previousAudio,true);
}

CaptureSample RX_DEVICE_CapturePoll(CaptureRange range, uint32_t *hz) {
  uint32_t raw;
  if (!BK48X9_GetFrequencyScanResult(&raw)) return CAPTURE_BUSY;
  if (!hz || raw>RADIO_MAX_HZ/10u) return CAPTURE_INVALID;
  *hz=raw*10u;
  if (*hz<RADIO_MIN_HZ ||
      ((*hz/10u < PREFERENCES_FilterBound()) != (range==CAPTURE_RANGE_VHF)))
    return CAPTURE_INVALID;
  return CAPTURE_SAMPLE;
}

#if UVK6_ENABLE_SPECTRUM
static bool spectrumVhf;
bool RX_DEVICE_SpectrumStart(void) {
  if (!RX_DEVICE_StopAll()) return false;
  RX_DEVICE_InvalidateBk();
  runningDevices |= 1u << RX_DEVICE_BK;
  uint16_t bandwidth = 0;
  (void)BK48X9_BandwidthFromHz(12000, &bandwidth);
  BK48X9_ToggleGpioOut(BK48X9_GPIO0_PIN28_RX_ENABLE, true);
  BK48X9_RX_TurnOn();
  BK48X9_SetRxMode(false);
  BK48X9_ExitSubAu();
  BK48X9_SetFilterBandwidth(bandwidth);
  BK48X9_SetAGC(AUTO_GAIN_INDEX);
  spectrumVhf = true;
  BK48X9_SelectFilterEx(FILTER_VHF);
  BK48X9_SetAF(BK48X9_AF_MUTE);
  BK48X9_ToggleAFDAC(false);
  return true;
}
void RX_DEVICE_SpectrumTune(uint32_t frequency_hz) {
  uint32_t frequency = frequency_hz / 10u;
  bool vhf = frequency < PREFERENCES_FilterBound();
  if (vhf != spectrumVhf) {
    BK48X9_SelectFilterEx(vhf ? FILTER_VHF : FILTER_UHF);
    spectrumVhf = vhf;
  }
  BK48X9_TuneTo(frequency, true);
}
uint16_t RX_DEVICE_SpectrumRssi(void) { return BK48X9_GetRSSI(); }
void RX_DEVICE_SpectrumStop(void) { RX_DEVICE_InvalidateBk(); }
#endif

// "Auto" squelch delays use the chip's own recommendation.
static uint8_t sqlDelay(uint8_t preference, uint8_t automatic, uint8_t recommended) {
  return preference >= automatic ? recommended : preference;
}

static bool preparePlan(const RxPlan *plan, bool foreground) {
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
    if ((mode == RX_AM && !BK48X9_GetCapabilities().am) ||
        (mode != RX_NFM && mode != RX_AM))
      return false;
    if (!BK48X9_BandwidthFromHz(plan->parameters.bandwidth_hz, &bandwidth))
      return false;
    break;
  case RX_DEVICE_SI:
    if (!UVK6_ENABLE_SI4732 || mode == RX_NFM || (mode == RX_WFM &&
          (plan->frequency_hz < 64000000u || plan->frequency_hz > 108000000u)) ||
        (mode != RX_WFM && plan->frequency_hz > 30000000u))
      return false;
    if (mode != RX_WFM &&
        !SI47XX_BandwidthFromHz(plan->parameters.bandwidth_hz,
                               mode == RX_USB || mode == RX_LSB, &bandwidth))
      return false;
    break;
  case RX_DEVICE_BROADCAST:
    if (UVK6_ENABLE_SI4732) return false;
    if (mode != RX_WFM || plan->frequency_hz < 64000000u ||
        plan->frequency_hz > 108000000u)
      return false;
    break;
  default:
    return false;
  }

  // Validate first. Only foreground selection may evict another path.
  if (!foreground) {
    for (uint8_t d=0; d<=RX_DEVICE_BROADCAST; ++d)
      if ((runningDevices & (1u << d)) && d != plan->device &&
          !RX_DEVICE_CanRunTogether((RxDevice)d, plan->device)) return false;
  }
  bool keepAudio = !foreground && plan->device == RX_DEVICE_BK &&
                   audioOpen && audioDevice != RX_DEVICE_BK;
  if (!keepAudio) {
    AUDIO_ToggleSpeaker(false);
    audioOpen = false;
  }
  if (foreground) {
    for (uint8_t d=0; d<=RX_DEVICE_BROADCAST; ++d)
      if (d != plan->device && !RX_DEVICE_StopChip((RxDevice)d)) return false;
  }

  // A frequency-only BK step does not need to repeat AGC/squelch/
  // bandwidth setup. SQL timing changes invalidate the shortcut.
  bool fastBk = bkCached && (runningDevices & (1u << RX_DEVICE_BK)) &&
                plan->device == RX_DEVICE_BK &&
                bkPrevious.parameters.mode == mode &&
                bkPrevious.parameters.bandwidth_hz == plan->parameters.bandwidth_hz &&
                bkPrevious.parameters.squelch == plan->parameters.squelch &&
                bkPrevious.parameters.rx_tone == plan->parameters.rx_tone &&
                (BOARD_RxCalibrationBand(bkPrevious.frequency_hz) == BOARD_RxCalibrationBand(plan->frequency_hz)) &&
                bkSqlOpenTime == gDeviceConfig.preferences.sql_open_time &&
                bkSqlCloseTime == gDeviceConfig.preferences.sql_close_time;
  uint32_t oldBkFrequency = bkPrevious.frequency_hz;
  if (plan->device == RX_DEVICE_BK) bkCached = false;
  runningDevices |= 1u << plan->device;

  switch (plan->device) {
  case RX_DEVICE_BK:
    if (!fastBk) {
      BK48X9_ToggleGpioOut(BK48X9_GPIO0_PIN28_RX_ENABLE, true);
      BK48X9_RX_TurnOn();
      BK48X9_SetRxMode(mode == RX_AM);
      BK48X9_SetFilterBandwidth(bandwidth);
      BK48X9_SetAGC(AUTO_GAIN_INDEX);
      BK48X9_SetAFC(8);
      BK48X9_SetAFCSpeed(63);
      BK48X9_SetupSquelch(squelchFor(plan->parameters.squelch, plan->frequency_hz),
                          sqlDelay(gDeviceConfig.preferences.sql_open_time,
                                   PREFERENCES_SQL_OPEN_AUTO, BK48X9_GetCapabilities().squelch_open_delay),
                          sqlDelay(gDeviceConfig.preferences.sql_close_time,
                                   PREFERENCES_SQL_CLOSE_AUTO, BK48X9_GetCapabilities().squelch_close_delay));
    }
    if (!fastBk || ((oldBkFrequency / 10u < PREFERENCES_FilterBound()) !=
                    (frequency < PREFERENCES_FilterBound())))
      BK48X9_SelectFilterEx(frequency < PREFERENCES_FilterBound()
                              ? FILTER_VHF : FILTER_UHF);
    if (!fastBk || oldBkFrequency != plan->frequency_hz) {
      RadioTone tone = plan->parameters.rx_tone;
      if (!tone) {
        BK48X9_ExitSubAu();
        BK48X9_CssOff();
      } else {
        DCS_CodeType_t type = TONE_Type(tone);
        if (type == CODE_TYPE_CONTINUOUS_TONE)
          BK48X9_SetCTCSSFrequency(CTCSS_Options[TONE_Code(tone)]);
        else
          BK48X9_SetCDCSSCodeWord(DCS_GetGolayCodeWord(type, TONE_Code(tone)));
        BK48X9_CssResetDetection(type != CODE_TYPE_CONTINUOUS_TONE);
      }
    }
    if (!fastBk || oldBkFrequency != plan->frequency_hz)
      BK48X9_TuneTo(frequency, true);
    bkPrevious = *plan;
    bkSqlOpenTime = gDeviceConfig.preferences.sql_open_time;
    bkSqlCloseTime = gDeviceConfig.preferences.sql_close_time;
    bkCached = true;
    BK48X9_MuteAudio();
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
      ready = SI47XX_SetVolume(0);
    // Previous plan may be restored by rx-runtime; don't leave an aborted
    // SI plan audible while the other receiver is being configured.
    if (!ready)
      (void)SI47XX_SetVolume(0);
    return ready;
  }
  case RX_DEVICE_BROADCAST:
    if (UVK6_ENABLE_SI4732) return false;
    if (BOARD_BroadcastUsesBkVhfPath()) {
      BK48X9_SetAF(BK48X9_AF_MUTE);
      BK48X9_ToggleAFDAC(false);
      BK48X9_ToggleGpioOut(BK48X9_GPIO0_PIN28_RX_ENABLE, true);
      BK48X9_SelectFilterEx(FILTER_VHF);
    }
    if (!BK1080_TuneTo(frequency)) {
      (void)BK1080_Mute(true);
      return false;
    }
    // An I2C NACK on unmute is not a successful reception.
    if (!BK1080_Mute(true)) {
      (void)BK1080_Mute(true);
      return false;
    }
    return true;
  }
  return false;
}

bool RX_DEVICE_Activate(const RxPlan *plan) {
  return preparePlan(plan, true);
}
bool RX_DEVICE_Prepare(const RxPlan *plan) {
  bool restore = audioOpen && plan && plan->device != audioDevice;
  RxDevice previousAudio = audioDevice;
  bool ok = preparePlan(plan, false);
  // Preparing another chip must neither mix its temporary output into the
  // speaker nor silently cut an independent background after preparation.
  if (restore && !audioOpen && !RX_DEVICE_RouteAudio(previousAudio, true)) return false;
  return ok;
}
bool RX_DEVICE_Sample(const RxPlan *plan, uint16_t *rssi, bool *open) {
  *rssi = 0;
  *open = false;
  uint8_t snr;
  switch (plan->device) {
  case RX_DEVICE_BK: {
    *rssi = BK48X9_GetRSSI();
    RadioTone tone = plan->parameters.rx_tone;
    bool carrier = BK48X9_IsSquelchOpen(); // Pump events even with SQ0.
    *open = (!plan->parameters.squelch || carrier) &&
            (!tone || BK48X9_CssIsDetected(TONE_Type(tone) != CODE_TYPE_CONTINUOUS_TONE));
    return true;
  }
  case RX_DEVICE_SI:
    if (!UVK6_ENABLE_SI4732 || !RSQ_GET()) return false;
    *rssi = rsqStatus.resp.RSSI;
    snr = rsqStatus.resp.SNR;
    break;
  case RX_DEVICE_BROADCAST:
    if (UVK6_ENABLE_SI4732) return false;
    *rssi = BK1080_GetRSSI();
    snr = BK1080_GetSNR();
    break;
  default:
    return false;
  }
  *open = !plan->parameters.squelch || snr > plan->parameters.squelch;
  return true;
}

bool RX_DEVICE_RouteAudio(RxDevice selected, bool open) {
  // Break before make: never mix audio from two chips. Receiver DSP stays on.
  AUDIO_ToggleSpeaker(false);
  audioOpen = false;
  uint8_t devices = runningDevices;
  if ((!UVK6_ENABLE_SI4732 && (devices & (1u << RX_DEVICE_SI))) ||
      (UVK6_ENABLE_SI4732 && (devices & (1u << RX_DEVICE_BROADCAST)))) return false;
  bool ok=true;
  if (devices&(1u<<RX_DEVICE_BK)) {
    BK48X9_MuteAudio();
  }
  if (UVK6_ENABLE_SI4732 && (devices&(1u<<RX_DEVICE_SI))) ok=SI47XX_SetVolume(0) && ok;
  if (!UVK6_ENABLE_SI4732 && (devices&(1u<<RX_DEVICE_BROADCAST))) ok=BK1080_Mute(true) && ok;
  if (!ok || !open) return ok;
  if (!(devices&(1u<<selected))) return false;
  switch(selected) {
  case RX_DEVICE_BK:
    BK48X9_SetAF(BK48X9_AF_FM);BK48X9_ToggleAFDAC(true);break;
  case RX_DEVICE_SI: if (!UVK6_ENABLE_SI4732) return false; ok=SI47XX_SetVolume(63);break;
  case RX_DEVICE_BROADCAST: if (UVK6_ENABLE_SI4732) return false; ok=BK1080_Mute(false);break;
  default: return false;
  }
  if (ok) {
    audioDevice = selected; audioOpen = true;
    SYS_DelayMs(8);
    AUDIO_ToggleSpeaker(true);
  }
  return ok;
}

bool RX_DEVICE_SeekNative(const RxPlan *plan) {
  if (!UVK6_ENABLE_SI4732 || !plan || plan->device != RX_DEVICE_SI)
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
  if (!UVK6_ENABLE_SI4732 || device != RX_DEVICE_SI)
    return RX_SEEK_ERROR;
  switch (SI47XX_SeekPoll(frequency_hz)) {
  case SI_SEEK_BUSY: return RX_SEEK_BUSY;
  case SI_SEEK_FOUND: return RX_SEEK_FOUND;
  case SI_SEEK_LIMIT: return RX_SEEK_LIMIT;
  default: return RX_SEEK_ERROR;
  }
}

void RX_DEVICE_SeekCancel(RxDevice device) {
  if (UVK6_ENABLE_SI4732 && device == RX_DEVICE_SI)
    SI47XX_SeekCancel();
}

bool RX_DEVICE_SeekSignal(RxDevice device, bool *found) {
  if (!found) return false;
  switch (device) {
  case RX_DEVICE_BK:
    // The caller applies a temporary squelch >= 4 even if monitoring uses 0.
    *found = BK48X9_IsSquelchOpen();
    return true;
  case RX_DEVICE_SI:
    if (!UVK6_ENABLE_SI4732 || !RSQ_GET()) return false;
    *found = rsqStatus.resp.RSSI >= 16 && rsqStatus.resp.SNR >= 7;
    return true;
  case RX_DEVICE_BROADCAST: {
    if (UVK6_ENABLE_SI4732) return false;
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
    SQL floor=squelchFor(level<4 ? 4 : level, plan->frequency_hz);
    bool carrier=BK48X9_GetRSSI()>=floor.ro && BK48X9_GetNoise()<=floor.no &&
                 BK48X9_GetGlitch()<=floor.go;
    RadioTone tone=plan->parameters.rx_tone;
    bool matched=!tone || BK48X9_CssIsDetected(TONE_Type(tone)!=CODE_TYPE_CONTINUOUS_TONE);
    *found=carrier && matched && (!level || BK48X9_IsSquelchOpen());
    return true;
  }
  if (plan->device==RX_DEVICE_SI) {
    if (!UVK6_ENABLE_SI4732 || !RSQ_GET()) return false;
    uint8_t snr=plan->parameters.squelch<7 ? 7 : plan->parameters.squelch+1u;
    *found=rsqStatus.resp.RSSI>=16 && rsqStatus.resp.SNR>=snr;
    return true;
  }
  if (plan->device==RX_DEVICE_BROADCAST) {
    if (UVK6_ENABLE_SI4732) return false;
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
      int16_t dBm = Rssi2DBm(rssi) + BK48X9_GetAttenuation();
      level = DBm2S(dBm, plan->frequency_hz >= 300000000u);
    }
  } else {
    level = ConvertDomain(rssi, 0, 75, 0, SEGMENTS);
  }
  return level > SEGMENTS ? SEGMENTS : level;
}

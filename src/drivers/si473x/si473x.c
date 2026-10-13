#include "drivers/si473x/si473x.h"
#include "external/printf/printf.h"
#include "drivers/dp32g030/gpio.h"
#include "core/misc.h"
#include "core/settings.h"
#include "hal/eeprom.h"
#include "hal/gpio.h"
#include "hal/i2c.h"
#include "hal/system.h"
#include "hal/systick.h"
#include "hal/uart.h"
#include <string.h>

static const uint8_t SI47XX_I2C_ADDR = 0x22;

#define RST_HIGH GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BK1080)
#define RST_LOW GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_BK1080)

RSQStatus rsqStatus;
static SsbMode currentSsbMode;

SI47XX_MODE si4732mode = SI47XX_FM;
uint16_t siCurrentFreq = 0;
bool isSi4732On = false;
static bool rdsReady;

static uint16_t fDiv() { return si4732mode == SI47XX_FM ? 1000 : 100; }

bool SI47XX_ReadBuffer(uint8_t *buf, uint8_t size) {
  if (!buf || !size)
    return false;
  uint8_t retries = 5;
  while (retries--) {
    I2C_Start();
    if (I2C_Write(SI47XX_I2C_ADDR + 1) == 0) {
      if (I2C_ReadBuffer(buf, size) == size) {
        I2C_Stop();
        return true;
      }
    }
    I2C_Stop();
    TIMER_DelayUs(1); // Short retry delay
  }
  // printf("SI R ERR\n");
  return false;
}

bool SI47XX_WriteBuffer(uint8_t *buf, uint8_t size) {
  I2C_Start();
  if (I2C_Write(SI47XX_I2C_ADDR) != 0) {
    I2C_Stop();
    return false;
  }
  if (I2C_WriteBuffer(buf, size) != 0) {
    I2C_Stop();
    return false;
  }
  I2C_Stop();
  return true;
}

bool SI47XX_IsSSB() {
  return si4732mode == SI47XX_USB || si4732mode == SI47XX_LSB;
}

static bool waitToSend(void) {
  uint32_t started = GetUptimeMs();
  do {
    uint8_t status = 0;
    if (SI47XX_ReadBuffer(&status, 1) && (status & STATUS_CTS))
      return true;
    TIMER_DelayUs(100);
  } while (GetUptimeMs() - started < 100);
  return false; // The receiver must never stall the system indefinitely.
}

static bool SI47XX_downloadPatch(void) {
  if (!SETTINGS_IsPatchPresent())
    return false;
  // Log("DL patch");

  uint8_t buf[64]; // 64 is optimal, more has no sense
  const uint16_t BUF_SIZE = ARRAY_SIZE(buf);
  const uint32_t PATCH_START = SETTINGS_GetEEPROMSize() - PATCH_SIZE;

  for (uint16_t offset = 0; offset < PATCH_SIZE; offset += BUF_SIZE) {
    const uint32_t rest = PATCH_SIZE - offset;
    const uint32_t eepromN = rest > BUF_SIZE ? BUF_SIZE : rest;
    if (EEPROM_ReadBuffer(PATCH_START + offset, buf, eepromN) != 0)
      return false;

    for (uint16_t i = 0; i < eepromN; i += 8) {
      if (!waitToSend() || !SI47XX_WriteBuffer(buf + i, 8))
        return false;
    }
  }
  return true;
}

bool SI47XX_SetProperty(uint16_t prop, uint16_t value) {
  if (!waitToSend())
    return false;
  uint8_t tmp[6] = {
      CMD_SET_PROPERTY, 0,           //
      prop >> 8,        prop & 0xff, //
      value >> 8,       value & 0xff //
  };
  if (!SI47XX_WriteBuffer(tmp, 6))
    return false;
  SYS_DelayMs(8); // irrespective of CTS coming up earlier than that
  return true;
}

/* uint16_t SI47XX_GetProperty(uint16_t prop, bool *valid) {
  uint8_t response[4] = {0};
  uint8_t tmp[4] = {CMD_GET_PROPERTY, 0, prop >> 8, prop & 0xff};
  waitToSend();
  SI47XX_WriteBuffer(tmp, 4);
  SI47XX_ReadBuffer(response, 4);

  if (valid) {
    *valid = !(response[0] & STATUS_ERR);
  }

  return MAKE_WORD(response[2], response[3]);
} */

bool RSQ_GET(void) {
  uint8_t cmd[2] = {si4732mode == SI47XX_FM ? CMD_FM_RSQ_STATUS
                                                : CMD_AM_RSQ_STATUS, 0x01};
  uint8_t response[sizeof(rsqStatus.raw)] = {0};
  bool ok = waitToSend() && SI47XX_WriteBuffer(cmd, 2) &&
            waitToSend() &&
            SI47XX_ReadBuffer(response, si4732mode == SI47XX_FM ? 8 : 6) &&
            !(response[0] & STATUS_ERR);
  // Never report a stale RSSI/SNR when the current request NACKs/times out.
  if (!ok) {
    memset(&rsqStatus, 0, sizeof(rsqStatus));
    return false;
  }
  memcpy(rsqStatus.raw, response, sizeof(response));
  return true;
}

bool SI47XX_SetVolume(uint8_t volume) {
  if (volume > 63)
    volume = 63;
  return SI47XX_SetProperty(PROP_RX_VOLUME, volume);
}

bool setAvcAmMaxGain(uint8_t gain) {
  return gain >= 12 && gain <= 90 &&
         SI47XX_SetProperty(PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN, gain * 340);
}

static void enableRDS(void) {
  rdsReady = SI47XX_SetProperty(PROP_FM_RDS_INT_SOURCE, FLG_RDSRECV) &&
    SI47XX_SetProperty(PROP_FM_RDS_INT_FIFO_COUNT, 1) &&
    SI47XX_SetProperty(
        PROP_FM_RDS_CONFIG,
        ((FLG_BLETHA_12 | FLG_BLETHB_12 | FLG_BLETHC_12 | FLG_BLETHD_12) << 8) |
            FLG_RDSEN);
}

bool SI47XX_SetAutomaticGainControl(uint8_t AGCDIS, uint8_t AGCIDX) {
  SI47XX_AgcOverrride agc;

  uint8_t cmd;

  if (si4732mode == SI47XX_FM)
    cmd = CMD_FM_AGC_OVERRIDE;
  else
    cmd = CMD_AM_AGC_OVERRIDE; // both for AM and SSB

  agc.arg.DUMMY = 0; // ARG1: bits 7:1 Always write to 0;
  agc.arg.AGCDIS = AGCDIS;
  agc.arg.AGCIDX = AGCIDX;

  uint8_t cmd2[] = {cmd, agc.raw[0], agc.raw[1]};
  return waitToSend() && SI47XX_WriteBuffer(cmd2, 3);
}

bool SI47XX_PowerUp() {
  // printf("SI PU\n");
  RST_HIGH;

  uint8_t cmd[3] = {CMD_POWER_UP, FLG_XOSCEN | FUNC_FM, OUT_ANALOG};
  if (si4732mode == SI47XX_AM) {
    cmd[1] = FLG_XOSCEN | FUNC_AM;
  }
  // POWER_UP is sent just after reset; CTS is not meaningful yet.
  SYS_DelayMs(10);
  if (!SI47XX_WriteBuffer(cmd, 3)) {
    RST_LOW;
    isSi4732On = false;
    return false;
  }
  SYS_DelayMs(500);

  isSi4732On = true;

  bool ready = SI47XX_SetVolume(63);
  if (ready && si4732mode == SI47XX_FM) {
    enableRDS(); // RDS is optional; audio RX remains usable if RDS fails.
  } else if (ready && si4732mode == SI47XX_AM) {
    ready = SI47XX_SetAutomaticGainControl(1, 0) &&
            SI47XX_SetProperty(PROP_AM_SOFT_MUTE_MAX_ATTENUATION, 0) &&
            SI47XX_SetProperty(PROP_AM_AGC_RELEASE_RATE, 20) &&
            setAvcAmMaxGain(40);
  }
  if (ready && siCurrentFreq != 0)
    ready = SI47XX_SetFreq(siCurrentFreq);
  if (!ready) {
    RST_LOW;
    isSi4732On = false;
    siCurrentFreq = 0;
  }
  return ready;
}

bool SI47XX_SsbSetup(SI47XX_SsbFilterBW AUDIOBW, uint8_t SBCUTFLT,
                     uint8_t AVC_DIVIDER, uint8_t AVCEN, uint8_t SMUTESEL,
                     uint8_t DSP_AFCDIS) {
  currentSsbMode.param.SBCUTFLT = SBCUTFLT;
  currentSsbMode.param.AVC_DIVIDER = AVC_DIVIDER;
  currentSsbMode.param.AVCEN = AVCEN;
  currentSsbMode.param.SMUTESEL = SMUTESEL;
  currentSsbMode.param.DSP_AFCDIS = DSP_AFCDIS;
  currentSsbMode.param.AUDIOBW = AUDIOBW;
  return SI47XX_SetProperty(PROP_SSB_MODE,
                              (currentSsbMode.raw[1] << 8) | currentSsbMode.raw[0]);
}

bool SI47XX_PatchPowerUp() {
  // printf("SI PPU\n");
  RST_HIGH;
  uint8_t cmd[3] = {CMD_POWER_UP, 0b00110001, OUT_ANALOG};
  SYS_DelayMs(10);
  if (!SI47XX_WriteBuffer(cmd, 3)) {
    RST_LOW;
    isSi4732On = false;
    return false;
  }
  SYS_DelayMs(60);

  if (!SI47XX_downloadPatch()) {
    Log("[SI] SSB patch missing or failed");
    RST_LOW;
    isSi4732On = false;
    return false;
  }
  isSi4732On = true;

  bool ready = SI47XX_SsbSetup(2, 1, 0, 1, 0, 1) &&
               setAvcAmMaxGain(42) && SI47XX_SetVolume(63);
  if (ready && siCurrentFreq)
    ready = SI47XX_SetFreq(siCurrentFreq);
  if (ready)
    ready = SI47XX_SetProperty(PROP_SSB_SOFT_MUTE_MAX_ATTENUATION, 0) &&
            SI47XX_SetProperty(PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN, 0x7800);
  if (!ready) {
    RST_LOW;
    isSi4732On = false;
    siCurrentFreq = 0;
    return false;
  }
  si4732mode = SI47XX_USB; // Patch boots in USB; caller restores requested mode.
  return true;
}

bool SI47XX_SetSsbBandwidth(SI47XX_SsbFilterBW bw) {
  return SI47XX_SsbSetup(bw, 1, 0, 1, 0, 1);
}

// Forward-only, bounded hardware seek. The application handles the single
// wrap at the selected band's edge, not the radio's global FM/AM band.
bool SI47XX_SeekStart(uint32_t lower_hz, uint32_t upper_hz, uint32_t step_hz) {
  if (!isSi4732On || (si4732mode != SI47XX_FM && si4732mode != SI47XX_AM) ||
      lower_hz >= upper_hz)
    return false;
  const uint32_t unit = si4732mode == SI47XX_FM ? 10000u : 1000u;
  if (lower_hz % unit || upper_hz % unit || step_hz % unit ||
      !step_hz || upper_hz / unit > 65535u)
    return false;
  if (si4732mode == SI47XX_FM &&
      step_hz != 50000u && step_hz != 100000u && step_hz != 200000u)
    return false;
  const uint16_t lower = lower_hz/unit, upper = upper_hz/unit;
  const uint16_t spacing = step_hz/unit;
  const uint16_t bottomProp = si4732mode == SI47XX_FM
                                  ? PROP_FM_SEEK_BAND_BOTTOM
                                  : PROP_AM_SEEK_BAND_BOTTOM;
  const uint16_t topProp = si4732mode == SI47XX_FM
                               ? PROP_FM_SEEK_BAND_TOP
                               : PROP_AM_SEEK_BAND_TOP;
  const uint16_t spacingProp = si4732mode == SI47XX_FM
                                   ? PROP_FM_SEEK_FREQ_SPACING
                                   : PROP_AM_SEEK_FREQ_SPACING;
  const uint16_t rssiProp = si4732mode == SI47XX_FM
                                ? PROP_FM_SEEK_TUNE_RSSI_THRESHOLD
                                : PROP_AM_SEEK_TUNE_RSSI_THRESHOLD;
  const uint16_t snrProp = si4732mode == SI47XX_FM
                               ? PROP_FM_SEEK_TUNE_SNR_THRESHOLD
                               : PROP_AM_SEEK_TUNE_SNR_THRESHOLD;
  if (!SI47XX_SetProperty(bottomProp, lower) ||
      !SI47XX_SetProperty(topProp, upper) ||
      !SI47XX_SetProperty(spacingProp, spacing) ||
      !SI47XX_SetProperty(rssiProp, 16) ||
      !SI47XX_SetProperty(snrProp, 7))
    return false;
  uint8_t cmd[6] = {si4732mode == SI47XX_FM ? CMD_FM_SEEK_START
                                            : CMD_AM_SEEK_START,
                    SEEK_START_ARG1_SEEK_UP, 0, 0, 0, 0};
  if (si4732mode == SI47XX_AM && siCurrentFreq > 1800)
    cmd[5] = 1; // SW antenna capacitor choice in the existing driver.
  if (!waitToSend() ||
      !SI47XX_WriteBuffer(cmd, si4732mode == SI47XX_FM ? 2 : 6))
    return false;
  siCurrentFreq = 0; // Hardware SEEK changed the carrier behind tune cache.
  return true;
}

SiSeekResult SI47XX_SeekPoll(uint32_t *frequency_hz) {
  if (!frequency_hz || !isSi4732On ||
      (si4732mode != SI47XX_FM && si4732mode != SI47XX_AM))
    return SI_SEEK_ERROR;
  uint8_t cmd[2] = {si4732mode == SI47XX_FM ? CMD_FM_TUNE_STATUS
                                             : CMD_AM_TUNE_STATUS, 0};
  uint8_t status[8] = {0};
  if (!waitToSend() || !SI47XX_WriteBuffer(cmd, 2) || !waitToSend() ||
      !SI47XX_ReadBuffer(status, sizeof(status)) ||
      !(status[0] & STATUS_CTS) || (status[0] & STATUS_ERR))
    return SI_SEEK_ERROR;
  if (!(status[0] & STATUS_STCINT))
    return SI_SEEK_BUSY;
  uint8_t ack[2] = {cmd[0], TUNE_STATUS_ARG1_CLEAR_INT};
  if (!waitToSend() || !SI47XX_WriteBuffer(ack, 2) || !waitToSend() ||
      !SI47XX_ReadBuffer(status, sizeof(status)))
    return SI_SEEK_ERROR;
  uint32_t result_hz = ((uint32_t)status[2] << 8 | status[3]) *
                       (si4732mode == SI47XX_FM ? 10000u : 1000u);
  *frequency_hz = result_hz;
  return status[1] & FIELD_TUNE_STATUS_RESP1_SEEK_LIMIT ? SI_SEEK_LIMIT :
         status[1] & FIELD_TUNE_STATUS_RESP1_VALID ? SI_SEEK_FOUND :
         SI_SEEK_LIMIT;
}

void SI47XX_SeekCancel(void) {
  if (isSi4732On && (si4732mode == SI47XX_FM || si4732mode == SI47XX_AM)) {
    uint8_t cmd[2] = {si4732mode == SI47XX_FM ? CMD_FM_TUNE_STATUS
                                               : CMD_AM_TUNE_STATUS,
                      TUNE_STATUS_ARG1_CANCEL_SEEK |
                      TUNE_STATUS_ARG1_CLEAR_INT};
    uint8_t response[8];
    if (waitToSend() && SI47XX_WriteBuffer(cmd, 2) && waitToSend())
      (void)SI47XX_ReadBuffer(response, sizeof(response));
  }
  siCurrentFreq = 0; // Re-apply always tunes after cancellation.
}


void SI47XX_PowerDown() {
  rdsReady = false;
  // printf("SI PD\n");
  uint8_t cmd[1] = {CMD_POWER_DOWN};

  waitToSend();
  SI47XX_WriteBuffer(cmd, 1);
  TIMER_DelayUs(10);
  RST_LOW;
  isSi4732On = false;
  siCurrentFreq = 0;
}

bool SI47XX_SwitchMode(SI47XX_MODE mode) {
  Log("[SI] mode %u -> %u", si4732mode, mode);
  if (si4732mode == mode && isSi4732On)
    return true;
  bool wasSSB = isSi4732On && SI47XX_IsSSB();
  bool needsSSB = mode == SI47XX_USB || mode == SI47XX_LSB;
  si4732mode = mode;
  if (!wasSSB || !needsSSB) {
    if (isSi4732On)
      SI47XX_PowerDown();
    bool ready = needsSSB ? SI47XX_PatchPowerUp() : SI47XX_PowerUp();
    if (!ready) {
      si4732mode = mode;
      return false;
    }
  }
  // Patch startup uses USB internally. Restore the requested sideband.
  si4732mode = mode;
  if (wasSSB && needsSSB)
    siCurrentFreq = 0; // Sideband is encoded in the tune command.
  return isSi4732On;
}

bool SI47XX_SetFreq(uint16_t freq) {
  if (freq == 0) {
    // printf("SI f=0 skip\n");
    return false; // Reject invalid carrier
  }
  if (siCurrentFreq == freq)
    return isSi4732On;
  uint8_t hb = (freq >> 8) & 0xFF;
  uint8_t lb = freq & 0xFF;

  bool isSW = freq > 1800;

  uint8_t size = 5;
  uint8_t cmd[6] = {CMD_FM_TUNE_FREQ, 0x00, hb, lb, 0, 0};

  if (si4732mode == SI47XX_FM || si4732mode == SI47XX_AM) {
    cmd[1] = 0x01; // FAST
  } else {
    cmd[1] = si4732mode == SI47XX_USB ? 0b10000000 : 0b01000000;
  }

  if (si4732mode != SI47XX_FM) {
    cmd[0] = CMD_AM_TUNE_FREQ;
    size = 6; // was 5
  }

  if (si4732mode == SI47XX_AM && isSW) {
    cmd[5] = 1;
  }

  if (!isSi4732On || !waitToSend() || !SI47XX_WriteBuffer(cmd, size))
    return false;
  siCurrentFreq = freq;
  return true;
}


bool SI47XX_SetBandwidth(SI47XX_FilterBW AMCHFLT, bool AMPLFLT) {
  SI47XX_BW_Config cfg = {0};
  cfg.param.AMCHFLT = AMCHFLT;
  cfg.param.AMPLFLT = AMPLFLT;
  return SI47XX_SetProperty(PROP_AM_CHANNEL_FILTER,
                              (cfg.raw[1] << 8) | cfg.raw[0]);
}

bool SI47XX_ReadRDS(SI47XX_RdsStatus *status, bool clear_fifo) {
  if (!status || !isSi4732On || si4732mode != SI47XX_FM || !rdsReady)
    return false;
  uint8_t cmd[2] = {CMD_FM_RDS_STATUS, RDS_STATUS_ARG1_CLEAR_INT |
                    (clear_fifo ? RDS_STATUS_ARG1_CLEAR_FIFO : 0)};
  uint8_t buf[13];
  if (!waitToSend() || !SI47XX_WriteBuffer(cmd, sizeof(cmd)) ||
      !waitToSend() || !SI47XX_ReadBuffer(buf, sizeof(buf)) ||
      !(buf[0] & STATUS_CTS) || (buf[0] & STATUS_ERR)) return false;
  SI47XX_RdsStatus result = {
    .fifo_used = clear_fifo ? 0 : buf[3],
    .synchronized = !!(buf[2] & FIELD_RDS_STATUS_RESP2_SYNC),
    .overflow = !!(buf[2] & FIELD_RDS_STATUS_RESP2_FIFO_OVERFLOW),
  };
  for (unsigned i=0; i<4; ++i) {
    result.blocks[i] = (uint16_t)buf[4+2*i]<<8 | buf[5+2*i];
    result.errors[i] = (buf[12] >> (6-2*i)) & 3;
  }
  *status = result;
  return true;
}







bool SI47XX_SetBFO(int16_t bfo) {
  return SI47XX_SetProperty(PROP_SSB_BFO, bfo);
}

bool SI47XX_TuneTo(uint32_t f) {
  if (SI47XX_IsSSB()) {
    uint32_t carrier = (uint32_t)siCurrentFreq * 100;
    int32_t bfo = ((int32_t)carrier - (int32_t)f) * 10;
    if (!siCurrentFreq || bfo <= -16000 || bfo >= 16000)
      carrier = (f / 100) * 100;
    if (!SI47XX_SetFreq(carrier / 100))
      return false;
    // Apply the BFO only after the coarse carrier was accepted.
    return SI47XX_SetBFO(((int32_t)carrier - (int32_t)f) * 10);
  }
  // FM_TUNE_FREQ accepts 10 kHz units.
  return SI47XX_SetFreq(f / fDiv());
}


static const uint16_t amBandwidthHz[] = {6000,4000,3000,2000,1000,1800,2500};
static const uint16_t ssbBandwidthHz[] = {1200,2200,3000,4000,500,1000};
bool SI47XX_BandwidthFromHz(uint32_t hz, bool ssb, uint16_t *index) {
  const uint16_t *values = ssb ? ssbBandwidthHz : amBandwidthHz;
  uint8_t count = ssb ? ARRAY_SIZE(ssbBandwidthHz) : ARRAY_SIZE(amBandwidthHz);
  for (uint8_t i = 0; i < count; ++i) {
    if (values[i] == hz) {
      *index = i;
      return true;
    }
  }
  return false;
}

bool SI47XX_Detect(void) {
  // Release the shared receiver reset line before probing the SI address.
  RST_HIGH;
  SYS_DelayMs(10);
  I2C_Start();
  bool present = I2C_Write(SI47XX_I2C_ADDR) == 0;
  I2C_Stop();
  if (!present)
    RST_LOW;
  return present;
}

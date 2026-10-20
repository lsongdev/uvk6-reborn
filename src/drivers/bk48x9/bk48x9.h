#ifndef DRIVER_BK48X9_h
#define DRIVER_BK48X9_h

#include "drivers/bk48x9/registers.h"
#include "core/measurements.h"
#include <stdbool.h>
#include <stdint.h>

#define BK48X9_F_MIN 1588000
#define BK48X9_F_MAX 134000000

#define VHF_UHF_BOUND1 24000000
#define VHF_UHF_BOUND2 28000000

typedef enum {
  FILTER_VHF,
  FILTER_UHF,
  FILTER_OFF,
  FILTER_AUTO,
} Filter;

enum BK48X9_AF_Type_t {
  BK48X9_AF_MUTE,
  BK48X9_AF_FM,
  BK48X9_AF_ALAM, // tone
  BK48X9_AF_BEEP, // for tx
  BK48X9_AF_RAW,  // (ssb without if filter = raw in sdr sharp)
  BK48X9_AF_USB,  // (or ssb = lsb and usb at the same time)
  BK48X9_AF_CTCO, // ctcss/dcs (fm with narrow filters for ctcss/dcs)
  BK48X9_AF_AM,
  BK48X9_AF_FSKO, // fsk out test with special fsk filters (need reg58 fsk on to
                  // give sound on speaker )
  BK48X9_AF_BYPASS, // (fm without filter = discriminator output)
};

typedef enum {
  XTAL_0_13M,
  XTAL_1_19_2M,
  XTAL_2_26M,
  XTAL_3_38_4M,
} XtalMode;

typedef enum {
  SQUELCH_RSSI_NOISE_GLITCH,
  SQUELCH_RSSI_GLITCH,
  SQUELCH_RSSI_NOISE,
  SQUELCH_RSSI,
} SquelchType;

typedef enum BK48X9_AF_Type_t BK48X9_AF_Type_t;

enum BK48X9_FilterBandwidth_t {
  BK48X9_FILTER_BW_6k,  //  "U 6K"
  BK48X9_FILTER_BW_7k,  //  "U 7K",
  BK48X9_FILTER_BW_9k,  //  "N 9k",
  BK48X9_FILTER_BW_10k, //  "N 10k",
  BK48X9_FILTER_BW_12k, //  "W 12k",
  BK48X9_FILTER_BW_14k, //  "W 14k",
  BK48X9_FILTER_BW_17k, //  "W 17k",
  BK48X9_FILTER_BW_20k, //  "W 20k",
  BK48X9_FILTER_BW_23k, //  "W 23k",
  BK48X9_FILTER_BW_26k, //	"W 26k",
};

typedef enum BK48X9_FilterBandwidth_t BK48X9_FilterBandwidth_t;

enum BK48X9_CssScanResult_t {
  BK48X9_CSS_RESULT_NOT_FOUND,
  BK48X9_CSS_RESULT_CTCSS,
  BK48X9_CSS_RESULT_CDCSS,
};

typedef enum {
  F_SC_T_0_2s,
  F_SC_T_0_4s,
  F_SC_T_0_8s,
  F_SC_T_1_6s,
} FreqScanTime;

typedef struct {
  uint16_t regValue;
  int8_t gainDb;
} Gain;

typedef enum BK48X9_CssScanResult_t BK48X9_CssScanResult_t;
extern const Gain GAIN_TABLE[32];

#define AUTO_GAIN_INDEX 0

// extern const uint8_t SQ[2][6][11];

typedef struct {
  uint16_t chip_id;
  bool am;
  uint16_t bandwidth_mask; // Bits correspond to BK48X9_FilterBandwidth_t.
  // Recommended squelch open (0..7) and close (0..3) delays for this chip.
  uint8_t squelch_open_delay, squelch_close_delay;
} BK48X9Capabilities;
BK48X9Capabilities BK48X9_GetCapabilities(void);
// Mute this receiver, retaining the chip-specific RX DSP/DAC requirements.
void BK48X9_MuteAudio(void);

bool BK48X9_BandwidthFromHz(uint32_t hz, uint16_t *index);
// mic: REG7D gain 0..31; deviation: BK4819 control word / 10, not measured Hz.
void BK48X9_Init(uint8_t mic, uint16_t deviation);
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t Register);
void BK48X9_WriteRegister(BK48X9_REGISTER_t Register, uint16_t Data);

void BK48X9_SetAGC(uint8_t gainIndex);
// Shared RX/TX reference trim (-1000..1000), AF gain (0..63), DAC gain (0..15).
// Invalid fields use the reference firmware defaults; call after chip reset.
void BK48X9_SetRxCalibration(int16_t trim, uint8_t volume, uint8_t dac);
int8_t BK48X9_GetAgcIndex();
uint8_t BK48X9_GetAttenuation();

void BK48X9_ToggleGpioOut(BK48X9_GPIO_PIN_t Pin, bool bSet);

void BK48X9_CssResetDetection(bool digital);
bool BK48X9_CssIsDetected(bool digital);
// Fixed-frequency CSS measurement; caller keeps PA off and restores RX.
void BK48X9_StartToneScan(void);
// CTCSS value is Hz*10; DCS value is the captured word. NOT_FOUND is pending.
BK48X9_CssScanResult_t BK48X9_ReadToneScan(uint32_t *value);
void BK48X9_CssOff(void); // No tone detection; squelch events stay on.
// BK4829 squelch from interrupt events (internal to the driver's squelch).
void BK48X9_SetCDCSSCodeWord(uint32_t CodeWord);
void BK48X9_SetCTCSSFrequency(uint32_t BaudRate);
void BK48X9_SetFilterBandwidth(BK48X9_FilterBandwidth_t Bandwidth);
void BK48X9_SetupPowerAmplifier(uint8_t Bias, uint32_t Frequency);
void BK48X9_SetFrequency(uint32_t Frequency);
void BK48X9_SetupSquelch(SQL sq, uint8_t delayO, uint8_t delayC);
void BK48X9_Squelch(uint8_t sql, uint8_t OpenDelay, uint8_t CloseDelay);

void BK48X9_SetAF(BK48X9_AF_Type_t AF);
void BK48X9_RX_TurnOn(void);
void BK48X9_SelectFilterEx(Filter filter);
void BK48X9_SelectFilter(uint32_t Frequency, uint32_t vhfUhfBound);
void BK48X9_DisableDTMF(void);
// Keep RF on: mute voice, then signal CTCSS phase reversal or DCS turn-off.
void BK48X9_StartTxTail(bool digital);
void BK48X9_EnterTxMute(void);
void BK48X9_ExitTxMute(void);
void BK48X9_Idle(void);
void BK48X9_ExitBypass(void);
void BK48X9_PrepareTransmit(void);
void BK48X9_ConfigureMicrophone(uint8_t mic);
void BK48X9_ConfigureVoiceTx(uint32_t frequency, uint8_t mic, uint16_t deviation,
                             uint32_t vhfUhfBound);
void BK48X9_TxOn_Beep(void);
void BK48X9_ExitSubAu(void);

uint16_t BK48X9_GetRSSI(void);
// RSSI of a sweep point just tuned, as settled as this chip needs it.
uint16_t BK48X9_GetScanRssi(void);
uint8_t BK48X9_GetNoise(void);
uint8_t BK48X9_GetGlitch(void);

bool BK48X9_GetFrequencyScanResult(uint32_t *pFrequency);
void BK48X9_DisableFrequencyScan(void);
void BK48X9_EnableFrequencyScanEx(FreqScanTime t);

void BK48X9_StopScan(void);

void BK48X9_ToggleAFDAC(bool on);
void BK48X9_SetRegValue(RegisterSpec s, uint16_t v);
void BK48X9_SetAFC(uint8_t level);
void BK48X9_SetAFCSpeed(uint8_t speed);
void BK48X9_TuneTo(uint32_t f, bool precise);
// Production RX modes are NFM and AM; WFM/SSB use other receiver chips.
void BK48X9_SetRxMode(bool am);
void BK48X9_InvalidateRxMode(void);
bool BK48X9_IsSquelchOpen();

#endif

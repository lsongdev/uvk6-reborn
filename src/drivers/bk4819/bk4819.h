#ifndef DRIVER_BK4819_h
#define DRIVER_BK4819_h

#include "drivers/bk4819/bk4819-regs.h"
#include "core/measurements.h"
#include <stdbool.h>
#include <stdint.h>

#define BK4819_F_MIN 1588000
#define BK4819_F_MAX 134000000

#define VHF_UHF_BOUND1 24000000
#define VHF_UHF_BOUND2 28000000

typedef enum {
  FILTER_VHF,
  FILTER_UHF,
  FILTER_OFF,
  FILTER_AUTO,
} Filter;

enum BK4819_AF_Type_t {
  BK4819_AF_MUTE,
  BK4819_AF_FM,
  BK4819_AF_ALAM, // tone
  BK4819_AF_BEEP, // for tx
  BK4819_AF_RAW,  // (ssb without if filter = raw in sdr sharp)
  BK4819_AF_USB,  // (or ssb = lsb and usb at the same time)
  BK4819_AF_CTCO, // ctcss/dcs (fm with narrow filters for ctcss/dcs)
  BK4819_AF_AM,
  BK4819_AF_FSKO, // fsk out test with special fsk filters (need reg58 fsk on to
                  // give sound on speaker )
  BK4819_AF_BYPASS, // (fm without filter = discriminator output)
};

typedef enum {
  MOD_FM,
  MOD_AM,
  MOD_LSB,
  MOD_USB,
  MOD_BYP,
  MOD_RAW,
  MOD_WFM,
} ModulationType;

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

typedef enum BK4819_AF_Type_t BK4819_AF_Type_t;

enum BK4819_FilterBandwidth_t {
  BK4819_FILTER_BW_6k,  //  "U 6K"
  BK4819_FILTER_BW_7k,  //  "U 7K",
  BK4819_FILTER_BW_9k,  //  "N 9k",
  BK4819_FILTER_BW_10k, //  "N 10k",
  BK4819_FILTER_BW_12k, //  "W 12k",
  BK4819_FILTER_BW_14k, //  "W 14k",
  BK4819_FILTER_BW_17k, //  "W 17k",
  BK4819_FILTER_BW_20k, //  "W 20k",
  BK4819_FILTER_BW_23k, //  "W 23k",
  BK4819_FILTER_BW_26k, //	"W 26k",
};

typedef enum BK4819_FilterBandwidth_t BK4819_FilterBandwidth_t;

enum BK4819_CssScanResult_t {
  BK4819_CSS_RESULT_NOT_FOUND,
  BK4819_CSS_RESULT_CTCSS,
  BK4819_CSS_RESULT_CDCSS,
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

typedef enum BK4819_CssScanResult_t BK4819_CssScanResult_t;
extern const Gain GAIN_TABLE[32];

#define AUTO_GAIN_INDEX 0

// extern const uint8_t SQ[2][6][11];

bool BK4819_BandwidthFromHz(uint32_t hz, uint16_t *index);
void BK4819_Init(void);
uint16_t BK4819_ReadRegister(BK4819_REGISTER_t Register);
void BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Data);
void BK4819_WriteU8(uint8_t Data);
void BK4819_WriteU16(uint16_t Data);

void BK4819_SetAGC(uint8_t gainIndex);
int8_t BK4819_GetAgcIndex();
uint8_t BK4819_GetAttenuation();

void BK4819_ToggleGpioOut(BK4819_GPIO_PIN_t Pin, bool bSet);

void BK4819_CssResetDetection(bool digital);
bool BK4819_CssIsDetected(bool digital);
void BK4819_SetCDCSSCodeWord(uint32_t CodeWord);
void BK4819_SetCTCSSFrequency(uint32_t BaudRate);
void BK4819_SetFilterBandwidth(BK4819_FilterBandwidth_t Bandwidth);
void BK4819_SetupPowerAmplifier(uint8_t Bias, uint32_t Frequency);
void BK4819_SetFrequency(uint32_t Frequency);
void BK4819_SetupSquelch(SQL sq, uint8_t delayO, uint8_t delayC);
void BK4819_Squelch(uint8_t sql, uint8_t OpenDelay, uint8_t CloseDelay);

void BK4819_SetAF(BK4819_AF_Type_t AF);
void BK4819_RX_TurnOn(void);
void BK4819_SelectFilterEx(Filter filter);
void BK4819_SelectFilter(uint32_t Frequency);
void BK4819_DisableDTMF(void);
void BK4819_EnterTxMute(void);
void BK4819_ExitTxMute(void);
void BK4819_SetupAircopy(void);
void BK4819_Idle(void);
void BK4819_ExitBypass(void);
void BK4819_PrepareTransmit(void);
void BK4819_ConfigureMicrophone(uint8_t mic);
void BK4819_ConfigureVoiceTx(uint32_t frequency, uint8_t mic, uint16_t deviation);
void BK4819_TxOn_Beep(void);
void BK4819_ExitSubAu(void);





uint16_t BK4819_GetRSSI(void);
uint8_t BK4819_GetNoise(void);
uint8_t BK4819_GetGlitch(void);
uint8_t BK4819_GetSNR(void);

bool BK4819_GetFrequencyScanResult(uint32_t *pFrequency);
void BK4819_DisableFrequencyScan(void);
void BK4819_EnableFrequencyScanEx(FreqScanTime t);

void BK4819_StopScan(void);






void BK4819_ToggleAFBit(bool on);
void BK4819_ToggleAFDAC(bool on);
void BK4819_SetRegValue(RegisterSpec s, uint16_t v);
void BK4819_SetAFC(uint8_t level);
void BK4819_SetAFCSpeed(uint8_t speed);
void BK4819_TuneTo(uint32_t f, bool precise);
// Production RX modes are NFM and AM; WFM/SSB use other receiver chips.
void BK4819_SetRxMode(bool am);
void BK4819_InvalidateRxMode(void);
bool BK4819_IsSquelchOpen();

void BK4819_XtalSet(XtalMode mode);

#endif

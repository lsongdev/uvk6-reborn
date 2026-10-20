#ifndef TX_MODEL_H
#define TX_MODEL_H
#include "core/radio-model.h"

typedef enum { TX_POWER_LOW, TX_POWER_MEDIUM, TX_POWER_HIGH } TxPower;
extern const char *const txPowerNames[TX_POWER_HIGH + 1];
// Where TX sits relative to RX: same, repeater offset up/down, or split.
typedef enum { TX_SHIFT_OFF, TX_SHIFT_PLUS, TX_SHIFT_MINUS, TX_SHIFT_SPLIT,
               TX_SHIFT_COUNT } TxShift;
#define TX_OFFSET_MAX_HZ 100000000u
typedef struct {
  bool enabled;
  TxPower power;
  TxShift shift;
  uint32_t offset_hz; // Used by + and -; kept while another shift is chosen.
  uint32_t split_hz;  // Absolute TX Hz used by Split; zero means not set.
  RadioTone tone;
  bool tail; // Send an end-of-transmission CSS signal; default off.
} TxConfig;

// Board implementation availability and factory calibration for the target Hz.
// A populated calibration record is not a measurement of RF output quality.
typedef struct {
  bool available;
  uint8_t power_mask; // Bit (1 << TxPower) for usable calibration records.
  const RadioSpan *ranges; // Fitted RF chip's tuning ranges, also used by RX.
} TxCapabilities;

typedef struct {
  uint32_t frequency_hz; // Resolved independently from the RX frequency.
  TxPower power;
  RadioTone tone;
  bool tail; // Send an end-of-transmission CSS signal; default off.
} TxPlan;

typedef enum {
  TX_OK, TX_DISABLED, TX_UNSUPPORTED_MODE, TX_UNVERIFIED_HARDWARE,
  TX_OUT_OF_BAND, TX_UNCALIBRATED, TX_BUSY, TX_LOW_BATTERY, TX_HW_FAULT
} TxResult;

static inline bool TX_MODEL_FrequencyValid(uint32_t hz) {
  // Stored intent is independent of the fitted chip; PTT checks its ranges.
  return !hz || (!(hz % 10u) && hz >= 18000000u && hz <= 1300000000u);
}

bool TX_MODEL_ConfigValid(const TxConfig *config);
// TX Hz for an RX Hz under the shift; zero when it cannot apply.
uint32_t TX_MODEL_Frequency(const TxConfig *config, uint32_t rx_hz);
TxResult TX_MODEL_Plan(const RxPlan *rx, const TxConfig *config,
                       const TxCapabilities *capabilities,
                       TxPlan *out);
#endif

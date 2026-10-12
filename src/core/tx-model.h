#ifndef TX_MODEL_H
#define TX_MODEL_H
#include "core/radio-model.h"

typedef enum { TX_POWER_LOW, TX_POWER_MEDIUM, TX_POWER_HIGH } TxPower;
typedef struct {
  bool enabled;
  TxPower power;
  uint32_t frequency_hz; // Zero follows RX (Same); otherwise independent Hz.
  RadioTone tone;
} TxConfig;

// The board TX capability is separate from BK4819 receive range. No board
// is assumed verified merely because a BK4819 is detected.
typedef struct {
  bool verified;
  bool low_power_calibrated;
} TxCapabilities;

typedef struct {
  bool authorized;
  uint32_t lower_hz; // Inclusive, verified board TX window.
  uint32_t upper_hz; // Exclusive.
} TxPolicy;

typedef struct {
  uint32_t frequency_hz; // Resolved independently from the RX frequency.
  TxPower power;
  RadioTone tone;
} TxPlan;

typedef enum {
  TX_OK, TX_DISABLED, TX_UNSUPPORTED_MODE, TX_UNVERIFIED_HARDWARE,
  TX_OUT_OF_BAND, TX_UNCALIBRATED, TX_BUSY, TX_LOW_BATTERY, TX_HW_FAULT
} TxResult;

static inline bool TX_MODEL_FrequencyValid(uint32_t hz) {
  return !hz || (!(hz % 10u) &&
    ((hz >= 18000000u && hz <= 660000000u) ||
     (hz >= 840000000u && hz <= 1300000000u)));
}

TxResult TX_MODEL_Plan(const RxPlan *rx, const TxConfig *config,
                       const TxCapabilities *capabilities,
                       const TxPolicy *policy, TxPlan *out);
#endif

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

// Board implementation availability and factory calibration for the target Hz.
// A populated calibration record is not a measurement of RF output quality.
typedef struct {
  bool available;
  uint8_t power_mask; // Bit (1 << TxPower) for usable calibration records.
} TxCapabilities;

typedef struct { uint32_t lower_hz, upper_hz; } TxFrequencyRange;
#define TX_RANGE_CAPACITY 2
typedef struct {
  bool authorized;
  uint8_t count;
  TxFrequencyRange ranges[TX_RANGE_CAPACITY]; // Upper bounds are exclusive.
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

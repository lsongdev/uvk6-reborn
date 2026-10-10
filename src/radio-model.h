#ifndef RADIO_MODEL_H
#define RADIO_MODEL_H

#include <stdbool.h>
#include <stdint.h>

// User parameters use Hz and concrete demodulation modes, never driver enums.
typedef enum { RX_FM, RX_WFM, RX_AM, RX_LSB, RX_USB, RX_MODE_COUNT } RxMode;
typedef enum { RX_DEVICE_BK, RX_DEVICE_SI, RX_DEVICE_BROADCAST } RxDevice;
typedef struct {
  RxMode mode;
  uint32_t step_hz;
  uint32_t bandwidth_hz; // Zero means fixed bandwidth (WFM).
  uint8_t squelch;
  bool tx_enabled;
} RadioParameters;
typedef struct {
  const char *name;
  uint32_t start_hz;
  uint32_t end_hz; // Exclusive.
  uint32_t initial_hz;
  bool shortcut;
  RadioParameters preset;
} RadioBand;
typedef struct { bool si; bool broadcast; bool ssb_patch; } ReceiverCapabilities;
typedef struct {
  RxDevice device;
  uint32_t frequency_hz;
  RadioParameters parameters;
} RxPlan;
typedef enum { RX_OK, RX_OUT_OF_RANGE, RX_UNSUPPORTED_MODE,
               RX_PATCH_MISSING, RX_INVALID_PARAMETERS } RxResult;

#define RADIO_MIN_HZ 150000u
#define RADIO_MAX_HZ 1340000000u
extern const RadioBand radioBands[];
extern const uint8_t radioBandCount;
extern const char *const rxModeNames[RX_MODE_COUNT];
uint8_t RADIO_MODEL_BandAt(uint32_t frequency_hz);
uint8_t RADIO_MODEL_NextBand(uint8_t current);
RxResult RADIO_MODEL_Plan(uint32_t frequency_hz, RadioParameters parameters,
                          ReceiverCapabilities capabilities, RxPlan *plan);
uint32_t RADIO_MODEL_Resolution(RxDevice device, RxMode mode);
uint32_t RADIO_MODEL_Step(uint32_t frequency_hz, uint32_t step_hz, bool up);
bool RADIO_MODEL_CanTransmit(const RxPlan *plan);

#endif

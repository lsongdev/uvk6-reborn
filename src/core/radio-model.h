#ifndef RADIO_MODEL_H
#define RADIO_MODEL_H

#include "core/tone.h"
#include <stdbool.h>
#include <stdint.h>

// User parameters use Hz and concrete demodulation modes, never driver enums.
typedef enum { RX_NFM, RX_WFM, RX_AM, RX_LSB, RX_USB, RX_MODE_COUNT } RxMode;
typedef enum { RX_DEVICE_BK, RX_DEVICE_SI, RX_DEVICE_BROADCAST } RxDevice;
typedef struct {
  RxMode mode;
  uint32_t step_hz;
  uint32_t bandwidth_hz; // Zero means fixed bandwidth (WFM).
  uint8_t squelch;
  RadioTone rx_tone; // Off or a CTCSS/DCS option, NFM only.
} RadioParameters;
typedef struct {
  const char *name;
  uint32_t start_hz;
  uint32_t end_hz; // Exclusive.
  uint32_t initial_hz;
  bool shortcut;
  RadioParameters preset;
} RadioBand;
// Bits follow the common nominal BK bandwidth list (6,7,9,10,12,14,17,20,23,26 kHz).
enum { RX_BK_BANDWIDTH_6K = 1u << 0, RX_BK_BANDWIDTH_12K = 1u << 4,
       RX_BK_BANDWIDTH_26K = 1u << 9, RX_BK_BANDWIDTH_ALL = (1u << 10) - 1u };
// The two disjoint RF tuning ranges of the fitted transceiver; end exclusive.
typedef struct { uint32_t start_hz, end_hz; } RadioSpan;
#define RADIO_RF_RANGES 2u
bool RADIO_MODEL_InRfRange(uint32_t hz, const RadioSpan *ranges);
typedef struct {
  bool si:1, broadcast:1, ssb_patch:1;
  bool bk_am:1;
  uint16_t bk_bandwidth_mask;
  const RadioSpan *bk_ranges;
} ReceiverCapabilities;
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
// Whether some mode of this hardware receives the frequency.
bool RADIO_MODEL_Covers(uint32_t frequency_hz, ReceiverCapabilities capabilities);
// The receivable part of a band holding the frequency, else the nearest
// such part; false when this hardware receives nothing in the band.
bool RADIO_MODEL_BandSpan(uint8_t band, uint32_t frequency_hz,
                          ReceiverCapabilities capabilities, RadioSpan *span);
// Next shortcut band that this hardware can receive in.
uint8_t RADIO_MODEL_NextBand(uint8_t current, ReceiverCapabilities capabilities);
RxResult RADIO_MODEL_Plan(uint32_t frequency_hz, RadioParameters parameters,
                          ReceiverCapabilities capabilities, RxPlan *plan);
uint32_t RADIO_MODEL_Resolution(RxDevice device, RxMode mode);
uint32_t RADIO_MODEL_Step(uint32_t frequency_hz, uint32_t step_hz, bool up);
typedef enum { RADIO_FIELD_MODE, RADIO_FIELD_STEP, RADIO_FIELD_BANDWIDTH, RADIO_FIELD_SQUELCH, RADIO_FIELD_RX_TONE } RadioParameterField;
bool RADIO_MODEL_Adjust(uint32_t frequency_hz, RadioParameters *parameters,
                        ReceiverCapabilities capabilities, RadioParameterField field, bool up);

#endif

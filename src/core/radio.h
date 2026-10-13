#ifndef RADIO_H
#define RADIO_H
#include "core/radio-model.h"
#include "core/channels.h"
#include "core/lootlist.h" // Measurement until metrics are independently defined.
#include <stdbool.h>
#include <stdint.h>

#define SQL_DELAY 50
extern bool gMonitorMode;

// Receiver hardware mirror. User-defined frequencies and presets are stored
// separately in RadioUserState, in Hz.
typedef struct {
  uint32_t frequency; // Driver's 10-Hz representation.
  uint16_t bandwidth; // Hardware-specific filter index.
  uint8_t squelch;
  RadioTone rx_tone;
  Radio radio_type;
  ModulationType modulation;
} RadioReceiver;

typedef struct {
  RadioReceiver context;
  Measurement msm;
  bool is_open;
} RadioState;

void RADIO_InitState(RadioState *state);
void RADIO_InitHardware(RadioState *state);
ReceiverCapabilities RADIO_GetReceiverCapabilities(void);
bool RADIO_ApplyRxPlan(RadioReceiver *receiver, const RxPlan *plan);
bool RADIO_SelectPreparedRxPlan(const RxPlan *plan, uint8_t devices, bool open);
void RADIO_Measure(RadioState *state);
void RADIO_SetAudioOpen(bool open);
void RADIO_StopReception(void);
void RADIO_UpdateSquelch(RadioState *state);
uint16_t RADIO_GetRSSI(const RadioReceiver *receiver);
uint8_t RADIO_GetSNR(const RadioReceiver *receiver);
uint8_t RADIO_GetNoise(const RadioReceiver *receiver);
uint8_t RADIO_GetGlitch(const RadioReceiver *receiver);
extern RadioState *gRadioState;
extern RadioState *vfo;
extern RadioReceiver *ctx;
#endif

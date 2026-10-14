#ifndef RADIO_RADIO_H
#define RADIO_RADIO_H
// The radio controller: slots, VFO/MR, the tune transaction, RDS, watch,
// Seek, FC and PTT. It runs from the system loop under every screen and
// knows nothing about menus, drafts or key prefixes; screens express
// intents through these calls and read state back for display.
#include "core/channel-model.h"
#include "core/capture.h"
#include "core/radio-state.h"
#include <stdbool.h>
#include <stdint.h>

// Lifecycle (system loop).
void RADIO_Init(void);           // After the configuration loads: tune the selection.
void RADIO_Update(void);         // Every loop, whatever screen is shown.
void RADIO_TxSafetyUpdate(void); // Highest priority: PTT, battery and TOT.
// Draws the screen at once; the controller calls it before a slow retune.
void RADIO_SetRedrawHook(void (*redraw)(void));

// Tuning the selected slot. A target the hardware can only approximate
// waits for RADIO_ConfirmTune; RADIO_PendingTune shows it.
void RADIO_TuneTo(uint32_t frequency_hz); // Keypad entry: band presets apply.
void RADIO_Step(bool up);                 // VFO step, or the next MR channel.
void RADIO_NextBand(void);
void RADIO_NextMode(void);
void RADIO_SwitchSlot(void);
void RADIO_SwitchSource(void);            // VFO ↔ MR.
const RxPlan *RADIO_PendingTune(void);    // NULL when nothing awaits confirmation.
void RADIO_ConfirmTune(bool accept);

// Selected slot settings. VFO-only changes refuse with a notice in MR.
const RadioSlot *RADIO_Selected(void);    // Effective state (MR: the channel).
uint8_t RADIO_SelectedId(void);
void RADIO_AdjustRx(RadioParameterField field, bool up);
void RADIO_ResetToPreset(void);
bool RADIO_SetTx(const TxConfig *tx);     // False (and a notice) when refused.
void RADIO_CycleTxPower(bool up);      // Low → Medium → High, with a notice.

// Features that own RF while they run.
void RADIO_StartSeek(void);
void RADIO_CancelSeek(void);
bool RADIO_Seeking(void);
void RADIO_StartCapture(void);
void RADIO_CaptureAdopt(void);            // Tune to the reading on screen.
void RADIO_CaptureCancel(void);
void RADIO_CaptureNextScope(void);
bool RADIO_Capturing(void);
void RADIO_PttPress(void);
void RADIO_PttRelease(void);              // Always honoured, even under keylock.
bool RADIO_TxBusy(void);
void RADIO_CancelInteractive(void);       // The main screen was left: stop FC, Seek, PTT.

// Notices and global state for display.
void RADIO_Notify(const char *text);
const char *RADIO_Notice(void);           // Brief notice (about 2.5 s), else NULL.
const char *RADIO_WatchStatus(void);      // Why watch is not running, else NULL.
bool RADIO_IsReceiving(void);
bool RADIO_IsWatching(void);
uint8_t RADIO_Heard(void);                // Slot on the speaker, UINT8_MAX if none.

typedef enum {
  RADIO_ACTIVITY_IDLE, RADIO_ACTIVITY_RECEIVING, RADIO_ACTIVITY_TRANSMITTING,
  RADIO_ACTIVITY_SEEKING, RADIO_ACTIVITY_UNAVAILABLE, RADIO_ACTIVITY_FAULT
} RadioActivity;
typedef struct {
  RadioSlot slot;           // Effective state: MR slots carry their channel.
  uint32_t frequency_hz;    // Live frequency (the Seek cursor while seeking).
  bool channel_missing;     // MR slot whose channel cannot be loaded.
  bool modified;            // VFO parameters differ from the band preset.
  bool receiving;           // This slot is heard and its signal measured.
  uint8_t signal_level;     // 0..14 while receiving.
  RadioActivity activity;
  char name[USER_CHANNEL_NAME_BYTES]; // Channel name or RDS PS; "" if none.
  const char *radio_text;   // Fresh RDS RadioText, else NULL.
  bool fc_running;          // FC runs on this slot.
  uint32_t capture_hz;      // Latest FC reading, 0 before the first.
  CaptureScope capture_scope;
  CaptureRange capture_range;
} RadioSlotStatus;
void RADIO_SlotStatus(uint8_t id, RadioSlotStatus *status);

// Channels.
void RADIO_ChannelDraft(UserChannel *channel); // Selected slot as a channel; PS as name.
uint16_t RADIO_ChannelNumber(void);            // Selected slot's channel, 0 in VFO.
void RADIO_ChannelSaved(uint16_t number, const UserChannel *channel);

// Settings hooks.
void RADIO_SlotsChanged(void);    // Slot count changed.
void RADIO_ChannelsChanged(void); // A channel was renamed or deleted.
bool RADIO_ApplyRxSettings(void); // Reapply the receive plan after RX preferences.

#endif

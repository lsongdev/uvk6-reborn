#ifndef APPS_RADIO_INTERNAL_H
#define APPS_RADIO_INTERNAL_H

// Private interface between the Radio app modules:
//   radio.c         slots, VFO/MR resolution, tune transaction, key dispatch
//   radio-seek.c    Seek state machine (temporary tuning, one final commit)
//   radio-capture.c FC frequency capture and preview
//   radio-watch.c   dual watch and primary restore
//   radio-tx.c      PTT, TX safety and per-slot TX settings
//   radio-menu.c    per-slot menu
//   radio-view.c    cards, title and confirmation rendering
#include "apps/radio.h"
#include "hal/display.h"
#include "core/text.h"
#include "ui/menu.h"
#include "core/receiver.h"
#include "core/misc.h"
#include "core/device-config.h"
#include "core/channel-store.h"
#include "core/channel-model.h"
#include "core/tuning.h"
#include "core/rx-runtime.h"
#include "core/watch.h"
#include "core/tx-model.h"
#include "core/tx-runtime.h"
#include "hal/tx-device.h"
#include "core/settings.h"
#include "core/seek.h"
#include "core/capture.h"
#include "core/rds-receiver.h"
#include "hal/rx-device.h"
#include "hal/scheduler.h"
#include "ui/components/signal.h"
#include "ui/components/frequency.h"
#include "ui/components/radio-card.h"
#include "ui/graphics.h"
#include "ui/statusline.h"
#include "apps/apps.h"
#include "ui/number-input.h"
#include "apps/channel-manager.h"
#include <string.h>

// RDS text belongs to the frequency it was received on.
// rt_until: a new RadioText owns the whole card header until then (0: no).
typedef struct { uint32_t frequency_hz, rt_until; char ps[9], text[65]; } SlotLabel;
#define RADIO_RT_SHOW_MS 10000u


// RX items share RadioParameterField order; changeValue passes them through.
enum {
  MENU_RX_MODE = RADIO_FIELD_MODE,
  MENU_RX_STEP = RADIO_FIELD_STEP,
  MENU_RX_BANDWIDTH = RADIO_FIELD_BANDWIDTH,
  MENU_RX_SQUELCH = RADIO_FIELD_SQUELCH,
  MENU_RX_TONE = RADIO_FIELD_RX_TONE,
  MENU_TX_ENABLE,
  MENU_TX_POWER,
  MENU_TX_SHIFT,
  MENU_TX_OFFSET, // TX Offset for +/-, TX Freq for Split.
  MENU_TX_TONE,
  MENU_RESET,
  MENU_SAVE_CHANNEL,
  MENU_ITEM_COUNT
};

// radio.c: selection and committed receive state
extern uint8_t selectedSlot, receivingSlot, inputSlot;
extern RadioSlot activeSlot; // Selected slot's effective source; MR never writes VFO memories.
extern RxRuntime runtime;    // Last committed receive plan, the restore target.
extern SlotLabel labels[RADIO_SLOT_CAPACITY];
extern RdsReceiver rds;
extern NumberInput number;
extern bool inputTx, inMenu, confirming, functionPending;
extern RxPlan pending;
extern const char *message;
void notify(const char *text);
bool loadChannelForSlot(uint8_t id, UserChannel *channel);
bool effectiveSlot(uint8_t id, RadioSlot *effective, UserChannel *channel);
bool applyReceiver(void *unused, const RxPlan *plan);
void stopReceiver(void *unused);
bool apply(const RxPlan *plan);
void request(uint32_t target, RadioParameters values);
void beginFrequencyInput(bool tx);
void nextMode(void);
void resetRds(void);

// radio-seek.c
extern Seek seek;
void startSeek(void);
void seekUpdate(void);
void finishSeek(bool found, uint32_t frequency_hz, const char *status);

// radio-capture.c. Set by the Makefile (ENABLE_FC); host tests keep FC.
#ifndef UVK6_ENABLE_FC
#define UVK6_ENABLE_FC 1
#endif
extern CaptureRuntime capture;
#if UVK6_ENABLE_FC
extern bool capturing;
#else
// A constant lets every FC branch in the other modules fold away.
#define capturing false
#endif
void startCapture(void);
void captureUpdate(void);
void captureKey(KEY_Code_t key, Key_State_t state);
void cancelCapture(const char *status);

// radio-watch.c
extern WatchRuntime watcher;
extern const char *watchStatus;
void watchUpdate(void);
bool pauseWatch(void);
bool pauseWatchTo(const WatchTarget *target);
void suspendWatchBk(bool suspend);
void delayWatch(void);

// radio-tx.c
extern TxRuntime transmitter;
bool txBusy(void);
void pttKey(Key_State_t state);
bool setTxValue(uint32_t value);
extern const char *const txPowerNames[3]; // Indexed by TxPower.
bool changeTxPower(bool up);
extern uint8_t txSlot; // Slot PTT transmits on: selected, or a talkback reply.

// radio-menu.c
bool menuKey(KEY_Code_t key, Key_State_t state);
void renderMenu(void);

// radio-view.c

#endif

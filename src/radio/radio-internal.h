#ifndef RADIO_INTERNAL_H
#define RADIO_INTERNAL_H

// Private state shared by the radio controller modules:
//   radio.c   slots, VFO/MR resolution, the tune transaction, RDS, status
//   seek.c    Seek (temporary tuning, one final commit)
//   capture.c FC frequency capture
//   tone-scan.c independent fixed-frequency CSS identification
//   watch.c   multi-slot watch and primary restore
//   tx.c      PTT, TX safety, talkback
// Nothing here knows about screens.
#include "radio/radio.h"
#include "core/text.h"
#include "radio/receiver.h"
#include "core/misc.h"
#include "store/device-config.h"
#include "store/channel-store.h"
#include "core/tuning.h"
#include "radio/rx-runtime.h"
#include "core/watch.h"
#include "core/tx-model.h"
#include "radio/tx-runtime.h"
#include "store/preferences.h"
#include "core/seek.h"
#include "core/rds-receiver.h"
#include "devices/tx-device.h"
#include "devices/rx-device.h"
#include "hal/scheduler.h"
#include "devices/display.h" // gRedrawScreen: ask the system loop to redraw.
#include <string.h>

// RDS text belongs to the frequency it was received on.
// rt_until: a new RadioText is shown until then (0: not shown).
typedef struct { uint32_t frequency_hz, rt_until; char ps[9], text[65]; } SlotLabel;
#define RADIO_RT_SHOW_MS 10000u

// radio.c: selection and committed receive state
extern bool initialized;
extern uint8_t selectedSlot, receivingSlot;
extern RadioSlot activeSlot; // Selected slot's effective source; MR never writes VFO memories.
extern RxRuntime runtime;    // Last committed receive plan, the restore target.
extern SlotLabel labels[RADIO_SLOT_CAPACITY];
extern RdsReceiver rds;
extern RxPlan pending;
extern bool confirming;
extern const char *message;
void notify(const char *text);
bool effectiveSlot(uint8_t id, RadioSlot *effective, UserChannel *channel);
bool applyReceiver(void *unused, const RxPlan *plan);
void stopReceiver(void *unused);
bool apply(const RxPlan *plan);
void request(uint32_t target, RadioParameters values);
bool requireVfoSource(void);
void resetRds(void);

// seek.c
extern Seek seek;
void seekUpdate(void);
void finishSeek(bool found, uint32_t frequency_hz, const char *status);

// capture.c. Set by the Makefile (ENABLE_FC); host tests keep FC.
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
void captureUpdate(void);
void cancelCapture(const char *status);

// tone-scan.c
extern bool toneScanning;
void toneScanUpdate(void);

// spectrum.c
#if UVK6_ENABLE_SPECTRUM
extern bool spectrumOn;
#else
#define spectrumOn false
#endif
void spectrumUpdate(void);

// watch.c
extern WatchRuntime watcher;
extern const char *watchStatus;
void watchUpdate(void);
bool pauseWatch(void);
bool restoreSelectedReceiver(void);
bool pauseWatchTo(const WatchTarget *target);
void suspendWatchBk(bool suspend);
void delayWatch(void);

// tx.c
extern TxRuntime transmitter;
void RADIO_AbortTx(void);
extern uint8_t txSlot; // Slot PTT transmits on: selected, or a talkback reply.

#endif

#ifndef RADIO_WATCH_H
#define RADIO_WATCH_H
#include "core/radio-state.h"

// Multi-slot watch over every enabled slot. Sources are split by behaviour,
// not by chip:
// - one auxiliary background (SI including SSB, or BK1080) plays
//   whenever nothing else talks; the selected slot wins, else the first;
// - squelched BK channels are events: polled in turn, and the first one with
//   a confirmed signal takes the speaker until it ends plus a hang time.
// Selected BK SQ0 is exclusive: no interrupted continuous listening.
// Events never interrupt each other; the selected slot has no audio priority.
typedef struct { uint8_t id; RxPlan plan; } WatchTarget;
typedef enum { WATCH_OFF, WATCH_POLL, WATCH_SETTLE, WATCH_VERIFY,
               WATCH_HOLD, WATCH_HANG, WATCH_RETRY } WatchPhase;
typedef enum { WATCH_QUIET, WATCH_BACKGROUND, WATCH_EVENT } WatchAudio;
typedef struct {
  WatchTarget events[RADIO_WATCH_LIMIT];
  WatchTarget background; // Valid when has_background.
  uint32_t deadline, hang_until, background_retry;
  uint32_t blocked_until[RADIO_WATCH_LIMIT];
  uint8_t count, cursor, blocked, devices;
  WatchPhase phase;
  WatchAudio audio;
  bool has_background, background_ready;
  bool bk_suspended; // FC borrows BK: BK slots sit out, broadcasts go on.
} WatchRuntime;
typedef struct {
  void *context;
  // Tune a target with its audio muted; other chips keep receiving.
  bool (*prepare)(void *context, const WatchTarget *target);
  bool (*signal)(void *context, const RxPlan *plan, bool *found);
  // Route the speaker to target (open) or silence every watched chip.
  bool (*select)(void *context, const WatchTarget *target, bool open);
  uint32_t (*now)(void *context); // Settle starts after blocking driver calls.
} WatchOperations;

#define WATCH_SETTLE_MS 180u
#define WATCH_VERIFY_MS 80u
#define WATCH_HANG_MS 2000u
#define WATCH_RETRY_MS 2000u
#define WATCH_SAMPLE_MS 50u

bool WATCH_IsEvent(const RxPlan *plan);
bool WATCH_SameReceiver(const RxPlan *a, const RxPlan *b);
// False when fewer than two distinct sources remain to watch.
bool WATCH_Start(WatchRuntime *runtime, const WatchTarget *targets, uint8_t count,
                 uint8_t selected_id, uint32_t now);
void WATCH_Stop(WatchRuntime *runtime);
void WATCH_Update(WatchRuntime *runtime, uint32_t now, const WatchOperations *ops);
// Slot owning the speaker, or UINT8_MAX while everything is quiet.
uint8_t WATCH_Owner(const WatchRuntime *runtime);
bool WATCH_IsProbing(const WatchRuntime *runtime);
// Take every BK event out of the watch while an independent auxiliary
// background keeps playing; resuming puts
// them back and polling starts over.
void WATCH_SuspendBk(WatchRuntime *runtime, bool suspend, uint32_t now, const WatchOperations *ops);
// The event being heard or in its reply window: PTT answers it (talkback).
bool WATCH_Talk(const WatchRuntime *runtime, WatchTarget *target);
#endif

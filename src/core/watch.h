#ifndef RADIO_WATCH_H
#define RADIO_WATCH_H
#include "core/radio-state.h"

// Saved VFO capacity, visible rows and watch candidates are separate budgets.
typedef struct { uint8_t id; RxPlan plan; } WatchTarget;
typedef enum { WATCH_OFF, WATCH_TUNE, WATCH_SETTLE, WATCH_VERIFY,
               WATCH_HOLD, WATCH_HANG, WATCH_RETRY } WatchPhase;
typedef struct {
  WatchTarget targets[RADIO_WATCH_LIMIT];
  uint32_t deadline, hold_at, hang_until, blocked_until[RADIO_WATCH_LIMIT];
  uint8_t count, cursor, blocked, priority_id;
  WatchPhase phase;
  bool signal, parallel;
  uint8_t prepared_mask, candidate_mask, signal_mask;
  uint32_t stable_at[RADIO_WATCH_LIMIT], confirm_at[RADIO_WATCH_LIMIT];
} WatchRuntime;
typedef struct {
  void *context;
  bool (*tune)(void *context, const WatchTarget *target);
  bool (*signal)(void *context, const RxPlan *plan, bool *found);
  void (*audio)(void *context, bool open);
  bool (*prepare)(void *context, const WatchTarget *target); // Keep other chips receiving.
  bool (*select)(void *context, const WatchTarget *target, bool open); // Audio only.
  uint32_t (*now)(void *context); // Settle starts after blocking driver calls.
} WatchOperations;

#define WATCH_VERIFY_MS 80u
#define WATCH_HANG_MS 1000u
#define WATCH_MAX_HOLD_MS 3000u
#define WATCH_RETRY_MS 2000u
#define WATCH_SAMPLE_MS 50u

bool WATCH_Eligible(const RxPlan *plan);
bool WATCH_SameReceiver(const RxPlan *a, const RxPlan *b);
// Selected participant owns audio; UINT8_MAX means no priority participant.
bool WATCH_Start(WatchRuntime *runtime, const WatchTarget *targets, uint8_t count, uint8_t selected_id);
void WATCH_Stop(WatchRuntime *runtime);
void WATCH_Update(WatchRuntime *runtime, uint32_t now, const WatchOperations *ops);
bool WATCH_IsProbing(const WatchRuntime *runtime);
#endif

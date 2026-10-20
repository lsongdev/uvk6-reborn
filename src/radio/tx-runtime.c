#include "radio/tx-runtime.h"

static bool onAir(const TxRuntime *runtime) {
  return runtime->mode == RADIO_TX || runtime->mode == RADIO_TX_TAIL;
}
static void stop(TxRuntime *runtime, const TxOperations *ops, bool requireRelease) {
  // Hard stops always disable PA before restoring RX, with no tail delay.
  ops->disable(ops->context);
  bool restored = ops->restore_rx(ops->context, &runtime->previous_rx);
  runtime->mode = restored ? RADIO_RX : RADIO_FAULT;
  if (requireRelease) runtime->wait_release = true;
}
static bool expired(const TxRuntime *runtime, uint32_t now_ms) {
  return (uint32_t)(now_ms - runtime->started_at) >=
         (uint32_t)runtime->tot_seconds * 1000u;
}

bool TX_RuntimePress(TxRuntime *runtime, const RxPlan *rx,
                     const TxPlan *plan, uint16_t tot_seconds,
                     uint32_t now_ms, const TxOperations *ops) {
  if (!runtime || !rx || !plan || !ops || !ops->mute_rx ||
      !ops->prepare || !ops->enable || !ops->disable || !ops->restore_rx ||
      runtime->mode != RADIO_RX || runtime->wait_release ||
      tot_seconds == 0 || tot_seconds > 600)
    return false;
  runtime->previous_rx = *rx;
  runtime->tot_seconds = tot_seconds;
  runtime->started_at = now_ms;
  runtime->tail = plan->tail && plan->tone != 0;
  ops->disable(ops->context);
  ops->mute_rx(ops->context);
  if (!ops->prepare(ops->context, plan) || !ops->enable(ops->context)) {
    stop(runtime, ops, true); // A failed attempt needs a new PTT edge.
    return false;
  }
  runtime->mode = RADIO_TX;
  return true;
}

void TX_RuntimeRelease(TxRuntime *runtime, uint32_t now_ms, const TxOperations *ops) {
  if (!runtime || !ops || !ops->disable) return;
  runtime->wait_release = false;
  if (onAir(runtime) && ops->restore_rx && expired(runtime, now_ms)) {
    stop(runtime, ops, true);
    return;
  }
  // Repeated queued/physical releases must not restart or cancel the tail.
  if (runtime->mode == RADIO_TX_TAIL) return;
  if (runtime->mode == RADIO_TX && ops->restore_rx) {
    if (runtime->tail && ops->begin_tail && ops->begin_tail(ops->context)) {
      runtime->tail_started_at = now_ms;
      runtime->mode = RADIO_TX_TAIL;
    } else {
      stop(runtime, ops, false);
    }
  } else ops->disable(ops->context);
}

void TX_RuntimeAbort(TxRuntime *runtime, const TxOperations *ops) {
  if (!runtime || !ops || !ops->disable) return;
  if (onAir(runtime) && ops->restore_rx) stop(runtime, ops, true);
  else ops->disable(ops->context);
}

TxRuntimeEvent TX_RuntimeUpdate(TxRuntime *runtime, uint32_t now_ms,
                                const TxOperations *ops) {
  if (!runtime || !onAir(runtime) || !ops || !ops->disable || !ops->restore_rx)
    return TX_EVENT_NONE;
  // TOT also bounds the tail, including when release occurs at the deadline.
  if (expired(runtime, now_ms)) {
    stop(runtime, ops, true);
    return TX_EVENT_TIMEOUT;
  }
  if (runtime->mode == RADIO_TX_TAIL &&
      (uint32_t)(now_ms-runtime->tail_started_at) >= TX_TAIL_MS) {
    stop(runtime, ops, false);
    return TX_EVENT_ENDED;
  }
  return TX_EVENT_NONE;
}

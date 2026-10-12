#include "core/tx-runtime.h"

static void stop(TxRuntime *runtime, const TxOperations *ops, bool timeout) {
  // The first operation on all exit paths is always disable.
  ops->disable(ops->context);
  bool restored = ops->restore_rx(ops->context, &runtime->previous_rx);
  runtime->mode = restored ? RADIO_RX : RADIO_FAULT;
  if (timeout)
    runtime->wait_release = true;
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
  ops->disable(ops->context);
  ops->mute_rx(ops->context);
  if (!ops->prepare(ops->context, plan) ||
      !ops->enable(ops->context)) {
    stop(runtime, ops, false);
    // Failed attempts must require a new PTT edge, never auto retry.
    runtime->wait_release = true;
    return false;
  }
  runtime->mode = RADIO_TX;
  return true;
}

void TX_RuntimeRelease(TxRuntime *runtime, const TxOperations *ops) {
  if (!runtime || !ops || !ops->disable)
    return;
  if (runtime->mode == RADIO_TX && ops->restore_rx)
    stop(runtime, ops, false);
  else
    ops->disable(ops->context);
  runtime->wait_release = false;
}

bool TX_RuntimeUpdate(TxRuntime *runtime, uint32_t now_ms,
                      const TxOperations *ops) {
  if (!runtime || runtime->mode != RADIO_TX || !ops ||
      !ops->disable || !ops->restore_rx)
    return false;
  if ((uint32_t)(now_ms - runtime->started_at) <
      (uint32_t)runtime->tot_seconds * 1000u)
    return false;
  stop(runtime, ops, true);
  return true;
}

#include "radio/rx-runtime.h"

void RX_RuntimeStop(RxRuntime *runtime, RxStop stop, void *context) {
  if (runtime)
    runtime->active = false;
  if (stop)
    stop(context);
}

RxTuneResult RX_RuntimeTune(RxRuntime *runtime, const RxPlan *plan,
                            RxApply apply, RxStop stop, void *context) {
  if (!runtime || !plan || !apply) {
    RX_RuntimeStop(runtime, stop, context);
    return RX_STOPPED;
  }

  if (apply(context, plan)) {
    runtime->current = *plan;
    runtime->active = true;
    return RX_APPLIED;
  }

  // The attempted hardware configuration may have partially completed.
  // Restore the previously applied plan once; never report the new one.
  if (runtime->active && apply(context, &runtime->current))
    return RX_RESTORED;

  RX_RuntimeStop(runtime, stop, context);
  return RX_STOPPED;
}

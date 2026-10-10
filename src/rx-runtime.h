#ifndef RX_RUNTIME_H
#define RX_RUNTIME_H

#include "radio-model.h"

// Tracks only the last successfully applied receive plan. The UI may still
// navigate unsupported frequencies without pretending they are tuned.
typedef struct {
  RxPlan current;
  bool active;
} RxRuntime;

typedef bool (*RxApply)(void *context, const RxPlan *plan);
typedef void (*RxStop)(void *context);

typedef enum { RX_APPLIED, RX_RESTORED, RX_STOPPED } RxTuneResult;

RxTuneResult RX_RuntimeTune(RxRuntime *runtime, const RxPlan *plan,
                            RxApply apply, RxStop stop, void *context);
void RX_RuntimeStop(RxRuntime *runtime, RxStop stop, void *context);

#endif

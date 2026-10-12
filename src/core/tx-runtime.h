#ifndef TX_RUNTIME_H
#define TX_RUNTIME_H
#include "core/tx-model.h"

// The runtime owns half-duplex transitions. Any error disables PA first;
// the RX plan remains frozen until receive restoration completes.
typedef enum { RADIO_RX, RADIO_TX, RADIO_FAULT } RadioMode;
typedef struct {
  RadioMode mode;
  RxPlan previous_rx;
  uint32_t started_at;
  uint16_t tot_seconds;
  bool wait_release;
} TxRuntime;
typedef struct {
  void *context;
  void (*mute_rx)(void *);
  bool (*prepare)(void *, const TxPlan *);
  bool (*enable)(void *);
  void (*disable)(void *);
  bool (*restore_rx)(void *, const RxPlan *);
} TxOperations;

bool TX_RuntimePress(TxRuntime *runtime, const RxPlan *rx,
                     const TxPlan *plan, uint16_t tot_seconds,
                     uint32_t now_ms, const TxOperations *operations);
void TX_RuntimeRelease(TxRuntime *runtime, const TxOperations *operations);
bool TX_RuntimeUpdate(TxRuntime *runtime, uint32_t now_ms,
                      const TxOperations *operations);
#endif

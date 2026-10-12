#ifndef RX_DEVICE_H
#define RX_DEVICE_H

#include "core/radio-model.h"
#include "core/rds-receiver.h"

// Apply an already validated receive plan once. This is the sole hardware
// tuning path; callers own the user state and failed-plan recovery.
bool RX_DEVICE_Apply(const RxPlan *plan, RxDevice previous);
bool RX_DEVICE_WatchSignal(const RxPlan *plan, bool *found);
bool RX_DEVICE_ReadRDS(RdsPacket *packet, bool clear_fifo);

typedef enum { RX_SEEK_BUSY, RX_SEEK_FOUND, RX_SEEK_LIMIT, RX_SEEK_ERROR } RxSeekResult;
bool RX_DEVICE_SeekNative(const RxPlan *plan);
bool RX_DEVICE_SeekStart(const RxPlan *plan, uint32_t lower_hz,
                         uint32_t upper_hz);
RxSeekResult RX_DEVICE_SeekPoll(RxDevice device, uint32_t *frequency_hz);
void RX_DEVICE_SeekCancel(RxDevice device);
bool RX_DEVICE_SeekSignal(RxDevice device, bool *found);

#endif

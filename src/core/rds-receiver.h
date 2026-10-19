#ifndef CORE_RDS_RECEIVER_H
#define CORE_RDS_RECEIVER_H
#include "core/rds.h"

typedef struct {
  RdsGroup group;
  uint8_t fifo_used; // Includes the returned entry; zero means no group.
  bool synchronized, overflow;
} RdsPacket;
typedef bool (*RdsRead)(void *context, RdsPacket *packet, bool clear_fifo);
typedef struct {
  RdsDecoder decoder;
  uint32_t poll_at, last_group;
  bool enabled, clear_fifo;
} RdsReceiver;

void RDS_ReceiverReset(RdsReceiver *receiver, uint32_t now);
// Bounded polling; true when visible text changes or expires.
bool RDS_ReceiverUpdate(RdsReceiver *receiver, uint32_t now, bool enabled,
                        RdsRead read, void *context);
#endif

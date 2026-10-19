#include "core/rds-receiver.h"
#include <string.h>

void RDS_ReceiverReset(RdsReceiver *r, uint32_t now) {
  memset(r, 0, sizeof(*r));
  r->poll_at = now+300; // Allow tuning to settle before clearing old FIFO data.
  r->last_group = now;
  r->clear_fifo = true;
}

bool RDS_ReceiverUpdate(RdsReceiver *r, uint32_t now, bool enabled,
                        RdsRead read, void *context) {
  bool changed = false;
  if (!enabled) {
    changed = r->decoder.ps[0] || r->decoder.text[0];
    if (r->enabled) RDS_ReceiverReset(r, now);
    return changed;
  }
  if (!r->enabled) {
    RDS_ReceiverReset(r, now);
    r->enabled = true;
  }
  if (now-r->last_group >= 15000) {
    changed = r->decoder.ps[0] || r->decoder.text[0];
    RDS_Reset(&r->decoder);
    r->last_group = now;
  }
  if (!read || (int32_t)(now-r->poll_at) < 0) return changed;
  r->poll_at = now+100;
  RdsPacket packet;
  if (r->clear_fifo) {
    if (read(context, &packet, true)) r->clear_fifo = false;
    return changed;
  }
  // RDS carries about 11 groups/sec. Four per 100 ms drains bursts while
  // keeping keyboard/audio work bounded; stop on the first bus failure.
  for (unsigned i=0; i<4; ++i) {
    if (!read(context, &packet, false) || !packet.fifo_used) break;
    if (packet.overflow) {
      changed |= r->decoder.ps[0] || r->decoder.text[0];
      RDS_Reset(&r->decoder);
    }
    if (packet.synchronized && packet.group.errors[0] <= 1 &&
        packet.group.errors[1] <= 1) {
      r->last_group = now;
      changed |= RDS_Feed(&r->decoder, &packet.group);
    }
    if (packet.fifo_used == 1) break;
  }
  return changed;
}

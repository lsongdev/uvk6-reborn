#include "core/watch.h"

bool WATCH_Eligible(const RxPlan *p) {
  // SSB mode/patch handoffs need measured latency before enabling polling.
  return p && p->parameters.mode != RX_LSB && p->parameters.mode != RX_USB;
}
bool WATCH_SameReceiver(const RxPlan *a, const RxPlan *b) {
  if (!a || !b) return false;
  const RadioParameters *x=&a->parameters, *y=&b->parameters;
  return a->device==b->device && a->frequency_hz==b->frequency_hz &&
         x->mode==y->mode && x->bandwidth_hz==y->bandwidth_hz &&
         x->squelch==y->squelch && x->rx_tone==y->rx_tone;
}
bool WATCH_Start(WatchRuntime *r, const WatchTarget *targets, uint8_t count) {
  if (!r || !targets || count<2 || count>RADIO_WATCH_LIMIT) return false;
  WatchRuntime next={0};
  for (uint8_t i=0; i<count; ++i) {
    if (targets[i].id>=RADIO_VFO_CAPACITY) return false;
    if (!WATCH_Eligible(&targets[i].plan)) continue;
    bool duplicate=false;
    for (uint8_t j=0; j<next.count; ++j) {
      if (targets[i].id==next.targets[j].id) return false;
      if (WATCH_SameReceiver(&targets[i].plan,&next.targets[j].plan)) duplicate=true;
    }
    if (!duplicate) next.targets[next.count++]=targets[i];
  }
  if (next.count<2) return false;
  next.phase=WATCH_TUNE;
  *r=next;
  return true;
}
void WATCH_Stop(WatchRuntime *r) { if (r) *r=(WatchRuntime){0}; }
bool WATCH_IsProbing(const WatchRuntime *r) {
  return r && r->phase!=WATCH_OFF && r->phase!=WATCH_RETRY &&
         !(r->phase==WATCH_HOLD && r->signal);
}
static uint32_t clockNow(const WatchOperations *ops, uint32_t fallback) {
  return ops->now ? ops->now(ops->context) : fallback;
}
static void advance(WatchRuntime *r, const WatchOperations *ops) {
  ops->audio(ops->context,false);
  r->signal=false;
  r->cursor=(r->cursor+1u)%r->count;
  r->phase=WATCH_TUNE;
}
static void failure(WatchRuntime *r, uint32_t now, const WatchOperations *ops) {
  r->blocked |= 1u<<r->cursor;
  r->blocked_until[r->cursor]=now+WATCH_RETRY_MS;
  advance(r,ops);
}
void WATCH_Update(WatchRuntime *r, uint32_t now, const WatchOperations *ops) {
  if (!r || r->phase==WATCH_OFF || !ops || !ops->tune ||
      !ops->signal || !ops->audio || r->count<2) return;
  if (r->phase==WATCH_RETRY) {
    if ((int32_t)(now-r->deadline)<0) return;
    r->phase=WATCH_TUNE;
  }
  if (r->phase==WATCH_TUNE) {
    // At most count candidates, and one RF apply per update: never spin.
    uint8_t checked=0;
    while (checked<r->count && (r->blocked&(1u<<r->cursor)) &&
           (int32_t)(now-r->blocked_until[r->cursor])<0) {
      r->cursor=(r->cursor+1u)%r->count; ++checked;
    }
    if (checked==r->count) {
      r->phase=WATCH_RETRY; r->deadline=now+WATCH_RETRY_MS; return;
    }
    r->blocked &= ~(1u<<r->cursor);
    ops->audio(ops->context,false);
    if (!ops->tune(ops->context,&r->targets[r->cursor])) {
      failure(r,clockNow(ops,now),ops); return;
    }
    uint32_t settle=r->targets[r->cursor].plan.device==RX_DEVICE_BK ? 180u : 350u;
    r->deadline=clockNow(ops,now)+settle;
    r->phase=WATCH_SETTLE;
    return;
  }
  if ((int32_t)(now-r->deadline)<0) return;
  bool found=false;
  if (!ops->signal(ops->context,&r->targets[r->cursor].plan,&found)) {
    failure(r,clockNow(ops,now),ops); return;
  }
  now=clockNow(ops,now);
  switch (r->phase) {
  case WATCH_SETTLE:
    if (!found) { advance(r,ops); return; }
    r->phase=WATCH_VERIFY; r->deadline=now+WATCH_VERIFY_MS; return;
  case WATCH_VERIFY:
    if (!found) { advance(r,ops); return; }
    r->hold_at=now; r->phase=WATCH_HOLD; break;
  case WATCH_HOLD:
  case WATCH_HANG:
    if (now-r->hold_at>=WATCH_MAX_HOLD_MS) { advance(r,ops); return; }
    if (!found && r->phase==WATCH_HOLD) {
      r->phase=WATCH_HANG; r->hang_until=now+WATCH_HANG_MS;
      r->deadline=now+WATCH_SAMPLE_MS;
      r->signal=false; ops->audio(ops->context,false); return;
    }
    if (!found) {
      if ((int32_t)(now-r->hang_until)>=0) advance(r,ops);
      else r->deadline=now+WATCH_SAMPLE_MS;
      return;
    }
    r->phase=WATCH_HOLD; break;
  default: return;
  }
  r->signal=true; r->deadline=now+WATCH_SAMPLE_MS;
  ops->audio(ops->context,true);
}

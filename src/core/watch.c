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
bool WATCH_Start(WatchRuntime *r, const WatchTarget *targets, uint8_t count, uint8_t selected_id) {
  if (!r || !targets || count<2 || count>RADIO_WATCH_LIMIT) return false;
  WatchRuntime next={.priority_id=selected_id};
  for (uint8_t i=0; i<count; ++i) {
    if (targets[i].id>=RADIO_SLOT_CAPACITY) return false;
    if (!WATCH_Eligible(&targets[i].plan)) continue;
    bool duplicate=false;
    for (uint8_t j=0; j<next.count; ++j) {
      if (targets[i].id==next.targets[j].id) return false;
      if (WATCH_SameReceiver(&targets[i].plan,&next.targets[j].plan)) duplicate=true;
    }
    if (!duplicate) next.targets[next.count++]=targets[i];
  }
  if (next.count<2) return false;
  for (uint8_t i=0; i<next.count; ++i)
    if (next.targets[i].id == selected_id) next.cursor=i;
  next.parallel = next.targets[0].plan.device != next.targets[1].plan.device;
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
static void parallelUpdate(WatchRuntime *r, uint32_t now, const WatchOperations *ops) {
  if (!ops->prepare || !ops->select) return;
  if (r->phase==WATCH_TUNE) r->deadline=now;
  // Configure each chip once; failed chips retry independently with a cooldown.
  for (uint8_t n=0; n<r->count; ++n) {
    uint8_t i=(r->cursor+n)%r->count, bit=1u<<i;
    if ((r->prepared_mask&bit) || ((r->blocked&bit) &&
        (int32_t)(now-r->blocked_until[i])<0)) continue;
    if (ops->prepare(ops->context,&r->targets[i])) {
      r->prepared_mask |= bit;
      r->blocked &= ~bit;
      r->stable_at[i]=clockNow(ops,now)+
        (r->targets[i].plan.device==RX_DEVICE_BK ? 180u : 350u);
    } else {
      r->blocked |= bit;
      r->blocked_until[i]=clockNow(ops,now)+WATCH_RETRY_MS;
    }
    now=clockNow(ops,now);
    break; // At most one hardware preparation per update.
  }
  if ((int32_t)(now-r->deadline)<0) return;
  uint8_t priority=UINT8_MAX;
  for (uint8_t i=0; i<r->count; ++i) {
    uint8_t bit=1u<<i;
    if (r->targets[i].id==r->priority_id) priority=i;
    if (!(r->prepared_mask&bit) || (int32_t)(now-r->stable_at[i])<0) continue;
    bool found=false;
    if (!ops->signal(ops->context,&r->targets[i].plan,&found)) {
      r->prepared_mask &= ~bit;
      r->blocked |= bit;
      r->blocked_until[i]=clockNow(ops,now)+WATCH_RETRY_MS;
      found=false;
    }
    if (!found) {
      r->candidate_mask &= ~bit;
      r->signal_mask &= ~bit;
    } else if (!(r->candidate_mask&bit)) {
      r->candidate_mask |= bit;
      r->confirm_at[i]=clockNow(ops,now)+WATCH_VERIFY_MS;
    } else if ((int32_t)(clockNow(ops,now)-r->confirm_at[i])>=0) {
      r->signal_mask |= bit;
    }
  }
  now=clockNow(ops,now);
  uint8_t chosen=UINT8_MAX;
  if (priority<r->count && (r->signal_mask&(1u<<priority))) chosen=priority;
  else if (r->cursor==priority && (r->signal || r->phase==WATCH_HANG)) {
    if (r->signal) r->hang_until=now+WATCH_HANG_MS;
    if ((int32_t)(now-r->hang_until)<0) {
      if (r->signal) (void)ops->select(ops->context,&r->targets[r->cursor],false);
      r->signal=false;r->phase=WATCH_HANG;r->deadline=now+WATCH_SAMPLE_MS;
      return;
    }
  }
  if (chosen==UINT8_MAX) {
    // Keep the current background owner when both are valid and no priority
    // participant exists; otherwise choose the first valid receiver.
    if (r->signal_mask&(1u<<r->cursor)) chosen=r->cursor;
    else for (uint8_t i=0; i<r->count; ++i)
      if (r->signal_mask&(1u<<i)) { chosen=i;break; }
  }
  if (chosen<r->count) {
    if ((!r->signal || chosen!=r->cursor) &&
        !ops->select(ops->context,&r->targets[chosen],true)) {
      uint8_t bit=1u<<chosen;
      r->signal_mask &= ~bit;r->candidate_mask &= ~bit;r->prepared_mask &= ~bit;
      r->blocked |= bit;r->blocked_until[chosen]=now+WATCH_RETRY_MS;
      r->signal=false;r->phase=WATCH_SETTLE;
    } else {
      r->cursor=chosen;r->signal=true;r->phase=WATCH_HOLD;
    }
  } else {
    if (r->signal) (void)ops->select(ops->context,&r->targets[r->cursor],false);
    r->signal=false;r->phase=r->prepared_mask ? WATCH_SETTLE : WATCH_RETRY;
  }
  r->deadline=now+(r->phase==WATCH_RETRY ? WATCH_RETRY_MS : WATCH_SAMPLE_MS);
}

void WATCH_Update(WatchRuntime *r, uint32_t now, const WatchOperations *ops) {
  if (!r || r->phase==WATCH_OFF || !ops || !ops->signal || r->count<2) return;
  if (!r->parallel && (!ops->tune || !ops->audio)) return;
  if (r->phase==WATCH_RETRY) {
    if ((int32_t)(now-r->deadline)<0) return;
    r->phase=WATCH_TUNE;
  }
  if (r->parallel) { parallelUpdate(r,now,ops); return; }
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
    if (r->targets[r->cursor].id != r->priority_id &&
        now-r->hold_at>=WATCH_MAX_HOLD_MS) { advance(r,ops); return; }
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

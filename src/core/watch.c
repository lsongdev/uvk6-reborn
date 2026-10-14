#include "core/watch.h"

bool WATCH_Eligible(const RxPlan *p) {
  // SSB mode/patch handoffs need measured latency before enabling polling.
  return p && p->parameters.mode != RX_LSB && p->parameters.mode != RX_USB;
}
// Continuous sound: a broadcast chip, or a BK channel whose squelch is off.
bool WATCH_IsBackground(const RxPlan *p) {
  return p && (p->device != RX_DEVICE_BK || !p->parameters.squelch);
}
bool WATCH_SameReceiver(const RxPlan *a, const RxPlan *b) {
  if (!a || !b) return false;
  const RadioParameters *x=&a->parameters, *y=&b->parameters;
  return a->device==b->device && a->frequency_hz==b->frequency_hz &&
         x->mode==y->mode && x->bandwidth_hz==y->bandwidth_hz &&
         x->squelch==y->squelch && x->rx_tone==y->rx_tone;
}

bool WATCH_Start(WatchRuntime *r, const WatchTarget *targets, uint8_t count,
                 uint8_t selected_id, uint32_t now) {
  if (!r || !targets || count>RADIO_WATCH_LIMIT) return false;
  // Deadlines are relative to now so a start near the clock wrap still polls.
  WatchRuntime next={.phase=WATCH_POLL,.deadline=now,.background_retry=now};
  // Only one background can play: the selected slot's, else the first.
  for (uint8_t i=0; i<count; ++i) {
    const WatchTarget *t=&targets[i];
    if (t->id>=RADIO_SLOT_CAPACITY) return false;
    if (!WATCH_Eligible(&t->plan) || !WATCH_IsBackground(&t->plan)) continue;
    if (!next.has_background || t->id==selected_id) next.background=*t;
    next.has_background=true;
  }
  for (uint8_t i=0; i<count; ++i) {
    const WatchTarget *t=&targets[i];
    if (!WATCH_Eligible(&t->plan) || WATCH_IsBackground(&t->plan)) continue;
    bool duplicate=false;
    for (uint8_t j=0; j<next.count; ++j)
      duplicate |= WATCH_SameReceiver(&t->plan,&next.events[j].plan);
    if (duplicate) continue;
    if (t->id==selected_id) next.cursor=next.count; // Check it first.
    next.events[next.count++]=*t;
  }
  if (next.count+next.has_background<2) return false;
  if (next.count) next.devices |= 1u<<RX_DEVICE_BK;
  if (next.has_background) next.devices |= 1u<<next.background.plan.device;
  *r=next;
  return true;
}
void WATCH_Stop(WatchRuntime *r) { if (r) *r=(WatchRuntime){0}; }

uint8_t WATCH_Owner(const WatchRuntime *r) {
  if (!r || r->phase==WATCH_OFF) return UINT8_MAX;
  return r->audio==WATCH_EVENT ? r->events[r->cursor].id :
         r->audio==WATCH_BACKGROUND ? r->background.id : UINT8_MAX;
}
bool WATCH_Talk(const WatchRuntime *r, WatchTarget *target) {
  if (!r || (r->phase!=WATCH_HOLD && r->phase!=WATCH_HANG)) return false;
  if (target) *target=r->events[r->cursor];
  return true;
}
bool WATCH_IsProbing(const WatchRuntime *r) {
  return r && r->phase!=WATCH_OFF && r->audio==WATCH_QUIET;
}

static uint32_t clockNow(const WatchOperations *ops, uint32_t fallback) {
  return ops->now ? ops->now(ops->context) : fallback;
}
static bool backgroundOnBk(const WatchRuntime *r) {
  return r->has_background && r->background.plan.device==RX_DEVICE_BK;
}
static void speaker(WatchRuntime *r, const WatchOperations *ops,
                    const WatchTarget *target, WatchAudio audio) {
  if (!ops->select(ops->context,target,audio!=WATCH_QUIET)) {
    if (audio==WATCH_BACKGROUND) r->background_ready=false;
    audio=WATCH_QUIET;
  }
  r->audio=audio;
}
static void block(WatchRuntime *r, uint32_t now) {
  r->blocked |= 1u<<r->cursor;
  r->blocked_until[r->cursor]=now+WATCH_RETRY_MS;
}
static void next(WatchRuntime *r, uint32_t now) {
  r->cursor=(r->cursor+1u)%r->count;
  r->phase=WATCH_POLL;
  r->deadline=now;
}
// A BK background is retuned and heard for a while after every round.
static void home(WatchRuntime *r, uint32_t now, const WatchOperations *ops) {
  r->visited=0;
  if ((int32_t)(now-r->background_retry)>=0 && ops->prepare(ops->context,&r->background)) {
    speaker(r,ops,&r->background,WATCH_BACKGROUND);
    r->phase=WATCH_HOME;
    r->deadline=clockNow(ops,now)+WATCH_HOME_MS;
    return;
  }
  r->background_retry=now+WATCH_RETRY_MS;
  r->phase=WATCH_POLL;
  r->deadline=now;
}
// After a talk (or its hang) the background plays again.
static void release(WatchRuntime *r, uint32_t now, const WatchOperations *ops) {
  r->audio=WATCH_QUIET;
  if (backgroundOnBk(r)) { r->cursor=(r->cursor+1u)%r->count; home(r,now,ops); return; }
  if (r->has_background && r->background_ready)
    speaker(r,ops,&r->background,WATCH_BACKGROUND);
  next(r,now);
}
static void poll(WatchRuntime *r, uint32_t now, const WatchOperations *ops) {
  if (backgroundOnBk(r) && (!r->count || r->visited>=r->count)) { home(r,now,ops); return; }
  uint8_t n=0;
  for (; n<r->count; ++n) {
    uint8_t i=(uint8_t)((r->cursor+n+0u)%r->count); // Unsigned: no __divsi3.
    if (!(r->blocked&(1u<<i)) || (int32_t)(now-r->blocked_until[i])>=0) { r->cursor=i; break; }
  }
  if (n==r->count) {
    if (backgroundOnBk(r)) { home(r,now,ops); return; }
    r->phase=WATCH_RETRY;
    r->deadline=now+WATCH_RETRY_MS;
    return;
  }
  r->blocked &= ~(1u<<r->cursor);
  ++r->visited;
  // Leaving a BK background for another channel must silence it first.
  if (r->audio==WATCH_BACKGROUND && backgroundOnBk(r))
    speaker(r,ops,&r->background,WATCH_QUIET);
  if (!ops->prepare(ops->context,&r->events[r->cursor])) {
    now=clockNow(ops,now); block(r,now); next(r,now); return;
  }
  r->phase=WATCH_SETTLE;
  r->deadline=clockNow(ops,now)+WATCH_SETTLE_MS;
}

void WATCH_SuspendBk(WatchRuntime *r, bool suspend, uint32_t now, const WatchOperations *ops) {
  if (!r || r->phase==WATCH_OFF || !ops || !ops->select || r->bk_suspended==suspend) return;
  r->bk_suspended=suspend;
  if (suspend) {
    bool broadcast=r->has_background && !backgroundOnBk(r) && r->background_ready;
    if (r->audio==WATCH_EVENT || r->phase==WATCH_HANG || (r->audio==WATCH_BACKGROUND && !broadcast)) {
      const WatchTarget *quiet=r->audio==WATCH_BACKGROUND ? &r->background : &r->events[r->cursor];
      if (broadcast) speaker(r,ops,&r->background,WATCH_BACKGROUND);
      else speaker(r,ops,quiet,WATCH_QUIET);
    }
  }
  r->visited=0;
  r->phase=WATCH_POLL;
  r->deadline=now;
}

void WATCH_Update(WatchRuntime *r, uint32_t now, const WatchOperations *ops) {
  if (!r || r->phase==WATCH_OFF || !ops || !ops->prepare || !ops->signal || !ops->select)
    return;
  // Bring a broadcast background up once; it keeps receiving underneath.
  if (r->has_background && !backgroundOnBk(r) && !r->background_ready &&
      (int32_t)(now-r->background_retry)>=0) {
    if (ops->prepare(ops->context,&r->background)) {
      r->background_ready=true;
      if (r->audio==WATCH_QUIET && r->phase!=WATCH_HANG)
        speaker(r,ops,&r->background,WATCH_BACKGROUND);
    } else r->background_retry=clockNow(ops,now)+WATCH_RETRY_MS;
    return; // At most one hardware preparation per update.
  }
  if (r->bk_suspended || (int32_t)(now-r->deadline)<0) return;
  if (r->phase==WATCH_POLL || r->phase==WATCH_RETRY || r->phase==WATCH_HOME) {
    if (r->phase==WATCH_HOME) r->visited=0;
    poll(r,now,ops);
    return;
  }
  const WatchTarget *event=&r->events[r->cursor];
  bool found=false;
  if (!ops->signal(ops->context,&event->plan,&found)) {
    now=clockNow(ops,now);
    block(r,now);
    if (r->audio==WATCH_EVENT || r->phase==WATCH_HANG) release(r,now,ops);
    else next(r,now);
    return;
  }
  now=clockNow(ops,now);
  switch (r->phase) {
  case WATCH_SETTLE:
    if (!found) { next(r,now); return; }
    r->phase=WATCH_VERIFY; r->deadline=now+WATCH_VERIFY_MS; return;
  case WATCH_VERIFY:
    if (!found) { next(r,now); return; }
    speaker(r,ops,event,WATCH_EVENT);
    if (r->audio!=WATCH_EVENT) { block(r,now); next(r,now); return; }
    r->phase=WATCH_HOLD; break;
  case WATCH_HOLD:
    // No time limit: a talk is heard to its end, then the hang keeps the
    // channel (muted) for a reply before anything else is heard.
    if (!found) {
      speaker(r,ops,event,WATCH_QUIET);
      r->phase=WATCH_HANG; r->hang_until=now+WATCH_HANG_MS;
    }
    break;
  case WATCH_HANG:
    if (found) {
      speaker(r,ops,event,WATCH_EVENT);
      if (r->audio==WATCH_EVENT) r->phase=WATCH_HOLD;
    } else if ((int32_t)(now-r->hang_until)>=0) { release(r,now,ops); return; }
    break;
  default: return;
  }
  r->deadline=now+WATCH_SAMPLE_MS;
}

#include "core/capture.h"

void CAPTURE_Start(CaptureRuntime *r, CaptureScope scope, uint32_t now) {
  *r = (CaptureRuntime){.scope=scope,
    .range=scope==CAPTURE_UHF ? CAPTURE_RANGE_UHF : CAPTURE_RANGE_VHF,
    .started_at=now, .attempt_at=now};
}

CaptureAction CAPTURE_Update(CaptureRuntime *r, uint32_t now,
                             CaptureSample sample, uint32_t hz) {
  // Unsigned elapsed time also handles the millisecond clock wrapping.
  if (now-r->started_at >= 15000u) return CAPTURE_TIMEOUT;
  if (sample==CAPTURE_BUSY && now-r->attempt_at < 500u) return CAPTURE_WAIT;
  if (sample==CAPTURE_SAMPLE) {
    uint32_t delta=hz>r->last_hz ? hz-r->last_hz : r->last_hz-hz;
    if (r->hits && delta<=3000u) {
      r->last_hz=hz;
      return CAPTURE_FOUND;
    }
    r->hits=1;
    r->last_hz=hz;
  } else r->hits=0;
  // Give each range two attempts so Auto can confirm a pair before moving.
  // BUSY expiry and invalid results advance too, including an empty band.
  if (++r->attempts>=2 && r->scope==CAPTURE_AUTO) {
    r->range=r->range==CAPTURE_RANGE_VHF ? CAPTURE_RANGE_UHF : CAPTURE_RANGE_VHF;
    r->hits=0;
    r->attempts=0;
  }
  r->attempt_at=now;
  return CAPTURE_RESTART;
}

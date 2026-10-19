#include "core/seek.h"

bool SEEK_Start(Seek *seek, RadioSpan range, uint32_t origin_hz, uint32_t step_hz) {
  if (!seek || !step_hz || origin_hz < range.start_hz ||
      origin_hz >= range.end_hz || range.end_hz <= range.start_hz)
    return false;
  *seek = (Seek){
      .origin_hz = origin_hz, .cursor_hz = origin_hz,
      .lower_hz = range.start_hz, .end_hz = range.end_hz,
      .step_hz = step_hz, .active = true};
  return true;
}

bool SEEK_Accept(const Seek *seek, uint32_t frequency_hz) {
  return seek && seek->active &&
      frequency_hz >= seek->lower_hz && frequency_hz < seek->end_hz &&
      frequency_hz != seek->origin_hz &&
      (!seek->wrapped || frequency_hz < seek->origin_hz);
}

bool SEEK_Wrap(Seek *seek) {
  if (!seek || !seek->active || seek->wrapped ||
      seek->lower_hz >= seek->origin_hz) {
    SEEK_Stop(seek);
    return false;
  }
  seek->wrapped = true;
  seek->cursor_hz = seek->lower_hz;
  return true;
}

bool SEEK_Next(Seek *seek, uint32_t *frequency_hz) {
  if (!seek || !seek->active || !frequency_hz)
    return false;
  uint32_t next = RADIO_MODEL_Step(seek->cursor_hz, seek->step_hz, true);
  if (next <= seek->cursor_hz || next >= seek->end_hz) {
    if (!SEEK_Wrap(seek))
      return false;
    next = seek->cursor_hz; // Inspect the first frequency after wrapping.
  }
  if (!SEEK_Accept(seek, next)) {
    SEEK_Stop(seek);
    return false;
  }
  seek->cursor_hz = next;
  *frequency_hz = next;
  return true;
}

void SEEK_Stop(Seek *seek) {
  if (seek)
    seek->active = false;
}

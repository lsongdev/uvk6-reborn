#include "core/spectrum.h"
#include <string.h>

static const uint32_t steps[] = {6250, 12500, 25000, 50000, 100000, 250000, 500000};
#define STEP_COUNT (sizeof(steps) / sizeof(steps[0]))

void SPECTRUM_Start(Spectrum *s, uint32_t center_hz, uint32_t step_hz) {
  // The smallest listed step not finer than the slot's own.
  uint8_t i = 0;
  while (i + 1u < STEP_COUNT && steps[i] < step_hz) ++i;
  memset(s, 0, sizeof(*s));
  s->center_hz = center_hz;
  s->step_hz = steps[i];
  s->cursor = SPECTRUM_CENTER;
}

uint32_t SPECTRUM_Frequency(const Spectrum *s, uint8_t point) {
  uint32_t half = SPECTRUM_CENTER * s->step_hz;
  uint32_t first = s->center_hz > half ? s->center_hz - half : 0;
  return first + point * s->step_hz;
}

bool SPECTRUM_Record(Spectrum *s, uint8_t level) {
  s->level[s->next] = level;
  if (level > s->peak[s->next]) s->peak[s->next] = level;
  if (++s->next < SPECTRUM_POINTS) return false;
  s->next = 0;
  for (uint8_t i = 0; i < SPECTRUM_POINTS; ++i)
    if (s->peak[i] > s->level[i]) --s->peak[i];
  return true;
}

void SPECTRUM_Move(Spectrum *s, bool up) {
  if (up ? s->cursor + 1u < SPECTRUM_POINTS : s->cursor > 0) {
    s->cursor = up ? s->cursor + 1u : s->cursor - 1u;
    return;
  }
  if (!up && s->center_hz <= SPECTRUM_CENTER * s->step_hz) return;
  // Pan: the measured points slide along; the one point in flight follows.
  uint8_t last = SPECTRUM_POINTS - 1u;
  if (up) {
    s->center_hz += s->step_hz;
    memmove(s->level, s->level + 1, last);
    memmove(s->peak, s->peak + 1, last);
    s->level[last] = s->peak[last] = 0;
    s->next = s->next ? s->next - 1u : last;
  } else {
    s->center_hz -= s->step_hz;
    memmove(s->level + 1, s->level, last);
    memmove(s->peak + 1, s->peak, last);
    s->level[0] = s->peak[0] = 0;
    s->next = s->next < last ? s->next + 1u : 0;
  }
}

bool SPECTRUM_Zoom(Spectrum *s, bool wider) {
  uint8_t i = 0;
  while (i + 1u < STEP_COUNT && steps[i] < s->step_hz) ++i;
  if (wider ? i + 1u >= STEP_COUNT : i == 0) return false;
  uint32_t center = SPECTRUM_Frequency(s, s->cursor);
  SPECTRUM_Start(s, center, steps[wider ? i + 1u : i - 1u]);
  return true;
}

uint8_t SPECTRUM_Floor(const Spectrum *s) {
  uint8_t floor = 0;
  for (uint8_t i = 0; i < SPECTRUM_POINTS; ++i)
    if (s->level[i] && (!floor || s->level[i] < floor)) floor = s->level[i];
  return floor;
}

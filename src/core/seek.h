#ifndef CORE_SEEK_H
#define CORE_SEEK_H

#include "core/radio-model.h"

// One forward pass over one explicitly selected band. end_hz is exclusive.
// Candidate frequencies are temporary; no EEPROM writes occur here.
typedef struct {
  uint32_t origin_hz, cursor_hz;
  uint32_t lower_hz, end_hz;
  uint32_t step_hz;
  bool wrapped;
  bool active;
} Seek;

// Scans the range (normally a band's receivable span) from the origin.
bool SEEK_Start(Seek *seek, RadioSpan range, uint32_t origin_hz, uint32_t step_hz);
bool SEEK_Next(Seek *seek, uint32_t *frequency_hz);
bool SEEK_Wrap(Seek *seek); // Hardware seek hit end: continue at band start.
bool SEEK_Accept(const Seek *seek, uint32_t frequency_hz);
void SEEK_Stop(Seek *seek);

#endif

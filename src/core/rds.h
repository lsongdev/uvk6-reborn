#ifndef CORE_RDS_H
#define CORE_RDS_H
#include <stdbool.h>
#include <stdint.h>

// RDS groups, independent of the receiver's command/response layout.
// errors: 0 clean, 1 corrected 1-2 bits, 2 corrected 3-5, 3 uncorrectable.
typedef struct { uint16_t blocks[4]; uint8_t errors[4]; } RdsGroup;
typedef struct {
  char ps[9], text[65];
  uint16_t pi;
  bool pi_valid, text_started;
  uint8_t text_ab, text_version, ps_seen, ps_confirmed;
  uint16_t text_seen, text_confirmed;
  uint8_t ps_pending[8], text_pending[64];
  // Clock time (4A): local minute of the day, 0-1439. clock_new is set when
  // two clean groups agree (equal or one minute apart); the owner clears it.
  uint16_t clock, clock_pending; // clock_pending: minute + 1, 0 if none.
  bool clock_new;
} RdsDecoder;

void RDS_Reset(RdsDecoder *decoder);
// True only when visible PS/RadioText changes. Groups 0A/B and 2A/B;
// 4A clock time only sets clock_new.
bool RDS_Feed(RdsDecoder *decoder, const RdsGroup *group);
#endif

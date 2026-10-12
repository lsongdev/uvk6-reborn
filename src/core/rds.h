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
} RdsDecoder;

void RDS_Reset(RdsDecoder *decoder);
// True only when visible PS/RadioText changes. Groups 0A/B and 2A/B.
bool RDS_Feed(RdsDecoder *decoder, const RdsGroup *group);
#endif

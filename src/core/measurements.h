#ifndef MEASUREMENTS_H
#define MEASUREMENTS_H

#include "core/misc.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const uint16_t RSSI_MIN = 28;
static const uint16_t RSSI_MAX = 226;

typedef struct {
  uint16_t ro;
  uint16_t rc;
  uint8_t no;
  uint8_t nc;
  uint8_t go;
  uint8_t gc;
} SQL;

static const uint8_t rssi2s[2][15] = {
    // VHF
    {121, 115, 109, 103, 97, 91, 85, 79, 73, 63, 53, 43, 33, 23, 13},
    // UHF
    {141, 135, 129, 123, 117, 111, 105, 99, 93, 83, 73, 63, 53, 43, 33},
};

long long Clamp(long long v, long long min, long long max);
int ConvertDomain(int aValue, int aMin, int aMax, int bMin, int bMax);
uint8_t DBm2S(int dbm, bool isUHF);
int16_t Rssi2DBm(uint16_t rssi);

uint32_t AdjustU(uint32_t val, uint32_t min, uint32_t max, int32_t inc);
uint32_t IncDecU(uint32_t val, uint32_t min, uint32_t max, bool inc);

SQL GetSql(uint8_t level);

#endif /* end of include guard: MEASUREMENTS_H */

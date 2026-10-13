#include "core/measurements.h"
#include <stdint.h>

long long Clamp(long long v, long long min, long long max) {
  return v <= min ? min : (v >= max ? max : v);
}

int ConvertDomain(int aValue, int aMin, int aMax, int bMin, int bMax) {
  const int aRange = aMax - aMin;
  const int bRange = bMax - bMin;
  aValue = Clamp(aValue, aMin, aMax);
  return ((aValue - aMin) * bRange + aRange / 2) / aRange + bMin;
}

uint8_t DBm2S(int dbm, bool isUHF) {
  uint8_t i = 0;
  dbm *= -1;
  for (i = 0; i < 15; i++) {
    if (dbm >= rssi2s[isUHF][i]) {
      return i;
    }
  }
  return i;
}

int16_t Rssi2DBm(uint16_t rssi) { return (rssi >> 1) - 160; }








uint32_t AdjustU(uint32_t val, uint32_t min, uint32_t max, int32_t inc) {
  if (inc > 0) {
    return val == max - inc ? min : val + inc;
  } else {
    return val > min ? val + inc : max + inc;
  }
}

uint32_t IncDecU(uint32_t val, uint32_t min, uint32_t max, bool inc) {
  return AdjustU(val, min, max, inc ? 1 : -1);
}


SQL GetSql(uint8_t level) {
  SQL sq = {0, 0, 255, 255, 255, 255};
  if (level == 0) {
    return sq;
  }

  // sq.ro = ConvertDomain(level, 0, 10, 10, 180);
  sq.ro = ConvertDomain(level, 0, 10, 60, 180);
  sq.no = ConvertDomain(level, 0, 10, 64, 12);
  sq.go = ConvertDomain(level, 0, 10, 100, 0); // was 32, 6

  sq.rc = sq.ro - 4;
  sq.nc = sq.no + 4;
  sq.gc = sq.go + 4;
  return sq;
}



#ifndef TEST_RF_RANGES_H
#define TEST_RF_RANGES_H
#include "core/radio-model.h"
// Tuning profile fixtures independent of the production backends.
// K1 retains the pre-existing extensions beyond its datasheet guarantee.
static const RadioSpan testBk4819Ranges[RADIO_RF_RANGES] __attribute__((unused)) = {
  {18000000u,660000001u}, {840000000u,1300000001u}
};
static const RadioSpan testBk4829Ranges[RADIO_RF_RANGES] __attribute__((unused)) = {
  {18000000u,660000001u}, {760000000u,1300000001u}
};
#endif

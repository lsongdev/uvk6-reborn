#ifndef CORE_CHANNEL_MODEL_H
#define CORE_CHANNEL_MODEL_H

#include "core/tx-model.h"
#include <stdbool.h>
#include <stdint.h>

#define USER_CHANNEL_COUNT 200u
// 16 stored bytes: 16 ASCII or 8 double-byte (GB2312) characters.
#define USER_CHANNEL_NAME_BYTES 17u
#define USER_CHANNEL_NAME_MAX (USER_CHANNEL_NAME_BYTES - 1u)

typedef struct {
  char name[USER_CHANNEL_NAME_BYTES];
  uint32_t frequency_hz;
  RadioParameters rx;
  TxConfig tx;
} UserChannel;

#endif

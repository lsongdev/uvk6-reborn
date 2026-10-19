#ifndef CORE_CHANNEL_STORE_H
#define CORE_CHANNEL_STORE_H

#include "core/channel-model.h"
#include <stdbool.h>
#include <stdint.h>

// Each channel has a 64-byte copy in STORAGE_CHANNELS_A and another in
// STORAGE_CHANNELS_B. A save writes and verifies the older copy, then mirrors
// it into the other: power loss keeps either the previous or the new version,
// and channels sharing a flash erase unit keep their current one in the
// other area.
#define CHANNEL_STORE_RECORD_BYTES 64u
#define CHANNEL_STORE_COPY_COUNT 2u
#define CHANNEL_STORE_AREA_BYTES (USER_CHANNEL_COUNT * CHANNEL_STORE_RECORD_BYTES)

// Channel numbers are stable, one-based EEPROM record identifiers.
bool CHANNEL_STORE_Init(void);
bool CHANNEL_STORE_Available(void);
bool CHANNEL_STORE_IsOccupied(uint16_t number);
bool CHANNEL_STORE_Load(uint16_t number, UserChannel *channel);
bool CHANNEL_STORE_Save(uint16_t number, const UserChannel *channel);
bool CHANNEL_STORE_Delete(uint16_t number);
uint16_t CHANNEL_STORE_FirstEmpty(void); // Zero when full/unavailable.
bool CHANNEL_STORE_Next(uint16_t number, bool up, uint16_t *next);

#endif

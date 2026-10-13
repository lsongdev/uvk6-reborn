#ifndef VFO_CHANNEL_MANAGER_H
#define VFO_CHANNEL_MANAGER_H

#include "core/channel-model.h"
#include "core/radio-state.h"
#include "hal/keyboard.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*ChannelManagerNotify)(const char *text);
typedef void (*ChannelManagerSaved)(uint8_t slot, uint16_t number,
                                   const UserChannel *channel);

void CHANNEL_MANAGER_Init(ChannelManagerNotify notify, ChannelManagerSaved saved);
void CHANNEL_MANAGER_LoadName(uint8_t slot, uint16_t number);
const char *CHANNEL_MANAGER_Name(uint8_t slot);
void CHANNEL_MANAGER_StartName(uint8_t slot, uint16_t current_number);
void CHANNEL_MANAGER_StartSave(uint8_t slot, uint16_t current_number,
                               const UserChannel *channel);
bool CHANNEL_MANAGER_Active(void);
bool CHANNEL_MANAGER_HandleKey(KEY_Code_t key, Key_State_t state);
bool CHANNEL_MANAGER_Render(void);

#endif

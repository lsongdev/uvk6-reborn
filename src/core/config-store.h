#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include "core/radio-state.h"
#include <stdbool.h>
#include <stdint.h>

#define CONFIG_STORE_SLOT_SIZE 256u
#define CONFIG_STORE_SLOTS 2u
#define CONFIG_STORE_FORMAT 3u

// Explicit on-disk fields. Never serialize C compiler bitfields or structs.
typedef struct {
  uint16_t battery_calibration;
  uint8_t backlight, contrast, brightness, battery_type, battery_style;
  uint8_t backlight_on_squelch, keylock, ptt_lock, beep;
  uint8_t si4732_power_off, mic, deviation, bound_240_280;
  uint8_t brightness_low, sql_open_time, sql_close_time;
} ConfigPreferences;

typedef struct {
  RadioUserState radio;
  ConfigPreferences preferences;
} DeviceConfig;

typedef bool (*ConfigRead)(void *context, uint32_t offset, void *out, uint16_t size);
typedef bool (*ConfigWrite)(void *context, uint32_t offset, const void *data,
                            uint16_t size);
typedef struct {
  void *context;
  ConfigRead read;
  ConfigWrite write;
  uint32_t capacity; // Must be confirmed by the platform's EEPROM driver.
  uint32_t reserved_tail; // E.g. separately stored SI4732 SSB patch.
} ConfigStore;

void CONFIG_Default(DeviceConfig *config);
bool CONFIG_Decode(const uint8_t bytes[CONFIG_STORE_SLOT_SIZE],
                   DeviceConfig *config, uint32_t *sequence);
bool CONFIG_Encode(const DeviceConfig *config, uint32_t sequence,
                   uint8_t bytes[CONFIG_STORE_SLOT_SIZE]);
bool CONFIG_Load(const ConfigStore *store, DeviceConfig *config, uint32_t *sequence);
bool CONFIG_Save(const ConfigStore *store, const DeviceConfig *config,
                 uint32_t *sequence);

#endif

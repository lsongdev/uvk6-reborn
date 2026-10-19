#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include "core/radio-state.h"
#include "core/tx-model.h"
#include <stdbool.h>
#include <stdint.h>

#define CONFIG_STORE_SLOT_SIZE 1024u
#define CONFIG_STORE_SLOTS 2u
// Older formats are not read: an unknown record means "no configuration".
#define CONFIG_STORE_FORMAT 9u

// Explicit on-disk fields. Never serialize C compiler bitfields or structs.
typedef struct {
  uint16_t battery_calibration;
  uint8_t backlight, contrast, brightness, battery_type, battery_style;
  uint8_t backlight_on_squelch, keylock, ptt_lock, beep;
  uint8_t si4732_power_off, mic, deviation, bound_240_280;
  uint8_t brightness_low, sql_open_time, sql_close_time;
  uint16_t tx_timeout_seconds; // Global PTT time-out, enforced by runtime.
  uint8_t tx_busy_lockout;     // User preference, not a hardware bypass.
} ConfigPreferences;

// Shared by boot-time initialization and reset; never serialize this struct.
#define CONFIG_DEFAULT_PREFERENCES { \
    .battery_calibration = 2000, .backlight = 3, .contrast = 8, \
    .brightness = 8, .battery_type = 0, .battery_style = 1, \
    .backlight_on_squelch = 1, .mic = 16, .deviation = 130, \
    .brightness_low = 0, .sql_open_time = 1, .sql_close_time = 1, \
    .tx_timeout_seconds = 120, .tx_busy_lockout = 1 }

typedef struct {
  RadioUserState radio;
  ConfigPreferences preferences;
} DeviceConfig;

// Offsets are within one slot; each slot lives in its own storage area.
typedef bool (*ConfigRead)(void *context, unsigned slot, uint16_t offset, void *out,
                           uint16_t size);
typedef bool (*ConfigWrite)(void *context, unsigned slot, const void *data,
                            uint16_t size);
typedef struct {
  void *context;
  ConfigRead read;
  ConfigWrite write; // Whole slot.
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

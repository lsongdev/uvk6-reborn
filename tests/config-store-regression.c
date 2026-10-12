#include "../src/core/config-store.c"
#include "../src/hal/eeprom-layout.h"
#include <assert.h>
#include <string.h>

typedef struct {
  uint8_t bytes[2048];
  unsigned writes;
  int write_limit;
} FakeEEPROM;
static bool readIO(void *ctx, uint32_t at, void *out, uint16_t n) {
  FakeEEPROM *io = ctx;
  if (at > sizeof(io->bytes) || n > sizeof(io->bytes)-at) return false;
  memcpy(out, io->bytes+at, n);
  return true;
}
static bool writeIO(void *ctx, uint32_t at, const void *data, uint16_t n) {
  FakeEEPROM *io = ctx;
  if (at > sizeof(io->bytes) || n > sizeof(io->bytes)-at) return false;
  ++io->writes;
  if (io->write_limit >= 0 && io->write_limit < n) {
    memcpy(io->bytes+at, data, io->write_limit); // Simulate power loss.
    return false;
  }
  memcpy(io->bytes+at, data, n);
  return true;
}
int main(void) {
  assert(EEPROM_WriteAllowed(0, 256));
  assert(EEPROM_WriteAllowed(256, 256));
  assert(!EEPROM_WriteAllowed(512, 1));
  assert(!EEPROM_WriteAllowed(511, 2));
  assert(!EEPROM_WriteAllowed(0x1e00, 1));
  assert(!EEPROM_WriteAllowed(UINT32_MAX, 64));
  DeviceConfig original, loaded;
  CONFIG_Default(&original);
  assert(RADIO_STATE_Valid(&original.radio));
  assert(original.radio.current.frequency_hz == 145500000);
  assert(RADIO_STATE_Select(&original.radio, 7));
  assert(RADIO_STATE_Valid(&original.radio));
  assert(RADIO_STATE_Select(&original.radio, 8));
  assert(RADIO_STATE_Valid(&original.radio));
  assert(RADIO_STATE_Select(&original.radio, 5));
  assert(RADIO_STATE_Set(&original.radio, 100000000,
         (RadioParameters){RX_WFM,100000,0,0,false}));
  assert(RADIO_STATE_Select(&original.radio, 5));
  assert(original.radio.current.frequency_hz == 145500000);
  assert(RADIO_STATE_Select(&original.radio, 3));
  assert(original.radio.current.frequency_hz == 100000000);
  assert(!RADIO_STATE_Set(&original.radio, 100000000,
         (RadioParameters){RX_WFM,100000,0,0,true})); // RX-only
  uint8_t record[CONFIG_STORE_SLOT_SIZE];
  assert(CONFIG_Encode(&original, 1, record));
  assert(!CONFIG_Decode(record, NULL, NULL));
  record[60] ^= 1;
  assert(!CONFIG_Decode(record, &loaded, NULL)); // CRC catches corruption.
  assert(CONFIG_Encode(&original, 1, record));
  assert(CONFIG_Decode(record, &loaded, NULL));
  assert(loaded.radio.bands[3].frequency_hz == 100000000);
  assert(loaded.radio.current.parameters.mode == RX_WFM);

  // Development storage is intentionally strict: never decode old layouts.
  record[4] = 1;
  assert(!CONFIG_Decode(record, &loaded, NULL));
  record[4] = 2;
  assert(!CONFIG_Decode(record, &loaded, NULL));
  original.preferences.brightness_low = 5;
  original.preferences.sql_open_time = 3;
  original.preferences.sql_close_time = 2;
  assert(CONFIG_Encode(&original, 10, record));
  assert(CONFIG_Decode(record, &loaded, NULL));
  assert(loaded.preferences.brightness_low == 5);
  assert(loaded.preferences.sql_open_time == 3);
  assert(loaded.preferences.sql_close_time == 2);
  loaded.radio.presets[6].step_hz = 25000;
  loaded.radio.presets[6].squelch = 2;
  assert(CONFIG_Encode(&loaded, 11, record));
  DeviceConfig roundTrip;
  assert(CONFIG_Decode(record, &roundTrip, NULL));
  assert(roundTrip.radio.presets[6].step_hz == 25000 && roundTrip.radio.presets[6].squelch == 2);
  assert(roundTrip.radio.bands[6].parameters.step_hz == 12500);
  original.radio.bands[6].parameters.mode = RX_NFM;

  FakeEEPROM memory;
  memset(&memory, 0xff, sizeof(memory));
  memory.writes = 0;
  memory.write_limit = -1;
  ConfigStore io = {&memory, readIO, writeIO, sizeof(memory.bytes), 8840};
  assert(!CONFIG_Save(&io, &original, NULL)); // Reserved patch won't fit.
  io.reserved_tail = 0;
  assert(!CONFIG_Load(&io, &loaded, NULL));
  uint32_t sequence = 0;
  assert(CONFIG_Save(&io, &original, &sequence) && sequence == 1);
  original.radio.presets[6].squelch = 3;
  original.radio.current.frequency_hz = 100010000;
  original.radio.bands[3] = original.radio.current;
  assert(CONFIG_Save(&io, &original, &sequence) && sequence == 2);
  assert(CONFIG_Load(&io, &loaded, &sequence) && sequence == 2);
  assert(loaded.radio.current.frequency_hz == 100010000);
  assert(loaded.radio.presets[6].squelch == 3);
  memory.bytes[CONFIG_STORE_SLOT_SIZE+90] ^= 1; // Latest CRC corrupted.
  assert(CONFIG_Load(&io, &loaded, &sequence) && sequence == 1);
  assert(loaded.radio.current.frequency_hz == 100000000);

  memory.write_limit = 18; // Partial write cannot destroy previous valid slot.
  assert(!CONFIG_Save(&io, &original, NULL));
  assert(CONFIG_Load(&io, &loaded, &sequence) && sequence == 1);
  assert(loaded.radio.current.frequency_hz == 100000000);
  assert(memory.writes == 3);
  assert(loaded.radio.presets[6].squelch == 4);
  return 0;
}

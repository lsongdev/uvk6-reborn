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
  assert(EEPROM_CandidateFromAckMask(0x01) == EEPROM_BL24C64);
  assert(EEPROM_CandidateFromAckMask(0x03) == EEPROM_BL24C128);
  assert(EEPROM_CandidateFromAckMask(0x0f) == EEPROM_M24M02);
  assert(EEPROM_CandidateFromAckMask(0xff) == EEPROM_4MBIT);
  assert(EEPROM_CandidateFromAckMask(0x05) == EEPROM_UNKNOWN);
  assert(EEPROM_CandidateFromAckMask(0x07) == EEPROM_UNKNOWN);
  assert(EEPROM_WriteAllowed(0, 512));
  assert(EEPROM_WriteAllowed(512, 512));
  assert(!EEPROM_WriteAllowed(1024, 1));
  assert(!EEPROM_WriteAllowed(1023, 2));
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
         (RadioParameters){RX_WFM,100000,0,0,0}));
  assert(RADIO_STATE_Select(&original.radio, 5));
  assert(original.radio.current.frequency_hz == 145500000);
  assert(RADIO_STATE_Select(&original.radio, 3));
  assert(original.radio.current.frequency_hz == 100000000);
  assert(!RADIO_STATE_Set(&original.radio, 100000000,
         (RadioParameters){RX_WFM,100000,0,11,0})); // Invalid squelch
  original.tx = (TxConfig){.enabled = true, .power = TX_POWER_LOW};
  original.preferences.tx_timeout_seconds = 90;
  original.preferences.tx_busy_lockout = 1;
  uint8_t record[CONFIG_STORE_SLOT_SIZE];
  assert(CONFIG_Encode(&original, 1, record));
  assert(!CONFIG_Decode(record, NULL, NULL));
  record[60] ^= 1;
  assert(!CONFIG_Decode(record, &loaded, NULL)); // CRC catches corruption.
  assert(CONFIG_Encode(&original, 1, record));
  assert(CONFIG_Decode(record, &loaded, NULL));
  assert(loaded.radio.bands[3].frequency_hz == 100000000);
  assert(loaded.radio.current.parameters.mode == RX_WFM);
  assert(loaded.tx.enabled && loaded.tx.power == TX_POWER_LOW);
  assert(loaded.preferences.tx_timeout_seconds == 90);
  assert(loaded.preferences.tx_busy_lockout == 1);

  // Format 5 extends format 4 without losing its existing settings.
  assert(CONFIG_Encode(&original,1,record));
  record[4]=4;put32(record+HEADER_CRC,crc32(record));
  assert(CONFIG_Decode(record,&loaded,NULL));
  assert(loaded.tx.enabled && !loaded.tx.frequency_hz && !loaded.tx.tone);
  assert(!loaded.radio.current.parameters.rx_tone);
  assert(CONFIG_Encode(&original,1,record));
  original.radio.bands[6].parameters.rx_tone=51;
  original.radio.presets[6].rx_tone=155;
  original.tx.tone=258;original.tx.frequency_hz=438600000;
  assert(CONFIG_Encode(&original,1,record));
  assert(CONFIG_Decode(record,&loaded,NULL));
  assert(loaded.radio.bands[6].parameters.rx_tone==51);
  assert(loaded.radio.presets[6].rx_tone==155);
  assert(loaded.tx.tone==258 && loaded.tx.frequency_hz==438600000);
  original.tx.tone=259;assert(!CONFIG_Encode(&original,1,record));
  original.tx.tone=258;
  original.tx.frequency_hz=700000000;assert(!CONFIG_Encode(&original,1,record));
  original.tx.frequency_hz=438600000;
  // Earlier layouts remain rejected: never decode old layouts.
  record[4] = 1;
  assert(!CONFIG_Decode(record, &loaded, NULL));
  record[4] = 2;
  assert(!CONFIG_Decode(record, &loaded, NULL));
  record[4] = 3;
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
  DeviceConfig receiveTone = original;
  assert(RADIO_STATE_Set(&receiveTone.radio,438500000,
    (RadioParameters){RX_NFM,12500,12000,4,155}));
  assert(CONFIG_Encode(&receiveTone,12,record));
  assert(CONFIG_Decode(record,&loaded,NULL));
  assert(loaded.radio.current.parameters.rx_tone==155);
  assert(loaded.radio.bands[6].parameters.rx_tone==155);
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

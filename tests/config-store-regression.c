#include "../src/core/config-store.c"
#include "../src/hal/eeprom-layout.h"
#include <assert.h>
#include <string.h>

typedef struct {
  uint8_t bytes[8192];
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
static bool encodeLegacy(const DeviceConfig *config, uint32_t sequence,
                   uint8_t bytes[RECORD_BYTES]) {
  if (!config || !bytes || !valid(config))
    return false;
  memset(bytes, 0, RECORD_BYTES);
  memcpy(bytes, "UVR2", 4);
  bytes[HEADER_VERSION] = 5u;
  bytes[HEADER_BANDS] = RADIO_STATE_BANDS;
  put32(bytes + HEADER_SEQUENCE, sequence);
  uint8_t *p = bytes + PAYLOAD_START;
  *p++ = config->radio.vfos[0].active_band;
  p = encodeBand(p, &config->radio.vfos[0].current);
  for (uint8_t i = 0; i < RADIO_STATE_BANDS; ++i)
    p = encodeBand(p, &config->radio.vfos[0].bands[i]);
  const ConfigPreferences *pref = &config->preferences;
  put16(p, pref->battery_calibration); p += 2;
  const uint8_t values[] = {
    pref->backlight, pref->contrast, pref->brightness, pref->battery_type,
    pref->battery_style, pref->backlight_on_squelch, pref->keylock,
    pref->ptt_lock, pref->beep, pref->si4732_power_off, pref->mic,
    pref->deviation, pref->bound_240_280, pref->brightness_low,
    pref->sql_open_time, pref->sql_close_time
  };
  memcpy(p, values, sizeof(values));
  p += sizeof(values);
  for (uint8_t i = 0; i < RADIO_STATE_BANDS; ++i) {
    const RadioParameters *preset = &config->radio.presets[i];
    put32(p, preset->step_hz);
    put16(p+4, preset->bandwidth_hz);
    p[6] = preset->mode; p[7] = preset->squelch;
    p += 8;
  }
  // TX settings are explicitly encoded; C enums/bitfields never persisted.
  *p++ = config->radio.vfos[0].tx.enabled ? 1u : 0u;
  *p++ = (uint8_t)config->radio.vfos[0].tx.power;
  put16(p, pref->tx_timeout_seconds); p += 2;
  *p++ = pref->tx_busy_lockout;
  // Format 5 appends fields; format 4's payload offsets stay unchanged.
  put16(p, config->radio.vfos[0].current.parameters.rx_tone); p += 2;
  for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i) {
    put16(p, config->radio.vfos[0].bands[i].parameters.rx_tone); p += 2;
    put16(p, config->radio.presets[i].rx_tone); p += 2;
  }
  put16(p, config->radio.vfos[0].tx.tone); p += 2;
  put32(p, config->radio.vfos[0].tx.frequency_hz);
  put32(bytes + HEADER_CRC, crc32(bytes,CONFIG_LEGACY_SLOT_SIZE));
  return true;
}
// A real format6 fixture: two records, 512-byte CRC, no count/watch extension.
static bool encodeV6(const DeviceConfig *config, uint32_t sequence, uint8_t *bytes) {
  uint8_t native[RECORD_BYTES];
  if (!CONFIG_Encode(config,sequence,native)) return false;
  memset(bytes,0,RECORD_BYTES);memcpy(bytes,"UVR2",4);
  bytes[4]=6;bytes[5]=RADIO_STATE_BANDS;put32(bytes+8,sequence);
  uint8_t *p=bytes+16;*p++=2;*p++=config->radio.selected_vfo;
  for (uint8_t id=0;id<2;++id) {
    const VfoConfig *v=&config->radio.vfos[id];*p++=v->active_band;
    p=encodeBand6(p,&v->current);
    for (uint8_t i=0;i<RADIO_STATE_BANDS;++i) p=encodeBand6(p,&v->bands[i]);
    *p++=v->tx.enabled;*p++=v->tx.power;put32(p,v->tx.frequency_hz);p+=4;
    put16(p,v->tx.tone);p+=2;
  }
  for (uint8_t i=0;i<RADIO_STATE_BANDS;++i) {
    const RadioParameters *preset=&config->radio.presets[i];
    put32(p,preset->step_hz);put16(p+4,preset->bandwidth_hz);
    p[6]=preset->mode;p[7]=preset->squelch;put16(p+8,preset->rx_tone);p+=10;
  }
  memcpy(p,native+FORMAT8_BYTES-21,18);p+=18;
  put16(p,config->preferences.tx_timeout_seconds);p+=2;
  *p++=config->preferences.tx_busy_lockout;assert(p==bytes+467);
  put32(bytes+HEADER_CRC,crc32(bytes,CONFIG_LEGACY_SLOT_SIZE));return true;
}
int main(void) {
  assert(EEPROM_CandidateFromAckMask(0x01) == EEPROM_BL24C64);
  assert(EEPROM_CandidateFromAckMask(0x03) == EEPROM_BL24C128);
  assert(EEPROM_CandidateFromAckMask(0x0f) == EEPROM_M24M02);
  assert(EEPROM_CandidateFromAckMask(0xff) == EEPROM_4MBIT);
  assert(EEPROM_CandidateFromAckMask(0x05) == EEPROM_UNKNOWN);
  assert(EEPROM_CandidateFromAckMask(0x07) == EEPROM_UNKNOWN);
  assert(!EEPROM_WriteAllowed(0, 512));
  assert(EEPROM_WriteAllowed(CONFIG_STORE_BASE, CONFIG_STORE_SLOT_SIZE));
  assert(!EEPROM_WriteAllowed(512, 512));
  assert(EEPROM_WriteAllowed(CONFIG_STORE_BASE+CONFIG_STORE_SLOT_SIZE,CONFIG_STORE_SLOT_SIZE));
  assert(!EEPROM_WriteAllowed(CONFIG_STORE_END-1,2));
  assert(!EEPROM_WriteAllowed(CONFIG_STORE_END,1));
  assert(!EEPROM_WriteAllowed(1024, 1));
  assert(!EEPROM_WriteAllowed(1023, 2));
  assert(!EEPROM_WriteAllowed(0x1e00, 1));
  assert(!EEPROM_WriteAllowed(UINT32_MAX, 64));
  DeviceConfig original, loaded;
  CONFIG_Default(&original);
  assert(RADIO_STATE_Valid(&original.radio));
  assert(original.radio.vfos[0].current.frequency_hz == 145500000);
  assert(RADIO_STATE_SelectBand(&original.radio, 0, 7));
  assert(RADIO_STATE_Valid(&original.radio));
  assert(RADIO_STATE_SelectBand(&original.radio, 0, 8));
  assert(RADIO_STATE_Valid(&original.radio));
  assert(RADIO_STATE_SelectBand(&original.radio, 0, 5));
  assert(RADIO_STATE_Set(&original.radio, 0, 100000000,
         (RadioParameters){RX_WFM,100000,0,0,0}));
  assert(RADIO_STATE_SelectBand(&original.radio, 0, 5));
  assert(original.radio.vfos[0].current.frequency_hz == 145500000);
  assert(RADIO_STATE_SelectBand(&original.radio, 0, 3));
  assert(original.radio.vfos[0].current.frequency_hz == 100000000);
  assert(!RADIO_STATE_Set(&original.radio, 0, 100000000,
         (RadioParameters){RX_WFM,100000,0,11,0})); // Invalid squelch
  original.radio.vfos[0].tx = (TxConfig){.enabled = true, .power = TX_POWER_LOW};
  original.preferences.tx_timeout_seconds = 90;
  original.preferences.tx_busy_lockout = 1;
  uint8_t record[CONFIG_STORE_SLOT_SIZE];
  assert(CONFIG_Encode(&original, 1, record));
  assert(!CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE, NULL, NULL));
  record[60] ^= 1;
  assert(!CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE, &loaded, NULL)); // CRC catches corruption.
  assert(CONFIG_Encode(&original, 1, record));
  assert(CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE, &loaded, NULL));
  assert(loaded.radio.vfos[0].bands[3].frequency_hz == 100000000);
  assert(loaded.radio.vfos[0].current.parameters.mode == RX_WFM);
  assert(loaded.radio.vfos[0].tx.enabled && loaded.radio.vfos[0].tx.power == TX_POWER_LOW);
  assert(loaded.preferences.tx_timeout_seconds == 90);
  assert(loaded.preferences.tx_busy_lockout == 1);

  // A real legacy layout migrates intact, without serializing the new fields.
  assert(encodeLegacy(&original,1,record));
  record[4]=4;put32(record+HEADER_CRC,crc32(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE));
  assert(CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE,&loaded,NULL));
  assert(loaded.radio.vfos[0].tx.enabled && !loaded.radio.vfos[0].tx.frequency_hz && !loaded.radio.vfos[0].tx.tone);
  assert(!loaded.radio.vfos[0].current.parameters.rx_tone);
  assert(CONFIG_Encode(&original,1,record));
  original.radio.vfos[0].bands[6].parameters.rx_tone=51;
  original.radio.presets[6].rx_tone=155;
  original.radio.vfos[0].tx.tone=258;original.radio.vfos[0].tx.frequency_hz=438600000;
  assert(CONFIG_Encode(&original,1,record));
  assert(CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE,&loaded,NULL));
  assert(loaded.radio.vfos[0].bands[6].parameters.rx_tone==51);
  assert(loaded.radio.presets[6].rx_tone==155);
  assert(loaded.radio.vfos[0].tx.tone==258 && loaded.radio.vfos[0].tx.frequency_hz==438600000);
  original.radio.vfos[0].tx.tone=259;assert(!CONFIG_Encode(&original,1,record));
  original.radio.vfos[0].tx.tone=258;
  original.radio.vfos[0].tx.frequency_hz=700000000;assert(!CONFIG_Encode(&original,1,record));
  original.radio.vfos[0].tx.frequency_hz=438600000;
  // Earlier layouts remain rejected: never decode old layouts.
  record[4] = 1;
  assert(!CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE, &loaded, NULL));
  record[4] = 2;
  assert(!CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE, &loaded, NULL));
  record[4] = 3;
  assert(!CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE, &loaded, NULL));
  original.preferences.brightness_low = 5;
  original.preferences.sql_open_time = 3;
  original.preferences.sql_close_time = 2;
  assert(CONFIG_Encode(&original, 10, record));
  assert(CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE, &loaded, NULL));
  assert(loaded.preferences.brightness_low == 5);
  assert(loaded.preferences.sql_open_time == 3);
  assert(loaded.preferences.sql_close_time == 2);
  loaded.radio.presets[6].step_hz = 25000;
  loaded.radio.presets[6].squelch = 2;
  assert(CONFIG_Encode(&loaded, 11, record));
  DeviceConfig roundTrip;
  assert(CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE, &roundTrip, NULL));
  assert(roundTrip.radio.presets[6].step_hz == 25000 && roundTrip.radio.presets[6].squelch == 2);
  assert(roundTrip.radio.vfos[0].bands[6].parameters.step_hz == 12500);
  original.radio.vfos[0].bands[6].parameters.mode = RX_NFM;

  FakeEEPROM memory;
  DeviceConfig receiveTone = original;
  assert(RADIO_STATE_Set(&receiveTone.radio, 0,438500000,
    (RadioParameters){RX_NFM,12500,12000,4,155}));
  assert(CONFIG_Encode(&receiveTone,12,record));
  assert(CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE,&loaded,NULL));
  assert(loaded.radio.vfos[0].current.parameters.rx_tone==155);
  assert(loaded.radio.vfos[0].bands[6].parameters.rx_tone==155);
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
  original.radio.vfos[0].current.frequency_hz = 100010000;
  original.radio.vfos[0].bands[3] = original.radio.vfos[0].current;
  assert(CONFIG_Save(&io, &original, &sequence) && sequence == 2);
  assert(CONFIG_Load(&io, &loaded, &sequence) && sequence == 2);
  assert(loaded.radio.vfos[0].current.frequency_hz == 100010000);
  assert(loaded.radio.presets[6].squelch == 3);
  memory.bytes[CONFIG_STORE_BASE+CONFIG_STORE_SLOT_SIZE+90] ^= 1; // Latest CRC corrupted.
  assert(CONFIG_Load(&io, &loaded, &sequence) && sequence == 1);
  assert(loaded.radio.vfos[0].current.frequency_hz == 100000000);

  memory.write_limit = 18; // Partial write cannot destroy previous valid slot.
  assert(!CONFIG_Save(&io, &original, NULL));
  assert(CONFIG_Load(&io, &loaded, &sequence) && sequence == 1);
  assert(loaded.radio.vfos[0].current.frequency_hz == 100000000);
  assert(memory.writes == 3);
  assert(loaded.radio.presets[6].squelch == 4);
  // Native format8 preserves independent B state, source, and last selection.
  assert(RADIO_STATE_Set(&original.radio,1,438550000,
      (RadioParameters){RX_NFM,25000,14000,7,50}));
  original.radio.vfos[1].tx=(TxConfig){true,TX_POWER_MEDIUM,438650000,155};
  original.radio.selected_vfo=1;
  assert(CONFIG_Encode(&original,20,record) && CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE,&loaded,NULL));
  assert(loaded.radio.selected_vfo==1 && loaded.radio.vfos[0].current.frequency_hz==100010000);
  assert(loaded.radio.vfos[1].current.frequency_hz==438550000);
  assert(loaded.radio.vfos[1].current.parameters.rx_tone==50);
  assert(loaded.radio.vfos[1].tx.frequency_hz==438650000 && loaded.radio.vfos[1].tx.tone==155);
  record[17]=2;put32(record+HEADER_CRC,crc32(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE));
  assert(!CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE,&loaded,NULL));
  assert(CONFIG_Encode(&original,20,record));record[16]=5;
  put32(record+HEADER_CRC,crc32(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE));assert(!CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE,&loaded,NULL));
  // Real v5 migration keeps tones/split TX on A, B starts from shared presets.
  assert(encodeLegacy(&original,21,record));
  assert(CONFIG_Decode(record,record[4]==CONFIG_STORE_FORMAT ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE,&loaded,NULL));
  assert(loaded.radio.selected_vfo==0 && loaded.radio.vfos[0].tx.tone==258);
  assert(loaded.radio.vfos[0].tx.frequency_hz==438600000);
  assert(loaded.radio.vfos[0].bands[6].parameters.rx_tone==51);
  assert(loaded.radio.vfos[1].current.frequency_hz==radioBands[6].initial_hz);
  assert(loaded.radio.vfos[1].current.parameters.rx_tone==155);
  assert(!loaded.radio.vfos[1].tx.enabled && !loaded.radio.vfos[1].tx.tone && !loaded.radio.vfos[1].tx.frequency_hz);
  // Load/migrate does not write; first format8 save retains the old good slot.
  memset(&memory,0xff,sizeof(memory));memory.writes=0;memory.write_limit=-1;
  memcpy(memory.bytes,record,sizeof(record));
  assert(CONFIG_Load(&io,&loaded,&sequence) && sequence==21 && !memory.writes);
  memory.write_limit=30;assert(!CONFIG_Save(&io,&loaded,NULL));
  assert(CONFIG_Load(&io,&loaded,&sequence) && sequence==21);
  memory.write_limit=-1;assert(CONFIG_Save(&io,&loaded,&sequence) && sequence==22);
  assert(memory.bytes[4]==5 && memory.bytes[CONFIG_STORE_BASE+4]==8);
  assert(CONFIG_Load(&io,&loaded,&sequence) && sequence==22);
  // Format6 upgrades retain both VFOs, B selection, tones, shared presets.
  assert(encodeV6(&original,30,record));
  assert(CONFIG_Decode(record,CONFIG_LEGACY_SLOT_SIZE,&loaded,&sequence) && sequence==30);
  assert(loaded.radio.selected_vfo==1 && loaded.radio.vfo_count==2 && !loaded.radio.watch_enabled);
  assert(loaded.radio.vfos[0].tx.tone==258 && loaded.radio.vfos[1].tx.tone==155);
  assert(loaded.radio.vfos[1].current.frequency_hz==438550000);
  assert(loaded.radio.vfos[2].current.frequency_hz==radioBands[5].initial_hz);
  assert(loaded.radio.vfos[3].current.parameters.rx_tone==155);
  assert(loaded.radio.vfos[0].watch && loaded.radio.vfos[1].watch && !loaded.radio.vfos[2].watch);
  assert(!CONFIG_Decode(record,RECORD_BYTES,&loaded,NULL)); // Wrong record length is not a migration.
  // Keep both old slots untouched during migration, including interrupted writes.
  memset(&memory,0xff,sizeof(memory));memory.writes=0;memory.write_limit=-1;
  memcpy(memory.bytes+CONFIG_LEGACY_SLOT_SIZE,record,CONFIG_LEGACY_SLOT_SIZE);
  assert(CONFIG_Load(&io,&loaded,&sequence) && sequence==30 && !memory.writes);
  memory.write_limit=15;assert(!CONFIG_Save(&io,&loaded,NULL));
  assert(CONFIG_Load(&io,&loaded,&sequence) && sequence==30);
  memory.write_limit=-1;assert(CONFIG_Save(&io,&loaded,&sequence) && sequence==31);
  assert(!memcmp(memory.bytes+CONFIG_LEGACY_SLOT_SIZE,record,CONFIG_LEGACY_SLOT_SIZE));
  assert(CONFIG_Load(&io,&loaded,&sequence) && sequence==31);
  // Enable four slots and save an offscreen selection, independent TX and watch set.
  assert(RADIO_STATE_SetCount(&loaded.radio,4));
  assert(RADIO_STATE_Set(&loaded.radio,2,145750000,(RadioParameters){RX_NFM,25000,14000,5,50}));
  assert(RADIO_STATE_Set(&loaded.radio,3,439250000,(RadioParameters){RX_NFM,12500,12000,4,155}));
  loaded.radio.vfos[3].tx=(TxConfig){true,TX_POWER_HIGH,439350000,50};
  assert(RADIO_STATE_SetWatch(&loaded.radio,0,false));
  assert(RADIO_STATE_SetWatch(&loaded.radio,3,true));
  assert(!RADIO_STATE_SetWatch(&loaded.radio,2,true));
  loaded.radio.selected_vfo=3;loaded.radio.watch_enabled=true;
  assert(CONFIG_Save(&io,&loaded,&sequence) && sequence==32);
  assert(CONFIG_Load(&io,&original,&sequence) && sequence==32);
  assert(original.radio.vfo_count==4 && original.radio.selected_vfo==3 && original.radio.watch_enabled);
  assert(original.radio.vfos[2].current.frequency_hz==145750000);
  assert(original.radio.vfos[3].tx.frequency_hz==439350000 && original.radio.vfos[3].tx.tone==50);
  assert(original.radio.vfos[1].watch && original.radio.vfos[3].watch && !original.radio.vfos[0].watch);
  original.radio.vfos[2].watch=true;assert(!CONFIG_Encode(&original,1,record));
  original.radio.vfos[2].watch=false;assert(RADIO_STATE_SetCount(&original.radio,2));
  assert(original.radio.selected_vfo==0 && !original.radio.vfos[3].watch);
  assert(original.radio.vfos[3].current.frequency_hz==439250000);
  assert(CONFIG_Encode(&original,1,record));
  record[18]=2;put32(record+HEADER_CRC,crc32(record,RECORD_BYTES));
  assert(!CONFIG_Decode(record,RECORD_BYTES,&loaded,NULL));
  assert(CONFIG_Encode(&original,1,record));record[20]=2;
  put32(record+HEADER_CRC,crc32(record,RECORD_BYTES));assert(!CONFIG_Decode(record,RECORD_BYTES,&loaded,NULL));
  io.capacity=CONFIG_STORE_END-1;assert(!CONFIG_Load(&io,&loaded,NULL) && !CONFIG_Save(&io,&original,NULL));
  return 0;
}

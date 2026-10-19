// Configuration format 9: one layout, two copies, newest valid wins.
#include "../src/core/config-store.c"
#include "../src/boards/uvk6/storage-layout.h"
#include <assert.h>
#include <string.h>

typedef struct {
  uint8_t bytes[CONFIG_STORE_SLOTS][CONFIG_STORE_SLOT_SIZE];
  unsigned writes;
  int write_limit;
} FakeSlots;
static bool readIO(void *ctx, unsigned slot, uint16_t at, void *out, uint16_t n) {
  FakeSlots *io = ctx;
  assert(slot < CONFIG_STORE_SLOTS);
  if (at > CONFIG_STORE_SLOT_SIZE || n > CONFIG_STORE_SLOT_SIZE-at) return false;
  memcpy(out, io->bytes[slot]+at, n);
  return true;
}
static bool writeIO(void *ctx, unsigned slot, const void *data, uint16_t n) {
  FakeSlots *io = ctx;
  assert(slot < CONFIG_STORE_SLOTS && n == CONFIG_STORE_SLOT_SIZE);
  ++io->writes;
  if (io->write_limit >= 0 && io->write_limit < n) {
    memcpy(io->bytes[slot], data, io->write_limit); // Simulate power loss.
    return false;
  }
  memcpy(io->bytes[slot], data, n);
  return true;
}
static FakeSlots memory;
static void wipe(void) {
  memset(&memory,0xff,sizeof(memory));memory.writes=0;memory.write_limit=-1;
}

int main(void) {
  // Write guard: only the config pair and the channel area are writable.
  assert(EEPROM_CandidateFromAckMask(0x01) == EEPROM_BL24C64);
  assert(EEPROM_CandidateFromAckMask(0xff) == EEPROM_4MBIT);
  assert(EEPROM_CandidateFromAckMask(0x05) == EEPROM_UNKNOWN);
  assert(!EEPROM_WriteAllowed(0, 512));
  assert(EEPROM_WriteAllowed(CONFIG_STORE_BASE, CONFIG_STORE_SLOT_SIZE));
  assert(EEPROM_WriteAllowed(CONFIG_STORE_BASE+CONFIG_STORE_SLOT_SIZE,CONFIG_STORE_SLOT_SIZE));
  assert(!EEPROM_WriteAllowed(CONFIG_STORE_END-1,2));
  assert(!EEPROM_WriteAllowed(CONFIG_STORE_END,1));
  assert(!EEPROM_WriteAllowed(0x1e00, 1)); // Factory calibration.
  assert(EEPROM_WriteAllowed(CHANNEL_STORE_BASE, CHANNEL_STORE_RECORD_BYTES));
  assert(!EEPROM_WriteAllowed(CHANNEL_STORE_END-1, 2));
  assert(!EEPROM_WriteAllowed(UINT32_MAX, 64));
  // Resources: only the tail of a large part, never via the normal guard.
  const uint32_t cap = 524288u, tail = cap - RESOURCE_TAIL_BYTES;
  assert(!EEPROM_WriteAllowed(tail, 16));
  assert(EEPROM_ResourceWriteAllowed(tail, 16, cap));
  assert(EEPROM_ResourceWriteAllowed(cap - 128, 128, cap));
  assert(!EEPROM_ResourceWriteAllowed(cap - 127, 128, cap));
  assert(!EEPROM_ResourceWriteAllowed(tail - 1, 2, cap));
  assert(!EEPROM_ResourceWriteAllowed(0x1e00, 16, cap)); // Calibration stays out.
  assert(!EEPROM_ResourceWriteAllowed(CONFIG_STORE_BASE, 16, cap));
  assert(!EEPROM_ResourceWriteAllowed(8192 - 16, 16, 8192)); // Small parts: none.
  // Maintenance writes reach the whole calibration block and nothing beside it.
  assert(EEPROM_CalibrationWriteAllowed(0x1e00, 128) && EEPROM_CalibrationWriteAllowed(0x1f80, 128));
  assert(EEPROM_CalibrationWriteAllowed(0x1ed0, 9) && EEPROM_CalibrationWriteAllowed(0x1fff, 1));
  assert(!EEPROM_CalibrationWriteAllowed(0x1dff, 2) && !EEPROM_CalibrationWriteAllowed(0x1f81, 128));
  assert(!EEPROM_CalibrationWriteAllowed(0x2000, 1) && !EEPROM_CalibrationWriteAllowed(0x1e00, 0));
  assert(!EEPROM_CalibrationWriteAllowed(0, 9) && !EEPROM_CalibrationWriteAllowed(CONFIG_STORE_BASE, 16));
  assert(!EEPROM_WriteAllowed(0x1ed0, 9)); // Never through the normal store.
  assert(EEPROM_ResourceBase(cap) + RESOURCE_SSB_PATCH_OFFSET == 0x7DD78u);
  assert(EEPROM_ResourceBase(0x1ffffu) == 0); // Small parts hold no resources.

  // Round trip of every field family, including the TX shift.
  DeviceConfig original, loaded;
  CONFIG_Default(&original);
  assert(RADIO_STATE_SetCount(&original.radio,4));
  original.radio.selected_slot=3;
  assert(RADIO_STATE_Set(&original.radio,2,145750000,(RadioParameters){RX_NFM,25000,14000,5,50}));
  assert(RADIO_STATE_Set(&original.radio,3,100000000,(RadioParameters){RX_WFM,100000,0,0,0}));
  original.radio.slots[1].tx=(TxConfig){true,TX_POWER_HIGH,TX_SHIFT_MINUS,5000000,0,155};
  original.radio.slots[2].tx=(TxConfig){false,TX_POWER_MEDIUM,TX_SHIFT_SPLIT,600000,146500000,258};
  original.radio.slots[0].source=RADIO_SOURCE_CHANNEL;original.radio.slots[0].channel_number=200;
  original.radio.presets[6].squelch=7;
  original.preferences.mic=9;original.preferences.tx_timeout_seconds=300;
  uint8_t record[CONFIG_STORE_SLOT_SIZE];
  assert(CONFIG_Encode(&original,7,record));
  uint32_t sequence=0;
  assert(CONFIG_Decode(record,&loaded,&sequence) && sequence==7);
  assert(loaded.radio.slot_count==4 && loaded.radio.selected_slot==3);
  assert(loaded.radio.slots[2].current.frequency_hz==145750000 &&
         loaded.radio.slots[2].current.parameters.rx_tone==50);
  assert(loaded.radio.slots[3].current.parameters.mode==RX_WFM);
  const TxConfig *tx=&loaded.radio.slots[1].tx;
  assert(tx->enabled && tx->power==TX_POWER_HIGH && tx->shift==TX_SHIFT_MINUS &&
         tx->offset_hz==5000000 && tx->tone==155);
  tx=&loaded.radio.slots[2].tx;
  assert(!tx->enabled && tx->shift==TX_SHIFT_SPLIT && tx->offset_hz==600000 &&
         tx->split_hz==146500000 && tx->tone==258);
  assert(loaded.radio.slots[0].source==RADIO_SOURCE_CHANNEL && loaded.radio.slots[0].channel_number==200);
  assert(loaded.radio.presets[6].squelch==7);
  assert(loaded.preferences.mic==9 && loaded.preferences.tx_timeout_seconds==300);

  // Invalid values never encode; corrupt or foreign records never decode.
  original.preferences.deviation=205;assert(!CONFIG_Encode(&original,1,record));
  original.preferences.deviation=130;
  original.radio.slots[1].tx.offset_hz=TX_OFFSET_MAX_HZ+10;assert(!CONFIG_Encode(&original,1,record));
  original.radio.slots[1].tx.offset_hz=5000000;
  original.radio.slots[2].tx.split_hz=700000000;assert(!CONFIG_Encode(&original,1,record));
  original.radio.slots[2].tx.split_hz=146500000;
  original.radio.slots[0].channel_number=USER_CHANNEL_COUNT+1;assert(!CONFIG_Encode(&original,1,record));
  original.radio.slots[0].channel_number=200;
  assert(CONFIG_Encode(&original,1,record));
  record[PAYLOAD_START+5]^=1;assert(!CONFIG_Decode(record,&loaded,NULL)); // CRC.
  assert(CONFIG_Encode(&original,1,record));
  record[HEADER_VERSION]=8;put32(record+HEADER_CRC,crc32(record,RECORD_BYTES));
  assert(!CONFIG_Decode(record,&loaded,NULL)); // Older formats are not read.
  assert(CONFIG_Encode(&original,1,record));
  record[PAYLOAD_START+2+3+BAND_BYTES*(RADIO_STATE_BANDS+1u)]|=0x20; // Unknown TX flag bit.
  put32(record+HEADER_CRC,crc32(record,RECORD_BYTES));
  assert(!CONFIG_Decode(record,&loaded,NULL));

  // Store: empty, then alternate copies; the newest valid copy loads.
  ConfigStore io={&memory,readIO,writeIO};
  wipe();
  assert(!CONFIG_Load(&io,&loaded,NULL));
  assert(CONFIG_Save(&io,&original,&sequence) && sequence==1);
  assert(CONFIG_Load(&io,&loaded,&sequence) && sequence==1);
  original.radio.selected_slot=2;
  assert(CONFIG_Save(&io,&original,&sequence) && sequence==2);
  assert(CONFIG_Load(&io,&loaded,&sequence) && sequence==2 && loaded.radio.selected_slot==2);
  // Interrupted write (power lost before the CRC is complete): the older
  // copy survives and loads.
  original.radio.selected_slot=1;
  memory.write_limit=HEADER_CRC+2;assert(!CONFIG_Save(&io,&original,NULL));
  memory.write_limit=-1;
  assert(CONFIG_Load(&io,&loaded,&sequence) && sequence==2 && loaded.radio.selected_slot==2);
  assert(CONFIG_Save(&io,&original,&sequence) && sequence==3);
  // A damaged newest copy falls back to the older one.
  memory.bytes[0][PAYLOAD_START]^=0xff;
  assert(CONFIG_Load(&io,&loaded,&sequence) && sequence==2);
  // Sequence numbers compare across the wrap.
  wipe();
  assert(CONFIG_Encode(&original,UINT32_MAX,record));
  memcpy(memory.bytes[0],record,RECORD_BYTES);
  assert(CONFIG_Save(&io,&original,&sequence) && sequence==0);
  assert(CONFIG_Load(&io,&loaded,&sequence) && sequence==0);
  // An old format-8 record is "no configuration": nothing loads.
  wipe();
  assert(CONFIG_Encode(&original,5,record));
  record[HEADER_VERSION]=8;put32(record+HEADER_CRC,crc32(record,RECORD_BYTES));
  memcpy(memory.bytes[0],record,RECORD_BYTES);
  assert(!CONFIG_Load(&io,&loaded,NULL));
  // A store without read or write access refuses.
  io.write=NULL;assert(!CONFIG_Save(&io,&original,NULL));
  io.read=NULL;assert(!CONFIG_Load(&io,&loaded,NULL));
  return 0;
}

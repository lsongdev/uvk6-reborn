#include "../src/core/channel-store.c"
#include <assert.h>

static uint8_t eeprom[524288];
static int writeLimit = -1;
static bool failRead;
uint32_t EEPROM_DetectSize(void) { return sizeof(eeprom); }
int EEPROM_ReadBuffer(uint32_t at, void *out, uint16_t size) {
  if (failRead || at+size>sizeof(eeprom)) return -1;
  memcpy(out,eeprom+at,size);return 0;
}
bool EEPROM_TryWriteBuffer(uint32_t at, const uint8_t *bytes, uint16_t size) {
  assert(at>=CHANNEL_STORE_BASE && at+size<=CHANNEL_STORE_END);
  unsigned n = writeLimit<0 ? size : (unsigned)writeLimit;
  if (n>size) n=size;
  memcpy(eeprom+at,bytes,n);
  return n==size;
}
int main(void) {
  memset(eeprom,0xff,sizeof(eeprom));
  UserChannel ch={.name="TEST",.frequency_hz=145500000,
    .rx={.mode=RX_NFM,.step_hz=12500,.bandwidth_hz=12000,.squelch=4}};
  UserChannel loaded;
  assert(CHANNEL_STORE_Init());
  assert(CHANNEL_STORE_Save(12,&ch));
  for (int n=0;n<(int)CHANNEL_RECORD_BYTES;++n) {
    writeLimit=n;
    ch.rx.squelch = (uint8_t)(4 + (n & 1));
    assert(!CHANNEL_STORE_Save(12,&ch));
    assert(CHANNEL_STORE_IsOccupied(12));
    assert(CHANNEL_STORE_Init());
    assert(CHANNEL_STORE_Load(12,&loaded));
    assert(loaded.frequency_hz==ch.frequency_hz);
    assert(loaded.rx.squelch==4);
  }
  writeLimit=-1;
  ch.rx.squelch = 5;
  assert(CHANNEL_STORE_Save(12,&ch));
  assert(CHANNEL_STORE_Init() && CHANNEL_STORE_Load(12,&loaded));
  assert(!strcmp(loaded.name,"TEST"));
  failRead=true;
  ch.rx.squelch = 6;
  assert(!CHANNEL_STORE_Save(12,&ch));
  assert(CHANNEL_STORE_IsOccupied(12)); // Cache commits only after verification.
  failRead=false;
  assert(CHANNEL_STORE_Init()); // Both CRC-protected copies remain recoverable.
  assert(CHANNEL_STORE_Save(128,&ch));
  uint16_t next;
  assert(CHANNEL_STORE_Next(12,true,&next) && next==128);
  assert(CHANNEL_STORE_Next(128,true,&next) && next==12);
  indexCache[127].sequence=UINT32_MAX;
  uint8_t wrapped[CHANNEL_RECORD_BYTES];
  encode(wrapped,128,&ch,UINT32_MAX);
  memcpy(eeprom+address(128,indexCache[127].copy),wrapped,sizeof(wrapped));
  assert(CHANNEL_STORE_Save(128,&ch));
  assert(indexCache[127].sequence==1);
  assert(CHANNEL_STORE_Init());
  assert(CHANNEL_STORE_IsOccupied(128));
  return 0;
}

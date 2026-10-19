// 200 channels, two 64-byte copies each; start-up reads only headers.
#include "../src/store/channel-store.c"
#include "fake-storage.h"
#include <assert.h>

static void fill(uint8_t value) {
  memset(fakeArea[STORAGE_CHANNELS_A],value,FAKE_AREA_BYTES);
  memset(fakeArea[STORAGE_CHANNELS_B],value,FAKE_AREA_BYTES);
}
int main(void) {
  // Erased (0xFF) and zero-filled storage both read as empty.
  fill(0x00);
  assert(CHANNEL_STORE_Init() && !CHANNEL_STORE_IsOccupied(1));
  fill(0xff);
  fakeReadBytes=0;
  assert(CHANNEL_STORE_Init() && CHANNEL_STORE_FirstEmpty()==1);
  assert(fakeReadBytes==USER_CHANNEL_COUNT*CHANNEL_STORE_COPY_COUNT*2u); // Headers only.

  // Round trip: a 16-character name and every TX field.
  UserChannel ch={.name="Repeater 438.725",.frequency_hz=438725000,
    .rx={.mode=RX_NFM,.step_hz=12500,.bandwidth_hz=12000,.squelch=4,.rx_tone=12},
    .tx={.enabled=true,.power=TX_POWER_MEDIUM,.shift=TX_SHIFT_MINUS,
         .offset_hz=5000000,.split_hz=0,.tone=12}};
  UserChannel loaded;
  assert(strlen(ch.name)==USER_CHANNEL_NAME_MAX);
  assert(CHANNEL_STORE_Save(12,&ch) && CHANNEL_STORE_Load(12,&loaded));
  assert(!strcmp(loaded.name,ch.name) && loaded.frequency_hz==438725000);
  assert(loaded.rx.rx_tone==12 && loaded.tx.enabled && loaded.tx.power==TX_POWER_MEDIUM);
  assert(loaded.tx.shift==TX_SHIFT_MINUS && loaded.tx.offset_hz==5000000 && loaded.tx.tone==12);
  ch.tx.shift=TX_SHIFT_SPLIT;ch.tx.split_hz=145500000;
  assert(CHANNEL_STORE_Save(12,&ch) && CHANNEL_STORE_Init() && CHANNEL_STORE_Load(12,&loaded));
  assert(loaded.tx.shift==TX_SHIFT_SPLIT && loaded.tx.split_hz==145500000);
  // Invalid channels are refused.
  UserChannel bad=ch;bad.tx.offset_hz=TX_OFFSET_MAX_HZ+10;
  assert(!CHANNEL_STORE_Save(13,&bad));
  bad=ch;bad.frequency_hz=0;assert(!CHANNEL_STORE_Save(13,&bad));
  assert(!CHANNEL_STORE_Save(0,&ch) && !CHANNEL_STORE_Save(USER_CHANNEL_COUNT+1,&ch));

  // Power loss at every byte of the first copy keeps the previous version.
  for (int n=0;n<(int)CHANNEL_STORE_RECORD_BYTES;++n) {
    fakeWriteLimit=n;
    ch.rx.squelch = (uint8_t)(5 + (n & 1));
    assert(!CHANNEL_STORE_Save(12,&ch));
    assert(CHANNEL_STORE_IsOccupied(12));
    assert(CHANNEL_STORE_Init());
    assert(CHANNEL_STORE_Load(12,&loaded) && loaded.rx.squelch==4);
  }
  fakeWriteLimit=-1;
  ch.rx.squelch = 5;
  assert(CHANNEL_STORE_Save(12,&ch));
  assert(CHANNEL_STORE_Init() && CHANNEL_STORE_Load(12,&loaded) && loaded.rx.squelch==5);
  // A save mirrors the record: both copies hold the same bytes afterwards.
  assert(!memcmp(fakeArea[STORAGE_CHANNELS_A]+11*64,fakeArea[STORAGE_CHANNELS_B]+11*64,64));
  // Power loss while mirroring keeps the new version.
  for (int n=0;n<(int)CHANNEL_STORE_RECORD_BYTES;++n) {
    ch.rx.squelch=(uint8_t)(n%2 ? 7 : 8);
    fakeWrites=0;fakeCutAt=2;fakeWriteLimit=n;
    assert(!CHANNEL_STORE_Save(12,&ch) && fakeWrites==2);
    fakeCutAt=0;fakeWriteLimit=-1;
    assert(CHANNEL_STORE_Init() && CHANNEL_STORE_Load(12,&loaded) && loaded.rx.squelch==ch.rx.squelch);
  }
  // A flash erase interrupted in one area: every channel recovers its
  // current version from the other.
  UserChannel other=ch;other.rx.squelch=3;
  assert(CHANNEL_STORE_Save(40,&other) && CHANNEL_STORE_Save(12,&ch));
  memset(fakeArea[STORAGE_CHANNELS_A],0xff,FAKE_AREA_BYTES);
  assert(CHANNEL_STORE_Init());
  assert(CHANNEL_STORE_Load(40,&loaded) && loaded.rx.squelch==3);
  assert(CHANNEL_STORE_Load(12,&loaded) && loaded.rx.squelch==ch.rx.squelch);
  assert(CHANNEL_STORE_Delete(40));
  // A failed read-back leaves the stored channel untouched.
  fakeFailRead=true;ch.rx.squelch=6;
  assert(!CHANNEL_STORE_Save(12,&ch) && CHANNEL_STORE_IsOccupied(12));
  fakeFailRead=false;
  assert(CHANNEL_STORE_Init() && CHANNEL_STORE_Load(12,&loaded));
  // Sequence numbers compare across their 8-bit wrap.
  for (unsigned i=0;i<300;++i) {
    ch.rx.squelch=(uint8_t)(i%10);
    assert(CHANNEL_STORE_Save(12,&ch));
  }
  assert(CHANNEL_STORE_Init() && CHANNEL_STORE_Load(12,&loaded) && loaded.rx.squelch==9);

  // Navigation, the last channel, and delete.
  assert(CHANNEL_STORE_Save(USER_CHANNEL_COUNT,&ch));
  uint16_t next;
  assert(CHANNEL_STORE_Next(12,true,&next) && next==USER_CHANNEL_COUNT);
  assert(CHANNEL_STORE_Next(USER_CHANNEL_COUNT,true,&next) && next==12);
  assert(CHANNEL_STORE_Next(12,false,&next) && next==USER_CHANNEL_COUNT);
  assert(CHANNEL_STORE_Delete(12) && !CHANNEL_STORE_IsOccupied(12));
  assert(CHANNEL_STORE_Init() && !CHANNEL_STORE_IsOccupied(12) && CHANNEL_STORE_IsOccupied(USER_CHANNEL_COUNT));
  assert(!CHANNEL_STORE_Delete(12) && CHANNEL_STORE_FirstEmpty()==1);
  assert(CHANNEL_STORE_Save(12,&ch) && CHANNEL_STORE_Load(12,&loaded)); // Reusable.
  return 0;
}

#include "radio/rds-receiver.h"
#include <assert.h>
#include <string.h>

static RdsGroup group(uint16_t b, uint16_t c, uint16_t d) {
  return (RdsGroup){.blocks={0x40a7,b,c,d}};
}
static void twice(RdsDecoder *d, RdsGroup g) { RDS_Feed(d,&g); RDS_Feed(d,&g); }
static void ps(RdsDecoder *d, const char text[8], bool versionB) {
  for (int i=3; i>=0; --i)
    twice(d,group((versionB ? 0x800 : 0) | i,0xffff,
                 (uint16_t)(uint8_t)text[2*i]<<8 | (uint8_t)text[2*i+1]));
}
static void rt(RdsDecoder *d, const uint8_t *text, unsigned length, bool versionB, bool ab) {
  unsigned size = versionB ? 2 : 4;
  for (int i=(length+size-1)/size-1; i>=0; --i) {
    uint8_t bytes[4]={' ',' ',' ',' '};
    for (unsigned j=0; j<size && i*size+j<length; ++j) bytes[j]=text[i*size+j];
    RdsGroup g = group(0x2000 | (versionB ? 0x800 : 0) | (ab ? 0x10 : 0) | i,
                       (uint16_t)bytes[0]<<8 | bytes[1],
                       (uint16_t)bytes[size-2]<<8 | bytes[size-1]);
    if (versionB) g.errors[2]=3; // C' is not used by 2B text.
    twice(d,g);
  }
}
// 4A clock time: MJD, UTC hour:minute and a signed half-hour offset.
static RdsGroup ct(uint32_t mjd, unsigned hour, unsigned minute, int halfHours) {
  unsigned offset = halfHours < 0 ? 32u | (unsigned)-halfHours : (unsigned)halfHours;
  return group(0x4000 | (mjd >> 15 & 3), (uint16_t)(mjd << 1 | hour >> 4),
               (uint16_t)((hour & 15) << 12 | minute << 6 | offset));
}
static RdsPacket packets[32];
static unsigned count, at, reads, clears;
static bool readOk=true;
static bool readPacket(void *unused, RdsPacket *packet, bool clear) {
  (void)unused;
  ++reads;
  if (!readOk) return false;
  if (clear) { ++clears; *packet=(RdsPacket){0}; return true; }
  *packet = at<count ? packets[at++] : (RdsPacket){0};
  return true;
}

int main(void) {
  RdsDecoder d={0};
  // One clock group is not trusted; the next minute (or a repeat) is.
  RdsGroup g=ct(60600,23,59,16);
  assert(!RDS_Feed(&d,&g) && !d.clock_new);
  g=ct(60601,0,0,16);RDS_Feed(&d,&g);
  assert(d.clock_new && d.clock==8*60); // 00:00 UTC +8 h.
  d.clock_new=false;RDS_Feed(&d,&g);assert(d.clock_new && d.clock==8*60);
  // Negative offsets wrap to the previous day.
  RDS_Reset(&d);g=ct(60601,2,30,-6);RDS_Feed(&d,&g);RDS_Feed(&d,&g);
  assert(d.clock_new && d.clock==23*60+30);
  // A jump, an error in C/D, 4B, or an invalid time/MJD is never published.
  RDS_Reset(&d);g=ct(60601,10,0,0);RDS_Feed(&d,&g);
  g=ct(60601,10,5,0);RDS_Feed(&d,&g);assert(!d.clock_new);
  g.errors[3]=1;RDS_Feed(&d,&g);assert(!d.clock_new);
  g.errors[3]=0;g.blocks[1]|=0x800;RDS_Feed(&d,&g);assert(!d.clock_new);
  RDS_Reset(&d);g=ct(0,10,0,0);RDS_Feed(&d,&g);RDS_Feed(&d,&g);assert(!d.clock_new);
  g=ct(60601,24,0,0);RDS_Feed(&d,&g);RDS_Feed(&d,&g);assert(!d.clock_new);
  RDS_Reset(&d);
  // A single observation of each PS segment must not publish a name.
  for (unsigned i=0; i<4; ++i) {
    RdsGroup g=group(i,0xffff,0x4141);
    assert(!RDS_Feed(&d,&g));
  }
  assert(!d.ps[0]);
  ps(&d,"RADIO FM",false);
  assert(!strcmp(d.ps,"RADIO FM"));
  // 0B uses D for PS, irrespective of the C block quality.
  g=group(0x800,0,0x5241);g.errors[2]=3;
  assert(!RDS_Feed(&d,&g));
  ps(&d,"NEW FM  ",true);
  assert(!strcmp(d.ps,"NEW FM"));
  g=group(0,0,0x5858);g.errors[3]=3;
  assert(!RDS_Feed(&d,&g) && !strcmp(d.ps,"NEW FM"));
  g=group(0x2000,0x4261,0x640d);g.errors[2]=2;
  assert(!RDS_Feed(&d,&g) && !d.text[0]);

  rt(&d,(const uint8_t *)"Music and news\r",15,false,false);
  assert(!strcmp(d.text,"Music and news"));
  // A/B change clears the previous RT immediately, while PS is retained.
  g=group(0x2011,0x6578,0x740d);
  assert(RDS_Feed(&d,&g) && !d.text[0] && !strcmp(d.ps,"NEW FM"));
  twice(&d,g);
  assert(!d.text[0]); // Segment 0 is still missing despite the terminator.
  twice(&d,group(0x2010,0x4e65,0x7720));
  assert(!strcmp(d.text,"New ext"));
  // Changing A/B version with unchanged flag cannot mix 2A and 2B lengths.
  rt(&d,(const uint8_t *)"Short\r",6,true,true);
  assert(!strcmp(d.text,"Short"));
  // Full-length messages without a carriage return, including last segment.
  uint8_t full[64]; memset(full,'X',sizeof(full));
  rt(&d,full,64,false,false);assert(strlen(d.text)==64);
  memset(full,'Y',32);rt(&d,full,32,true,false);assert(strlen(d.text)==32);
  // Printable ASCII, safe fallback for extended characters and controls.
  uint8_t chars[]={'A',0x80,0x0a,'B',0x0d};
  rt(&d,chars,sizeof(chars),false,true);assert(!strcmp(d.text,"A? B"));
  // A clean station PI change clears both outputs, even on an unused group.
  g=group(0x4000,0,0);g.blocks[0]=0x1234;g.errors[0]=3;
  assert(!RDS_Feed(&d,&g) && d.ps[0]);
  g.errors[0]=0;assert(RDS_Feed(&d,&g));
  assert(!d.ps[0] && !d.text[0] && d.pi==0x1234);

  RdsReceiver r={0};
  RDS_ReceiverReset(&r,0);
  assert(!RDS_ReceiverUpdate(&r,0,true,readPacket,NULL) && !reads);
  assert(!RDS_ReceiverUpdate(&r,299,true,readPacket,NULL) && !reads);
  readOk=false;
  RDS_ReceiverUpdate(&r,300,true,readPacket,NULL);
  assert(r.clear_fifo && reads==1);
  readOk=true;
  RDS_ReceiverUpdate(&r,400,true,readPacket,NULL);
  assert(!r.clear_fifo && clears==1);
  for (unsigned i=0;i<8;++i) {
    packets[i]=(RdsPacket){.group=group(i%4,0,0x4141),.fifo_used=8-i,.synchronized=true};
  }
  count=8;
  assert(!RDS_ReceiverUpdate(&r,500,true,readPacket,NULL) && at==4);
  assert(RDS_ReceiverUpdate(&r,600,true,readPacket,NULL) && at==8);
  assert(!strcmp(r.decoder.ps,"AAAAAAAA"));
  // Failure stops the drain immediately; old text eventually expires.
  readOk=false;unsigned before=reads;
  assert(!RDS_ReceiverUpdate(&r,700,true,readPacket,NULL) && reads==before+1);
  assert(RDS_ReceiverUpdate(&r,15600,true,readPacket,NULL) && !r.decoder.ps[0]);
  ps(&r.decoder,"RADIO FM",false);
  assert(RDS_ReceiverUpdate(&r,15700,false,readPacket,NULL) && !r.decoder.ps[0]);
  assert(!r.enabled && r.clear_fifo);
  // Unsynchronized FIFO data is ignored; overflow discards partial/old text.
  readOk=true;
  RDS_ReceiverUpdate(&r,16000,true,readPacket,NULL);
  RDS_ReceiverUpdate(&r,16300,true,readPacket,NULL);
  count=1;at=0;
  packets[0]=(RdsPacket){.fifo_used=1,.group=group(0,0,0x4141)};
  RDS_ReceiverUpdate(&r,16400,true,readPacket,NULL);
  assert(!r.decoder.pi_valid);
  ps(&r.decoder,"RADIO FM",false);
  at=0;packets[0].synchronized=true;packets[0].overflow=true;
  assert(RDS_ReceiverUpdate(&r,16500,true,readPacket,NULL));
  assert(!r.decoder.ps[0] && r.decoder.ps_seen==1 && !r.decoder.ps_confirmed);
  // Timer arithmetic also works across uint32 wraparound.
  readOk=true;before=reads;
  RDS_ReceiverReset(&r,UINT32_MAX-100);
  RDS_ReceiverUpdate(&r,UINT32_MAX-100,true,readPacket,NULL);
  RDS_ReceiverUpdate(&r,198,true,readPacket,NULL);assert(reads==before);
  RDS_ReceiverUpdate(&r,199,true,readPacket,NULL);assert(reads==before+1);
  return 0;
}

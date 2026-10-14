// Render real firmware UI into host-only framebuffer snapshots.
#include "ui/components/radio-card.h"
#include "ui/graphics.h"
#include "core/rds.h"
#include <stdio.h>
#include <assert.h>
uint8_t gFrameBuffer[8][128];
bool gRedrawScreen;
static const char *outputDirectory;
static void snapshot(const char *filename) {
  char path[512];
  snprintf(path,sizeof(path),"%s/%s",outputDirectory,filename);
  FILE *file = fopen(path, "wb"); assert(file);
  fputs("P4\n128 64\n",file);
  for(unsigned y=0;y<64;y++) for(unsigned x=0;x<128;x+=8) {
    unsigned byte=0;
    for(unsigned i=0;i<8;i++) if(GetPixel(x+i,y)) byte|=128>>i;
    fputc(byte,file);
  }
  fclose(file);
}
static void base(void) {
  FillRect(0,0,128,64,C_CLEAR);
  PrintSmall(0,4,"RADIO");
  PrintSmallEx(108,4,POS_R,C_FILL,"F");DrawRect(115,0,12,5,C_FILL);
}
int main(int argc, char **argv) {
  if (argc != 2) return 1;
  outputDirectory = argv[1];
  UI_RadioCardView v={.id='A',.selected=true,.frequency_hz=103900000,.step_hz=100000,.mode=RX_WFM,
    .band_name="FM Radio",.activity=UI_RADIO_CARD_RECEIVING,.signal_valid=true,.signal_level=8};
  UI_RadioCardView second={.id='B',.frequency_hz=438506250,.step_hz=12500,
    .mode=RX_NFM,.band_name="70cm"};
  // Frequency only: large digits, extra decimals above the unit.
  base();UI_RadioCardRender(8,&v);snapshot("plain.pbm");
  UI_RadioCardRender(8+UI_RADIO_CARD_HEIGHT,&second);snapshot("frequency.pbm");
  // Names: PS over frequency with RT in the header; MR channel name.
  v.name="RADIO FM";v.radio_text="Music and news at the top of the hour";
  base();UI_RadioCardRender(8,&v);snapshot("single.pbm");
  second.name="Local repeater";
  UI_RadioCardRender(8+UI_RADIO_CARD_HEIGHT,&second);snapshot("name.pbm");
  snapshot("dual.pbm");
  // A frequency draft owns the body; its end stays visible above the unit.
  second.edit_text="1340.000001_";
  UI_RadioCardRender(8+UI_RADIO_CARD_HEIGHT,&second);snapshot("tuned.pbm");
  // First-release A/B: only the selected row owns RX and valid signal.
  base();FillRect(100,0,10,5,C_CLEAR);
  v=(UI_RadioCardView){.id='A',.selected=true,.frequency_hz=145500000,.step_hz=12500,
    .mode=RX_NFM,.band_name="2m",.activity=UI_RADIO_CARD_RECEIVING,.signal_valid=true,.signal_level=8};
  second=(UI_RadioCardView){.id='B',.frequency_hz=438500000,.step_hz=12500,
    .mode=RX_NFM,.band_name="70cm",.activity=UI_RADIO_CARD_IDLE};
  UI_RadioCardRender(8,&v);UI_RadioCardRender(36,&second);snapshot("ab-a.pbm");
  v.selected=false;v.activity=UI_RADIO_CARD_IDLE;v.signal_valid=false;
  second.selected=true;second.activity=UI_RADIO_CARD_RECEIVING;second.signal_valid=true;second.signal_level=11;
  base();FillRect(100,0,10,5,C_CLEAR);UI_RadioCardRender(8,&v);UI_RadioCardRender(36,&second);snapshot("ab-b.pbm");
  snapshot("multi-ab.pbm");
  v.id='C';v.selected=true;v.frequency_hz=145600000;
  second.id='D';second.selected=false;second.frequency_hz=438600000;
  second.activity=UI_RADIO_CARD_IDLE;second.signal_valid=false;
  base();FillRect(100,0,10,5,C_CLEAR);
  UI_RadioCardRender(8,&v);UI_RadioCardRender(36,&second);snapshot("multi-cd.pbm");
  v.id='A';v.frequency_hz=145500000;
  second.id='B';second.frequency_hz=438500000;second.activity=UI_RADIO_CARD_WATCHING;
  base();FillRect(0,0,110,5,C_CLEAR);PrintSmall(0,4,"DW B");
  UI_RadioCardRender(8,&v);UI_RadioCardRender(36,&second);snapshot("watch-probe.pbm");
  second.activity=UI_RADIO_CARD_RECEIVING;second.signal_valid=true;second.signal_level=11;
  UI_RadioCardRender(36,&second);snapshot("watch-rx.pbm");
  second.activity=UI_RADIO_CARD_IDLE;second.signal_valid=false;
  v.header_message="TX mode not supported";
  base();FillRect(100,0,10,5,C_CLEAR);
  UI_RadioCardRender(8,&v);UI_RadioCardRender(36,&second);snapshot("error-short.pbm");
  v.header_message="Channel store unavailable: please retry";
  UI_RadioCardRender(8,&v);snapshot("error-long.pbm");
  base();
  v=(UI_RadioCardView){.id='A',.selected=true,.frequency_hz=1339999980,.step_hz=25000,
    .mode=RX_NFM,.band_name="UHF",.activity=UI_RADIO_CARD_RECEIVING,.signal_level=14,.signal_valid=true};
  UI_RadioCardRender(8,&v);snapshot("max.pbm");
  v.name="My Channnnnnnnnnnnnel";UI_RadioCardRender(8,&v);snapshot("maxtext.pbm");
  base();
  v=(UI_RadioCardView){.id='A',.selected=true,.frequency_hz=410020,.step_hz=1000,.mode=RX_AM,.band_name="LW"};
  UI_RadioCardRender(8,&v);snapshot("lw.pbm");
  v=(UI_RadioCardView){.id='A',.selected=true,.frequency_hz=438500000,.step_hz=12500,
    .mode=RX_NFM,.band_name="70cm",.activity=UI_RADIO_CARD_TRANSMITTING,.power_label="LOW"};
  UI_RadioCardRender(8,&v);snapshot("tx.pbm");
  v.activity=UI_RADIO_CARD_SEEKING;UI_RadioCardRender(8,&v);snapshot("seek.pbm");
  const uint16_t ps[]={0x5241,0x4449,0x4f20,0x464d};
  const uint16_t rt[][2]={{0x4d75,0x7369},{0x6320,0x616e},{0x6420,0x6e65},{0x7773,0x0d20}};
  RdsDecoder decoder={0};
  for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<4;++i) {
    RdsGroup g={.blocks={0x40a7,i,0,ps[i]}};RDS_Feed(&decoder,&g);
    g=(RdsGroup){.blocks={0x40a7,0x2000|i,rt[i][0],rt[i][1]}};RDS_Feed(&decoder,&g);
  }
  base();
  v=(UI_RadioCardView){.id='A',.selected=true,.frequency_hz=103900000,.step_hz=100000,.mode=RX_WFM,
    .band_name="FM Radio",.name=decoder.ps,.radio_text=decoder.text,
    .activity=UI_RADIO_CARD_RECEIVING,.signal_level=8,.signal_valid=true};
  UI_RadioCardRender(8,&v);UI_RadioCardRender(8,&v);snapshot("rds.pbm");
  return 0;
}

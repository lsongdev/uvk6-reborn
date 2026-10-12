// Render real firmware UI into host-only framebuffer snapshots.
#include "ui/components/vfo.h"
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
  UI_VfoState a={0},b={0};
  UI_VfoView v={.id='A',.selected=true,.frequency_hz=103900000,.step_hz=100000,.mode=RX_WFM,
    .band_name="FM Radio",.name="RADIO FM",.rds="Music and news",
    .activity=UI_VFO_RECEIVING,.signal_valid=true,.signal_level=8};
  base();UI_VfoRender(8,&v,&a,0);snapshot("plain.pbm");
  UI_VfoRender(8,&v,&a,1000);snapshot("single.pbm");
  UI_VfoShowFrequency(&a,2000);UI_VfoRender(8,&v,&a,2000);
  UI_VfoView second={.id='B',.frequency_hz=438506250,.step_hz=12500,
    .mode=RX_NFM,.band_name="70cm",.name="Local repeater"};
  UI_VfoRender(8+UI_VFO_HEIGHT,&second,&b,2000);snapshot("frequency.pbm");
  UI_VfoRender(8,&v,&a,3000);UI_VfoRender(8+UI_VFO_HEIGHT,&second,&b,3000);snapshot("name.pbm");
  snapshot("dual.pbm");
  UI_VfoShowFrequency(&a,3100);UI_VfoRender(8,&v,&a,3100);snapshot("tuned.pbm");
  // First-release A/B: only the selected row owns RX and valid signal.
  base();FillRect(100,0,10,5,C_CLEAR);a=(UI_VfoState){0};b=(UI_VfoState){0};
  v=(UI_VfoView){.id='A',.selected=true,.frequency_hz=145500000,.step_hz=12500,
    .mode=RX_NFM,.band_name="2m",.activity=UI_VFO_RECEIVING,.signal_valid=true,.signal_level=8};
  second=(UI_VfoView){.id='B',.frequency_hz=438500000,.step_hz=12500,
    .mode=RX_NFM,.band_name="70cm",.activity=UI_VFO_IDLE};
  UI_VfoRender(8,&v,&a,0);UI_VfoRender(36,&second,&b,0);snapshot("ab-a.pbm");
  v.selected=false;v.activity=UI_VFO_IDLE;v.signal_valid=false;
  second.selected=true;second.activity=UI_VFO_RECEIVING;second.signal_valid=true;second.signal_level=11;
  base();FillRect(100,0,10,5,C_CLEAR);UI_VfoRender(8,&v,&a,1000);UI_VfoRender(36,&second,&b,1000);snapshot("ab-b.pbm");
  snapshot("multi-ab.pbm");
  v.id='C';v.selected=true;v.frequency_hz=145600000;
  second.id='D';second.selected=false;second.frequency_hz=438600000;
  second.activity=UI_VFO_IDLE;second.signal_valid=false;
  base();FillRect(100,0,10,5,C_CLEAR);
  UI_VfoRender(8,&v,&a,0);UI_VfoRender(36,&second,&b,0);snapshot("multi-cd.pbm");
  v.id='A';v.frequency_hz=145500000;
  second.id='B';second.frequency_hz=438500000;second.activity=UI_VFO_WATCHING;
  base();FillRect(0,0,110,5,C_CLEAR);PrintSmall(0,4,"DW B");
  UI_VfoRender(8,&v,&a,0);UI_VfoRender(36,&second,&b,0);snapshot("watch-probe.pbm");
  second.activity=UI_VFO_RECEIVING;second.signal_valid=true;second.signal_level=11;
  UI_VfoRender(36,&second,&b,0);snapshot("watch-rx.pbm");
  second.activity=UI_VFO_IDLE;second.signal_valid=false;
  v.message="TX mode not supported";
  base();FillRect(100,0,10,5,C_CLEAR);
  UI_VfoRender(8,&v,&a,0);UI_VfoRender(36,&second,&b,0);snapshot("error-short.pbm");
  UI_VfoRender(8,&v,&a,UI_VFO_SCROLL_PAUSE_MS+a.overflow*UI_VFO_SCROLL_PIXEL_MS);snapshot("error-long.pbm");
  base();a=(UI_VfoState){0};
  v=(UI_VfoView){.id='A',.selected=true,.frequency_hz=1339999980,.step_hz=25000,
    .mode=RX_NFM,.band_name="UHF",.activity=UI_VFO_RECEIVING,.signal_level=14,.signal_valid=true};
  UI_VfoRender(8,&v,&a,0);snapshot("max.pbm");
  v.name="Main receiver";UI_VfoRender(8,&v,&a,1000);snapshot("maxtext.pbm");
  base();a=(UI_VfoState){0};
  v=(UI_VfoView){.id='A',.selected=true,.frequency_hz=410020,.step_hz=1000,.mode=RX_AM,.band_name="LW"};
  UI_VfoRender(8,&v,&a,0);snapshot("lw.pbm");
  v=(UI_VfoView){.id='A',.selected=true,.frequency_hz=438500000,.step_hz=12500,
    .mode=RX_NFM,.band_name="70cm",.activity=UI_VFO_TRANSMITTING,.power_label="LOW"};
  UI_VfoRender(8,&v,&a,0);snapshot("tx.pbm");
  v.activity=UI_VFO_SEEKING;UI_VfoRender(8,&v,&a,5000);snapshot("seek.pbm");
  const uint16_t ps[]={0x5241,0x4449,0x4f20,0x464d};
  const uint16_t rt[][2]={{0x4d75,0x7369},{0x6320,0x616e},{0x6420,0x6e65},{0x7773,0x0d20}};
  RdsDecoder decoder={0};
  for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<4;++i) {
    RdsGroup g={.blocks={0x40a7,i,0,ps[i]}};RDS_Feed(&decoder,&g);
    g=(RdsGroup){.blocks={0x40a7,0x2000|i,rt[i][0],rt[i][1]}};RDS_Feed(&decoder,&g);
  }
  base();a=(UI_VfoState){0};
  v=(UI_VfoView){.id='A',.selected=true,.frequency_hz=103900000,.step_hz=100000,.mode=RX_WFM,
    .band_name="FM Radio",.name=decoder.ps,.rds=decoder.text,
    .activity=UI_VFO_RECEIVING,.signal_level=8,.signal_valid=true};
  UI_VfoRender(8,&v,&a,0);UI_VfoRender(8,&v,&a,1000);snapshot("rds.pbm");
  base();FillRect(100,0,10,5,C_CLEAR);a=(UI_VfoState){0};b=(UI_VfoState){0};
  v=(UI_VfoView){.id='A',.selected=true,.frequency_hz=438500000,.step_hz=12500,
    .mode=RX_NFM,.band_name="70cm",.name="CH438.500"};
  second=(UI_VfoView){.id='B',.frequency_hz=145500000,.step_hz=12500,
    .mode=RX_NFM,.band_name="2m",.name="RX145.500"};
  UI_VfoRender(8,&v,&a,0);UI_VfoRender(36,&second,&b,0);snapshot("font-frequency.pbm");
  UI_VfoRender(8,&v,&a,1000);UI_VfoRender(36,&second,&b,1000);snapshot("font-name.pbm");
  return 0;
}

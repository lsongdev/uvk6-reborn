// Render the real firmware UI into host-only framebuffer snapshots.
#include "ui/components/vfo.h"
#include "ui/graphics.h"
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
int main(int argc, char **argv) {
  if (argc != 2) return 1;
  outputDirectory = argv[1];
  UI_VfoState a={0},b={0};
  UI_VfoView v={.id='A',.selected=true,.frequency_hz=103900000,.mode=RX_WFM,
    .band_short="FM",.name="FM station",.rds="Music and news",.signal_valid=true,.signal_level=8};
  PrintSmall(0,4,"RADIO");DrawHLine(0,6,128,C_FILL);DrawRect(115,0,12,5,C_FILL);
  UI_VfoRender(8,&v,&a,0);snapshot("single.pbm");
  v.name=NULL; v.rds=NULL;
  UI_VfoRender(8,&v,&a,0);snapshot("plain.pbm");
  v.name="FM station"; v.rds="Music and news";
  UI_VfoRender(8,&v,&a,0);
  v=(UI_VfoView){.id='B',.frequency_hz=410000,.mode=RX_AM,.band_short="LW",.name="Local station"};
  UI_VfoRender(8+UI_VFO_HEIGHT,&v,&b,0);snapshot("dual.pbm");
  v=(UI_VfoView){.id='A',.selected=true,.frequency_hz=1339999980,.mode=RX_NFM,.band_short="UHF",.signal_level=14,.signal_valid=true};
  UI_VfoRender(8,&v,&a,0);snapshot("max.pbm");
  v.name="Main receiver";v.rds="Long frequency example";
  UI_VfoRender(8,&v,&a,0);snapshot("maxtext.pbm");
  v=(UI_VfoView){.id='A',.selected=true,.frequency_hz=401000,.mode=RX_AM,.band_short="LW",.signal_level=4,.signal_valid=true};
  UI_VfoRender(8,&v,&a,0);snapshot("lw.pbm");
  return 0;
}

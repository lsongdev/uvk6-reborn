// Real framebuffer rendering: separate cards, title clipping and scrolling.
#include "ui/components/vfo.h"
#include "ui/components/frequency.h"
#include "ui/graphics.h"
#include <assert.h>
#include <string.h>
uint8_t gFrameBuffer[8][128];
bool gRedrawScreen;
static uint8_t saved[8][128];
static void sameRows(unsigned first, unsigned last) {
  for (unsigned y=first; y<last; ++y)
    for (unsigned x=0; x<128; ++x)
      assert(GetPixel(x,y) == !!(saved[y/8][x] & (1<<(y%8))));
}
int main(void) {
  const struct { uint32_t frequency; const char *text, *unit; } cases[] = {
    {103900000,"103.900","MHz"},
    {410000,"410","kHz"},
    {95925000,"95.925","MHz"},
    {438500000,"438.500","MHz"},
    {438512500,"438.5125","MHz"},
    {145506250,"145.50625","MHz"},
    {7100000,"7.100","MHz"},
    {410020,"410.02","kHz"},
    {999000,"999","kHz"},
    {1000000,"1.000","MHz"},
    {1340000000,"1340.000","MHz"},
    {1339999980,"1339.99998","MHz"},
    {1000001,"1.000001","MHz"},
  };
  for (unsigned i=0; i<sizeof(cases)/sizeof(cases[0]); ++i) {
    char text[16];
    assert(!strcmp(UI_FormatFrequency(cases[i].frequency,text),cases[i].unit));
    assert(!strcmp(text,cases[i].text));
  }
  UI_VfoState a={0}, b={0};
  UI_VfoView view={.id='A',.selected=true,.frequency_hz=1340000000,.mode=RX_NFM,
                  .band_short="UHF",.changed=true,.name="Main receiver",.signal_valid=true,.signal_level=14};
  view.frequency_hz=103900000;
  UI_VfoRender(8,&view,&a,0);
  memcpy(saved,gFrameBuffer,sizeof(saved));
  view.frequency_hz=1339999980;
  UI_VfoRender(8,&view,&a,0);
  for (unsigned y=8;y<28;++y) for (unsigned x=0;x<128;++x)
    if (x<12 || x>=114)
      assert(GetPixel(x,y) == !!(saved[y/8][x] & (1<<(y%8))));
  memset(gFrameBuffer,0xaa,sizeof(gFrameBuffer));
  memcpy(saved,gFrameBuffer,sizeof(saved));
  UI_VfoRender(8,&view,&a,0);
  sameRows(0,8); sameRows(28,64);
  assert(!strcmp(a.label,"Main receiver") && !a.overflow);
  memcpy(saved,gFrameBuffer,sizeof(saved));
  view=(UI_VfoView){.id='B',.frequency_hz=401000,.mode=RX_AM,.band_short="LW",.rds="Radio text"};
  UI_VfoRender(28,&view,&b,0);
  sameRows(0,28);
  assert(!strcmp(b.label,"Radio text"));
  view.name="Very long station name";
  view.rds="Another long radio text message";
  UI_VfoRender(28,&view,&b,100);
  assert(!strcmp(b.label,"Very long station name / Another long radio text message"));
  assert(b.overflow && !UI_VfoUpdate(&b,1000));
  memcpy(saved,gFrameBuffer,sizeof(saved));
  assert(UI_VfoUpdate(&b,2000) && b.offset > 0);
  UI_VfoRender(28,&view,&b,2000);
  sameRows(0,28); sameRows(36,64); // Scroll cannot alter frequency or other VFO.
  memcpy(saved,gFrameBuffer,sizeof(saved));
  view.name=NULL; view.rds=NULL;
  UI_VfoRender(28,&view,&b,2100);
  for (unsigned y=28;y<48;++y) for (unsigned x=0;x<128;++x)
    if (x<12 || x>=117)
      assert(GetPixel(x,y) == !!(saved[y/8][x] & (1<<(y%8))));
  assert(!b.label[0] && !b.overflow && !b.offset);
  return 0;
}

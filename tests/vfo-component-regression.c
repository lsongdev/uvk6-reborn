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
  // Names and frequencies use the same native 12 px ASCII glyphs.
  memset(gFrameBuffer,0,sizeof(gFrameBuffer));
  UI_BodyTextWindow(30,22,98,"A",0);
  unsigned inkRows=0;
  for(unsigned y=0;y<64;++y) {
    bool ink=false;
    for(unsigned x=30;x<128;++x) ink |= GetPixel(x,y);
    inkRows += ink;
  }
  assert(inkRows == 12);
  // Frequency and name use identical glyph pixels and the same baseline.
  UI_VfoState matching={0};
  UI_VfoView match={.frequency_hz=438500000,.mode=RX_NFM,.step_hz=12500,.band_name="70cm"};
  UI_VfoRender(8,&match,&matching,0);
  memcpy(saved,gFrameBuffer,sizeof(saved));
  match.name="438.500";
  UI_VfoRender(8,&match,&matching,1000);
  for (unsigned y=17;y<34;++y) for(unsigned x=30;x<110;++x)
    assert(GetPixel(x+18,y)==!!(saved[y/8][x]&(1<<(y%8))));
  // Scrolling must clip pixels and retain native descenders.
  memset(gFrameBuffer,0,sizeof(gFrameBuffer));
  UI_BodyTextWindow(30,22,98,"Agjp WWWWWWWWW",1);
  bool descender=false;
  for(unsigned y=0;y<64;++y) for(unsigned x=0;x<128;++x) {
    if(x<30 || y<9 || y>25) assert(!GetPixel(x,y));
    if(y==25 && GetPixel(x,y)) descender=true;
  }
  assert(descender);
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
  UI_VfoView view={.id='A',.selected=true,.frequency_hz=103900000,.step_hz=100000,
    .mode=RX_WFM,.band_name="FM Radio",.name="Main receiver",
    .activity=UI_VFO_RECEIVING,.signal_valid=true,.signal_level=14};
  memset(gFrameBuffer,0xaa,sizeof(gFrameBuffer));
  memcpy(saved,gFrameBuffer,sizeof(saved));
  UI_VfoRender(8,&view,&a,100);
  sameRows(0,8); sameRows(36,64);
  assert(!a.show_name && !UI_VfoUpdate(&a,1099));
  assert(UI_VfoUpdate(&a,1100) && a.show_name);
  UI_VfoRender(8,&view,&a,1100);
  assert(!strcmp(a.label,"Main receiver"));
  UI_VfoShowFrequency(&a,1200);
  UI_VfoRender(8,&view,&a,1200);
  assert(!a.show_name); // Same-frequency retune starts a fresh hold.
  assert(!UI_VfoUpdate(&a,2199));
  assert(UI_VfoUpdate(&a,2200));
  view.frequency_hz=1339999980;view.mode=RX_NFM;view.band_name="UHF";
  UI_VfoRender(8,&view,&a,2300);
  assert(!a.show_name && a.tuned_at == 2300);
  for(unsigned y=17;y<34;++y) {
    assert(!GetPixel(28,y) && !GetPixel(29,y)); // Longest frequency cannot hit status.
  }
  memcpy(saved,gFrameBuffer,sizeof(saved));
  view=(UI_VfoView){.id='B',.frequency_hz=401000,.step_hz=1000,.mode=RX_AM,
    .band_name="Longwave",.rds="Radio text"};
  UI_VfoRender(36,&view,&b,2300);
  sameRows(0,36);
  view.name="Very long station name";view.rds="Another long radio text message";
  UI_VfoRender(36,&view,&b,2400);
  assert(!strcmp(b.label,"Very long station name / Another long radio text message"));
  assert(!b.show_name && b.overflow);
  assert(UI_VfoUpdate(&b,3300));
  UI_VfoRender(36,&view,&b,3300);
  assert(!b.offset && !UI_VfoUpdate(&b,4499));
  memcpy(saved,gFrameBuffer,sizeof(saved));
  assert(UI_VfoUpdate(&b,4650) && b.offset == 1);
  UI_VfoRender(36,&view,&b,4650);
  sameRows(0,44); sameRows(62,64);
  for(unsigned y=44;y<62;++y) for(unsigned x=0;x<30;++x)
    assert(GetPixel(x,y) == !!(saved[y/8][x] & (1<<(y%8))));
  view.name=NULL;view.rds=NULL;UI_VfoRender(36,&view,&b,4700);
  assert(!b.show_name && !b.label[0] && !b.overflow && !b.offset);
  view.name="Late RDS";UI_VfoRender(36,&view,&b,4800);
  assert(b.show_name); // RDS arriving after the hold can be shown immediately.
  view.activity=UI_VFO_SEEKING;UI_VfoRender(36,&view,&b,4900);
  assert(!b.show_name && !UI_VfoUpdate(&b,10000));
  view.activity=UI_VFO_IDLE;UI_VfoRender(36,&view,&b,10000);
  assert(!b.show_name); // Search holds frequency until its last activity expires.
  assert(!UI_VfoUpdate(&b,10999));
  assert(UI_VfoUpdate(&b,11000));
  view.band_name="Very long custom band name that must never overlap the frequency or mode";
  UI_VfoRender(36,&view,&b,11001);
  assert(b.band_overflow);
  memcpy(saved,gFrameBuffer,sizeof(saved));
  assert(UI_VfoUpdate(&b,12351));UI_VfoRender(36,&view,&b,12351);
  for(unsigned y=36;y<44;++y) for(unsigned x=14+b.band_width;x<128;++x)
    assert(GetPixel(x,y) == !!(saved[y/8][x] & (1<<(y%8))));
  memcpy(saved,gFrameBuffer,sizeof(saved));UI_VfoRender(37,&view,&b,13000);
  sameRows(0,64); // Out-of-bounds cards never touch the framebuffer.
  a=(UI_VfoState){0};view.band_name="LW";
  UI_VfoRender(8,&view,&a,UINT32_MAX-500);
  assert(!UI_VfoUpdate(&a,498));
  assert(UI_VfoUpdate(&a,499) && a.show_name);
  UI_VfoRender(8,&view,&a,500);
  memcpy(saved,gFrameBuffer,sizeof(saved));
  char label[128]; strcpy(label,a.label);
  view.message="TX mode not supported";
  UI_VfoRender(8,&view,&a,500);
  sameRows(0,16); sameRows(36,64);
  assert(!strcmp(a.label,view.message) && a.show_name && a.overflow);
  assert(UI_VfoUpdate(&a,1850) && a.offset==1);
  UI_VfoRender(8,&view,&a,1850);
  for (unsigned y=16;y<36;++y) for(unsigned x=0;x<30;++x)
    assert(GetPixel(x,y)==!!(saved[y/8][x]&(1<<(y%8))));
  view.message=NULL; UI_VfoRender(8,&view,&a,1850);
  assert(!strcmp(a.label,label) && !a.showing_message);
  sameRows(0,64); // Message removal restores the original name and surrounding rows.
  view.message=NULL; view.edit_text="1340.000001_";
  memcpy(saved,gFrameBuffer,sizeof(saved));
  UI_VfoRender(8,&view,&a,2000);
  sameRows(0,8);sameRows(36,64);
  assert(a.tuning && !a.show_name);
  assert(!UI_VfoUpdate(&a,10000) && !a.show_name);
  for(unsigned y=17;y<34;++y) assert(!GetPixel(28,y) && !GetPixel(29,y));
  view.edit_text=NULL;UI_VfoRender(8,&view,&a,10000);
  assert(!a.tuning && !a.show_name);
  assert(UI_VfoUpdate(&a,11000) && a.show_name);
  return 0;
}

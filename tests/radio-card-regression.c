// Real framebuffer rendering: stateless cards, reserved tail column, no scrolling.
// The body is always large: the name if there is one, else the frequency.
#include "ui/components/radio-card.h"
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
static bool ink(unsigned x0, unsigned x1, unsigned y0, unsigned y1) {
  for (unsigned y=y0; y<y1; ++y) for (unsigned x=x0; x<x1; ++x) if (GetPixel(x,y)) return true;
  return false;
}
static unsigned leftmost(unsigned y0, unsigned y1) {
  for (unsigned x=28; x<128; ++x) if (ink(x,x+1,y0,y1)) return x; // Body only.
  return 128;
}
static void render(uint8_t y, const UI_RadioCardView *view) {
  memset(gFrameBuffer,0,sizeof(gFrameBuffer));
  UI_RadioCardRender(y,view);
}
int main(void) {
  const struct { uint32_t frequency; const char *text, *unit; } cases[] = {
    {103900000,"103.900","MHz"}, {410000,"410","kHz"}, {95925000,"95.925","MHz"},
    {438500000,"438.500","MHz"}, {438512500,"438.5125","MHz"}, {145506250,"145.50625","MHz"},
    {7100000,"7.100","MHz"}, {410020,"410.02","kHz"}, {999000,"999","kHz"},
    {1000000,"1.000","MHz"}, {1340000000,"1340.000","MHz"}, {1339999980,"1339.99998","MHz"},
    {1000001,"1.000001","MHz"},
  };
  for (unsigned i=0; i<sizeof(cases)/sizeof(cases[0]); ++i) {
    char text[16];
    assert(!strcmp(UI_FormatFrequency(cases[i].frequency,text),cases[i].unit));
    assert(!strcmp(text,cases[i].text));
  }
  // Large digits: medium glyphs at 2x, one pixel bolder per dot.
  assert(UI_LargeTextWidth("438.506") == 2*UI_MediumTextWidth("438.506"));
  memset(gFrameBuffer,0,sizeof(gFrameBuffer));
  UI_MediumTextWindow(0,10,20,"1",0);
  unsigned thin=0; for (unsigned x=0;x<20;++x) thin+=ink(x,x+1,0,12);
  memset(gFrameBuffer,0,sizeof(gFrameBuffer));
  UI_LargeTextWindow(0,20,40,"1",0);
  unsigned bold=0; for (unsigned x=0;x<40;++x) bold+=ink(x,x+1,0,24);
  assert(bold == 2*thin+1);

  // Frequency alone: three decimals fit, extra decimals stack above the unit.
  UI_RadioCardView view={.id='A',.selected=true,.frequency_hz=438500000,.step_hz=12500,
    .mode=RX_NFM,.band_name="70cm",.activity=UI_RADIO_CARD_RECEIVING,.signal_valid=true,.signal_level=9};
  render(8,&view);
  assert(ink(30,113,17,32) && !ink(113,115,16,36) && !ink(115,128,16,23));
  memcpy(saved,gFrameBuffer,sizeof(saved));
  render(8,&view);
  sameRows(0,64); // Stateless: the same view always draws the same pixels.
  view.frequency_hz=438506250;render(8,&view);
  assert(ink(115,128,16,23) && !ink(113,115,16,24)); // The mode may be wider than the unit.
  view.frequency_hz=1339999980;render(8,&view);
  assert(leftmost(17,32) >= 30 && ink(115,128,16,23)); // 1339.99 + 998
  assert(!ink(0,128,36,64)); // A card never draws outside its 28 rows.
  // Drafts keep their end (the cursor) visible in the tail column.
  view.edit_text="1340.000001_";render(8,&view);
  assert(leftmost(17,32) >= 30 && ink(124,128,16,23));
  view.edit_text=NULL;

  // A name takes the large body; the frequency moves into the header.
  view.frequency_hz=438500000;view.name=NULL;render(8,&view);
  memcpy(saved,gFrameBuffer,sizeof(saved));
  view.name="RPT";render(8,&view);
  bool header=false;
  for (unsigned x=60;x<113;++x) for (unsigned y=8;y<16;++y)
    header |= GetPixel(x,y) != !!(saved[y/8][x]&(1<<(y%8)));
  // The unit corner now holds the mode; nothing sits above it for a short name.
  assert(header && ink(30,113,17,32) && !ink(113,128,16,24) && ink(115,128,24,32));
  unsigned nameLeft=leftmost(17,32);
  view.name="My Channnnnnnnnnnnnel";render(8,&view);
  assert(leftmost(17,32) < nameLeft && leftmost(17,32) >= 30);
  assert(ink(115,128,16,23) && !ink(113,115,16,24)); // The mode may be wider than the unit.
  // Seeking and editing always show the large frequency with its unit.
  view.activity=UI_RADIO_CARD_SEEKING;render(8,&view);
  assert(ink(115,128,24,32));
  view.activity=UI_RADIO_CARD_IDLE;

  // Header: status message over RT over band; a notice also takes the
  // header frequency's place, the mode stays at the right edge.
  view.name="RADIO FM";view.radio_text="Music and news at the top of the hour";
  render(8,&view);memcpy(saved,gFrameBuffer,sizeof(saved));
  view.radio_text=NULL;render(8,&view);
  bool differs=false;
  for (unsigned x=14;x<60;++x) for (unsigned y=8;y<16;++y)
    differs |= GetPixel(x,y) != !!(saved[y/8][x]&(1<<(y%8)));
  assert(differs);
  sameRows(16,36); // RT never touches the body.
  // A RadioText also takes the header frequency's place.
  view.radio_text="Music";render(8,&view);memcpy(saved,gFrameBuffer,sizeof(saved));
  view.frequency_hz=145500000;render(8,&view);sameRows(8,16);
  view.frequency_hz=438500000;
  view.radio_text="RT";view.header_message="No channel memory";
  render(8,&view);memcpy(saved,gFrameBuffer,sizeof(saved));
  view.radio_text=NULL;render(8,&view);
  sameRows(0,64);
  view.frequency_hz=145500000;render(8,&view);
  sameRows(8,16); // A notice hides the header frequency, so it cannot change it.

  // Second row and bounds.
  render(36,&view);
  assert(!ink(0,128,0,36) && ink(0,128,36,64));
  memset(gFrameBuffer,0xaa,sizeof(gFrameBuffer));memcpy(saved,gFrameBuffer,sizeof(saved));
  UI_RadioCardRender(37,&view);
  sameRows(0,64); // Out-of-bounds cards never touch the framebuffer.
  return 0;
}

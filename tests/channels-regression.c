// Settings → Channels: browse, rename and delete against the real store.
#include "../src/ui/screens/channels.c"
#include <assert.h>

#include "fake-storage.h"
uint8_t gFrameBuffer[8][128];
bool gRedrawScreen;
static char *gTextinputText;
static uint8_t gTextInputSize;
static void (*gTextInputCallback)(void);
static unsigned exits, runs, changes;
static char title[32];
bool SCREEN_Pop(void) { ++exits; return true; }
void TEXT_INPUT_Open(char *text, uint8_t max, void (*onAccept)(void), bool replaceTop) {
  assert(!replaceTop); gTextinputText=text; gTextInputSize=max; gTextInputCallback=onAccept; ++runs;
}
void RADIO_ChannelsChanged(void) { ++changes; }
void STATUSLINE_SetText(const char *text) { TEXT_Copy(title,sizeof(title),text); }
static void press(KEY_Code_t k) { assert(key(k,KEY_RELEASED)); }
static void save(uint16_t n, const char *name) {
  UserChannel ch={.frequency_hz=438500000+n*12500u,
    .rx={.mode=RX_NFM,.step_hz=12500,.bandwidth_hz=12000,.squelch=4}};
  strcpy(ch.name,name);
  assert(CHANNEL_STORE_Save(n,&ch));
}

int main(void) {
  memset(fakeArea,0xff,sizeof(fakeArea));
  assert(CHANNEL_STORE_Init());
  enter();render();
  assert(!cursor && channelsScreen.title && !strcmp(channelsScreen.title,"Channels"));
  press(KEY_MENU);press(KEY_STAR);assert(!runs && !confirmDelete); // Nothing to act on.
  // Gaps are skipped; the list scrolls to keep the cursor on screen.
  const uint16_t numbers[]={3,7,8,20,150,200};
  for (unsigned i=0;i<6;++i) save(numbers[i],"CH");
  enter();assert(cursor==3 && top==3);
  for (unsigned i=0;i<4;++i) press(KEY_DOWN);
  assert(cursor==150 && top==3);
  press(KEY_DOWN);assert(cursor==200 && top==7);
  press(KEY_DOWN);assert(cursor==200); // No wrap.
  for (unsigned i=0;i<5;++i) press(KEY_UP);
  assert(cursor==3 && top==3);
  render();
  // Rename: text input starts from the stored name; accepting writes it.
  press(KEY_DOWN);assert(cursor==7);
  press(KEY_MENU);assert(runs==1 && gTextInputSize==USER_CHANNEL_NAME_MAX && !strcmp(gTextinputText,"CH"));
  strcpy(gTextinputText,"Repeater");gTextInputCallback();
  UserChannel loaded;
  assert(CHANNEL_STORE_Load(7,&loaded) && !strcmp(loaded.name,"Repeater") && changes==1);
  assert(loaded.frequency_hz==438500000+7*12500u);
  // Delete asks first; EXIT keeps the channel.
  press(KEY_STAR);assert(confirmDelete);render();
  press(KEY_EXIT);assert(!confirmDelete && CHANNEL_STORE_IsOccupied(7) && !exits);
  press(KEY_STAR);press(KEY_MENU);
  assert(!CHANNEL_STORE_IsOccupied(7) && cursor==8 && changes==2 && !strcmp(message,"Deleted"));
  // Deleting the last channel moves the cursor back.
  press(KEY_DOWN);press(KEY_DOWN);press(KEY_DOWN);assert(cursor==200);
  press(KEY_STAR);press(KEY_MENU);assert(cursor==150);
  render();
  press(KEY_EXIT);assert(exits==1);
  return 0;
}

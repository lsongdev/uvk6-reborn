#include "ui/screens/tone-scan.h"
#include "radio/radio.h"
#include "core/text.h"
#include "ui/graphics.h"
#include "ui/frequency.h"

static void toneScanEnter(void) { RADIO_StartToneScan(); }
static void toneScanLeave(void) { RADIO_StopToneScan(false); }
static void toneScanRender(void) {
  char text[24];
  UI_FrequencyText(RADIO_Selected()->current.frequency_hz, text);
  PrintMedium(2,20,text);
  const RadioTone *tone = RADIO_ToneScanResult();
  if (tone && *tone) TONE_Format(*tone,text);
  else TEXT_Copy(text,sizeof(text),tone ? "Searching..." : "Unavailable");
  PrintMedium(2,34,text);
  const char *notice = RADIO_Notice();
  PrintSmall(2,45,notice ? notice : tone && *tone >= 51 ? "Equivalent DCS code" :
             tone && !*tone ? "Keep signal on" : "");
  PrintSmall(2,58,RADIO_Selected()->source == RADIO_SOURCE_CHANNEL ? "Read only  EXIT:Back" :
             tone && *tone ? "MENU:Use RX  EXIT:Back" : "EXIT:Back");
}
static bool toneScanKey(KEY_Code_t key, Key_State_t state) {
  if (state == KEY_RELEASED) {
    if (key == KEY_EXIT) SCREEN_Pop();
    else if (key == KEY_MENU && RADIO_StopToneScan(true)) SCREEN_Pop();
  }
  return true;
}
const Screen toneScanScreen = {
  .title="RX Tone Scan", .enter=toneScanEnter, .leave=toneScanLeave, .render=toneScanRender, .key=toneScanKey
};

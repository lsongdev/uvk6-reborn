#include "devices/audio.h"
#include "drivers/bk48x9/bk48x9.h"
#include "hal/gpio.h"
#include "hal/system.h"

static bool speakerOn = false;

void AUDIO_ToggleSpeaker(bool on) {
  speakerOn = on;
  GPIO_Write(GPIO_AUDIO_PATH, on);
}


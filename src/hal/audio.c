#include "hal/audio.h"
#include "drivers/dp32g030/gpio.h"
#include "drivers/bk4819/bk4819.h"
#include "hal/gpio.h"
#include "hal/system.h"

static bool speakerOn = false;

void AUDIO_ToggleSpeaker(bool on) {
  speakerOn = on;
  if (on) {
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
  } else {
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
  }
}


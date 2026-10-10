#include "apps.h"
#include "../driver/st7565.h"
#include "../driver/uart.h"
#include "../helper/menu.h"
#include "../ui/graphics.h"
#include "../ui/statusline.h"
#include "about.h"
#include "appslist.h"
#include "bandscan.h"
#include "chcfg.h"
#include "chlist.h"
#include "chscan.h"
#include "fc.h"
#include "finput.h"
#include "lootlist.h"
#include "reset.h"
#include "scaner.h"
#include "settings.h"
#include "textinput.h"
#include "vfo1.h"

#define APPS_STACK_SIZE 8

AppType_t gCurrentApp = APP_NONE;

static AppType_t loadedVfoApp = APP_NONE;

static AppType_t appsStack[APPS_STACK_SIZE] = {APP_NONE};
static int8_t stackIndex = -1;

static bool pushApp(AppType_t app) {
  if (stackIndex < APPS_STACK_SIZE - 1) {
    appsStack[++stackIndex] = app;
  } else {
    for (uint8_t i = 1; i < APPS_STACK_SIZE; ++i) {
      appsStack[i - 1] = appsStack[i];
    }
    appsStack[stackIndex] = app;
  }
  return true;
}

static AppType_t popApp(void) {
  if (stackIndex > 0) {
    return appsStack[stackIndex--]; // Do not care about existing value
  }
  return appsStack[stackIndex];
}

AppType_t APPS_Peek(void) {
  if (stackIndex >= 0) {
    return appsStack[stackIndex];
  }
  return APP_NONE;
}

const AppType_t appsAvailableToRun[RUN_APPS_COUNT] = {APP_VFO1};

// Only the minimal radio, input, settings and explicit reset remain reachable.
const App apps[APPS_COUNT] = {
    [APP_NONE] = {"None", NULL, NULL, NULL, NULL, NULL},
    [APP_FINPUT] = {"Freq input", FINPUT_init, FINPUT_update, FINPUT_render,
                    FINPUT_key, FINPUT_deinit},
    [APP_SETTINGS] = {"Settings", SETTINGS_init, NULL, SETTINGS_render,
                      SETTINGS_key, SETTINGS_deinit},
    [APP_RESET] = {"Reset", RESET_Init, RESET_Update, RESET_Render, RESET_key, NULL},
    [APP_VFO1] = {"Radio", VFO1_init, VFO1_update, VFO1_render, VFO1_key, NULL, true},
};

bool APPS_key(KEY_Code_t Key, Key_State_t state) {
  if (apps[gCurrentApp].key) {
    return apps[gCurrentApp].key(Key, state);
  }
  return false;
}

void APPS_init(AppType_t app) {

  STATUSLINE_SetText("%s", apps[app].name);
  gRedrawScreen = true;

  LogC(LOG_C_YELLOW, "[APP] Init %s", apps[app].name);
  if (apps[app].init) {
    apps[app].init();
  }
}

void APPS_update(void) {
  if (apps[gCurrentApp].update) {
    apps[gCurrentApp].update();
  }
}

void APPS_render(void) {
  if (apps[gCurrentApp].render) {
    UI_ClearScreen();
    apps[gCurrentApp].render();
  }
}

void APPS_deinit(void) {
  LogC(LOG_C_YELLOW, "[APP] Deinit %s", apps[gCurrentApp].name);
  MENU_Deinit();
  if (apps[gCurrentApp].deinit) {
    apps[gCurrentApp].deinit();
  }
}

RadioState radioState;
void APPS_run(AppType_t app) {
  if (app <= APP_NONE || app >= APPS_COUNT || !apps[app].name)
    return;
  if (stackIndex >= 0 && appsStack[stackIndex] == app) {
    return;
  }
  APPS_deinit();
  pushApp(app);
  gCurrentApp = app;

  if (loadedVfoApp != gCurrentApp && apps[gCurrentApp].needsRadioState) {
    LogC(LOG_C_MAGENTA, "[APP] Load radio state for %s",
         apps[gCurrentApp].name);
    gRadioState = &radioState;
    RADIO_InitState(gRadioState, 1);
    RADIO_LoadVFOs(gRadioState);
    gRadioState->multiwatch_enabled = false;
    loadedVfoApp = gCurrentApp;
  }

  APPS_init(app);
}

void APPS_runManual(AppType_t app) { APPS_run(app); }

bool APPS_exit(void) {
  if (stackIndex == 0) {
    return false;
  }
  APPS_deinit();
  AppType_t app = popApp();
  gCurrentApp = APPS_Peek();

  APPS_init(gCurrentApp);

  STATUSLINE_SetText("%s", apps[gCurrentApp].name);
  gRedrawScreen = true;
  // }
  return true;
}

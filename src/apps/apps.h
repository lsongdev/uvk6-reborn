#ifndef APPS_H
#define APPS_H

#include "hal/keyboard.h"
#include "core/receiver.h"

#define RUN_APPS_COUNT 2

typedef enum {
  APP_NONE,
  APP_RADIO,
  APP_SETTINGS,
  APP_BANDS,       // Internal editor launched from Settings.
  APP_CHANNELS,    // Channel list launched from Settings.
  APP_TEXTINPUT,   // Internal channel-name editor.
  APP_RESET,       // Initialization when configuration is absent.
  APPS_COUNT,
} AppType_t;

typedef struct App {
  const char *name;
  void (*init)(void);
  void (*update)(void);
  void (*render)(void);
  bool (*key)(KEY_Code_t Key, Key_State_t state);
  void (*deinit)(void);
  bool needsRadioState;
  // RadioState radioState;
} App;

extern const App apps[APPS_COUNT];
extern const AppType_t appsAvailableToRun[RUN_APPS_COUNT];

extern AppType_t gCurrentApp;

AppType_t APPS_Peek();
bool APPS_key(KEY_Code_t Key, Key_State_t state);
void APPS_init(AppType_t app);
void APPS_update(void);
void APPS_render(void);
void APPS_run(AppType_t app);
bool APPS_exit(void);

#endif /* end of include guard: APPS_H */

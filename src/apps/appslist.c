#include "appslist.h"
#include "../helper/menu.h"
#include "apps.h"

static MenuItem appsItems[RUN_APPS_COUNT];

static const Menu appsMenu = {"Apps", appsItems, RUN_APPS_COUNT};

static void run(const MenuItem *item) { APPS_runManual(item->setting); }

void APPSLIST_render(void) { MENU_Render(); }

void APPSLIST_init(void) {
  for (uint8_t i = 0; i < RUN_APPS_COUNT; ++i) {
    appsItems[i].action = run;
    appsItems[i].setting = i;
  }
}

bool APPSLIST_key(KEY_Code_t key, Key_State_t state) {

  if (state == KEY_RELEASED || state == KEY_LONG_PRESSED_CONT) {
    MENU_HandleInput(key);
  }
  return false;
}

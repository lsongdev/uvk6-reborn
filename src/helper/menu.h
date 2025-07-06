#ifndef MENU_H

#define MENU_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*MenuAction)(void);
typedef void (*MenuOnEnter)(void);

typedef struct MenuItem {
  const char *name;
  const uint8_t setting; // настройка, которую меняем
  void (*get_value_text)(const MenuItem *item, char *buf, uint8_t buf_size);
  void (*change_value)(const MenuItem *item, bool up);
  struct Menu *submenu; // если не NULL — переход в подменю

  /* int64_t (*get_value)(const MenuItem *item);
  void (*set_value)(const MenuItem *item, int64_t); */

  void (*action)(const MenuItem *item);
  const void *user_data; // Данные для action
} MenuItem;

typedef struct Menu {
  const char *title;
  const MenuItem *items;
  uint16_t num_items;
  MenuOnEnter on_enter;
} Menu;

void MENU_Init(Menu *main_menu);
void MENU_Render(void);
bool MENU_HandleInput(uint8_t key);
void MENU_Back(void);

#endif /* end of include guard: MENU_H */

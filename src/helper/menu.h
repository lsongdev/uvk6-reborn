#ifndef MENU_H

#define MENU_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*MenuAction)(void);
typedef void (*MenuOnEnter)(void);

typedef struct MenuItem {
  const char *name;
  void (*action)(void *user_data);
  const void *user_data; // Данные для action
  struct Menu *submenu; // если не NULL — переход в подменю
  void (*get_value_text)(const void *user_data, char *buf, uint8_t buf_size);
  void (*change_value)(const void *user_data, bool up);

  int64_t (*get_value)(const void *user_data);
  void (*set_value)(const void *user_data, int64_t);
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

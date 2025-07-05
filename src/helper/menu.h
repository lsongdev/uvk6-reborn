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
} MenuItem;

typedef struct Menu {
  const char *title;
  const MenuItem *items;
  uint16_t num_items;
  MenuOnEnter on_enter; // вызывается при входе в это меню
} Menu;

// Инициализация меню-системы с главным меню
void MENU_Init(Menu *main_menu);

// Отрисовка текущего активного меню
void MENU_Render(void);

// Обработка нажатия клавиши
bool MENU_HandleInput(uint8_t key);

// Выход из текущего подменю (возврат к предыдущему)
void MENU_Back(void);

#endif /* end of include guard: MENU_H */

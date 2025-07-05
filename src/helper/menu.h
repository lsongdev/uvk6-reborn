#ifndef MENU_H

#define MENU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct MenuItem MenuItem;
typedef struct Menu Menu;

typedef void (*MenuDrawFunc)(MenuItem *item, bool isCurrent);
typedef void (*MenuSelectFunc)(Menu *menu, MenuItem *item);

struct MenuItem {
  const char *name;
  MenuDrawFunc draw; // функция отрисовки конкретного пункта (опционально)
  MenuSelectFunc on_select; // вызывается при выборе пункта
  void *
      user_data; // любые данные (например, ParamType или указатель на параметр)
};

struct Menu {
  MenuItem *items;
  uint16_t item_count;
  uint16_t cursor;
  uint16_t visible_lines;
  void (*on_exit)(Menu *menu); // колбек при выходе из меню
  void *context;               // любые внешние данные
};

#endif /* end of include guard: MENU_H */

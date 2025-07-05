#include "menu.h"
#include "../ui/components.h"
#include "../ui/menu.h"

void MENU_Init(Menu *menu, MenuItem *items, uint16_t item_count,
               uint16_t visible_lines, void *context) {
  menu->items = items;
  menu->item_count = item_count;
  menu->cursor = 0;
  menu->visible_lines = visible_lines;
  menu->context = context;
}

void MENU_Render(Menu *menu) {
  uint16_t max_items = (menu->item_count < menu->visible_lines)
                           ? menu->item_count
                           : menu->visible_lines;
  uint16_t offset = (menu->cursor < 2) ? 0
                    : (menu->cursor > menu->item_count - max_items + 2)
                        ? menu->item_count - max_items
                        : menu->cursor - 2;

  for (uint16_t i = 0; i < max_items; i++) {
    uint16_t item_idx = i + offset;
    bool is_current = (menu->cursor == item_idx);
    MenuItem *item = &menu->items[item_idx];

    if (item->draw) {
      item->draw(item, is_current);
    } else {
      UI_ShowMenuItem(i, item->name, is_current);
    }
  }

  UI_DrawScrollBar(menu->item_count, menu->cursor, menu->visible_lines);
}

void MENU_Next(Menu *menu) {
    if (menu->cursor + 1 < menu->item_count) {
        menu->cursor++;
    }
}

void MENU_Prev(Menu *menu) {
    if (menu->cursor > 0) {
        menu->cursor--;
    }
}

void MENU_Select(Menu *menu) {
    MenuItem *item = &menu->items[menu->cursor];
    if (item->on_select) {
        item->on_select(menu, item);
    }
}


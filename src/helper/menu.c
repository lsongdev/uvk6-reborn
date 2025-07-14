#include "menu.h"
#include "../ui/graphics.h"
#include "../ui/statusline.h"
#include <stdbool.h>

#define MENU_STACK_DEPTH 3
static const uint8_t MENU_Y = 8;
static const uint8_t MENU_ITEM_H = 11;
static const uint8_t MENU_LINES_TO_SHOW = 5;

static Menu *menu_stack[MENU_STACK_DEPTH];
static uint8_t menu_stack_top = 0;

static Menu *active_menu = NULL;
static uint16_t current_index = 0;

static void UI_DrawScrollBar(const uint16_t size, const uint16_t i,
                             const uint8_t nLines) {
  const uint8_t y = ConvertDomain(i, 0, size - 1, MENU_Y, LCD_HEIGHT - 3);

  DrawVLine(LCD_WIDTH - 2, MENU_Y, LCD_HEIGHT - MENU_Y, C_FILL);

  FillRect(LCD_WIDTH - 3, y, 3, 3, C_FILL);
}

static void UI_ShowMenuItem(uint8_t line, const MenuItem *item,
                            bool isCurrent) {}

void MENU_Init(Menu *main_menu) {
  active_menu = main_menu;
  current_index = 0;
  menu_stack_top = 0;
  if (main_menu->on_enter)
    main_menu->on_enter();
}

void MENU_Render(void) {
  if (!active_menu)
    return;

  STATUSLINE_SetText(active_menu->title);
  const uint8_t menuItemH =
      active_menu->itemHeight ? active_menu->itemHeight : MENU_ITEM_H;
  const uint8_t linesToShow = (LCD_HEIGHT - MENU_Y) / menuItemH;

  const uint16_t offset = (current_index >= 2) ? current_index - 2 : 0;
  const uint16_t visible = (active_menu->num_items < linesToShow)
                               ? active_menu->num_items
                               : linesToShow;

  for (uint16_t i = 0; i < visible; ++i) {
    uint16_t idx = i + offset;
    if (idx >= active_menu->num_items)
      break;

    const bool isActive = idx == current_index;

    if (active_menu->render_item) {
      active_menu->render_item(idx, isActive);
    } else {
      const MenuItem *item = &active_menu->items[idx];
      uint8_t by = MENU_Y + i * MENU_ITEM_H + 8;
      PrintMedium(3, by, "%s %c", item->name, item->submenu ? '>' : ' ');
      if (item->get_value_text) {
        char value_buf[32];
        item->get_value_text(item, value_buf, sizeof(value_buf));
        PrintSmallEx(LCD_WIDTH - 7, by, POS_R, C_FILL, "%s", value_buf);
      }
    }

    if (isActive) {
      FillRect(0, MENU_Y + i * menuItemH, LCD_WIDTH - 4, menuItemH, C_INVERT);
    }
  }

  UI_DrawScrollBar(active_menu->num_items, current_index, linesToShow);
}

bool MENU_HandleInput(uint8_t key) {
  if (!active_menu) {
    return false;
  }

  switch (key) {
  case KEY_UP:
    if (current_index > 0) {
      current_index--;
      return true;
    }
    break;
  case KEY_DOWN:
    if (current_index + 1 < active_menu->num_items) {
      current_index++;
      return true;
    }
    break;
  case KEY_MENU: {
    const MenuItem *item = &active_menu->items[current_index];
    if (item->action) {
      item->action(item);
    }
    if (item->submenu) {
      if (menu_stack_top < MENU_STACK_DEPTH) {
        menu_stack[menu_stack_top++] = active_menu;
        active_menu = item->submenu;
        current_index = 0;
        if (active_menu->on_enter) {
          active_menu->on_enter();
        }
      }
    }
    return true;
  }
  case KEY_STAR:
  case KEY_F: {
    const MenuItem *item = &active_menu->items[current_index];
    if (item->change_value) {
      item->change_value(item, key == KEY_STAR);
      return true;
    }
  }
    return true;
  case KEY_EXIT:
    MENU_Back();
    return true;
  }
  return false;
}

void MENU_Back(void) {
  if (menu_stack_top > 0) {
    active_menu = menu_stack[--menu_stack_top];
    current_index = 0;
    if (active_menu->on_enter)
      active_menu->on_enter();
  }
}

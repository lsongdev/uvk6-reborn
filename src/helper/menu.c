#include "menu.h"
#include "../ui/graphics.h"
#include "../ui/statusline.h"
#include <stdbool.h>

#define MENU_STACK_DEPTH 3
static const uint8_t MENU_Y = 8;
static const uint8_t MENU_ITEM_H = 11;
static const uint8_t MENU_LINES_TO_SHOW = 4;

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
                            bool isCurrent) {
  uint8_t by = MENU_Y + line * MENU_ITEM_H + 8;
  PrintMedium(4, by, "%s", item->name);
  if (item->get_value_text) {
    char value_buf[32];
    item->get_value_text(item->user_data, value_buf, sizeof(value_buf));
    PrintSmallEx(LCD_WIDTH - 4, by + 8, POS_R, C_FILL, "%s", value_buf);
  }
  if (isCurrent) {
    FillRect(0, MENU_Y + line * MENU_ITEM_H, LCD_WIDTH - 4, MENU_ITEM_H,
             C_INVERT);
  }
}

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

  const uint16_t max_lines = 4;
  const uint16_t offset = (current_index >= 2) ? current_index - 2 : 0;
  const uint16_t visible =
      (active_menu->num_items < max_lines) ? active_menu->num_items : max_lines;

  for (uint16_t i = 0; i < visible; ++i) {
    uint16_t idx = i + offset;
    if (idx >= active_menu->num_items)
      break;

    UI_ShowMenuItem(i, &active_menu->items[idx], idx == current_index);
  }

  UI_DrawScrollBar(active_menu->num_items, current_index, max_lines);
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
      item->action((void *)item->user_data);
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
      item->change_value(item->user_data, key == KEY_STAR);
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

#ifndef MENU_H
#define MENU_H

#include "../helper/channels.h"
#include <stdbool.h>
#include <stdint.h>

static const uint8_t MENU_Y = 8;
static const uint8_t MENU_ITEM_H = 11;
static const uint8_t MENU_LINES_TO_SHOW = 4;

void UI_ShowMenuItem(uint8_t line, const char *name, bool isCurrent);
void UI_ShowMenu(void (*getItemText)(uint16_t index, char *name), uint16_t size,
                 uint16_t currentIndex);
void UI_ShowMenuEx(void (*showItem)(uint16_t i, uint16_t index, bool isCurrent),
                   uint16_t size, uint16_t currentIndex, uint16_t linesMax);
void UI_DrawScrollBar(const uint16_t size, const uint16_t currentIndex,
                      const uint8_t linesCount);

void PrintRTXCode(char *Output, uint8_t codeType, uint8_t code);

#endif /* end of include guard: MENU_H */

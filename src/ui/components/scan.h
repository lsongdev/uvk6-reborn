#ifndef UI_COMPONENT_SCAN_H
#define UI_COMPONENT_SCAN_H
#include "ui/graphics.h"
#include "core/lootlist.h"

void UI_Scanlists(uint8_t baseX, uint8_t baseY, uint16_t sl);
void UI_DrawLoot(const Loot *loot, uint8_t x, uint8_t y, TextPos pos);
void UI_DisplayScanlists(uint32_t y);
void UI_RenderScanScreen();
#endif

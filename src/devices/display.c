#include "devices/display.h"
#include "drivers/st7565/st7565.h"
_Static_assert(LCD_WIDTH == ST7565_WIDTH && LCD_HEIGHT == ST7565_HEIGHT,
               "Display framebuffer must match the LCD controller");
uint8_t gFrameBuffer[LCD_HEIGHT/8][LCD_WIDTH];
bool gRedrawScreen = true;
void DISPLAY_Init(bool full, uint8_t contrast) {
  ST7565_Init(full, contrast);
}
void DISPLAY_Blit(void) {
  ST7565_Blit(gFrameBuffer);
}

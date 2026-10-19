#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H
#include <stdbool.h>
#include <stdint.h>
#define LCD_WIDTH 128
#define LCD_HEIGHT 64
#define LCD_XCENTER (LCD_WIDTH/2)
#define LCD_YCENTER (LCD_HEIGHT/2)
extern uint8_t gFrameBuffer[LCD_HEIGHT/8][LCD_WIDTH];
extern bool gRedrawScreen;
void DISPLAY_Init(bool full, uint8_t contrast);
void DISPLAY_Blit(void);
#endif

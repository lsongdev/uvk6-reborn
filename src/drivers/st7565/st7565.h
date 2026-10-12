#ifndef DRIVER_ST7565_H
#define DRIVER_ST7565_H
#include <stdbool.h>
#include <stdint.h>
#define ST7565_WIDTH 128
#define ST7565_HEIGHT 64
void ST7565_Init(bool full, uint8_t contrast);
void ST7565_Blit(const uint8_t frame[ST7565_HEIGHT/8][ST7565_WIDTH]);
#endif

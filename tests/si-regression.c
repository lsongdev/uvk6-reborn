#include "../src/driver/si473x.c"
#include <assert.h>
#include <string.h>

static uint8_t lastTune[6];
static int16_t lastBfo;
static unsigned tuneCommands;
static unsigned powerCommands;
static bool patchPresent = true;
static bool ack = true;

void I2C_Start(void) {}
void I2C_Stop(void) {}
int I2C_Write(uint8_t byte) { (void)byte; return ack ? 0 : -1; }
int I2C_WriteBuffer(const uint8_t *buffer, uint16_t size) {
  if (buffer[0] == CMD_FM_TUNE_FREQ || buffer[0] == CMD_AM_TUNE_FREQ) {
    memset(lastTune, 0, sizeof(lastTune));
    memcpy(lastTune, buffer, size);
    ++tuneCommands;
  }
  if (buffer[0] == CMD_SET_PROPERTY && size == 6 &&
      ((uint16_t)buffer[2]<<8 | buffer[3]) == PROP_SSB_BFO)
    lastBfo = (int16_t)((uint16_t)buffer[4]<<8 | buffer[5]);
  if (buffer[0] == CMD_POWER_UP)
    ++powerCommands;
  return 0;
}
uint16_t I2C_ReadBuffer(void *buffer, uint16_t size) {
  memset(buffer, 0, size);
  ((uint8_t *)buffer)[0] = STATUS_CTS;
  return size;
}
uint32_t GetUptimeMs(void) { static uint32_t time; return ++time; }
void TIMER_DelayUs(uint32_t delay) { (void)delay; }
void SYS_DelayMs(uint32_t delay) { (void)delay; }
void GPIO_ClearBit(volatile uint32_t *reg, uint8_t bit) { (void)reg; (void)bit; }
void GPIO_SetBit(volatile uint32_t *reg, uint8_t bit) { (void)reg; (void)bit; }
void Log(const char *pattern, ...) { (void)pattern; }
bool SETTINGS_IsPatchPresent(void) { return patchPresent; }
uint32_t SETTINGS_GetEEPROMSize(void) { return 32768; }
int EEPROM_ReadBuffer(uint32_t address, void *buffer, uint16_t size) {
  (void)address;
  memset(buffer, 0, size);
  return 0;
}
void FillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color) {
  (void)x; (void)y; (void)w; (void)h; (void)color;
}
void PrintMediumBoldEx(uint8_t x, uint8_t y, TextPos pos, Color color, const char *pattern, ...) {
  (void)x; (void)y; (void)pos; (void)color; (void)pattern;
}
void ST7565_Blit(void) {}

int main(void) {
  si4732mode = SI47XX_FM;
  isSi4732On = false;
  SI47XX_SwitchMode(SI47XX_FM);
  assert(isSi4732On && powerCommands == 1);
  SI47XX_SwitchMode(SI47XX_FM);
  assert(powerCommands == 1); // Preparing audio does not power-cycle an unchanged mode.
  SI47XX_TuneTo(9593000);
  assert(siCurrentFreq == 9593 && lastTune[0] == CMD_FM_TUNE_FREQ);
  assert(lastTune[2] == (9593>>8) && lastTune[3] == (9593&255));

  SI47XX_SwitchMode(SI47XX_LSB);
  assert(isSi4732On && si4732mode == SI47XX_LSB);
  SI47XX_TuneTo(710005);
  assert(siCurrentFreq == 7100 && lastTune[0] == CMD_AM_TUNE_FREQ);
  assert(lastTune[1] == 0x40 && lastBfo == -50);
  unsigned count = tuneCommands;
  SI47XX_TuneTo(710010);
  assert(tuneCommands == count && lastBfo == -100);
  SI47XX_SwitchMode(SI47XX_USB);
  SI47XX_TuneTo(710010);
  assert(tuneCommands == count+1 && lastTune[1] == 0x80 && lastBfo == -100);
  SI47XX_TuneTo(720075);
  assert(siCurrentFreq == 7200 && lastBfo == -750);

  SI47XX_PowerDown();
  patchPresent = false;
  SI47XX_SwitchMode(SI47XX_LSB);
  assert(!isSi4732On && si4732mode == SI47XX_LSB);
  ack = false;
  assert(!SI47XX_Detect());
  return 0;
}

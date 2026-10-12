// Exercise the real BK1080 I2C path without accessing hardware.
#include "../src/drivers/bk1080/bk1080.c"
#include <assert.h>
#include <string.h>

static bool ack = true;
static unsigned writes;
void I2C_Start(void) {}
void I2C_Stop(void) {}
int I2C_Write(uint8_t byte) { (void)byte; return ack ? 0 : -1; }
int I2C_WriteBuffer(const uint8_t *data, uint16_t size) {
  (void)data; (void)size;
  ++writes;
  return ack ? 0 : -1;
}
uint16_t I2C_ReadBuffer(void *data, uint16_t size) {
  memset(data, 0, size);
  return size;
}
void SYS_DelayMs(uint32_t ms) { (void)ms; }
void GPIO_ClearBit(volatile uint32_t *reg, uint8_t bit) {
  (void)reg; (void)bit;
}
void GPIO_SetBit(volatile uint32_t *reg, uint8_t bit) {
  (void)reg; (void)bit;
}

int main(void) {
  ack = false;
  assert(!BK1080_SetFrequency(9590000));
  assert(currentF == 0);
  assert(!BK1080_Init(9590000, true));
  assert(!gIsInitBK1080);
  ack = true;
  assert(BK1080_Init(9590000, true));
  assert(gIsInitBK1080 && currentF == 9590000);
  unsigned previous = writes;
  assert(BK1080_SetFrequency(9590000));
  assert(writes == previous);
  ack = false;
  assert(!BK1080_Mute(true) && !BK1080_Mute(false));
  assert(!BK1080_SetFrequency(10010000));
  assert(currentF == 9590000);
  ack = true;
  assert(BK1080_SetFrequency(10010000));
  assert(currentF == 10010000);
  return 0;
}

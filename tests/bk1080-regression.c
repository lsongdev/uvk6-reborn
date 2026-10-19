// Exercise the real BK1080 I2C path without accessing hardware.
#include "../src/drivers/bk1080/bk1080.c"
#include <assert.h>
#include <string.h>

static bool ack = true;
static unsigned writes;
static uint16_t lastWord;
void I2C_Start(void) {}
void I2C_Stop(void) {}
int I2C_Write(uint8_t byte) { (void)byte; return ack ? 0 : -1; }
int I2C_WriteBuffer(const uint8_t *data, uint16_t size) {
  assert(size==2); lastWord=(uint16_t)((uint16_t)data[0]<<8)|data[1];
  ++writes;
  return ack ? 0 : -1;
}
uint16_t I2C_ReadBuffer(void *data, uint16_t size) {
  memset(data, 0, size);
  return size;
}
void SYS_DelayMs(uint32_t ms) { (void)ms; }
void GPIO_Write(GpioLine line, bool high) { assert(line == GPIO_BK1080); (void)high; }

int main(void) {
  ack = false;
  assert(!BK1080_SetFrequency(9590000));
  assert(currentF == 0);
  assert(!BK1080_TuneTo(9590000));
  assert(!gIsInitBK1080);
  ack = true;
  assert(BK1080_TuneTo(9590000));
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
  assert(BK1080_Mute(true) && lastWord==0x4201 && currentF==10010000);
  ack=false;
  assert(!BK1080_Standby() && currentF==10010000); // Do not commit a failed stop.
  ack=true;
  assert(BK1080_Standby() && lastWord==0x0241 && currentF==0);
  assert(BK1080_TuneTo(10010000) && currentF==10010000); // Same station wakes/tunes again.
  return 0;
}

#include "../src/hal/uart-protocol.h"
#include <assert.h>
#include <string.h>

int main(void) {
  uint8_t buf[256] = {0};
  assert(!UART_CommandValid(NULL, 8));
  assert(!UART_CommandValid(buf, 3));
  assert(!UART_CommandValid(buf, 249));

  buf[0] = 0x1b; buf[1] = 0x05; // Read EEPROM
  buf[8] = 128;
  assert(UART_CommandValid(buf, 16));
  assert(!UART_CommandValid(buf, 15));
  assert(!UART_CommandValid(buf, 17));
  buf[8] = 129; assert(!UART_CommandValid(buf, 16));
  buf[8] = 0; assert(!UART_CommandValid(buf, 16));
  buf[0] = 0x1d; buf[8] = 8;
  assert(!UART_CommandValid(buf, 16)); // Raw write never accepted.
  buf[0] = 0x14; assert(UART_CommandValid(buf, 8));
  assert(!UART_CommandValid(buf, 16));
  buf[0] = 0x2f; assert(UART_CommandValid(buf, 8));
  buf[0] = 0xdd; assert(UART_CommandValid(buf, 4));
  assert(!UART_CommandValid(buf, 8));
  buf[0] = 0x70; buf[1] = 0x05; // Resource write: length byte matches the payload.
  buf[8] = 128; assert(UART_CommandValid(buf, 144));
  assert(!UART_CommandValid(buf, 143));
  buf[8] = 1; assert(UART_CommandValid(buf, 17));
  buf[8] = 0; assert(!UART_CommandValid(buf, 16));
  buf[8] = 129; assert(!UART_CommandValid(buf, 145));
  buf[0] = 0xff; assert(!UART_CommandValid(buf, 4));
  return 0;
}

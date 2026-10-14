#include "hal/uart-protocol.h"

static uint16_t read16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

bool UART_CommandValid(const uint8_t *data, uint16_t size) {
  if (!data || size < 4 || size > 248)
    return false;
  switch (read16(data)) {
  case 0x0514: // Version, timestamp.
  case 0x052f:
    return size == 8;
  case 0x051b: // EEPROM read: 16-byte request, at most 128-byte response.
    return size == 16 && data[8] > 0 && data[8] <= 128;
  case 0x05dd: // Reset with no extra request data.
    return size == 4;
  case 0x0570: // Resource write: 16-byte request header, then 1..128 bytes.
    return size > 16 && data[8] <= 128 && size == 16u + data[8];
  default:
    // In particular 0x051d (raw EEPROM write) is not exposed; 0x0570 only
    // reaches the resource tail (EEPROM_WriteResource).
    return false;
  }
}

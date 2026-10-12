#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>

// Validates the command payload (after transport CRC and deobfuscation).
// EEPROM writes are intentionally not accepted by the RX-only protocol.
bool UART_CommandValid(const uint8_t *data, uint16_t size);
#endif

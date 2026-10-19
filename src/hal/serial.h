#ifndef HAL_SERIAL_H
#define HAL_SERIAL_H

#include <stdbool.h>
#include <stdint.h>

// A byte transport for the PC link. Its driver fills `ring` in a loop and
// head() is the index it writes next; send() never blocks indefinitely and
// drops bytes nobody can receive.
typedef struct {
  uint8_t *ring;
  uint16_t ring_bytes;
  uint16_t (*head)(void);
  void (*send)(const void *data, uint32_t size);
  // Runs over the K-plug, whose lines are also the mic, speaker and PTT:
  // keys pause while it talks so the cable cannot key or confuse the radio.
  bool on_audio_jack;
} SerialPort;

// The board's link transports: the UART, plus USB CDC where fitted.
#define BOARD_SERIAL_PORTS_MAX 2u
extern const SerialPort *const BOARD_SERIAL_PORTS[];
extern const uint8_t BOARD_SERIAL_PORT_COUNT;

#endif

// The real PC link over two fake serial ports (UART and USB on the K1):
// commands on either port are found, each reply goes back on its own port,
// and each port keeps its own obfuscation state.
#include "../src/app/link/uart-link.c"
#include "../src/app/link/uart-protocol.c"
#include "fake-storage.h"
#include <assert.h>

Receiver gReceiver;
BK48X9Capabilities BK48X9_GetCapabilities(void) { return (BK48X9Capabilities){4829, true, 0, 5, 3}; }
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) { (void)reg; return 0; }
bool BOARD_MaintenanceWrite(uint32_t a, const uint8_t *d, uint16_t n) { (void)a; (void)d; (void)n; return false; }
bool BOARD_RawRead(uint32_t a, void *d, uint16_t n) { (void)a; memset(d, 0x5A, n); return true; }
uint32_t BOARD_RawSize(void) { return 2097152u; }
void LOOP_TakeStats(LoopStats *s) { memset(s, 0, sizeof(*s)); }
ReceiverCapabilities RECEIVER_Capabilities(void) { return (ReceiverCapabilities){0}; }
static bool txBusy;
static uint16_t changedChannel;
bool RADIO_TxBusy(void) { return txBusy; }
void RADIO_ChannelsChanged(uint16_t retune) { changedChannel = retune; }
void SYS_Reset(void) {}
// CRC16/XMODEM, as the bootloader and tools/resources.py use.
uint16_t CRC_Calculate(const void *data, uint16_t size) {
  const uint8_t *p = data;
  uint16_t crc = 0;
  for (uint16_t i = 0; i < size; ++i) {
    crc ^= (uint16_t)p[i] << 8;
    for (int b = 0; b < 8; ++b) crc = crc & 0x8000 ? (uint16_t)(crc << 1 ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

typedef struct { uint8_t ring[256]; uint16_t head; uint8_t sent[512]; uint16_t sentBytes; } FakePort;
static FakePort fake[2];
static uint16_t head0(void) { return fake[0].head; }
static uint16_t head1(void) { return fake[1].head; }
static void send0(const void *d, uint32_t n) { memcpy(fake[0].sent + fake[0].sentBytes, d, n); fake[0].sentBytes += n; }
static void send1(const void *d, uint32_t n) { memcpy(fake[1].sent + fake[1].sentBytes, d, n); fake[1].sentBytes += n; }
static const SerialPort port0 = {fake[0].ring, sizeof(fake[0].ring), head0, send0, true};
static const SerialPort port1 = {fake[1].ring, sizeof(fake[1].ring), head1, send1, false};
const SerialPort *const BOARD_SERIAL_PORTS[] = {&port0, &port1};
const uint8_t BOARD_SERIAL_PORT_COUNT = 2;

// Appends one framed command to a port's ring, wrapping like the hardware.
static void frame(unsigned p, const uint8_t *payload, uint16_t size, bool obfuscate) {
  uint8_t body[80];
  memcpy(body, payload, size);
  uint16_t crc = CRC_Calculate(payload, size);
  body[size] = crc & 0xFF; body[size + 1] = crc >> 8;
  if (obfuscate) for (uint16_t i = 0; i < size + 2u; ++i) body[i] ^= Obfuscation[i % 16];
  uint8_t out[88] = {0xAB, 0xCD, (uint8_t)size, (uint8_t)(size >> 8)};
  memcpy(out + 4, body, size + 2u);
  out[size + 6] = 0xDC; out[size + 7] = 0xBA;
  for (uint16_t i = 0; i < size + 8u; ++i) {
    fake[p].ring[fake[p].head] = out[i];
    fake[p].head = (uint16_t)((fake[p].head + 1u) % sizeof(fake[p].ring));
  }
}
static void hello(unsigned p, uint32_t stamp, bool obfuscate) {
  uint8_t cmd[8] = {0x14, 0x05, 4, 0, (uint8_t)stamp, (uint8_t)(stamp >> 8), (uint8_t)(stamp >> 16), (uint8_t)(stamp >> 24)};
  frame(p, cmd, sizeof(cmd), obfuscate);
}
static unsigned handleAll(void) {
  unsigned n = 0;
  while (LINK_IsCommandAvailable()) { LINK_HandleCommand(); ++n; }
  return n;
}
// The reply's first payload byte pair, deobfuscated when the port is.
static uint16_t replyId(unsigned p, bool obfuscated) {
  const uint8_t *s = fake[p].sent;
  assert(fake[p].sentBytes >= 8 && s[0] == 0xAB && s[1] == 0xCD);
  uint8_t lo = s[4], hi = s[5];
  if (obfuscated) { lo ^= Obfuscation[0]; hi ^= Obfuscation[1]; }
  return (uint16_t)(lo | hi << 8);
}

int main(void) {
  // A plain hello on the UART answers on the UART only.
  hello(0, 0x11111111u, false);
  assert(LINK_IsCommandAvailable() && LINK_CommandPort() == &port0 && LINK_CommandPort()->on_audio_jack);
  LINK_HandleCommand();
  assert(!LINK_IsCommandAvailable());
  assert(fake[1].sentBytes == 0 && replyId(0, false) == 0x0515);
  // An obfuscated hello on USB switches only USB to obfuscation.
  fake[0].sentBytes = 0;
  hello(1, 0x22222222u, true);
  assert(LINK_IsCommandAvailable() && LINK_CommandPort() == &port1 && !LINK_CommandPort()->on_audio_jack);
  LINK_HandleCommand();
  assert(!LINK_IsCommandAvailable());
  assert(fake[0].sentBytes == 0 && replyId(1, true) == 0x0515);
  // Both ports at once, the second wrapping its ring: one reply each.
  fake[0].sentBytes = fake[1].sentBytes = 0;
  fake[1].head = 250;
  ports[1].read = 250;
  hello(0, 0x33333333u, false);
  hello(1, 0x44444444u, true);
  assert(handleAll() == 2);
  assert(replyId(0, false) == 0x0515 && replyId(1, true) == 0x0515);
  // One session timestamp serves both ports (the last hello's): a raw read
  // with it answers on each port in that port's own obfuscation; a stale
  // timestamp is ignored.
  fake[0].sentBytes = fake[1].sentBytes = 0;
  uint8_t read[16] = {0x1B, 0x05, 12, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0x44, 0x44, 0x44, 0x44};
  frame(1, read, sizeof(read), true);
  frame(0, read, sizeof(read), false);
  assert(handleAll() == 2);
  assert(replyId(1, true) == 0x051C && replyId(0, false) == 0x051C);
  fake[0].sentBytes = 0;
  read[12] = 0x33;
  frame(0, read, sizeof(read), false);
  assert(handleAll() == 1 && fake[0].sentBytes == 0);
  // Garbage before a frame and a corrupt CRC are skipped per port.
  fake[1].sentBytes = 0;
  fake[0].ring[fake[0].head] = 0x00; fake[0].head = (uint16_t)((fake[0].head + 1u) % 256u);
  uint8_t bad[8] = {0x14, 0x05, 4, 0, 1, 2, 3, 4};
  frame(0, bad, sizeof(bad), false);
  fake[0].ring[(fake[0].head + 256u - 4u) % 256u] ^= 0xFF; // Inside the CRC.
  assert(handleAll() == 0 && !fake[0].sentBytes && !fake[1].sentBytes);

  // Channels over the link, in the normalized record format.
  memset(fakeArea, 0xFF, sizeof(fakeArea));
  assert(CHANNEL_STORE_Init());
  hello(0, 0x55555555u, false);
  assert(handleAll() == 1);
  UserChannel ch = {.name = "Repeater", .frequency_hz = 438725000,
    .rx = {.mode = RX_NFM, .step_hz = 12500, .bandwidth_hz = 12000, .squelch = 4, .rx_tone = 9},
    .tx = {.enabled = true, .power = TX_POWER_HIGH, .shift = TX_SHIFT_MINUS,
           .offset_hz = 5000000, .tone = 155}};
  uint8_t record[CHANNEL_RECORD_BYTES];
  CHANNEL_STORE_Encode(&ch, record);
  // 88.5 Hz CTCSS and DCS 023 inverted, as type and value.
  assert(record[28] == 0x75 && record[29] == 0x13 && record[39] == 0x13 && record[40] == 0x30);
  assert(!memcmp(record, "Repeater\0\0\0\0\0\0\0\0", 16));
  uint8_t cmd[12 + CHANNEL_RECORD_BYTES] = {0x82, 0x05, 12 + CHANNEL_RECORD_BYTES - 4, 0, 5, 0, 1, 0, 0x55, 0x55, 0x55, 0x55};
  memcpy(cmd + 12, record, sizeof(record));
  const uint8_t *reply = fake[0].sent + 4; // Plain reply payload.
  #define SEND(n) (fake[0].sentBytes = 0, frame(0, cmd, n, false), assert(handleAll() == 1))
  // Read 5: empty. Write it, read it back byte for byte.
  cmd[0] = 0x80; SEND(12);
  assert(reply[0] == 0x81 && reply[4] == 5 && reply[6] == 0 && fake[0].sentBytes == 8 + 8);
  cmd[0] = 0x82; SEND(sizeof(cmd));
  assert(reply[0] == 0x83 && reply[6] == 0 && changedChannel == 5 && CHANNEL_STORE_IsOccupied(5));
  cmd[0] = 0x80; SEND(12);
  assert(reply[6] == 1 && !memcmp(reply + 8, record, sizeof(record)));
  // Refused: no such channel, an invalid record, an unknown tone, TX busy.
  changedChannel = 0;
  cmd[0] = 0x82; cmd[4] = 201; SEND(sizeof(cmd)); assert(reply[6] == 2);
  cmd[4] = 0; SEND(sizeof(cmd)); assert(reply[6] == 2);
  cmd[4] = 6; cmd[12 + 16] = cmd[12 + 17] = cmd[12 + 18] = cmd[12 + 19] = 0; // RX 0 Hz.
  SEND(sizeof(cmd)); assert(reply[6] == 1);
  memcpy(cmd + 12, record, sizeof(record)); cmd[12 + 28] = 0x76; // 88.6 Hz.
  SEND(sizeof(cmd)); assert(reply[6] == 1 && !CHANNEL_STORE_IsOccupied(6) && !changedChannel);
  memcpy(cmd + 12, record, sizeof(record));
  txBusy = true; SEND(sizeof(cmd)); assert(reply[6] == 4 && !CHANNEL_STORE_IsOccupied(6));
  txBusy = false;
  // Delete 5, and deleting an empty channel is done too.
  cmd[4] = 5; cmd[6] = 0; SEND(sizeof(cmd));
  assert(reply[6] == 0 && !CHANNEL_STORE_IsOccupied(5) && changedChannel == 5);
  SEND(sizeof(cmd)); assert(reply[6] == 0);
  // A write with a short record is not a command.
  fake[0].sentBytes = 0; frame(0, cmd, sizeof(cmd) - 1, false); assert(handleAll() == 0);
  return 0;
}

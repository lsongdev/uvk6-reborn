// The real PC link over two fake serial ports (UART and USB on the K1):
// commands on either port are found, each reply goes back on its own port,
// and each port keeps its own obfuscation state.
#include "../src/app/link/uart-link.c"
#include "../src/app/link/uart-protocol.c"
#include <assert.h>

Receiver gReceiver;
BK48X9Capabilities BK48X9_GetCapabilities(void) { return (BK48X9Capabilities){4829, true, 0, 5, 3}; }
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) { (void)reg; return 0; }
bool BOARD_MaintenanceWrite(uint32_t a, const uint8_t *d, uint16_t n) { (void)a; (void)d; (void)n; return false; }
bool BOARD_RawRead(uint32_t a, void *d, uint16_t n) { (void)a; memset(d, 0x5A, n); return true; }
uint32_t BOARD_RawSize(void) { return 2097152u; }
void LOOP_TakeStats(LoopStats *s) { memset(s, 0, sizeof(*s)); }
ReceiverCapabilities RECEIVER_Capabilities(void) { return (ReceiverCapabilities){0}; }
const char *STORAGE_Name(void) { return "test"; }
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
  uint8_t body[64];
  memcpy(body, payload, size);
  uint16_t crc = CRC_Calculate(payload, size);
  body[size] = crc & 0xFF; body[size + 1] = crc >> 8;
  if (obfuscate) for (uint16_t i = 0; i < size + 2u; ++i) body[i] ^= Obfuscation[i % 16];
  uint8_t out[72] = {0xAB, 0xCD, (uint8_t)size, (uint8_t)(size >> 8)};
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
  return 0;
}

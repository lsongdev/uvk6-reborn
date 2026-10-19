#include "drivers/py32f071/usb.h"
#include "py32f0xx.h"
#include <stddef.h>
#include <string.h>

// Byte-wide registers of the PY32F071 USB device, a Mentor MUSB-style core,
// as Puya's own CherryUSB port addresses them (the vendor header packs them
// into words). INDEX selects which endpoint the CSR/MAX_PKT/COUNT bytes act
// on. The three interrupt status bytes clear when read.
typedef struct {
  volatile uint8_t ADDR, POWER, reserved0[2];
  volatile uint8_t INT_USB, INT_OUT, INT_IN, reserved1;
  volatile uint8_t INT_USBE, INT_OUTE, INT_INE, reserved2;
  volatile uint16_t FRAME;
  volatile uint8_t INDEX, reserved3;
  volatile uint8_t EP0_CSR, EP0_COUNT, reserved4[2];
  volatile uint8_t IN_CSR2, IN_CSR1, MAX_PKT_IN, reserved5;
  volatile uint8_t OUT_CSR2, OUT_CSR1, MAX_PKT_OUT, reserved6;
  volatile uint16_t OUT_COUNT;
  volatile uint8_t reserved7[2];
  volatile uint32_t FIFO[8]; // Bytes go through the low byte of each word.
} UsbRegisters;
_Static_assert(offsetof(UsbRegisters, EP0_CSR) == 0x10 && offsetof(UsbRegisters, FIFO) == 0x20,
               "USB register layout");
#define USB_REGS ((UsbRegisters *)USBD_BASE)

#define INT_SUSPEND 0x01u
#define INT_RESUME 0x02u
#define INT_RESET 0x04u
#define POWER_SUSPEND_ENABLE 0x01u
#define EP0_OUT_READY 0x01u
#define EP0_IN_READY 0x02u
#define EP0_SENT_STALL 0x04u
#define EP0_DATA_END 0x08u
#define EP0_SETUP_END 0x10u
#define EP0_SEND_STALL 0x20u
#define EP0_SERVICED_OUT 0x40u
#define EP0_SERVICED_SETUP_END 0x80u
#define IN_READY 0x01u
#define IN_FIFO_NOT_EMPTY 0x02u
#define IN_FLUSH 0x08u
#define IN_CLEAR_TOGGLE 0x40u
#define IN_MODE 0x20u // IN_CSR2: the endpoint transmits.
#define OUT_READY 0x01u
#define OUT_FLUSH 0x10u
#define OUT_CLEAR_TOGGLE 0x80u

#define EP0_SIZE 64u
#define BULK_SIZE 64u
#define DATA_IN 1u   // 0x81, bulk.
#define DATA_OUT 2u  // 0x02, bulk.
#define NOTIFY_IN 3u // 0x83, interrupt; never sends.

// pid.codes test VID/PID: a private build, not posing as the stock "K1 CPS".
#define VENDOR_ID 0x1209u
#define PRODUCT_ID 0x0001u

static const uint8_t deviceDescriptor[] = {
  18, 1, 0x00, 0x02, 0xEF, 0x02, 0x01, EP0_SIZE, // USB 2.0, IAD class triple.
  VENDOR_ID & 0xFF, VENDOR_ID >> 8, PRODUCT_ID & 0xFF, PRODUCT_ID >> 8,
  0x00, 0x01, 1, 2, 3, 1,
};
static const uint8_t configDescriptor[] = {
  9, 2, 75, 0, 2, 1, 0, 0x80, 50,         // Two interfaces, bus powered, 100 mA.
  8, 11, 0, 2, 2, 2, 0, 0,                // Interface association: CDC ACM.
  9, 4, 0, 0, 1, 2, 2, 0, 0,              // Communication interface.
  5, 0x24, 0x00, 0x10, 0x01,              // CDC header 1.10.
  5, 0x24, 0x01, 0x00, 1,                 // Call management: data on interface 1.
  4, 0x24, 0x02, 0x02,                    // ACM: line coding and control lines.
  5, 0x24, 0x06, 0, 1,                    // Union: 0 controls 1.
  7, 5, 0x80 | NOTIFY_IN, 3, 8, 0, 16,    // Notification endpoint.
  9, 4, 1, 0, 2, 10, 0, 0, 0,             // Data interface.
  7, 5, DATA_OUT, 2, BULK_SIZE, 0, 0,
  7, 5, 0x80 | DATA_IN, 2, BULK_SIZE, 0, 0,
};
_Static_assert(sizeof(configDescriptor) == 75, "wTotalLength");

#define RX_RING_BYTES 256u
#define TX_RING_BYTES 256u // Both powers of two: indexes wrap by mask.
_Static_assert(!(RX_RING_BYTES & (RX_RING_BYTES - 1u)) && !(TX_RING_BYTES & (TX_RING_BYTES - 1u)),
               "Ring sizes are powers of two");
static uint8_t rxRing[RX_RING_BYTES];
static volatile uint16_t rxHead;
static uint8_t txRing[TX_RING_BYTES];
static volatile uint16_t txHead, txTail;
static volatile bool configured, hostOpen, txBusy, txLastFull;

typedef enum { EP0_IDLE, EP0_TX, EP0_RX, EP0_STATUS } Ep0State;
static Ep0State ep0;
static const uint8_t *ep0Data;
static uint16_t ep0Left;
static bool ep0Zlp;
static uint8_t pendingAddress;
static uint8_t lineCoding[7] = {0x80, 0x25, 0, 0, 0, 0, 8}; // 9600 8N1, unused.
static uint8_t reply[34]; // Strings and small answers.

static volatile uint8_t *fifo(uint8_t endpoint) {
  return (volatile uint8_t *)&USB_REGS->FIFO[endpoint];
}
static void writeFifo(uint8_t endpoint, const uint8_t *data, uint16_t size) {
  volatile uint8_t *port = fifo(endpoint);
  for (uint16_t i = 0; i < size; ++i) *port = data[i];
}

// ASCII to a USB string descriptor in `reply`.
static uint16_t stringDescriptor(const char *text) {
  uint16_t n = 0;
  while (text[n] && 2u + 2u * (n + 1u) <= sizeof(reply)) {
    reply[2 + 2 * n] = (uint8_t)text[n];
    reply[3 + 2 * n] = 0;
    ++n;
  }
  reply[0] = (uint8_t)(2u + 2u * n);
  reply[1] = 3;
  return reply[0];
}
// The serial number is the MCU's unique ID, so each radio keeps its own
// /dev/cu.usbmodem name.
static uint16_t serialDescriptor(void) {
  char text[9];
  uint32_t id = *(const volatile uint32_t *)UID_BASE ^ *(const volatile uint32_t *)(UID_BASE + 4u);
  for (unsigned i = 0; i < 8; ++i) text[i] = "0123456789ABCDEF"[(id >> (28u - 4u * i)) & 15u];
  text[8] = 0;
  return stringDescriptor(text);
}

static void openEndpoints(void) {
  USB_REGS->INDEX = DATA_IN;
  USB_REGS->IN_CSR2 = IN_MODE;
  USB_REGS->IN_CSR1 = IN_FLUSH | IN_CLEAR_TOGGLE;
  USB_REGS->MAX_PKT_IN = BULK_SIZE / 8u;
  USB_REGS->INDEX = NOTIFY_IN;
  USB_REGS->IN_CSR2 = IN_MODE;
  USB_REGS->IN_CSR1 = IN_FLUSH | IN_CLEAR_TOGGLE;
  USB_REGS->MAX_PKT_IN = 1; // 8 bytes.
  USB_REGS->INDEX = DATA_OUT;
  USB_REGS->OUT_CSR2 = 0;
  USB_REGS->OUT_CSR1 = OUT_FLUSH | OUT_CLEAR_TOGGLE;
  USB_REGS->MAX_PKT_OUT = BULK_SIZE / 8u;
  USB_REGS->INDEX = 0;
  USB_REGS->INT_INE = 1u | (1u << DATA_IN);
  USB_REGS->INT_OUTE = 1u << DATA_OUT;
  txHead = txTail = 0;
  txBusy = txLastFull = false;
  configured = true;
}

// Loads the next control IN packet; the last one carries DATA_END.
static void ep0Send(void) {
  uint16_t n = ep0Left < EP0_SIZE ? ep0Left : EP0_SIZE;
  writeFifo(0, ep0Data, n);
  ep0Data += n;
  ep0Left -= n;
  bool last = !ep0Left && !(n == EP0_SIZE && ep0Zlp);
  if (!n) last = true;
  USB_REGS->EP0_CSR = EP0_IN_READY | (last ? EP0_DATA_END : 0);
  ep0 = last ? EP0_STATUS : EP0_TX;
}
static void ep0Reply(const uint8_t *data, uint16_t size, uint16_t requested) {
  ep0Data = data;
  ep0Left = size < requested ? size : requested;
  ep0Zlp = ep0Left < requested && ep0Left % EP0_SIZE == 0;
  USB_REGS->EP0_CSR = EP0_SERVICED_OUT;
  ep0Send();
}
static void ep0Ack(void) {
  USB_REGS->EP0_CSR = EP0_SERVICED_OUT | EP0_DATA_END;
  ep0 = EP0_STATUS;
}
static void ep0Stall(void) {
  USB_REGS->EP0_CSR = EP0_SERVICED_OUT | EP0_SEND_STALL;
  ep0 = EP0_IDLE;
}

static void setup(const uint8_t s[8]) {
  uint8_t type = s[0], request = s[1];
  uint16_t value = s[2] | s[3] << 8, length = s[6] | s[7] << 8;
  if ((type & 0x60) == 0x20) { // CDC class, to the communication interface.
    switch (request) {
    case 0x20: // SET_LINE_CODING: accepted, the link ignores the rate.
      if (length != sizeof(lineCoding)) { ep0Stall(); return; }
      USB_REGS->EP0_CSR = EP0_SERVICED_OUT;
      ep0 = EP0_RX;
      return;
    case 0x21: ep0Reply(lineCoding, sizeof(lineCoding), length); return;
    case 0x22: hostOpen = value & 1u; ep0Ack(); return; // DTR.
    case 0x23: ep0Ack(); return; // SEND_BREAK.
    default: ep0Stall(); return;
    }
  }
  if (type & 0x60) { ep0Stall(); return; }
  switch (request) {
  case 0: // GET_STATUS: bus powered, no remote wakeup, nothing halted.
    reply[0] = reply[1] = 0;
    ep0Reply(reply, 2, length);
    return;
  case 1: case 3: ep0Ack(); return; // CLEAR/SET_FEATURE.
  case 5: pendingAddress = value & 0x7Fu; ep0Ack(); return; // Applied after status.
  case 6: // GET_DESCRIPTOR
    switch (value >> 8) {
    case 1: ep0Reply(deviceDescriptor, sizeof(deviceDescriptor), length); return;
    case 2: ep0Reply(configDescriptor, sizeof(configDescriptor), length); return;
    case 3: {
      static const uint8_t languages[] = {4, 3, 0x09, 0x04};
      uint8_t index = value & 0xFF;
      if (!index) { ep0Reply(languages, sizeof(languages), length); return; }
      uint16_t n = index == 1 ? stringDescriptor("uvk6-reborn")
                 : index == 2 ? stringDescriptor("UV-K1 Reborn")
                 : index == 3 ? serialDescriptor() : 0;
      if (n) ep0Reply(reply, n, length); else ep0Stall();
      return;
    }
    default: ep0Stall(); return; // No qualifier: full speed only.
    }
  case 8: reply[0] = configured; ep0Reply(reply, 1, length); return; // GET_CONFIGURATION
  case 9: // SET_CONFIGURATION
    if (value > 1) { ep0Stall(); return; }
    if (value) openEndpoints(); else configured = hostOpen = false;
    ep0Ack();
    return;
  case 10: reply[0] = 0; ep0Reply(reply, 1, length); return; // GET_INTERFACE
  case 11: ep0Ack(); return; // SET_INTERFACE: only alternate setting 0.
  default: ep0Stall(); return;
  }
}

static void serviceEp0(void) {
  USB_REGS->INDEX = 0;
  uint8_t csr = USB_REGS->EP0_CSR;
  if (csr & EP0_SENT_STALL) {
    USB_REGS->EP0_CSR = csr & ~EP0_SENT_STALL;
    ep0 = EP0_IDLE;
  }
  if (csr & EP0_SETUP_END) { // Host aborted the transfer.
    USB_REGS->EP0_CSR = EP0_SERVICED_SETUP_END;
    ep0 = EP0_IDLE;
  }
  switch (ep0) {
  case EP0_TX: ep0Send(); return;
  case EP0_RX:
    if (!(csr & EP0_OUT_READY)) return;
    {
      uint8_t n = USB_REGS->EP0_COUNT;
      volatile uint8_t *port = fifo(0);
      for (uint8_t i = 0; i < n; ++i) {
        uint8_t byte = *port;
        if (i < sizeof(lineCoding)) lineCoding[i] = byte;
      }
    }
    ep0Ack();
    return;
  case EP0_STATUS:
    ep0 = EP0_IDLE;
    if (pendingAddress) {
      USB_REGS->ADDR = pendingAddress;
      pendingAddress = 0;
    }
    break; // A new SETUP may already wait.
  case EP0_IDLE: break;
  }
  if (!(USB_REGS->EP0_CSR & EP0_OUT_READY)) return;
  if (USB_REGS->EP0_COUNT != 8) { USB_REGS->EP0_CSR = EP0_SERVICED_OUT; return; }
  uint8_t s[8];
  volatile uint8_t *port = fifo(0);
  for (unsigned i = 0; i < sizeof(s); ++i) s[i] = *port;
  setup(s);
}

// Sends the next packet from the TX ring, or the zero-length packet that
// ends a transfer whose last packet was full.
static void kick(void) {
  if (!configured || txBusy) return;
  uint16_t queued = (uint16_t)((unsigned)txHead - txTail) & (TX_RING_BYTES - 1u);
  if (!queued && !txLastFull) return;
  uint16_t n = queued < BULK_SIZE ? queued : BULK_SIZE;
  uint8_t index = USB_REGS->INDEX;
  USB_REGS->INDEX = DATA_IN;
  volatile uint8_t *port = fifo(DATA_IN);
  for (uint16_t i = 0; i < n; ++i) {
    *port = txRing[txTail];
    txTail = (uint16_t)((txTail + 1u) & (TX_RING_BYTES - 1u));
  }
  USB_REGS->IN_CSR1 = IN_READY;
  USB_REGS->INDEX = index;
  txLastFull = n == BULK_SIZE;
  txBusy = true;
}

static void receive(void) {
  USB_REGS->INDEX = DATA_OUT;
  if (!(USB_REGS->OUT_CSR1 & OUT_READY)) return;
  uint16_t n = USB_REGS->OUT_COUNT;
  volatile uint8_t *port = fifo(DATA_OUT);
  uint16_t head = rxHead;
  for (uint16_t i = 0; i < n; ++i) { // Overwrites unread bytes, like the UART ring.
    rxRing[head] = *port;
    head = (uint16_t)((head + 1u) & (RX_RING_BYTES - 1u));
  }
  rxHead = head;
  USB_REGS->OUT_CSR1 &= (uint8_t)~OUT_READY;
}

void USB_IRQHandler(void) {
  uint8_t bus = USB_REGS->INT_USB, in = USB_REGS->INT_IN, out = USB_REGS->INT_OUT;
  if (bus & INT_RESET) {
    configured = hostOpen = txBusy = txLastFull = false;
    ep0 = EP0_IDLE;
    pendingAddress = 0;
    USB_REGS->ADDR = 0;
    USB_REGS->INDEX = 0;
    USB_REGS->INT_INE = 1u;
    USB_REGS->INT_OUTE = 0;
    USB_REGS->POWER |= POWER_SUSPEND_ENABLE;
  }
  if (in & 1u) serviceEp0();
  if (in & (1u << DATA_IN)) {
    txBusy = false;
    kick();
  }
  if (out & (1u << DATA_OUT)) receive();
  USB_REGS->INDEX = 0;
}

static uint16_t usbHead(void) { return rxHead; }

static void usbSend(const void *data, uint32_t size) {
  const uint8_t *bytes = data;
  for (uint32_t i = 0; i < size; ++i) {
    if (!configured || !hostOpen) return; // Nobody listening: drop.
    uint16_t next = (uint16_t)((txHead + 1u) & (TX_RING_BYTES - 1u));
    // A host that stopped reading must not freeze the main loop: drop the
    // stale output instead. Not a mute: tools keep DTR up across sessions,
    // so the next request must still get its reply.
    for (uint32_t spins = 200000u; next == txTail; ) {
      __disable_irq(); kick(); __enable_irq();
      if (!--spins) {
        __disable_irq(); txTail = txHead; __enable_irq();
        return;
      }
    }
    txRing[txHead] = bytes[i];
    txHead = next;
  }
  __disable_irq(); kick(); __enable_irq();
}

const SerialPort USB_PORT = {rxRing, RX_RING_BYTES, usbHead, usbSend, false};

void USB_Init(void) {
  RCC->APBENR1 |= RCC_APBENR1_USBEN;
  RCC->APBRSTR1 |= RCC_APBRSTR1_USBDRST;
  RCC->APBRSTR1 &= ~RCC_APBRSTR1_USBDRST;
  USB_REGS->INDEX = 0;
  USB_REGS->ADDR = 0;
  USB_REGS->INT_USBE = INT_RESET | INT_SUSPEND | INT_RESUME;
  USB_REGS->INT_INE = 1u;
  USB_REGS->INT_OUTE = 0;
  NVIC_SetPriority(USB_IRQn, 3);
  NVIC_EnableIRQ(USB_IRQn);
}

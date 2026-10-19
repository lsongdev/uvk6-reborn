#include "hal/uart.h"
#include "app/loop.h"
#include "drivers/bk48x9/bk48x9.h"
#include "radio/receiver.h"
#include "app/link/uart-link.h"
#include "hal/board.h"
#include "hal/storage.h"
#include "hal/crc.h"
#include "hal/system.h"
#include "app/link/uart-protocol.h"
#include <stdbool.h>
#include <string.h>

// The PC link protocol (tools/resources.py): framing, obfuscation and the
// command set, over whatever transport the board's UART_* provides.
static const char Version[] = "s0v4";

static bool bIsInLockScreen = false;

#define RING_INDEX(x, y) (((x) + (y)) % sizeof(gUartRxRing))

typedef struct {
  uint16_t ID;
  uint16_t Size;
} Header_t;

typedef struct {
  uint8_t Padding[2];
  uint16_t ID;
} Footer_t;

typedef struct {
  Header_t Header;
  uint32_t Timestamp;
} CMD_0514_t;

typedef struct {
  Header_t Header;
  struct {
    char Version[16];
    bool bHasCustomAesKey;
    bool bIsInLockScreen;
    uint8_t Padding[2];
    uint32_t Challenge[4];
  } Data;
} REPLY_0514_t;

typedef struct {
  Header_t Header;
  uint32_t Offset;
  uint8_t Size;
  uint8_t Padding[3];
  uint32_t Timestamp;
} CMD_051B_t;

typedef struct {
  Header_t Header;
  struct {
    uint32_t Offset;
    uint8_t Size;
    uint8_t Padding[3];
    uint8_t Data[128];
  } Data;
} REPLY_051B_t;

typedef struct {
  Header_t Header;
  uint32_t Offset;
  uint8_t Size;
  uint8_t Padding[2];
  bool bAllowPassword;
  uint32_t Timestamp;
  uint8_t Data[128];
} CMD_051D_t;

typedef struct {
  Header_t Header;
  struct {
    uint32_t Offset;
    uint8_t Padding[2];
  } Data;
} REPLY_051D_t;

// Maintenance write (tools/resources.py): the firmware accepts only the
// resource tail and the calibration block, and replies after reading back.
typedef struct {
  Header_t Header;
  uint32_t Offset;
  uint8_t Size;
  uint8_t Padding[3];
  uint32_t Timestamp;
  uint8_t Data[128];
} CMD_0570_t;

typedef struct {
  Header_t Header;
  struct {
    uint32_t Offset;
    uint8_t Ok;
    uint8_t Padding[3];
  } Data;
} REPLY_0570_t;

typedef struct {
  Header_t Header;
  struct {
    uint16_t Voltage;
    uint16_t Current;
  } Data;
} REPLY_0529_t;

typedef struct {
  Header_t Header;
  uint32_t Timestamp;
} CMD_052F_t;

typedef struct {
  Header_t Header;
  uint8_t RegNum;
} CMD_0601_t;

typedef struct {
  Header_t Header;
  struct {
    uint16_t Val;
    uint8_t v1;
    uint8_t v2;
  } Data;
} REPLY_0601_t;

typedef struct {
  Header_t Header;
  uint8_t RegNum;
  uint16_t RegValue;
} CMD_0602_t;

typedef struct {
  Header_t Header;
  struct {
    uint16_t Val;
    uint8_t v1;
    uint8_t v2;
  } Data;
} REPLY_0602_t;

static const uint8_t Obfuscation[16] = {0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91,
                                        0x0D, 0x40, 0x21, 0x35, 0xD5, 0x40,
                                        0x13, 0x03, 0xE9, 0x80};

static union {
  uint8_t Buffer[256];
  struct {
    Header_t Header;
    uint8_t Data[252];
  };
} UART_Command;

static uint32_t Timestamp;
static uint16_t gUART_WriteIndex;
static bool bIsEncrypted = true;

static Header_t Header;
static Footer_t Footer;
static uint8_t *pBytes;
static void SendReply(void *pReply, uint16_t Size) {
  uint16_t i;

  if (bIsEncrypted) {
    pBytes = (uint8_t *)pReply;
    for (i = 0; i < Size; i++) {
      pBytes[i] ^= Obfuscation[i % 16];
    }
  }

  Header.ID = 0xCDAB;
  Header.Size = Size;
  UART_Send(&Header, sizeof(Header));
  UART_Send(pReply, Size);
  if (bIsEncrypted) {
    Footer.Padding[0] = Obfuscation[(Size + 0) % 16] ^ 0xFF;
    Footer.Padding[1] = Obfuscation[(Size + 1) % 16] ^ 0xFF;
  } else {
    Footer.Padding[0] = 0xFF;
    Footer.Padding[1] = 0xFF;
  }
  Footer.ID = 0xBADC;

  UART_Send(&Footer, sizeof(Footer));
}

static void SendVersion(void) {
  REPLY_0514_t Reply = {0};

  Reply.Header.ID = 0x0515;
  Reply.Header.Size = sizeof(Reply.Data);
  strcpy(Reply.Data.Version, Version);
  Reply.Data.bHasCustomAesKey = false;
  Reply.Data.bIsInLockScreen = bIsInLockScreen;

  SendReply(&Reply, sizeof(Reply));
}

static void CMD_0514(const uint8_t *pBuffer) {
  const CMD_0514_t *pCmd = (const CMD_0514_t *)pBuffer;

  Timestamp = pCmd->Timestamp;
  SendVersion();
}

static void CMD_051B(const uint8_t *pBuffer) {
  const CMD_051B_t *pCmd = (const CMD_051B_t *)pBuffer;
  REPLY_051B_t Reply;

  if (pCmd->Timestamp != Timestamp) {
    return;
  }

  memset(&Reply, 0, sizeof(Reply));
  Reply.Header.ID = 0x051C;
  // Range checked here, not only in the transport parser. Never expose
  // uninitialized reply bytes or exceed the 128-byte reply storage.
  if (!pCmd->Size || pCmd->Size > sizeof(Reply.Data.Data) ||
      !BOARD_RawRead(pCmd->Offset, Reply.Data.Data, pCmd->Size))
    return;
  Reply.Header.Size = pCmd->Size + 8 + 4;
  Reply.Data.Offset = pCmd->Offset;
  Reply.Data.Size = pCmd->Size;
  SendReply(&Reply, pCmd->Size + 8 + 4);
}

// Read-only physical storage information; name includes JEDEC ID on SPI boards.
static void CMD_0572(const uint8_t *buffer) {
  const CMD_0514_t *cmd = (const CMD_0514_t *)buffer;
  if (cmd->Timestamp != Timestamp) return;
  struct {
    Header_t Header;
    uint32_t Capacity;
    char Name[24];
  } reply = {{0x0573, 28}, BOARD_RawSize(), {0}};
  strncpy(reply.Name, STORAGE_Name(), sizeof(reply.Name) - 1);
  SendReply(&reply, sizeof(reply));
}

// Diagnostics: main-loop health, the receive plan and raw BK4819/BK4829
// status registers (RSSI 0x67, noise 0x65, glitch 0x63, interrupt 0x0C).
static void CMD_0574(const uint8_t *buffer) {
  const CMD_0514_t *cmd = (const CMD_0514_t *)buffer;
  if (cmd->Timestamp != Timestamp) return;
  struct {
    Header_t Header;
    LoopStats Loop;
    uint32_t Frequency;
    uint8_t Device, Open, Broadcast, Si;
    uint16_t Rssi, Noise, Glitch, Status;
  } reply = {{0x0575, 0}, {0}, gReceiver.plan.frequency_hz,
      (uint8_t)gReceiver.plan.device, gReceiver.open, RECEIVER_Capabilities().broadcast,
      RECEIVER_Capabilities().si, BK48X9_ReadRegister(BK48X9_REG_67) & 0x1FFu,
      BK48X9_ReadRegister(BK48X9_REG_65) & 0x7Fu, BK48X9_ReadRegister(BK48X9_REG_63) & 0xFFu,
      BK48X9_ReadRegister(BK48X9_REG_0C)};
  _Static_assert(sizeof(reply) == 4 + 12 + 4 + 4 + 8, "0x0575 layout is naturally aligned");
  LOOP_TakeStats(&reply.Loop);
  reply.Header.Size = sizeof(reply) - sizeof(reply.Header);
  SendReply(&reply, sizeof(reply));
}

static void CMD_0576(const uint8_t *buffer) {
  const CMD_0514_t *cmd = (const CMD_0514_t *)buffer;
  if (cmd->Timestamp != Timestamp) return;
  // Exclude REG_02 and scan-result/FIFO ports: a diagnostic must not consume
  // receive events or data. REG_30 is the bus driver's requested-state cache.
  static const uint8_t regs[] = {
    0x0C, 0x10, 0x11, 0x12, 0x13, 0x14, 0x19, 0x1F, 0x2A, 0x2B,
    0x2F, 0x30, 0x31, 0x33, 0x36, 0x37, 0x38, 0x39, 0x3B, 0x3C,
    0x3D, 0x3F, 0x40, 0x42, 0x43, 0x47, 0x48, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x54, 0x55, 0x67, 0x70, 0x73, 0x77, 0x78,
    0x7B, 0x7D, 0x7E,
  };
  struct {
    Header_t Header;
    uint16_t Chip, Count;
    struct { uint16_t Reg, Value; } Values[sizeof(regs)];
  } reply = {{0x0577, 0}, BK48X9_GetCapabilities().chip_id, sizeof(regs), {{0}}};
  for (unsigned i = 0; i < sizeof(regs); ++i) {
    reply.Values[i].Reg = regs[i];
    reply.Values[i].Value = BK48X9_ReadRegister(regs[i]);
  }
  reply.Header.Size = sizeof(reply) - sizeof(reply.Header);
  SendReply(&reply, sizeof(reply));
}

static void CMD_0570(const uint8_t *pBuffer) {
  const CMD_0570_t *pCmd = (const CMD_0570_t *)pBuffer;
  REPLY_0570_t Reply = {0};
  if (pCmd->Timestamp != Timestamp) return;
  Reply.Header.ID = 0x0571;
  Reply.Header.Size = sizeof(Reply.Data);
  Reply.Data.Offset = pCmd->Offset;
  Reply.Data.Ok = BOARD_MaintenanceWrite(pCmd->Offset, pCmd->Data, pCmd->Size);
  SendReply(&Reply, sizeof(Reply));
}

static void CMD_052F(const uint8_t *pBuffer) {
  const CMD_052F_t *pCmd = (const CMD_052F_t *)pBuffer;

  Timestamp = pCmd->Timestamp;

  SendVersion();
}

bool UART_IsCommandAvailable(void) {
  uint16_t DmaLength;
  uint16_t CommandLength;
  uint16_t Index;
  uint16_t TailIndex;
  uint16_t Size;
  uint16_t CRC;
  uint16_t i;

  DmaLength = UART_RxHead();
  while (1) {
    if (gUART_WriteIndex == DmaLength) {
      return false;
    }

    while (gUART_WriteIndex != DmaLength &&
           gUartRxRing[gUART_WriteIndex] != 0xABU) {
      gUART_WriteIndex = RING_INDEX(gUART_WriteIndex, 1);
    }

    if (gUART_WriteIndex == DmaLength) {
      return false;
    }

    if (gUART_WriteIndex < DmaLength) {
      CommandLength = DmaLength - gUART_WriteIndex;
    } else {
      CommandLength = (DmaLength + sizeof(gUartRxRing)) - gUART_WriteIndex;
    }
    if (CommandLength < 8u) {
      return 0;
    }
    if (gUartRxRing[RING_INDEX(gUART_WriteIndex, 1)] == 0xCD) {
      break;
    }
    gUART_WriteIndex = RING_INDEX(gUART_WriteIndex, 1);
  }

  Index = RING_INDEX(gUART_WriteIndex, 2);
  Size = (gUartRxRing[RING_INDEX(Index, 1)] << 8) | gUartRxRing[Index];
  if ((uint32_t)Size + 8u > sizeof(gUartRxRing)) {
    gUART_WriteIndex = DmaLength;
    return false;
  }
  if (CommandLength < Size + 8) {
    return false;
  }
  Index = RING_INDEX(Index, 2);
  TailIndex = RING_INDEX(Index, Size + 2);
  if (gUartRxRing[TailIndex] != 0xDC ||
      gUartRxRing[RING_INDEX(TailIndex, 1)] != 0xBA) {
    gUART_WriteIndex = DmaLength;
    return false;
  }
  if (TailIndex < Index) {
    uint16_t ChunkSize = sizeof(gUartRxRing) - Index;

    memcpy(UART_Command.Buffer, gUartRxRing + Index, ChunkSize);
    memcpy(UART_Command.Buffer + ChunkSize, gUartRxRing, TailIndex);
  } else {
    memcpy(UART_Command.Buffer, gUartRxRing + Index, TailIndex - Index);
  }

  TailIndex = RING_INDEX(TailIndex, 2);
  if (TailIndex < gUART_WriteIndex) {
    memset(gUartRxRing + gUART_WriteIndex, 0,
           sizeof(gUartRxRing) - gUART_WriteIndex);
    memset(gUartRxRing, 0, TailIndex);
  } else {
    memset(gUartRxRing + gUART_WriteIndex, 0, TailIndex - gUART_WriteIndex);
  }

  gUART_WriteIndex = TailIndex;

  // Candidate framing must not alter connection state until CRC validation.
  bool encrypted = bIsEncrypted;
  if (UART_Command.Header.ID == 0x0514) encrypted = false;
  if (UART_Command.Header.ID == 0x6902) encrypted = true;
  if (encrypted) {
    for (i = 0; i < Size + 2; i++) {
      UART_Command.Buffer[i] ^= Obfuscation[i % 16];
    }
  }

  CRC = UART_Command.Buffer[Size] | (UART_Command.Buffer[Size + 1] << 8);
  if (CRC_Calculate(UART_Command.Buffer, Size) != CRC ||
      !UART_CommandValid(UART_Command.Buffer, Size))
    return false;

  bIsEncrypted = encrypted;
  return true;
}

void UART_HandleCommand(void) {
  switch (UART_Command.Header.ID) {
    // VERSION
  case 0x0514:
    CMD_0514(UART_Command.Buffer);
    break;

    // EEPROM READ
  case 0x051B:
    CMD_051B(UART_Command.Buffer);
    break;

    // VERSION, BL OFF
  case 0x052F:
    CMD_052F(UART_Command.Buffer);
    break;

    // RESOURCE WRITE
  case 0x0572:
    CMD_0572(UART_Command.Buffer);
    break;

  case 0x0574:
    CMD_0574(UART_Command.Buffer);
    break;

  case 0x0576:
    CMD_0576(UART_Command.Buffer);
    break;

  case 0x0570:
    CMD_0570(UART_Command.Buffer);
    break;

    // RESET
  case 0x05DD:
    SYS_Reset();
    break;
  }
}


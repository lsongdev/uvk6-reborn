#include "../src/drivers/si473x/si473x.c"
#include <assert.h>
#include <string.h>

static uint8_t lastTune[6];
static int16_t lastBfo;
static unsigned tuneCommands;
static unsigned powerCommands;
static bool patchPresent = true;
static bool ack = true;
static uint8_t rdsResponse[13] = {STATUS_CTS};
static uint8_t seekResponse[8] = {STATUS_CTS};
static bool seekRead;
static uint8_t lastSeek[6];
static uint8_t lastSeekLen;
static bool shortRead, busy;
static unsigned rdsCommands;
static uint8_t rdsArgument;

void I2C_Start(void) {}
void I2C_Stop(void) {}
int I2C_Write(uint8_t byte) { (void)byte; return ack ? 0 : -1; }
int I2C_WriteBuffer(const uint8_t *buffer, uint16_t size) {
  if (buffer[0] == CMD_FM_SEEK_START || buffer[0] == CMD_AM_SEEK_START) {
    lastSeekLen = size;
    memcpy(lastSeek, buffer, size);
  }
  if (buffer[0] == CMD_FM_RDS_STATUS) {
    ++rdsCommands;
    rdsArgument = buffer[1];
  }
  if (buffer[0] == CMD_SET_PROPERTY && size == 6) {
    uint16_t property = (uint16_t)buffer[2]<<8 | buffer[3];
    if (property == PROP_FM_RDS_INT_FIFO_COUNT) assert(buffer[5] == 1);
    if (property == PROP_FM_RDS_CONFIG) assert(buffer[4] == 0x55 && buffer[5] == 1);
  }
  if (buffer[0] == CMD_FM_TUNE_FREQ || buffer[0] == CMD_AM_TUNE_FREQ) {
    memset(lastTune, 0, sizeof(lastTune));
    memcpy(lastTune, buffer, size);
    ++tuneCommands;
  }
  if (buffer[0] == CMD_SET_PROPERTY && size == 6 &&
      ((uint16_t)buffer[2]<<8 | buffer[3]) == PROP_SSB_BFO)
    lastBfo = (int16_t)((uint16_t)buffer[4]<<8 | buffer[5]);
  if (buffer[0] == CMD_POWER_UP)
    ++powerCommands;
  return 0;
}
uint16_t I2C_ReadBuffer(void *buffer, uint16_t size) {
  if (seekRead && size == sizeof(seekResponse)) {
    memcpy(buffer, seekResponse, size);
    return shortRead ? size-1 : size;
  }
  if (size == sizeof(rdsResponse)) {
    memcpy(buffer,rdsResponse,size);
    return shortRead ? size-1 : size;
  }
  memset(buffer, 0, size);
  ((uint8_t *)buffer)[0] = busy ? 0 : STATUS_CTS;
  return size;
}
uint32_t GetUptimeMs(void) { static uint32_t time; return ++time; }
void TIMER_DelayUs(uint32_t delay) { (void)delay; }
void SYS_DelayMs(uint32_t delay) { (void)delay; }
void GPIO_Write(GpioLine line, bool high) { assert(line == GPIO_BK1080); (void)high; }
bool SETTINGS_IsPatchPresent(void) { return patchPresent; }
uint32_t SETTINGS_GetEEPROMSize(void) { return 32768; }
int EEPROM_ReadBuffer(uint32_t address, void *buffer, uint16_t size) {
  (void)address;
  memset(buffer, 0, size);
  return 0;
}

int main(void) {
  si4732mode = SI47XX_FM;
  isSi4732On = false;
  SI47XX_SwitchMode(SI47XX_FM);
  assert(isSi4732On && powerCommands == 1);
  SI47XX_SwitchMode(SI47XX_FM);
  assert(powerCommands == 1); // Preparing audio does not power-cycle an unchanged mode.
  SI47XX_TuneTo(9593000);
  assert(siCurrentFreq == 9593 && lastTune[0] == CMD_FM_TUNE_FREQ);
  assert(lastTune[2] == (9593>>8) && lastTune[3] == (9593&255));

  // Verify bounded native forward FM search and CTS/STCINT handling.
  assert(SI47XX_SeekStart(64000000, 108000000, 100000));
  assert(siCurrentFreq == 0 && lastSeekLen == 2 &&
         lastSeek[0] == CMD_FM_SEEK_START &&
         lastSeek[1] == SEEK_START_ARG1_SEEK_UP);
  seekRead = true;
  uint32_t foundFrequency = 0;
  seekResponse[0] = STATUS_CTS;
  assert(SI47XX_SeekPoll(&foundFrequency) == SI_SEEK_BUSY);
  seekResponse[0] = STATUS_CTS | STATUS_STCINT;
  seekResponse[1] = FIELD_TUNE_STATUS_RESP1_VALID;
  seekResponse[2] = (9620u >> 8); seekResponse[3] = (9620u & 255u);
  assert(SI47XX_SeekPoll(&foundFrequency) == SI_SEEK_FOUND &&
         foundFrequency == 96200000u);
  seekResponse[1] = FIELD_TUNE_STATUS_RESP1_SEEK_LIMIT;
  assert(SI47XX_SeekPoll(&foundFrequency) == SI_SEEK_LIMIT);
  ack = false;
  assert(SI47XX_SeekPoll(&foundFrequency) == SI_SEEK_ERROR);
  ack = true;
  SI47XX_SeekCancel();
  assert(siCurrentFreq == 0);
  seekRead = false;
  // A search never implicitly enables a disallowed FM channel spacing.
  assert(!SI47XX_SeekStart(64000000,108000000,12500));

  // Real SI response decoding, including a single remaining FIFO group.
  const uint8_t response[] = {STATUS_CTS,1,5,1,0x40,0xa7,0x20,0x03,0x52,0x41,0x54,0x45,0x1b};
  memcpy(rdsResponse,response,sizeof(response));
  SI47XX_RdsStatus rdsStatus;
  assert(SI47XX_ReadRDS(&rdsStatus,false));
  assert(rdsArgument == 1 && rdsStatus.fifo_used == 1);
  assert(rdsStatus.synchronized && rdsStatus.overflow);
  assert(rdsStatus.blocks[0]==0x40a7 && rdsStatus.blocks[1]==0x2003 &&
         rdsStatus.blocks[2]==0x5241 && rdsStatus.blocks[3]==0x5445);
  for (unsigned i=0;i<4;++i) assert(rdsStatus.errors[i]==i);
  assert(SI47XX_ReadRDS(&rdsStatus,true) && rdsArgument==3 && !rdsStatus.fifo_used);
  rdsResponse[3]=0;
  assert(SI47XX_ReadRDS(&rdsStatus,false) && !rdsStatus.fifo_used);
  SI47XX_RdsStatus saved=rdsStatus;
  rdsResponse[0] |= STATUS_ERR;
  assert(!SI47XX_ReadRDS(&rdsStatus,false) && !memcmp(&saved,&rdsStatus,sizeof(saved)));
  rdsResponse[0]=STATUS_CTS;
  shortRead=true;assert(!SI47XX_ReadRDS(&rdsStatus,false));shortRead=false;
  ack=false;assert(!SI47XX_ReadRDS(&rdsStatus,false));ack=true;
  busy=true;unsigned previous=rdsCommands;
  assert(!SI47XX_ReadRDS(&rdsStatus,false) && rdsCommands==previous);busy=false;
  assert(!memcmp(&saved,&rdsStatus,sizeof(saved)));

  SI47XX_SwitchMode(SI47XX_AM);
  assert(SI47XX_SeekStart(1800000,30000000,5000) &&
         lastSeek[0] == CMD_AM_SEEK_START && lastSeekLen == 6);
  SI47XX_SeekCancel();
  SI47XX_SwitchMode(SI47XX_LSB);
  assert(isSi4732On && si4732mode == SI47XX_LSB);
  assert(!SI47XX_ReadRDS(&rdsStatus,false));
  SI47XX_TuneTo(710005);
  assert(siCurrentFreq == 7100 && lastTune[0] == CMD_AM_TUNE_FREQ);
  assert(lastTune[1] == 0x40 && lastBfo == -50);
  unsigned count = tuneCommands;
  SI47XX_TuneTo(710010);
  assert(tuneCommands == count && lastBfo == -100);
  SI47XX_SwitchMode(SI47XX_USB);
  SI47XX_TuneTo(710010);
  assert(tuneCommands == count+1 && lastTune[1] == 0x80 && lastBfo == -100);
  SI47XX_TuneTo(720075);
  assert(siCurrentFreq == 7200 && lastBfo == -750);

  // A NACK during fine tuning must propagate and not commit a new carrier.
  // A failed measurement must clear cached RSSI and SNR.
  memset(&rsqStatus, 0xff, sizeof(rsqStatus));
  ack = false;
  assert(!RSQ_GET());
  assert(rsqStatus.resp.RSSI == 0 && rsqStatus.resp.SNR == 0);
  unsigned beforeFail = tuneCommands;
  uint16_t previousCarrier = siCurrentFreq;
  assert(!SI47XX_TuneTo(730050));
  assert(siCurrentFreq == previousCarrier && tuneCommands == beforeFail);
  assert(!SI47XX_SetProperty(PROP_RX_VOLUME, 63));
  ack = true;

  SI47XX_PowerDown();
  patchPresent = false;
  SI47XX_SwitchMode(SI47XX_LSB);
  assert(!isSi4732On && si4732mode == SI47XX_LSB);
  ack = false;
  assert(!SI47XX_Detect());
  SI47XX_PowerDown();
  assert(!SI47XX_SwitchMode(SI47XX_FM));
  assert(!isSi4732On);
  return 0;
}

// Actual K6 calibration reader and BK4819 driver, with read-only storage
// and a register bus model. Crystal trim must survive RX/TX transitions.
#include "hal/board.h"
#include "hal/storage.h"
#include "store/calibration.h"
#include "drivers/bk48x9/bk48x9.h"
#include <assert.h>
#include <string.h>
static uint8_t misc[8] = {3,0,0x46,0,0x50,0,44,14};
static bool readable = true;
static uint16_t regs[128];
void BK48X9_BusInit(void) {}
void SYS_DelayMs(uint32_t ms) { (void)ms; }
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) { return reg==0x0C ? 0 : regs[reg]; }
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) { regs[reg]=value; }
bool STORAGE_Read(StorageArea area, uint32_t offset, void *out, uint16_t size) {
  assert(area==STORAGE_CALIBRATION && offset==CALIBRATION_RX_MISC_OFFSET && size==8);
  if (!readable) return false;
  memcpy(out,misc,size); return true;
}
static void apply(int16_t expected) {
  BoardRxCalibration calibration;
  assert(BOARD_RxCalibration(&calibration));
  assert(calibration.trim==expected && calibration.volume==58 && calibration.dac==8);
  BK48X9_SetRxCalibration(calibration.trim,calibration.volume,calibration.dac);
  assert(regs[0x3B]==(uint16_t)(22656+expected) && regs[0x48]==0xB3A8);
}
int main(void) {
  assert(!BOARD_RxCalibration(NULL));
  BK48X9_Init(16,130);
  apply(3); assert(regs[0x3B]==0x5883);
  BK48X9_RX_TurnOn(); BK48X9_SetRxMode(false);
  BK48X9_TuneTo(14550000u,true);
  BK48X9_ConfigureVoiceTx(43875000u,16,1300,24000000u);
  BK48X9_SetCTCSSFrequency(670); BK48X9_PrepareTransmit();
  assert(regs[0x3B]==0x5883);
  BK48X9_Idle(); BK48X9_ExitSubAu(); BK48X9_InvalidateRxMode();
  BK48X9_RX_TurnOn(); BK48X9_SetRxMode(false); BK48X9_TuneTo(14550000u,true);
  assert(regs[0x3B]==0x5883);
  misc[0]=0xFE; misc[1]=0xFF; apply(-2);
  misc[0]=0xFF; apply(-1); // -1 is valid when the rest of the record is populated.
  memset(misc,0xFF,sizeof(misc)); apply(0);
  readable=false; apply(0); readable=true;
  misc[0]=0xE9; misc[1]=3; apply(0); // +1001 is invalid.
  misc[0]=0x17; misc[1]=0xFC; apply(0); // -1001 is invalid.
  misc[0]=0xE8; misc[1]=3; apply(1000);
  misc[0]=0x18; misc[1]=0xFC; apply(-1000);
  memset(misc,0,sizeof(misc)); apply(0);
  return 0;
}

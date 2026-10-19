#ifndef BK48X9_INTERNAL_H
#define BK48X9_INTERNAL_H
#include "drivers/bk48x9/bk48x9.h"
typedef struct {
  BK48X9Capabilities capabilities;
  uint16_t squelch_interrupts,vco_restart;
  uint8_t bus_delay_us,lna_attenuation[4];
} BK48X9Chip;
extern const BK48X9Chip BK48X9_CHIP;
void BK48X9_BusInit(void);
void BK48X9_ResetGpioOutputs(void);
bool BK48X9_BeginInit(void);
void BK48X9_EndInit(void);
void BK48X9_InitDtmfCoefficients(void);
bool BK48X9_WaitForCrystal(void);
bool BK48X9_ConfigureDemodulator(bool am);
void BK48X9_ConfigureTxAudio(uint8_t mic,uint16_t deviation);
void BK48X9_SquelchEventsReset(void);
bool BK48X9_SquelchEventOpen(void);
void BK48X9_XtalSet(XtalMode mode); // Used by the 4819 backend only.
#endif

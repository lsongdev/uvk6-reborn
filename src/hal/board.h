/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

// Clocks, pins, ADC, CRC, UART, I2C and timers; storage starts separately.
void BOARD_Init(void);
void BOARD_ADC_GetBatteryInfo(uint16_t *pVoltage, uint16_t *pCurrent);

typedef enum { BOARD_RADIO_BK4819, BOARD_RADIO_BK4829 } BoardRadioChip;
BoardRadioChip BOARD_RadioChip(void);

// Whether an SI4732 may be fitted; false on boards with only BK1080.
// This is a wiring capability, not a detection result.
bool BOARD_HasSi4732(void);

// The BK1080's antenna path runs through the BK4819/BK4829 VHF LNA switch
// (UV-K1): broadcast reception needs that switch on VHF, so the BK chip
// cannot be retuned in the background while the broadcast chip plays.
bool BOARD_BroadcastUsesBkVhfPath(void);

// Key matrix outputs after a scan, bit n = GPIO_KEY_OUTn; some boards share
// these lines with other parts.
extern const uint8_t BOARD_KEYPAD_IDLE_LEVELS;

// Backlight output; level is the user brightness (0 = off, higher brighter).
void BOARD_BacklightInit(void);
void BOARD_BacklightSetLevel(uint8_t level);

// Maintenance over the PC link (tools/resources.py), in the storage medium's
// own addresses. Writes reach only the board's maintenance ranges and are
// read back.
uint32_t BOARD_RawSize(void);
bool BOARD_RawRead(uint32_t address, void *data, uint16_t size);
bool BOARD_MaintenanceWrite(uint32_t address, const uint8_t *data, uint16_t size);

#endif

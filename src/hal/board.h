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

// Factory PA record and its three frequency anchors, in Hz. The record
// offset is relative to STORAGE_CALIBRATION, never a physical flash address.
typedef struct {
  uint16_t offset;
  uint32_t lower_hz, middle_hz, upper_hz;
} BoardTxCalibration;
bool BOARD_TxCalibration(uint32_t frequency_hz, BoardTxCalibration *record);

typedef struct { int16_t trim; uint8_t volume,dac; } BoardRxCalibration;
// Whether this board supplies a factory RX profile; unreadable fields use
// that board's defaults. No calibration storage is ever written here.
bool BOARD_RxCalibration(BoardRxCalibration *out);
typedef struct {
  uint8_t rssi_open,rssi_close,noise_open,noise_close,glitch_open,glitch_close;
} BoardSquelchCalibration;
bool BOARD_SquelchCalibration(uint32_t hz,uint8_t level,BoardSquelchCalibration *out);
uint8_t BOARD_RxCalibrationBand(uint32_t hz);

// The RF chip GPIOs driving the two status colors differ between boards.
void BOARD_SetGreenLed(bool on);
void BOARD_SetRedLed(bool on);

// Physical arrow arrangement; menu navigation keeps its logical UP/DOWN.
bool BOARD_HasHorizontalArrows(void);

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

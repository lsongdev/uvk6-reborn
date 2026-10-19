#include "store/device-config.h"
#include "devices/battery.h"
#include "hal/board.h"
#include "hal/storage.h"
#include "store/calibration.h"
#include "core/misc.h"
#include "store/preferences.h"

uint16_t gBatteryVoltage = 0;
uint16_t gBatteryCurrent = 0;
uint8_t gBatteryPercent = 0;
bool gChargingWithTypeC = true;

static uint16_t batAdcV = 0;
static uint16_t batAvgV = 0;

const char *BATTERY_TYPE_NAMES[3] = {"1600mAh", "2200mAh", "3500mAh"};
const char *BATTERY_STYLE_NAMES[3] = {"Icon", "%", "V"};

const uint16_t Voltage2PercentageTable[][11][2] = {
    [BAT_1600] =
        {
            {840, 100},
            {780, 90},
            {760, 80},
            {740, 70},
            {720, 60},
            {710, 50},
            {700, 40},
            {690, 30},
            {680, 20},
            {672, 10},
            {600, 0},
        },

    [BAT_2200] =
        {
            {840, 100},
            {800, 90},
            {784, 80},
            {768, 70},
            {756, 60},
            {748, 50},
            {742, 40},
            {738, 30},
            {732, 20},
            {720, 10},
            {600, 0},
        },
    [BAT_3500] =
        {
            /* {840, 100},
            {762, 90},
            {744, 80},
            {726, 70},
            {710, 60},
            {690, 50},
            {674, 40},
            {660, 30},
            {648, 20},
            {628, 10},
            {600, 0}, */
            {840, 100},
            {820, 90},
            {780, 80},
            {770, 70},
            {760, 60},
            {750, 50},
            {740, 40},
            {730, 30},
            {720, 20},
            {700, 10},
            {600, 0},
        },
};

uint8_t BATTERY_VoltsToPercent(const unsigned int voltage_10mV) {
  const uint16_t(*crv)[2] = Voltage2PercentageTable[gDeviceConfig.preferences.battery_type];

  // Предполагаем, что таблица отсортирована от ВЫСОКОГО напряжения к НИЗКОМУ.
  // Пример: {420, 100}, {410, 90}, ... {300, 0}
  // Проверка: if (voltage_10mV > crv[i][0]) — значит мы нашли диапазон.

  for (uint8_t i = 1;
       i < ARRAY_SIZE(Voltage2PercentageTable[gDeviceConfig.preferences.battery_type]); i++) {
    if (voltage_10mV > crv[i][0]) {
      // Точки интерполяции:
      // P1 (v1, p1) = crv[i-1] (Выше напряжение, Выше процент)
      // P2 (v2, p2) = crv[i]   (Ниже напряжение, Ниже процент)

      uint32_t v1 = crv[i - 1][0];
      uint32_t p1 = crv[i - 1][1];
      uint32_t v2 = crv[i][0];
      uint32_t p2 = crv[i][1];

      // Нам нужно линейно интерполировать между P1 и P2.
      // Формула прямой по двум точкам:
      // p = p2 + (voltage - v2) * (p1 - p2) / (v1 - v2)

      // Все разности тут положительные, так как v1 > v2 и p1 > p2 (обычно).
      // Проверим, чтобы не переполнилось умножение.
      // (voltage - v2) ~ 100 (1 вольт в единицах 10мВ)
      // (p1 - p2) ~ 10-20 (процентов)
      // 100 * 20 = 2000. Влезает в uint32_t со свистом.
      // Упрощенная и точная формула без float:
      uint32_t percents = p2 + ((voltage_10mV - v2) * (p1 - p2)) / (v1 - v2);

      return (percents > 100) ? 100 : (uint8_t)percents;
    }
  }
  return 0;
}

bool BATTERY_UseFactoryCalibration(void) {
  if (gDeviceConfigReady) return false;
  uint8_t data[CALIBRATION_BATTERY_POINTS * 2u];
  if (!STORAGE_Read(STORAGE_CALIBRATION, CALIBRATION_BATTERY_OFFSET, data, sizeof(data)))
    return false;
  uint16_t previous = 0, reference = 0;
  for (unsigned i = 0; i < CALIBRATION_BATTERY_POINTS; ++i) {
    uint16_t value = (uint16_t)data[i * 2u] | (uint16_t)((uint16_t)data[i * 2u + 1u] << 8);
    if (value <= previous || value > 4095u) return false;
    if (i == CALIBRATION_BATTERY_REFERENCE) reference = value;
    previous = value;
  }
  gDeviceConfig.preferences.battery_calibration = reference;
  return true;
}

void BATTERY_UpdateBatteryInfo() {
  BOARD_ADC_GetBatteryInfo(&batAdcV, &gBatteryCurrent);
  bool charg = gBatteryCurrent >= 501;
  if (batAvgV == 0 || charg != gChargingWithTypeC) {
    batAvgV = batAdcV;
  } else {
    batAvgV = batAvgV >= batAdcV ? batAvgV - (batAvgV - batAdcV) / 7u
                                 : batAvgV + (batAdcV - batAvgV) / 7u;
  }

  gBatteryVoltage = (uint32_t)batAvgV * 760u / gDeviceConfig.preferences.battery_calibration;
  gChargingWithTypeC = charg;
  gBatteryPercent = BATTERY_VoltsToPercent(gBatteryVoltage);
}

uint32_t BATTERY_GetPreciseVoltage(uint16_t cal) {
  return (uint32_t)batAvgV * 76000u / cal;
}

uint16_t BATTERY_GetCal(uint32_t v) {
  return (uint32_t)gDeviceConfig.preferences.battery_calibration *
         BATTERY_GetPreciseVoltage(gDeviceConfig.preferences.battery_calibration) / v;
}

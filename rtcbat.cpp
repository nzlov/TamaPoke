#include "rtcbat.h"
#include "pin_config.h"  // define XPOWERS_CHIP_AXP2101
#include <Wire.h>
#include <time.h>
#include <SensorPCF85063.hpp>
#include <XPowersLib.h>

static SensorPCF85063 rtc;
static XPowersPMU pmu;
static bool rtcOk = false;
static bool pmuOk = false;

bool rtcBegin() {
  rtcOk = rtc.begin(Wire, IIC_SDA, IIC_SCL);
  if (!rtcOk) Serial.println("PCF85063 no detectado");
  return rtcOk;
}

uint32_t rtcEpoch() {
  if (!rtcOk || !rtc.isClockIntegrityGuaranteed()) return 0;
  RTC_DateTime t = rtc.getDateTime();
  uint16_t year = t.getYear();
  uint8_t month = t.getMonth();
  uint8_t day = t.getDay();
  uint8_t hour = t.getHour();
  uint8_t minute = t.getMinute();
  uint8_t second = t.getSecond();
  if (year < 2025 || year > 2099 || month < 1 || month > 12 ||
      hour > 23 || minute > 59 || second > 59) return 0;
  static const uint8_t DAYS[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  auto leap = [](uint16_t value) {
    return value % 4 == 0 && (value % 100 != 0 || value % 400 == 0);
  };
  uint8_t days = DAYS[month - 1];
  if (month == 2 && leap(year)) days++;
  if (day < 1 || day > days) return 0;
  uint32_t elapsedDays = 0;
  for (uint16_t value = 1970; value < year; value++)
    elapsedDays += leap(value) ? 366 : 365;
  for (uint8_t value = 1; value < month; value++) {
    elapsedDays += DAYS[value - 1];
    if (value == 2 && leap(year)) elapsedDays++;
  }
  elapsedDays += day - 1;
  return elapsedDays * 86400UL + (uint32_t)hour * 3600UL +
         (uint32_t)minute * 60UL + second;
}

void rtcSetEpoch(uint32_t e) {
  if (!rtcOk) return;
  time_t tt = e;
  struct tm tmv;
  gmtime_r(&tt, &tmv);
  rtc.setDateTime(RTC_DateTime(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                               tmv.tm_hour, tmv.tm_min, tmv.tm_sec));
}

bool batBegin() {
  pmuOk = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (!pmuOk) Serial.println("AXP2101 no detectado");
  return pmuOk;
}

// Enciende la alimentacion de la AMOLED. En la Waveshare 1.75 el panel (OLED VDD)
// cuelga del rail BLDO1 a 3.3V del AXP2101. El firmware daba por hecho que estaba
// encendido; si el PMU se resetea (drenaje total), BLDO1 queda OFF y la pantalla
// se ve negra aunque el resto funcione. Hay que llamarla ANTES de gfx->begin().
void pmuEnablePanel() {
  if (!pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    Serial.println("AXP2101 no detectado (pmuEnablePanel)");
    return;
  }
  pmu.setBLDO1Voltage(3300);   // OLED VDD
  pmu.enableBLDO1();
}

// el estado de energia (I2C) se cachea ~2 s: leerlo en cada frame del loop
// metia trafico I2C inutil y podia oscilar (parpadeo de brillo)
static uint32_t powerCacheT = 0;
static int cachedPct = -1;
static bool cachedCharging = false, cachedUsb = true;

static void refreshPower() {
  uint32_t now = millis();
  if (powerCacheT && now - powerCacheT < 2000) return;
  powerCacheT = now ? now : 1;
  if (!pmuOk) { cachedPct = -1; cachedCharging = false; cachedUsb = true; return; }
  cachedPct = pmu.isBatteryConnect() ? pmu.getBatteryPercent() : -1;
  cachedCharging = pmu.isCharging();
  cachedUsb = pmu.isVbusIn();
}

int batPercent() { refreshPower(); return cachedPct; }
bool batCharging() { refreshPower(); return cachedCharging; }
bool usbPresent() { refreshPower(); return cachedUsb; }

void pwrSetup() {
  if (!pmuOk) return;
  pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
  pmu.enableLongPressShutdown();
  pmu.setLongPressPowerOFF();
  pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
  pmu.clearIrqStatus();
}

bool pwrShortPressed() {
  if (!pmuOk) return false;
  pmu.getIrqStatus();
  bool hit = pmu.isPekeyShortPressIrq();
  if (hit) pmu.clearIrqStatus();
  return hit;
}

void pwrShutdown() {
  if (pmuOk) pmu.shutdown();
}

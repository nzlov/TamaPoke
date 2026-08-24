#include "Arduino.h"
#include "Preferences.h"
#include "content.h"
#include "trainers.h"
#include <cstdio>

uint32_t g_seed = 1;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

int main() {
  bool ok = contentBegin() && contentReady() && contentPackCount() == 13 &&
            regionAll() == 4 && dexCount() == 493 && dexValid(1) &&
            regionBattleAvailable(0);
  printf("%s  a CRC-valid bad region is ignored without poisoning later packs\n",
         ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

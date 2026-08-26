#include "Arduino.h"
#include "Preferences.h"
#include <cstdio>
#include "wild.h"
#include "dex.h"

uint32_t g_seed = 73;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

int main() {
  ck(wildEncounterMaxLevel(1, false) == 6,
     "normal encounters include five levels above the player");
  ck(wildEncounterMaxLevel(94, false) == 99,
     "normal encounters include every lower level");
  ck(wildEncounterMaxLevel(95, false) == 100,
     "normal encounter levels stop at the game limit");
  ck(wildEncounterMaxLevel(1, true) == 100 &&
     wildEncounterMaxLevel(100, true) == 100,
     "hard encounters allow every level");
  ck(wildEscapeChance(50, 50) == 90 && wildEscapeChance(80, 50) == 90,
     "escape defaults to ninety percent when not under-levelled");
  ck(wildEscapeChance(50, 100) == 45,
     "a lower level scales escape chance by the level ratio");
  ck(wildEscapeChance(1, 100) == 10,
     "escape chance never falls below ten percent");
  ck(wildFoeEscapeChance(41, 100) == 0,
     "a wild foe does not flee above forty percent HP");
  ck(wildFoeEscapeChance(40, 100) == 10,
     "a wild foe has ten percent escape chance at forty percent HP");
  ck(wildFoeEscapeChance(25, 100) == 20,
     "wild foe escape chance rises linearly as HP falls");
  ck(wildFoeEscapeChance(10, 100) == 30 &&
     wildFoeEscapeChance(1, 100) == 30,
     "wild foe escape chance reaches and keeps its thirty percent cap");
  ck(wildFoeEscapeChance(0, 100) == 0 && wildFoeEscapeChance(10, 0) == 0,
     "fainted foes and invalid HP cannot escape");

  uint8_t commonFull = wildCaptureChance(R_COMUN, 100, 100, false, 100);
  uint8_t commonLow = wildCaptureChance(R_COMUN, 1, 100, false, 100);
  uint8_t statusLow = wildCaptureChance(R_COMUN, 1, 100, true, 100);
  uint8_t betterBall = wildCaptureChance(R_COMUN, 1, 100, true, 200);
  uint8_t legend = wildCaptureChance(R_LEGENDARIO, 1, 100, true, 100);
  ck(commonFull > 0, "a healthy common creature remains catchable");
  ck(commonLow > commonFull, "low HP improves capture odds");
  ck(statusLow > commonLow, "status improves capture odds");
  ck(betterBall > statusLow, "the pack-provided ball multiplier matters");
  ck(legend < statusLow, "rarity lowers derived capture odds");
  ck(wildCaptureChance(R_COMUN, 0, 0, false, 100) == 0,
     "invalid HP cannot be captured");
  return bad ? 1 : 0;
}

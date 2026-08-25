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

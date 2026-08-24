#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "content.h"
#include <cstdio>

uint32_t g_seed = 1;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void loop();
extern bool recoveryMode;

int main() {
  setup();
  loop();
  bool ok = recoveryMode && !contentReady() && contentPackCount() == 0;
  printf("%s  firmware stays in the USB installer when required packs are absent\n",
         ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

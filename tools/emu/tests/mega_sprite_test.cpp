#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "content.h"
#include "sdmon.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 73;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

static int failures = 0;
static void ck(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

int main() {
  contentBegin();
  const MegaFormEntry *form = megaFormFor(6);
  ck(form && form->spriteSize, "moves-core provides optional Mega Charizard art");

  PmdMon normal, mega;
  bool normalOk = normal.load(6, false, GENDER_NONE, false);
  bool megaOk = mega.load(6, false, GENDER_NONE, true);
  ck(normalOk && megaOk, "base and Mega Charizard sprites both load");
  bool same = normalOk && megaOk && normal.palCount == mega.palCount &&
              !std::memcmp(normal.pal, mega.pal,
                           normal.palCount * sizeof(uint16_t));
  ck(!same, "Mega Charizard uses distinct form artwork");
  normal.unload(); mega.unload();

  PmdMon venusaurNormal, venusaurFallback;
  bool fallbackOk = venusaurNormal.load(3, false, GENDER_NONE, false) &&
                    venusaurFallback.load(3, false, GENDER_NONE, true);
  bool fallbackSame = fallbackOk &&
      venusaurNormal.palCount == venusaurFallback.palCount &&
      !std::memcmp(venusaurNormal.pal, venusaurFallback.pal,
                   venusaurNormal.palCount * sizeof(uint16_t));
  ck(fallbackSame, "a missing Mega form safely falls back to base artwork");
  venusaurNormal.unload(); venusaurFallback.unload();

  return failures ? 1 : 0;
}

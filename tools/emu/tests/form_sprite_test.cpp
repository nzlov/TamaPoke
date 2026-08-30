#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "content.h"
#include "gender.h"
#include "sdmon.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 97;
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

static bool samePalette(const PmdMon &left, const PmdMon &right) {
  return left.palCount == right.palCount &&
         !std::memcmp(left.pal, right.pal,
                      left.palCount * sizeof(uint16_t));
}

int main() {
  contentBegin();

  const MegaFormEntry *pyroar = megaFormFor(668, MEGA_FORM_STANDARD);
  ck(pyroar && pyroar->spriteSize,
     "the regional pack indexes generated Mega form artwork");
  PmdMon pyroarBase, pyroarMega;
  bool pyroarOk = pyroarBase.load(668, false, GENDER_NONE, false) &&
                  pyroarMega.load(668, false, GENDER_NONE, true,
                                  MEGA_FORM_STANDARD);
  ck(pyroarOk, "generated base and Mega sprites load through PmdMon");
  bool pyroarActions = pyroarOk;
  for (uint8_t action = 0; action < PMD_NACTS; action++)
    pyroarActions = pyroarActions && pyroarBase.acts[action].frames == 4;
  for (uint8_t action : {PMD_IDLE, PMD_HURT, PMD_ATTACK})
    pyroarActions = pyroarActions &&
                    pyroarBase.acts[pmdFacingAction(action, true)].frames == 4;
  ck(pyroarActions,
     "Pyroar base art exposes four authored frames for every runtime action");
  ck(pyroarOk && !samePalette(pyroarBase, pyroarMega),
     "Mega Evolution uses distinct generated form artwork");
  ck(pyroarOk && pyroarMega.has(pmdFacingAction(PMD_IDLE, true)) &&
     pyroarMega.has(pmdFacingAction(PMD_HURT, true)) &&
     pyroarMega.has(pmdFacingAction(PMD_ATTACK, true)),
     "generated Mega art provides rear battle actions");
  pyroarBase.unload();
  pyroarMega.unload();

  ck(contentGigantamaxEligible(839),
     "Coalossal remains eligible for Gigantamax");
  PmdMon coalossalBase, coalossalGmax;
  bool coalossalOk = coalossalBase.load(839, false, GENDER_NONE, false) &&
                     coalossalGmax.load(839, false, GENDER_NONE, false,
                                        MEGA_FORM_NONE, true);
  ck(coalossalOk, "base and Gigantamax sprites load through PmdMon");
  ck(coalossalOk && !samePalette(coalossalBase, coalossalGmax),
     "Gigantamax uses distinct generated form artwork");
  ck(coalossalOk && coalossalGmax.has(pmdFacingAction(PMD_IDLE, true)) &&
     coalossalGmax.has(pmdFacingAction(PMD_HURT, true)) &&
     coalossalGmax.has(pmdFacingAction(PMD_ATTACK, true)),
     "generated Gigantamax art provides rear battle actions");
  coalossalBase.unload();
  coalossalGmax.unload();

  return failures ? 1 : 0;
}

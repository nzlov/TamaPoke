#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "wild.h"
#include <cstdio>

uint32_t g_seed = 97;
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
  ck(wildRareThreshold(0) == 100,
     "the wild rare base chance is exactly one in 4096");
  ck(wildRareThreshold(1) == 4196 && wildRareThreshold(15) == 61540,
     "each blessing adds exactly one percentage point");
  ck(wildRareThreshold(99) == wildRareThreshold(15),
     "corrupt or future bonuses cannot exceed the cap");
  ck(wildGigantamaxFactorForRoll(6, 4) &&
     !wildGigantamaxFactorForRoll(6, 5) &&
     !wildGigantamaxFactorForRoll(7, 0),
     "only eligible wild species receive the five-percent Gigantamax factor");

  ck(wildRareForRoll(99, 0) && !wildRareForRoll(100, 0),
     "one roll owns the exact base-probability boundary");
  ck(wildRareForRoll(4195, 1) && !wildRareForRoll(4196, 1),
     "the same roll includes the blessing probability");

  uint8_t a = 13, d = 16, s = 19, h = 31;
  wildApplyRare(true, a, d, s, h);
  ck(a == 20 && d == 20 && s == 20 && h == 31,
     "a rare result floors every IV at twenty without adding ten");

  a = 32; d = 40; s = 21; h = 20;
  wildApplyRare(true, a, d, s, h);
  ck(a == 32 && d == 40 && s == 21 && h == 20,
     "the rare IV floor does not cap values above thirty-one");

  a = 13; d = 16; s = 19; h = 31;
  wildApplyRare(false, a, d, s, h);
  ck(a == 13 && d == 16 && s == 19 && h == 31,
     "an ordinary result applies neither rare IV effect");

  PartyMon legacy;
  legacy.dex = 7;
  legacy.sparkle = 1;
  Pet migrated;
  migrated.importState(legacy);
  ck(migrated.shiny,
     "a legacy sparkle-only creature migrates to the combined rare state");

  Preferences prefs;
  prefs.begin("tamapoke", false);
  prefs.clear();
  prefs.end();
  Pet pet;
  pet.begin();
  pet.dbgHatchAs(6, true);
  pet.registerCaught(25, false);
  ck(pet.isRegistered(25) && !pet.isShinyRegistered(25),
     "a normal catch is not mislabeled by the active creature's color");
  pet.shiny = false;
  pet.registerCaught(7, true);
  ck(pet.isShinyRegistered(7),
     "the caught creature's own color is what the Pokedex records");

  return bad ? 1 : 0;
}

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
  ck(wildColorChance(0) == 5 && wildSparkleChance(0) == 1,
     "wild color and sparkle keep their distinct base chances");
  ck(wildColorChance(15) == 20 && wildSparkleChance(15) == 16,
     "the shared bonus caps both chances at fifteen points");
  ck(wildColorChance(99) == 20 && wildSparkleChance(99) == 16,
     "corrupt or future bonuses cannot exceed the cap");

  WildTraits none = wildTraitsForRolls(5, 1, 0);
  WildTraits color = wildTraitsForRolls(4, 1, 0);
  WildTraits sparkle = wildTraitsForRolls(5, 0, 0);
  WildTraits both = wildTraitsForRolls(4, 0, 0);
  ck(!none.color && !none.sparkle, "both independent rolls can miss");
  ck(color.color && !color.sparkle, "the color roll can win by itself");
  ck(!sparkle.color && sparkle.sparkle, "the sparkle roll can win by itself");
  ck(both.color && both.sparkle, "two independent wins can coexist");

  uint8_t a = 13, d = 16, s = 19, h = 31;
  wildApplyTraits(color, a, d, s, h);
  ck(a == 20 && d == 20 && s == 20 && h == 31,
     "color gives every IV a floor of twenty");

  a = 13; d = 16; s = 19; h = 31;
  wildApplyTraits(sparkle, a, d, s, h);
  ck(a == 23 && d == 26 && s == 29 && h == 41,
     "sparkle adds ten without a thirty-one cap");

  a = 13; d = 16; s = 19; h = 31;
  wildApplyTraits(both, a, d, s, h);
  ck(a == 30 && d == 30 && s == 30 && h == 41,
     "color is applied before the independent sparkle bonus");

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

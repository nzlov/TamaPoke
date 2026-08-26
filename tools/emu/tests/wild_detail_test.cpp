// Narrow state-machine coverage for the wild-opponent detail card. This does
// not need regional art packs: rendering is compile-checked by the emulator
// build, while these assertions exercise the real battle tap/swipe handlers.
#include "Arduino.h"
#include "Preferences.h"
#include "battle.h"
#include "party.h"
#include "pet.h"
#include <cstring>

uint32_t g_seed = 0xC0FFEE;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

extern Pet pet;
extern bool battleOpen, btlOver, btlWild, btlFoeDetailOpen;
extern uint8_t btlFoeDetailPage, btlMenu;
extern Combatant btlFoe;
extern PartyMon btlWildMon;
void battleTap(int16_t x, int16_t y);
void onSwipe(int dir);

static int failures = 0;

static void check(bool condition, const char *message) {
  printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
  if (!condition) failures++;
}

int main() {
  pet.speciesId = 1;  // keep onSwipe out of the first-boot starter modal
  battleOpen = true;
  btlOver = false;
  btlMenu = 0;
  btlFoeDetailOpen = false;
  btlFoeDetailPage = 0;
  btlFoe.dex = 25;
  btlFoe.hp = 73;
  btlFoe.maxHp = 100;
  btlFoe.moves[0] = 1;

  btlWild = false;
  battleTap(330, 100);
  check(!btlFoeDetailOpen, "trainer and link opponents do not expose wild details");

  btlWild = true;
  btlWildMon.nature = NATURE_ADAMANT;
  Combatant before = btlFoe;
  battleTap(330, 100);
  check(btlFoeDetailOpen && btlFoeDetailPage == 0,
        "tapping a wild opponent opens the Pokedex page");

  onSwipe(-1);
  check(btlFoeDetailPage == 1 && btlWildMon.nature == NATURE_ADAMANT,
        "the stats page retains the generated individual's nature");
  onSwipe(-1);
  check(btlFoeDetailPage == 2, "the moves page is reachable");

  battleTap(233, 410);
  check(!btlFoeDetailOpen && btlMenu == 0 &&
            memcmp(&before, &btlFoe, sizeof(before)) == 0,
        "closing the read-only card returns to the unchanged turn");
  return failures ? 1 : 0;
}

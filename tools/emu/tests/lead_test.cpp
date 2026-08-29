// The pet-name menu assigns one persistent battle lead. Viewing another pet
// must not change it, and an already-leading pet exposes an inert menu row.
#include "Arduino.h"
#include "Preferences.h"
#include "battle.h"
#include "party.h"
#include "pet.h"
#include <cstdio>

uint32_t g_seed = 227;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void onTap(int16_t x, int16_t y);
void startBattle(int16_t dex, uint8_t lvl);
void pickDefault(uint8_t cap);
extern Pet pet;
extern bool menuOpen, battleOpen;
extern uint16_t squadMask;
extern int8_t btlSquadSource[PARTY_SLOTS];

static int fail(const char *message) {
  std::printf("FAIL  %s\n", message);
  return 1;
}

int main() {
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(1);
  if (pet.isEgg()) pet.dbgHatchAs(1, false);

  party.captureActive(pet, false);
  PartyMon second = party.slots[party.activeIndex()];
  second.dex = 4;
  second.nick[0] = 'L';
  second.nick[1] = 0;
  party.replaceAt(1, second);
  if (!party.activate(1, pet)) return fail("test could not view the second pet");

  menuOpen = true;
  onTap(233, 117);  // first menu row: LEAD
  if (party.leadIndex() != 1 || menuOpen)
    return fail("LEAD did not assign the viewed pet exclusively");

  Party rebootedRoster;
  rebootedRoster.begin();
  if (rebootedRoster.leadIndex() != 1)
    return fail("the selected lead was not restored from the roster snapshot");

  menuOpen = true;
  onTap(233, 117);  // the same row now says LEADING and is disabled
  if (!menuOpen || party.leadIndex() != 1)
    return fail("LEADING was still clickable");

  menuOpen = false;
  if (!party.activate(0, pet) || party.leadIndex() != 1)
    return fail("viewing another pet changed the selected lead");

  squadMask = 0;
  pickDefault(1);
  if (squadMask != (1u << 1))
    return fail("the default limited squad did not include the selected lead");

  menuOpen = false;
  battleOpen = false;
  startBattle(9, 10);
  if (!battleOpen || btlSquadSource[0] != 1)
    return fail("battle did not send out the selected lead first");

  battleOpen = false;
  menuOpen = true;
  onTap(233, 117);  // LEAD on the viewed first pet replaces the old selection
  if (party.leadIndex() != 0 || menuOpen)
    return fail("assigning a new lead did not clear the previous selection");

  std::puts("PASS  one persistent menu-selected lead enters battle first");
  return 0;
}

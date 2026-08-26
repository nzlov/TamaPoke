#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "battle.h"
#include "content.h"
#include "items.h"
#include "party.h"
#include "pet.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 0xB4771E;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void render();
void onTap(int16_t x, int16_t y);
void onSwipeV(int dir);
void boxTap(int16_t x, int16_t y);
void btlFinish(bool won);
void btlCompleteCapture();
uint8_t uiCurrentScreen();

extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern Party party;
extern Combatant btlYou, btlFoe;
extern PartyMon btlWildMon, capturedMon, partyPending;
extern bool battleOpen, btlWild, partyPick, boxOpen;
extern int8_t btlTrainer;
extern bool btlLink;
extern uint8_t btlSquadN, btlEnteredMask, btlWildMechanic;
extern int8_t btlSquadSource[];
extern uint32_t btlWinUntil;
extern uint16_t btlRewardTraining[3];
extern ItemKey btlRewardItems[2];
extern uint8_t btlRewardItemCount;

static int bad = 0;

static void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static bool screenIs(const char *name) {
  return std::strcmp(SCREEN_NAME[uiCurrentScreen()], name) == 0;
}

int main() {
  setup();
  pet.speciesId = 1;
  pet.ageMinutes = 49UL * MINUTES_PER_LEVEL;
  pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = 31;
  pet.trAtk = pet.trDef = pet.trSpe = 10;
  party.captureActive(pet, false);
  btlSquadN = 1;
  btlSquadSource[0] = 0;
  btlEnteredMask = 1;
  btlWildMechanic = 0;
  btlTrainer = -1;
  btlWild = true;
  btlLink = false;
  battleOpen = true;
  btlFinish(true);
  check(btlWinUntil && screenIs("win"),
        "defeating a wild creature opens the reward settlement page");
  check(btlRewardTraining[0] + btlRewardTraining[1] + btlRewardTraining[2] > 0,
        "the settlement snapshot records the awarded training attributes");
  check(btlRewardItemCount > 0 && btlRewardItems[0] != ITEM_KEY_NONE,
        "the settlement snapshot records the awarded item");
  gfx->frameReady = false;
  render();
  check(gfx->frameReady, "the reward settlement page flushes to the panel");
  onTap(233, 390);
  check(!btlWinUntil && !battleOpen, "dismissing settlement leaves the battle");

  uint8_t partyBefore = party.count();
  btlWildMon = party.slots[0];
  int16_t captureDex = dexCount() ? 1 : -1;
  btlWildMon.dex = captureDex;
  btlWild = true;
  battleOpen = true;
  btlTrainer = -1;
  btlCompleteCapture();
  check(screenIs("win") && capturedMon.dex == captureDex,
        "the caught creature is included in the reward settlement");
  check(party.count() == partyBefore + 1 && !partyPick,
        "a caught creature is stored automatically when capacity is available");
  onTap(233, 390);
  check(!btlWinUntil && capturedMon.empty() && !btlWild,
        "capture settlement dismisses directly to normal play without a detail page");

  PartyMon filler = party.slots[0];
  filler.dex = 1;
  for (uint8_t i = 0; i < PARTY_SLOTS; i++) party.slots[i] = filler;
  for (uint8_t i = 0; i < BOX_SLOTS; i++) party.box[i] = filler;
  btlWildMon = filler;
  btlWildMon.dex = captureDex;
  btlWild = true;
  battleOpen = true;
  btlCompleteCapture();
  check(screenIs("win") && partyPick && partyPending.dex == captureDex,
        "a full collection keeps the caught creature pending behind settlement");
  onSwipeV(1);
  check(screenIs("win") && partyPick,
        "the modal settlement cannot discard a pending caught creature by swiping");
  onTap(233, 390);
  check(screenIs("box") && partyPick && capturedMon.empty(),
        "dismissing a full capture settlement opens the replacement page");
  boxTap(233, 400);
  check(!boxOpen && !partyPick && partyPending.empty(),
        "the replacement page can release the newly caught creature");

  std::puts(bad ? "FAILURES" : "capture is settled and stored through the reward flow");
  return bad ? 1 : 0;
}

#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "battle.h"
#include "content.h"
#include "items.h"
#include "inventory.h"
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
extern Inventory inventory;
extern Combatant btlYou, btlFoe;
extern PartyMon btlWildMon, capturedMon, partyPending;
extern bool battleOpen, btlWild, btlOver, partyPick, boxOpen;
extern int8_t btlTrainer;
extern bool btlLink;
extern uint8_t btlSquadN, btlEnteredMask, btlWildMechanic;
extern uint8_t btlMenu, btlItemPage;
extern bool btlTapDebounceArmed;
extern int8_t btlSquadSource[];
extern uint32_t btlWinUntil;
extern bool btlCaptureAnimating, btlCaptureSuccess;
extern uint32_t btlCaptureStartedAt;
extern ItemKey btlCaptureItem;
extern uint16_t btlRewardTraining[3];
extern ItemKey btlRewardItems[2];
extern uint8_t btlRewardItemCount;
void btlUpdateCapture(uint32_t now);

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
  if (pet.awaitingStarter()) pet.chooseStarter(1);
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
  const ItemEntry *masterBall = nullptr;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item && item->effect == ITEM_EFFECT_CATCH &&
        item->param == ITEM_CATCH_GUARANTEED) {
      masterBall = item;
      break;
    }
  }
  check(masterBall, "the move pack provides a guaranteed-catch ball");
  btlWildMon = party.slots[0];
  int16_t captureDex = dexCount() ? 1 : -1;
  btlWildMon.dex = captureDex;
  btlFoe.dex = captureDex;
  btlFoe.maxHp = btlFoe.hp = 100;
  btlFoe.ailment = AIL_NONE;
  btlFoe.confuseTurns = 0;
  btlWild = true;
  btlOver = false;
  battleOpen = true;
  btlTrainer = -1;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    while (item && inventory.count(item->key)) inventory.consume(item->key);
  }
  uint8_t selectedBallImages = 0;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (!item || item->effect != ITEM_EFFECT_CATCH) continue;
    inventory.add(item->key);
    btlMenu = 3;
    btlItemPage = 0;
    btlTapDebounceArmed = false;
    onTap(149, 296);
    if (btlCaptureItem == item->key && btlCaptureAnimating) selectedBallImages++;
    btlCaptureItem = ITEM_KEY_NONE;
    btlCaptureAnimating = false;
  }
  check(selectedBallImages == 4,
        "each capture item carries its own key into the throw animation");
  uint8_t masterBefore = masterBall ? inventory.count(masterBall->key) : 0;
  if (masterBall) inventory.add(masterBall->key);
  uint8_t masterStocked = masterBall ? inventory.count(masterBall->key) : 0;
  btlMenu = 3;
  btlItemPage = 0;
  btlTapDebounceArmed = false;
  onTap(149, 296);
  check(masterBall && btlCaptureItem == masterBall->key &&
        btlCaptureAnimating && btlCaptureSuccess,
        "the Master Ball remains selected throughout its guaranteed capture animation");
  uint8_t masterDuringThrow = masterBall ? inventory.count(masterBall->key) : 0;
  btlTapDebounceArmed = false;
  onTap(149, 296);
  check(masterBall && btlCaptureItem == masterBall->key && btlCaptureAnimating &&
        inventory.count(masterBall->key) == masterDuringThrow,
        "the capture animation blocks a duplicate ball action");
  btlUpdateCapture(btlCaptureStartedAt + 3650UL);
  check(screenIs("win") && capturedMon.dex == captureDex,
        "the guaranteed-catch ball includes a full-HP creature in settlement");
  check(masterBall && masterStocked == masterBefore + 1 &&
        inventory.count(masterBall->key) == masterBefore,
        "the guaranteed-catch ball is consumed exactly once");
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

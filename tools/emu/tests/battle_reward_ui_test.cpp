#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "battle.h"
#include "content.h"
#include "items.h"
#include "inventory.h"
#include "party.h"
#include "pet.h"
#include "player.h"
#include "motion.h"
#include "perf.h"
#include "ui_scroll.h"
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
void startBattle(int16_t dex, uint8_t lvl);
void startTrainerBattle(uint8_t idx, bool hard);
bool btlAttemptRun(uint8_t roll);
uint8_t uiCurrentScreen();

extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern Party party;
extern PlayerProgress player;
extern Inventory inventory;
extern Combatant btlYou, btlFoe;
extern Combatant btlSquad[];
extern PartyMon btlWildMon, capturedMon, partyPending;
extern bool battleOpen, btlWild, btlOver, btlWon, partyPick, boxOpen;
extern uint8_t gymRegion;
extern bool btlHard;
extern int8_t btlTrainer;
extern bool btlLink;
extern uint8_t btlSquadN, btlEnteredMask, btlWildMechanic;
extern uint8_t btlMenu, btlItemPage, btlMsgCount;
extern bool btlTapDebounceArmed;
extern int8_t btlSquadSource[];
extern uint32_t btlWinUntil;
extern bool btlCaptureAnimating, btlCaptureSuccess, btlCaptureCuePlayed;
extern bool btlTurnAnimating, btlTurnShowingRound;
extern uint32_t btlCaptureStartedAt;
extern uint32_t btlTurnBeatStartedAt;
extern ItemKey btlCaptureItem;
extern ThrowGestureDetector btlThrowDetector;
extern bool btlThrowArmed;
extern uint32_t btlThrowStartedAt;
extern ItemKey btlThrowItem;
extern uint16_t btlRewardTraining[3];
extern ItemRef btlRewardItems[];
extern uint8_t btlRewardItemCount;
extern uint8_t btlGmaxBonusDrops;
extern UiScrollView btlRewardScroll;
extern bool btlLearnPromptOpen;
extern uint8_t btlTaskSubmitState;
extern bool btlCapturedTaskHard;
extern ItemRef btlCapturedTaskRewards[3];
extern uint8_t btlCapturedTaskRewardCount;
void btlUpdateCapture(uint32_t now);
void btlUpdateThrow(uint32_t now);
bool btlFeedThrowSample(const MotionSample &sample);
bool btlStartCapture(const ItemEntry &item, uint8_t roll, uint32_t now);
void btlUpdateTurnPresentation(uint32_t now);
void btlApplyGmaxInventoryEffect(uint8_t actorSide, uint8_t bonusRewardItems,
                                 bool restoreLastItem);
extern ItemRef btlLastConsumedItem;

static int bad = 0;

static void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static bool screenIs(const char *name) {
  return std::strcmp(SCREEN_NAME[uiCurrentScreen()], name) == 0;
}

static MoveId findMove(const char *name) {
  for (MoveId id = 1; id < moveCount(); id++)
    if (!std::strcmp(moveEntry(id).name, name)) return id;
  return MOVE_NONE;
}

static void finishTurnActions() {
  if (btlTurnAnimating && !btlTurnShowingRound)
    btlUpdateTurnPresentation(btlTurnBeatStartedAt + 60000UL);
}

static void openRoundWithSeed(uint32_t seed) {
  randomSeed(seed);
  if (btlTurnAnimating && btlTurnShowingRound)
    btlUpdateTurnPresentation(btlTurnBeatStartedAt + 700UL);
}

int main() {
  UiScrollView reusableScroll;
  reusableScroll.configure(100, 200, 450, 50);
  check(reusableScroll.scroll(-1) && reusableScroll.offset() == 50 &&
        reusableScroll.contentY(0) == 50 &&
        !reusableScroll.fullyVisible(50, 40),
        "the reusable scroll view advances, translates, and clips generic content");
  reusableScroll.scroll(1);
  check(reusableScroll.offset() == 0 && !reusableScroll.canScrollUp() &&
        reusableScroll.canScrollDown(),
        "the reusable scroll view clamps at the start of arbitrary content");

  setup();
  const ItemEntry *replenishItem = itemCount() ? itemAt(0) : nullptr;
  if (replenishItem) {
    inventory.add(replenishItem->key);
    inventory.consume(replenishItem->key);
    uint8_t consumedCount = inventory.count(replenishItem->key);
    btlLastConsumedItem = { replenishItem->key, MOVE_NONE };
    btlApplyGmaxInventoryEffect(0, 0, true);
    check(inventory.count(replenishItem->key) == consumedCount + 1 &&
          !btlLastConsumedItem,
          "G-Max Replenish restores and clears the latest consumed battle item");
    inventory.consume(replenishItem->key);
  }
  if (pet.awaitingStarter()) pet.chooseStarter(1);
  pet.speciesId = 1;
  pet.ageMinutes = 49UL * MINUTES_PER_LEVEL;
  pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = 31;
  pet.trAtk = pet.trDef = pet.trSpe = 10;
  party.captureActive(pet, false);
  btlSquadN = 1;
  btlSquadSource[0] = (int8_t)party.activeIndex();
  btlEnteredMask = 1;
  btlWildMechanic = 0;
  btlTrainer = -1;
  btlWild = true;
  btlLink = false;
  battleOpen = true;

  MoveId observedMoves[2] = { MOVE_NONE, MOVE_NONE };
  for (MoveId move = 1; move < moveCount(); move++) {
    if (pet.knowsMove(move)) continue;
    if (!observedMoves[0]) observedMoves[0] = move;
    else { observedMoves[1] = move; break; }
  }
  for (MoveId &move : pet.reserveMoves) move = MOVE_NONE;
  party.captureActive(pet, false);
  combatantFromPet(btlYou, pet);
  btlYou.observedMove = observedMoves[0];
  btlSquad[0] = btlYou;
  btlYou.hp = 0;
  btlWild = false;
  btlFinish(false);
  check(!btlLearnPromptOpen && pet.knowsMove(observedMoves[0]),
        "a move observed before fainting is retained after a lost battle when a learned slot is free");

  MoveId fullSet[LEARNED_MOVE_SLOTS] = {};
  uint8_t fullCount = 0;
  for (MoveId move = 1; move < moveCount() && fullCount < LEARNED_MOVE_SLOTS; move++)
    if (move != observedMoves[1]) fullSet[fullCount++] = move;
  for (uint8_t i = 0; i < MOVE_SLOTS; i++) pet.moves[i] = fullSet[i];
  for (uint8_t i = 0; i < RESERVE_MOVE_SLOTS; i++)
    pet.reserveMoves[i] = fullSet[MOVE_SLOTS + i];
  party.captureActive(pet, false);
  combatantFromPet(btlYou, pet);
  btlYou.observedMove = observedMoves[1];
  btlSquad[0] = btlYou;
  btlYou.hp = 0;
  btlMsgCount = 0;
  btlTapDebounceArmed = false;
  battleOpen = true;
  randomSeed(17);
  btlFinish(false);
  check(btlLearnPromptOpen && !pet.knowsMove(observedMoves[1]),
        "a full learned set waits for an explicit post-battle retention choice");
  gfx->frameReady = false;
  render();
  check(gfx->frameReady, "the post-battle move retention prompt flushes to the panel");
  onTap(150, 350);
  check(!btlLearnPromptOpen && pet.learnedMoveCount() == LEARNED_MOVE_SLOTS &&
        pet.knowsMove(observedMoves[1]),
        "retaining the observed move randomly replaces one of eight learned moves");

  MoveId declined = MOVE_NONE;
  for (MoveId move = 1; move < moveCount(); move++)
    if (!pet.knowsMove(move)) { declined = move; break; }
  combatantFromPet(btlYou, pet);
  btlYou.observedMove = declined;
  btlSquad[0] = btlYou;
  btlMsgCount = 0;
  btlTapDebounceArmed = false;
  battleOpen = true;
  btlFinish(false);
  check(btlLearnPromptOpen, "a later full-set observation opens a new retention choice");
  onTap(320, 350);
  check(!btlLearnPromptOpen && !pet.knowsMove(declined),
        "declining keeps the existing eight learned moves unchanged");

  btlMsgCount = 0;
  btlWild = true;
  battleOpen = true;
  combatantFromPet(btlYou, pet);
  btlSquad[0] = btlYou;
  uint32_t inventoryWrites = perfSample(PERF_INVENTORY_SAVE).nvsWrites;
  btlFinish(true);
  check(btlWinUntil && screenIs("win"),
        "defeating a wild creature opens the reward settlement page");
  check(btlRewardTraining[0] + btlRewardTraining[1] + btlRewardTraining[2] > 0,
        "the settlement snapshot records the awarded training attributes");
  check((btlRewardItemCount == 1 || btlRewardItemCount == 2) &&
        btlRewardItems[0].key != ITEM_KEY_NONE &&
        (btlRewardItemCount == 1 ||
         btlRewardItems[0].key != btlRewardItems[1].key),
        "a normal wild victory records one base reward and at most one distinct bonus");
  check(perfSample(PERF_INVENTORY_SAVE).nvsWrites == inventoryWrites + 1,
        "a multi-item wild reward commits the inventory once");
  gfx->frameReady = false;
  render();
  check(gfx->frameReady, "the reward settlement page flushes to the panel");
  onTap(233, 390);
  check(!btlWinUntil && !battleOpen, "dismissing settlement leaves the battle");

  btlWild = true;
  btlLink = false;
  battleOpen = true;
  btlGmaxBonusDrops = 3;
  inventoryWrites = perfSample(PERF_INVENTORY_SAVE).nvsWrites;
  btlFinish(true);
  check(btlRewardItemCount >= 4 && btlRewardItemCount <= 5,
        "three successful G-Max Gold Rush hits add three weighted wild rewards");
  check(perfSample(PERF_INVENTORY_SAVE).nvsWrites == inventoryWrites + 1,
        "Gold Rush rewards remain inside the single victory inventory commit");
  onTap(233, 438);
  btlGmaxBonusDrops = 0;

  btlHard = true;
  btlWildMechanic = BMECH_Z_MOVE;
  btlWild = true;
  btlLink = false;
  battleOpen = true;
  inventoryWrites = perfSample(PERF_INVENTORY_SAVE).nvsWrites;
  btlFinish(true);
  check((btlRewardItemCount == 3 || btlRewardItemCount == 4) &&
        btlRewardItems[0].key != ITEM_KEY_NONE &&
        btlRewardItems[1].key != ITEM_KEY_NONE &&
        btlRewardItems[0].key != btlRewardItems[1].key &&
        (btlRewardItemCount == 3 ||
         (btlRewardItems[2].key != btlRewardItems[0].key &&
          btlRewardItems[2].key != btlRewardItems[1].key)),
        "a hard wild victory records two base rewards, an optional distinct bonus, and its mechanic reward");
  check(perfSample(PERF_INVENTORY_SAVE).nvsWrites == inventoryWrites + 1,
        "hard-mode item and mechanic rewards share one inventory commit");
  btlRewardTraining[0] = btlRewardTraining[1] = btlRewardTraining[2] = 1;
  btlRewardItemCount = 0;
  for (uint16_t i = 0; i < itemCount() && btlRewardItemCount < 4; i++) {
    const ItemEntry *item = itemAt(i);
    if (item) btlRewardItems[btlRewardItemCount++] = { item->key, MOVE_NONE };
  }
  gfx->frameReady = false;
  render();
  check(gfx->frameReady && btlRewardScroll.canScrollDown(),
        "the maximum hard reward settlement exposes overflow through the scroll view");
  onSwipeV(-1);
  check(btlWinUntil && btlRewardScroll.offset() > 0,
        "swiping up scrolls rewards without dismissing the settlement");
  gfx->frameReady = false;
  render();
  check(gfx->frameReady, "the scrolled hard reward settlement flushes to the panel");
  onSwipeV(1);
  check(btlRewardScroll.offset() == 0,
        "swiping down returns the reward view to its first rows");
  onTap(233, 438);
  check(!btlWinUntil && !battleOpen, "dismissing the hard settlement leaves the battle");
  btlHard = false;
  btlWildMechanic = BMECH_NONE;

  uint8_t partyBefore = party.count();
  for (DailyTask &task : player.dailyTasks.entries) task.completed = 1;
  const ItemEntry *masterBall = nullptr;
  const ItemEntry *catchBall = nullptr;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item && item->effect == ITEM_EFFECT_CATCH &&
        item->param != ITEM_CATCH_GUARANTEED && !catchBall) catchBall = item;
    if (item && item->effect == ITEM_EFFECT_CATCH &&
        item->param == ITEM_CATCH_GUARANTEED) {
      masterBall = item;
    }
  }
  check(masterBall && catchBall,
        "the move pack provides ordinary and guaranteed-catch balls");
  int16_t captureDex = dexCount() ? 1 : -1;
  startBattle(captureDex, 50);
  btlWildMon = party.slots[0];
  btlWildMon.dex = captureDex;
  btlFoe.dex = captureDex;
  btlFoe.maxHp = btlFoe.hp = 100;
  btlFoe.ailment = AIL_NONE;
  btlFoe.confuseTurns = 0;
  btlWild = true;
  btlOver = false;
  battleOpen = true;
  btlTrainer = -1;
  openRoundWithSeed(1);
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    while (item && inventory.count(item->key)) inventory.consume(item->key);
  }

  if (masterBall) inventory.add(masterBall->key);
  uint8_t armedStock = masterBall ? inventory.count(masterBall->key) : 0;
  btlThrowArmed = true;
  btlThrowStartedAt = 1000;
  btlThrowItem = masterBall ? masterBall->key : ITEM_KEY_NONE;
  btlThrowDetector.arm(1000);
  btlFeedThrowSample({1100, 0.0f, 0.0f, 2.2f, 0.0f, 500.0f, 0.0f});
  btlFeedThrowSample({1110, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f});
  check(masterBall && btlThrowArmed && !btlCaptureAnimating &&
        inventory.count(masterBall->key) == armedStock,
        "arming and a tap spike do not consume the selected ball");
  btlFeedThrowSample({1250, 0.0f, 0.0f, 1.1f, 0.0f, 100.0f, 0.0f});
  btlFeedThrowSample({1300, 0.0f, 0.0f, 1.3f, 0.0f, 220.0f, 0.0f});
  btlFeedThrowSample({1350, 0.0f, 0.0f, 1.7f, 0.0f, 300.0f, 0.0f});
  btlFeedThrowSample({1400, 0.0f, 0.0f, 1.3f, 0.0f, 250.0f, 0.0f});
  check(masterBall && btlThrowArmed && !btlCaptureAnimating &&
        inventory.count(masterBall->key) == armedStock,
        "flick motion alone does not consume the ball before the terminal hold");
  for (uint32_t at = 1450; at <= 1750; at += 50) {
    btlFeedThrowSample({at, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f});
  }
  check(masterBall && !btlThrowArmed && btlCaptureAnimating &&
        btlCaptureItem == masterBall->key &&
        inventory.count(masterBall->key) + 1 == armedStock,
        "flick-and-hold starts the existing capture path and consumes one ball");
  uint8_t armedAfterThrow = masterBall ? inventory.count(masterBall->key) : 0;
  btlFeedThrowSample({1800, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f});
  check(masterBall && inventory.count(masterBall->key) == armedAfterThrow,
        "samples after the accepted gesture cannot consume another ball");
  btlCaptureAnimating = false;
  btlCaptureItem = ITEM_KEY_NONE;

  if (masterBall) inventory.add(masterBall->key);
  uint8_t timeoutStock = masterBall ? inventory.count(masterBall->key) : 0;
  btlThrowArmed = true;
  btlThrowStartedAt = 2000;
  btlThrowItem = masterBall ? masterBall->key : ITEM_KEY_NONE;
  btlThrowDetector.arm(2000);
  btlUpdateThrow(5000);
  check(masterBall && !btlThrowArmed && btlMenu == 3 &&
        inventory.count(masterBall->key) == timeoutStock,
        "a timed-out throw returns to the bag without consuming the ball");
  if (masterBall) inventory.consume(masterBall->key);

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
  filler.level = 100;
  filler.ageMinutes = 99UL * MINUTES_PER_LEVEL;
  for (uint8_t i = 0; i < PARTY_SLOTS; i++) party.slots[i] = filler;
  for (uint8_t i = 0; i < BOX_SLOTS; i++) party.box[i] = filler;
  partyPick = false;
  partyPending = PartyMon();
  boxOpen = false;
  player.dailyTasks.entries[0].species = captureDex;
  player.dailyTasks.entries[0].completed = 0;
  btlWildMon = filler;
  btlWildMon.dex = captureDex;
  btlWildMon.level = 120;
  btlWildMon.ageMinutes = 119UL * MINUTES_PER_LEVEL;
  btlWild = true;
  battleOpen = true;
  btlCompleteCapture();
  check(screenIs("win") && btlTaskSubmitState == 1 && btlCapturedTaskHard &&
        capturedMon.level == 120 &&
        !partyPick && partyPending.empty(),
        "a level-120 task target keeps its level for the hard submit warning");
  gfx->frameReady = false;
  render();
  check(gfx->frameReady,
        "the captured-task submit warning renders and flushes over settlement");
  onSwipeV(1);
  check(screenIs("win") && btlTaskSubmitState == 1 && !partyPick,
        "the captured-task prompt cannot be dismissed or stored by swiping");
  onTap(150, 328);
  check(btlTaskSubmitState == 2 && player.dailyTasks.entries[0].completed &&
        capturedMon.empty() && !partyPick &&
        (btlCapturedTaskRewardCount == 2 || btlCapturedTaskRewardCount == 3) &&
        btlCapturedTaskRewards[0].key != ITEM_KEY_NONE,
        "confirming submits level 120 directly and grants the hard task reward");
  gfx->frameReady = false;
  render();
  check(gfx->frameReady, "the direct-submit task reward flushes to the panel");
  onTap(233, 335);
  check(!btlWinUntil && !boxOpen && !partyPick,
        "closing the direct-submit reward returns to play without replacement");

  player.dailyTasks.entries[0].completed = 0;
  btlWildMon = filler;
  btlWildMon.dex = captureDex;
  btlWildMon.level = 120;
  btlWildMon.ageMinutes = 119UL * MINUTES_PER_LEVEL;
  btlWild = true;
  battleOpen = true;
  btlCompleteCapture();
  onTap(300, 328);
  check(screenIs("win") && btlTaskSubmitState == 0 && partyPick &&
        partyPending.dex == captureDex && partyPending.level == 100 &&
        partyPending.ageMinutes == 99UL * MINUTES_PER_LEVEL,
        "declining direct submission lowers the catch to level 100 before storage");
  onSwipeV(1);
  check(screenIs("win") && partyPick,
        "the modal settlement cannot discard a pending caught creature by swiping");
  onTap(233, 390);
  check(screenIs("box") && partyPick && capturedMon.empty(),
        "dismissing a full capture settlement opens the replacement page");
  boxTap(233, 400);
  check(!boxOpen && !partyPick && partyPending.empty(),
        "the replacement page can release the newly caught creature");

  for (uint8_t i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
  for (uint8_t i = 0; i < BOX_SLOTS; i++) party.boxReleaseAt(i);
  pet.dbgHatchAs(1, false);
  party.captureActive(pet, false);
  gymRegion = 0;
  startTrainerBattle(0, false);
  check(battleOpen && btlSquadN == 1, "a one-member gym battle starts");
  for (MoveId &move : btlFoe.moves) move = MOVE_NONE;
  openRoundWithSeed(1);
  uint16_t hpBeforeRun = btlYou.hp;
  check(!btlAttemptRun(99) && !btlOver && !pet.isDead() && btlYou.hp == hpBeforeRun,
        "a failed gym escape consumes the turn without forcing a defeat");
  finishTurnActions();
  btlWild = true;
  openRoundWithSeed(1);
  check(!btlAttemptRun(99) && !btlOver && !pet.isDead() && btlYou.hp == hpBeforeRun,
        "a failed wild escape also leaves death to normal enemy damage");
  finishTurnActions();
  MoveId tackle = findMove("TACKLE");
  btlYou.maxHp = btlYou.hp = 1000;
  btlFoe.moves[0] = tackle;
  openRoundWithSeed(1);
  check(tackle && !btlAttemptRun(99) && !btlOver && !pet.isDead() &&
        btlYou.hp > 0 && btlYou.hp < 1000,
        "the opponent takes its normal action after a failed escape");
  finishTurnActions();

  btlFoe.angry = false;
  btlFoe.maxHp = 100;
  btlFoe.hp = 40;
  for (MoveId &move : btlFoe.moves) move = MOVE_NONE;
  btlFoe.moves[0] = tackle;
  btlYou.maxHp = btlYou.hp = 1000;
  btlOver = false;
  btlMsgCount = 0;
  battleOpen = true;
  btlHard = true;
  if (catchBall) inventory.add(catchBall->key);
  openRoundWithSeed(23);  // Intent 1/100; committed escape roll 99/100 fails after anger.
  check(catchBall && btlStartCapture(*catchBall, 99, 1000),
        "an ordinary ball starts a guaranteed failed-capture fixture");
  btlUpdateCapture(5250);
  check(!btlCaptureAnimating && !btlOver && !pet.isDead() && btlYou.hp == 1000,
        "a preselected escape that fails after capture anger still consumes the foe's only action");
  finishTurnActions();

  btlFoe.angry = false;
  btlFoe.hp = 40;
  btlYou.hp = 1000;
  btlOver = false;
  btlMsgCount = 0;
  battleOpen = true;
  if (catchBall) inventory.add(catchBall->key);
  openRoundWithSeed(1);  // Intent 38/100 stays outside the pre-capture ten-percent band.
  check(catchBall && btlStartCapture(*catchBall, 99, 1000),
        "a second ordinary ball starts the committed-attack fixture");
  btlUpdateCapture(5250);
  check(!btlCaptureAnimating && !btlOver && !pet.isDead() && btlYou.hp < 1000,
        "a preselected attack executes normally after capture anger");
  finishTurnActions();

  btlFoe.angry = false;
  btlFoe.hp = 40;
  btlYou.hp = 1000;
  btlOver = false;
  btlMsgCount = 0;
  battleOpen = true;
  if (catchBall) inventory.add(catchBall->key);
  openRoundWithSeed(140);  // Intent 3/100 and committed escape roll 8/100 both succeed.
  check(catchBall && btlStartCapture(*catchBall, 99, 1000),
        "a third ordinary ball starts the successful-escape fixture");
  btlUpdateCapture(5250);
  check(!btlCaptureAnimating && btlOver && !pet.isDead() && btlYou.hp == 1000,
        "a preselected escape that succeeds after capture anger ends the battle without attacking");

  std::puts(bad ? "FAILURES" : "capture is settled and stored through the reward flow");
  return bad ? 1 : 0;
}

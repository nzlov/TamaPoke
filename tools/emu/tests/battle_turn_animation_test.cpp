// A resolved exchange remains authoritative immediately, but its visible
// actions must play in order before the next input is accepted.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "i18n.h"
#include <cstdio>

uint32_t g_seed = 47;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void startBattle(int16_t dex, uint8_t lvl);
void commitBattleMove(uint8_t moveSlot, uint8_t percent);
void battleTap(int16_t x, int16_t y);
void btlUpdateTurnPresentation(uint32_t now);
uint8_t uiCurrentScreen();
bool uiScreenContinuous(uint8_t screen);
extern Pet pet;
extern Combatant btlYou, btlFoe;
extern bool battleOpen, btlTurnAnimating, btlTurnShowingRound, btlOver, btlWon;
extern bool btlHard, btlPetIn;
extern int8_t btlTrainer;
extern uint8_t btlRegion;
extern uint8_t btlMenu, btlMsgCount, btlTurnBeatCount, btlTurnBeatAt;
extern uint16_t btlTurnNumber, btlHpShown[2];
extern uint32_t btlTurnBeatStartedAt, btlLungeUntil[2], btlWinUntil;
enum : uint8_t { BTL_ACTION_NONE = 0, BTL_ACTION_MELEE, BTL_ACTION_RANGED };
struct BtlTurnBeat {
  char text[96];
  uint16_t hp[2];
  uint8_t kind, actor, target, moveType, moveStyle, sfx;
  bool hit, faint;
};
extern BtlTurnBeat btlTurnBeats[];

static int failures = 0;
static void check(bool condition, const char *message) {
  std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
  if (!condition) failures++;
}

int main() {
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  pet.ageMinutes = 50UL * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();

  startBattle(9, 50);
  check(battleOpen, "the battle starts");
  btlYou.hp = btlYou.maxHp = 60000;
  btlFoe.hp = btlFoe.maxHp = 60000;
  btlHpShown[0] = btlHpShown[1] = 60000;
  btlYou.base[SI_SPE] = 60000;
  btlFoe.base[SI_SPE] = 1;
  btlMenu = 0;

  MoveId meleeMove = MOVE_NONE, specialMove = MOVE_NONE;
  for (MoveId move = 1; move < moveCount(); move++) {
    const MoveEntry &entry = moveEntry(move);
    if (!meleeMove && entry.cat == MC_PHYS && entry.power &&
        (entry.tags & MT_CONTACT)) meleeMove = move;
    if (!specialMove && entry.cat == MC_SPEC && entry.power)
      specialMove = move;
  }
  check(meleeMove && specialMove,
        "the fixture provides contact and special moves for animation coverage");
  char roundLabel[40];
  battleRoundLabel(roundLabel, sizeof(roundLabel), 7, "10级");
  check(!strcmp(roundLabel, "TURN 7"),
        "a pre-turn UI pack cannot show its level-10 medal as the round number");
  battleRoundLabel(roundLabel, sizeof(roundLabel), 7, T(S_BTL_ROUND_FMT));
  check(strstr(roundLabel, "7") != nullptr,
        "the active UI pack formats the actual round number");
  btlYou.moves[0] = meleeMove;

  commitBattleMove(0, 100);
  check(btlTurnAnimating && btlTurnShowingRound && btlTurnNumber == 1,
        "a resolved exchange opens with TURN 1");
  check(btlTurnBeatCount >= 2 && btlMsgCount == 0,
        "both actions are queued instead of appearing together");

  battleTap(69 + 10, 274 + 10);
  check(btlMenu == 0, "battle input stays locked during the presentation");

  uint32_t roundEnd = btlTurnBeatStartedAt + 700;
  btlUpdateTurnPresentation(roundEnd);
  check(!btlTurnShowingRound && btlTurnBeatAt == 0,
        "the round title advances automatically to the first action");
  check(btlTurnBeats[0].moveStyle == BTL_ACTION_MELEE,
        "a contact move is classified as a close-range action");
  check(btlLungeUntil[0] > roundEnd && btlLungeUntil[1] <= roundEnd,
        "the faster combatant animates first");

  btlUpdateTurnPresentation(roundEnd + 60000);
  check(!btlTurnAnimating, "all action and effect beats finish automatically");
  check(btlHpShown[0] == btlYou.hp && btlHpShown[1] == btlFoe.hp,
        "the visible HP bars finish at the authoritative result");

  btlYou.moves[0] = specialMove;
  commitBattleMove(0, 100);
  check(btlTurnAnimating && btlTurnNumber == 2,
        "the next exchange is labelled TURN 2");
  uint32_t secondRoundEnd = btlTurnBeatStartedAt + 700;
  btlUpdateTurnPresentation(secondRoundEnd);
  check(btlTurnBeats[0].moveStyle == BTL_ACTION_RANGED &&
        btlTurnBeats[0].moveType == moveEntry(specialMove).type,
        "a special move keeps its type on the ranged effect beat");

  btlUpdateTurnPresentation(secondRoundEnd + 60000);
  startBattle(9, 50);
  btlTrainer = 0;
  btlRegion = 0;
  btlHard = false;
  btlPetIn = false;
  btlYou.moves[0] = meleeMove;
  btlYou.base[SI_SPE] = 60000;
  btlFoe.base[SI_SPE] = 1;
  btlFoe.hp = 1;
  btlHpShown[1] = 1;
  commitBattleMove(0, 100);
  check(btlOver && btlWon && btlWinUntil && btlTurnAnimating,
        "a winning exchange keeps its queued presentation before the victory page");
  check(uiScreenContinuous(uiCurrentScreen()),
        "the winning presentation remains on a continuously rendered screen");
  btlUpdateTurnPresentation(btlTurnBeatStartedAt + 60000);
  check(!btlTurnAnimating && btlWinUntil,
        "the winning action finishes and leaves the victory page ready");
  return failures ? 1 : 0;
}

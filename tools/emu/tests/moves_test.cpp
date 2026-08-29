// Move storage and learnset population. Asserts against the real Pet/Party
// rather than restating their rules.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include "moves.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 0xC0FFEE;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
static uint32_t g_ms = 0;
uint32_t millis() { return g_ms; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}
static void dump(const char *tag, const MoveId *mv, uint8_t count) {
  printf("     %s:", tag);
  for (uint8_t i = 0; i < count; i++)
    printf(" %s", mv[i] ? moveEntry(mv[i]).name : "-");
  printf("\n");
}

int main() {
  // A mature creature fills four battle slots and then four reserves using
  // natural level-up moves only.
  Pet p;
  p.dbgHatchAs(6, false);
  p.ageMinutes = 5940;                 // level 100
  p.relearnFromLevel();
  dump("Charizard active ", p.moves, MOVE_SLOTS);
  dump("Charizard reserve", p.reserveMoves, RESERVE_MOVE_SLOTS);
  ck(p.moveCount() == 4, "L100 Charizard has 4 battle moves");
  ck(p.learnedMoveCount() == 8, "and 4 reserve moves");
  bool distinct = true, valid = true;
  MoveId learned[LEARNED_MOVE_SLOTS] = {};
  for (int i = 0; i < MOVE_SLOTS; i++) learned[i] = p.moves[i];
  for (int i = 0; i < RESERVE_MOVE_SLOTS; i++)
    learned[MOVE_SLOTS + i] = p.reserveMoves[i];
  for (int i = 0; i < LEARNED_MOVE_SLOTS; i++) {
    if (!moveValid(learned[i])) valid = false;
    for (int j = i + 1; j < LEARNED_MOVE_SLOTS; j++)
      if (learned[i] && learned[i] == learned[j]) distinct = false;
  }
  ck(distinct, "no duplicate moves");
  ck(valid, "every move is a valid runtime move ID");

  bool naturalOnly = true;
  for (MoveId move : learned) {
    bool natural = false;
    for (uint16_t i = 0; i < learnCount(6); i++)
      if (learnMove(6, i) == move && learnMethod(6, i) == LM_LEVEL_UP &&
          learnLevel(6, i) <= p.level()) natural = true;
    if (!natural) naturalOnly = false;
  }
  ck(naturalOnly, "automatic learning excludes TM, tutor and egg entries");

  // A hatchling receives only its natural level-1 entries.
  Pet baby;
  baby.dbgHatchAs(6, false);
  baby.ageMinutes = 0;                 // level 1
  baby.relearnFromLevel();
  dump("Charizard L1", baby.moves, MOVE_SLOTS);
  ck(baby.moveCount() >= 1, "a level 1 pet still knows at least one move");
  ck(baby.learnedMoveCount() <= LEARNED_MOVE_SLOTS, "never exceeds 8 learned slots");

  bool legal = true;
  for (int i = 0; i < MOVE_SLOTS; i++) {
    if (!baby.moves[i]) continue;
    bool found = false;
    for (uint8_t k = 0; k < learnCount(6); k++)
      if (learnMove(6, k) == baby.moves[i] && learnMethod(6, k) == LM_LEVEL_UP &&
          learnLevel(6, k) <= 1) found = true;
    if (!found) legal = false;
  }
  ck(legal, "a level 1 pet knows only natural level-1 moves");

  // A move stone uses the complete species learnset as compatibility metadata.
  MoveId stoneMove = MOVE_NONE;
  for (uint16_t i = 0; i < learnCount(6); i++) {
    MoveId move = learnMove(6, i);
    if (learnMethod(6, i) != LM_LEVEL_UP && !p.knowsMove(move)) {
      stoneMove = move;
      break;
    }
  }
  ck(moveValid(stoneMove) && p.canLearnStone(stoneMove),
     "a non-level move in the species learnset is stone-compatible");
  MoveId before[LEARNED_MOVE_SLOTS];
  memcpy(before, learned, sizeof(before));
  ck(p.teachMove(stoneMove), "a compatible move stone teaches its move");
  ck(p.learnedMoveCount() == LEARNED_MOVE_SLOTS && p.knowsMove(stoneMove),
     "a full learned set stays at 8 and contains the new move");
  uint8_t retained = 0;
  for (MoveId move : before) if (p.knowsMove(move)) retained++;
  ck(retained == LEARNED_MOVE_SLOTS - 1,
     "teaching into a full set randomly replaces exactly one learned move");
  ck(!p.teachMove(stoneMove), "the same move cannot be learned twice");

  MoveId incompatible = MOVE_NONE;
  for (MoveId move = 1; move < moveCount(); move++)
    if (!speciesCanLearnMove(6, move)) { incompatible = move; break; }
  ck(moveValid(incompatible) && !p.canLearnStone(incompatible) &&
     !p.teachMove(incompatible), "an incompatible move stone is rejected");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}

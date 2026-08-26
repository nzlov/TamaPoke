#include "Arduino.h"
#include "Preferences.h"
#include "battle.h"
#include "content.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 71;
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

static int failures = 0;
#define CHECK(value) do { \
  if (!(value)) { std::printf("FAIL line %d: %s\n", __LINE__, #value); failures++; } \
} while (0)

static MoveId findMove(const char *name) {
  for (MoveId move = 1; move < moveCount(); move++)
    if (!std::strcmp(moveEntry(move).name, name)) return move;
  return MOVE_NONE;
}

static Combatant mon(SpeciesId species, MoveId move) {
  Combatant result;
  result.dex = species;
  result.level = 50;
  result.maxHp = result.hp = 100;
  for (uint8_t i = 0; i < SI_COUNT; i++) result.base[i] = 100;
  result.type1 = T_FIRE;
  result.type2 = T_NONE;
  result.moves[0] = move;
  return result;
}

int main() {
  contentBegin();
  MoveId flame = findMove("FLAMETHROWER");
  MoveId recover = findMove("RECOVER");
  CHECK(flame && recover);

  BattleSideMechanics side;
  Combatant zUser = mon(6, flame);
  CHECK(battleMechanicAvailable(side, zUser, BMECH_Z_MOVE, flame));
  CHECK(!battleMechanicAvailable(side, zUser, BMECH_Z_MOVE, recover));
  CHECK(battleActivateMechanic(side, zUser, BMECH_Z_MOVE, flame));
  BattleMove normal = battleMove(flame);
  BattleMove zMove = battleMoveFor(zUser, flame, BMECH_Z_MOVE);
  CHECK(zMove.mechanic == BMECH_Z_MOVE && zMove.entry.power > normal.entry.power);
  Combatant target = mon(3, flame);
  target.type1 = T_GRASS;
  CHECK(battleDamage(zUser, target, zMove, false, 255) >
        battleDamage(zUser, target, normal, false, 255));
  CHECK(zMove.entry.acc == 0 && zMove.entry.ailment == AIL_NONE);
  Combatant another = mon(9, flame);
  CHECK(!battleMechanicAvailable(side, another, BMECH_Z_MOVE, flame));
  CHECK(!battleMechanicAvailable(side, zUser, BMECH_MEGA, flame));

  Combatant maxUser = mon(9, flame);
  maxUser.hp = 40;
  CHECK(battleActivateMechanic(side, maxUser, BMECH_DYNAMAX, flame));
  CHECK(maxUser.hp == 80 && maxUser.maxHp == 200 && maxUser.dynamaxTurns == 3);
  BattleMove maxMove = battleMoveFor(maxUser, flame);
  CHECK(maxMove.mechanic == BMECH_DYNAMAX && maxMove.entry.power > normal.entry.power);
  CHECK(battleDamage(maxUser, target, maxMove, false, 255) >
        battleDamage(maxUser, target, normal, false, 255));
  battleAfterAction(maxUser);
  battleAfterAction(maxUser);
  CHECK(maxUser.activeMechanic == BMECH_DYNAMAX && maxUser.dynamaxTurns == 1);
  battleAfterAction(maxUser);
  CHECK(maxUser.activeMechanic == BMECH_NONE && maxUser.hp == 40 && maxUser.maxHp == 100);

  BattleSideMechanics switchSide;
  Combatant switched = mon(9, flame);
  switched.hp = 25;
  CHECK(battleActivateMechanic(switchSide, switched, BMECH_DYNAMAX, flame));
  battleOnSwitchOut(switched);
  CHECK(switched.activeMechanic == BMECH_NONE && switched.hp == 25 && switched.maxHp == 100);

  Combatant megaUser = mon(6, flame);
  CHECK(battleMegaEligible(megaUser.dex));
  CHECK(battleMegaEligible(719));
  CHECK(battleMegaEligible(998));
  const MegaFormEntry *charizardMega = megaFormFor(megaUser.dex);
  uint16_t expectedMegaAtk = dexValid(megaUser.dex)
      ? (uint16_t)(100 + charizardMega->bAtk - dexEntry(megaUser.dex).bAtk)
      : 120;
  CHECK(battleActivateMechanic(side, megaUser, BMECH_MEGA, flame));
  CHECK(megaUser.activeMechanic == BMECH_MEGA &&
        megaUser.base[SI_ATK] == expectedMegaAtk);
  CHECK(megaUser.type1 == T_FIRE && megaUser.type2 == T_DRAGON);
  CHECK(side.used(BMECH_Z_MOVE) && side.used(BMECH_DYNAMAX) && side.used(BMECH_MEGA));

  BattleSideMechanics guardSide;
  Combatant guard = mon(9, recover);
  Combatant attacker = mon(6, flame);
  CHECK(battleActivateMechanic(guardSide, guard, BMECH_DYNAMAX, recover));
  BattleMove guardMove = battleMoveFor(guard, recover);
  CHECK(guardMove.entry.effect == EF_PROTECT);
  CHECK(battleMovesFirst(guard, guardMove, attacker, normal));
  TurnLog log;
  battleAct(guard, attacker, guardMove, log);
  uint16_t hpBefore = guard.hp;
  battleAct(attacker, guard, normal, log);
  CHECK(log.missed && guard.hp == hpBefore);

  CHECK(wildBattleMechanic(30, 0, true) == BMECH_NONE);
  CHECK(wildBattleMechanic(29, 0, false) == BMECH_Z_MOVE);
  CHECK(wildBattleMechanic(29, 1, false) == BMECH_DYNAMAX);
  CHECK(wildBattleMechanic(29, 2, false) == BMECH_Z_MOVE);
  CHECK(wildBattleMechanic(29, 2, true) == BMECH_MEGA);
  CHECK(wildBattleMechanic(0, 0, false, false) == BMECH_DYNAMAX);

  if (failures) return 1;
  std::puts("PASS special battle mechanics");
  return 0;
}

#include "Arduino.h"
#include "Preferences.h"
#include "battle.h"
#include "pet.h"
#include <cstdio>

uint32_t g_seed = 307;
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

static MoveId damagingMove(MoveId skip = MOVE_NONE) {
  for (MoveId move = 1; move < moveCount(); move++)
    if (move != skip && moveTracksProgress(move)) return move;
  return MOVE_NONE;
}

static MoveId nonPowerMove() {
  for (MoveId move = 1; move < moveCount(); move++)
    if (!moveTracksProgress(move)) return move;
  return MOVE_NONE;
}

int main() {
  Pet first;
  first.begin();
  MoveId attack = damagingMove();
  MoveId other = damagingMove(attack);
  MoveId status = nonPowerMove();
  ck(moveValid(attack) && moveValid(other) && moveValid(status),
     "the fixture exposes two power moves and one non-power move");

  ck(moveLevelFromProgress(0) == 0 && moveLevelFromProgress(2) == 0 &&
         moveLevelFromProgress(3) == 1 && moveLevelFromProgress(8) == 1 &&
         moveLevelFromProgress(9) == 2 && moveLevelFromProgress(26) == 2 &&
         moveLevelFromProgress(27) == 3 && moveLevelFromProgress(81) == 4,
     "levels change at total-progress thresholds 3^n");
  ck(movePowerBonus(attack, 3) == 1 && movePowerBonus(attack, 27) == 1 &&
         movePowerBonus(attack, 81) == 2 &&
         moveLevelFromProgress(59049) == 10 &&
         movePowerBonus(attack, 59049) == 4,
     "power bonus is ceil(level / 3)");

  first.moves[0] = attack;
  first.moves[1] = other;
  first.moves[2] = status;
  Pet second;
  second.moves[0] = attack;
  first.recordMoveUse(attack, false);
  first.recordMoveUse(attack, false);
  first.recordMoveUse(attack, false);
  ck(first.moveUseCount(attack) == 3 && first.moveLevel(attack) == 1 &&
         first.moveUseCount(other) == 0 && second.moveUseCount(attack) == 0,
     "progress is isolated by creature and move");
  ck(!first.recordMoveUse(status, true) && first.moveUseCount(status) == 0 &&
         first.moveLevel(status) == 0,
     "moves without base power never gain progress or levels");

  PartyMon stored;
  stored.moves[0] = attack;
  stored.recordMoveUse(attack, true);
  ck(stored.moveUseCount(attack) == 6 && stored.moveLevel(attack) == 1,
     "a knockout grants one use plus five bonus progress");

  Combatant attacker, defender;
  attacker.dex = defender.dex = 1;
  attacker.level = defender.level = 50;
  attacker.maxHp = attacker.hp = defender.maxHp = defender.hp = 500;
  for (uint8_t i = 0; i < SI_COUNT; i++)
    attacker.base[i] = defender.base[i] = 100;
  attacker.type1 = defender.type1 = T_NORMAL;
  attacker.type2 = defender.type2 = T_NONE;
  attacker.moves[0] = attack;
  attacker.moveUses[0] = 81;
  BattleMove leveled = battleMoveFor(attacker, attack);
  ck(leveled.levelPowerBonus == 2,
     "battle derives the current move power bonus from stored progress");

  BattleMove strike = leveled;
  strike.entry.cat = MC_PHYS;
  strike.entry.power = 60;
  strike.entry.acc = 0;
  strike.entry.effect = EF_NONE;
  strike.entry.fieldFlags = MF_NONE;
  strike.entry.tags = MT_NONE;
  strike.entry.ailment = AIL_NONE;
  strike.entry.ailChance = 0;
  strike.entry.statMask = 0;
  strike.entry.stages = 0;
  strike.entry.target = TG_FOE;
  TurnLog log;
  BattleField field;
  battleAct(attacker, defender, field, strike, log);
  ck(log.moveUsed && !log.skipped,
     "an actually launched move is marked for progress");

  BattleMove inaccurate = strike;
  inaccurate.entry.acc = 1;
  bool missedAfterLaunch = false;
  for (uint32_t seed = 1; seed <= 64 && !missedAfterLaunch; seed++) {
    g_seed = seed;
    defender.hp = defender.maxHp;
    battleAct(attacker, defender, field, inaccurate, log);
    missedAfterLaunch = log.missed && log.moveUsed;
  }
  Combatant immune = defender;
  immune.type1 = T_GHOST;
  immune.type2 = T_NONE;
  BattleMove normal = strike;
  normal.entry.type = T_NORMAL;
  normal.entry.acc = 0;
  battleAct(attacker, immune, field, normal, log);
  ck(missedAfterLaunch && log.immune && log.moveUsed,
     "misses and type immunities still count as launched moves");

  attacker.ailment = AIL_SLEEP;
  attacker.ailTurns = 2;
  defender.hp = defender.maxHp;
  battleAct(attacker, defender, field, strike, log);
  ck(log.skipped && !log.moveUsed,
     "a turn prevented before launch does not count as a use");

  attacker.ailment = AIL_NONE;
  attacker.ailTurns = 0;
  attacker.charging = 0;
  defender.hp = defender.maxHp;
  BattleMove charge = strike;
  charge.entry.effect = EF_CHARGE;
  battleAct(attacker, defender, field, charge, log);
  bool chargedOnce = log.charged && log.moveUsed;
  battleAct(attacker, defender, field, charge, log);
  ck(chargedOnce && !log.moveUsed,
     "a two-turn charge move records one use for the whole activation");

  attacker.charging = 0;
  defender.hp = 1;
  battleAct(attacker, defender, field, strike, log);
  PartyMon knockout;
  knockout.moves[0] = attack;
  if (log.moveUsed) knockout.recordMoveUse(log.move, log.targetFainted);
  ck(log.targetFainted && knockout.moveUseCount(attack) == 6,
     "a direct move knockout awards the confirmed six total progress");

  return bad ? 1 : 0;
}

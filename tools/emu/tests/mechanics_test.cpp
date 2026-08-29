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
  BattleField field;
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
  CHECK(battleDamage(zUser, target, field, zMove, false, 255) >
        battleDamage(zUser, target, field, normal, false, 255));
  CHECK(zMove.entry.acc == 0 && zMove.entry.ailment == AIL_NONE);
  Combatant another = mon(9, flame);
  CHECK(!battleMechanicAvailable(side, another, BMECH_Z_MOVE, flame));
  CHECK(!battleMechanicAvailable(side, zUser, BMECH_MEGA, flame));

  Combatant maxUser = mon(9, flame);
  maxUser.hp = 40;
  CHECK(battleActivateMechanic(side, maxUser, BMECH_DYNAMAX, flame));
  CHECK(maxUser.hp == 80 && maxUser.maxHp == 200 && maxUser.dynamaxTurns == 3);
  CHECK(battleDynamaxEligible(6));
  CHECK(!battleDynamaxEligible(0));
  CHECK(!battleDynamaxEligible(888) && !battleDynamaxEligible(889) &&
        !battleDynamaxEligible(890));
  BattleSideMechanics forbiddenSide;
  Combatant forbidden = mon(890, flame);
  CHECK(!battleActivateMechanic(forbiddenSide, forbidden, BMECH_DYNAMAX, flame));

  BattleSideMechanics gmaxSide;
  Combatant gmaxUser = mon(6, flame);
  gmaxUser.gigantamaxFactor = true;
  CHECK(battleGigantamaxEligible(6) && !battleGigantamaxEligible(7));
  BattleMove pendingWildfire = battleMoveFor(gmaxUser, flame, BMECH_DYNAMAX);
  BattleMove pendingGuard = battleMoveFor(gmaxUser, recover, BMECH_DYNAMAX);
  CHECK(pendingWildfire.gmaxEffect == GMAX_EFFECT_WILDFIRE &&
        pendingWildfire.mechanic == BMECH_DYNAMAX);
  CHECK(pendingGuard.gmaxMove == GMAX_MOVE_NONE &&
        pendingGuard.entry.effect == EF_PROTECT);
  CHECK(battleActivateMechanic(gmaxSide, gmaxUser, BMECH_DYNAMAX, flame));
  CHECK(gmaxUser.gigantamax);
  BattleMove wildfire = battleMoveFor(gmaxUser, flame);
  CHECK(wildfire.gmaxMove != GMAX_MOVE_NONE &&
        !std::strcmp(gmaxMoveName(wildfire.gmaxMove), "G-Max Wildfire"));
  CHECK(!std::strcmp(maxMoveName(T_FIRE), "Max Flare"));
  CHECK(!std::strcmp(maxMoveName(T_NORMAL, true), "Max Guard"));
  int8_t zhLocale = uiFindLocale("zh-CN");
  CHECK(zhLocale >= 0 && uiActivateLocale((uint8_t)zhLocale));
  CHECK(!std::strcmp(maxMoveName(T_DARK), "极巨恶霸"));
  CHECK(!std::strcmp(maxMoveName(T_NORMAL, true), "极巨防壁"));
  CHECK(!std::strcmp(gmaxMoveName(wildfire.gmaxMove), "超极巨地狱灭焰"));
  int8_t enLocale = uiFindLocale("en-US");
  CHECK(enLocale >= 0 && uiActivateLocale((uint8_t)enLocale));
  CHECK(wildfire.gmaxEffect == GMAX_EFFECT_WILDFIRE);
  CHECK(wildfire.entry.effect == EF_NONE && wildfire.entry.acc == 0);

  Combatant wildfireTarget = mon(9, flame);
  wildfireTarget.maxHp = wildfireTarget.hp = 1000;
  wildfireTarget.type1 = T_NORMAL;
  TurnLog gmaxLog;
  battleAct(gmaxUser, wildfireTarget, field, wildfire, gmaxLog, 100, 0);
  CHECK(field.sides[1].gmaxResidualEffect == GMAX_EFFECT_WILDFIRE &&
        field.sides[1].gmaxResidualTurns == 4);
  TurnLog gmaxEndUser, gmaxEndTarget;
  FieldLog gmaxFieldLog;
  uint16_t beforeResidual = wildfireTarget.hp;
  battleEndRound(field, gmaxUser, wildfireTarget, gmaxEndUser, gmaxEndTarget,
                 gmaxFieldLog);
  CHECK(beforeResidual - wildfireTarget.hp == wildfireTarget.maxHp / 6);

  Combatant fireTarget = wildfireTarget;
  fireTarget.hp = fireTarget.maxHp;
  fireTarget.type1 = T_FIRE;
  battleEndRound(field, gmaxUser, fireTarget, gmaxEndUser, gmaxEndTarget,
                 gmaxFieldLog);
  CHECK(fireTarget.hp == fireTarget.maxHp);

  MoveId water = findMove("SURF");
  BattleMove offType = battleMoveFor(gmaxUser, water);
  CHECK(offType.gmaxMove == GMAX_MOVE_NONE && offType.entry.effect == EF_SET_WEATHER &&
        offType.entry.param == BWEATHER_RAIN);

  bool seenEffects[GMAX_EFFECT_COUNT] = {};
  uint8_t signatureCount = 0;
  for (SpeciesId species = 1; species <= 1025; species++) {
    for (uint8_t type = 0; type < TYPE_COUNT; type++) {
      const GmaxMoveEntry *signature = gmaxMoveFor(species, type);
      if (!signature) continue;
      signatureCount++;
      CHECK(signature->effect > GMAX_EFFECT_NONE &&
            signature->effect < GMAX_EFFECT_COUNT && signature->name &&
            signature->name[0]);
      seenEffects[signature->effect] = true;
    }
  }
  CHECK(signatureCount == 33);
  for (uint8_t effect = 1; effect < GMAX_EFFECT_COUNT; effect++)
    CHECK(seenEffects[effect]);

  Combatant urshifu = mon(892, flame);
  urshifu.gigantamax = true;
  urshifu.activeMechanic = BMECH_DYNAMAX;
  MoveId dark = findMove("CRUNCH");
  BattleMove oneBlow = battleMoveFor(urshifu, dark);
  BattleMove rapidFlow = battleMoveFor(urshifu, water);
  CHECK(oneBlow.gmaxEffect == GMAX_EFFECT_ONE_BLOW &&
        !std::strcmp(gmaxMoveName(oneBlow.gmaxMove), "G-Max One Blow"));
  CHECK(rapidFlow.gmaxEffect == GMAX_EFFECT_RAPID_FLOW &&
        !std::strcmp(gmaxMoveName(rapidFlow.gmaxMove), "G-Max Rapid Flow"));
  Combatant protectedTarget = mon(9, flame);
  protectedTarget.maxHp = protectedTarget.hp = 1000;
  protectedTarget.protectedTurn = true;
  battleAct(urshifu, protectedTarget, field, oneBlow, gmaxLog);
  CHECK(!gmaxLog.missed && gmaxLog.damage && protectedTarget.hp < protectedTarget.maxHp);

  MoveId normalHit = findMove("TACKLE");
  CHECK(normalHit != MOVE_NONE);
  Combatant meowth = mon(52, normalHit);
  meowth.gigantamax = true;
  meowth.activeMechanic = BMECH_DYNAMAX;
  BattleMove goldRush = battleMoveFor(meowth, normalHit);
  Combatant rewardTarget = mon(9, flame);
  rewardTarget.maxHp = rewardTarget.hp = 1000;
  battleAct(meowth, rewardTarget, field, goldRush, gmaxLog);
  CHECK(gmaxLog.bonusRewardItems == 1 && rewardTarget.confuseTurns);

  Combatant snorlax = mon(143, normalHit);
  snorlax.gigantamax = true;
  snorlax.activeMechanic = BMECH_DYNAMAX;
  BattleMove replenish = battleMoveFor(snorlax, normalHit);
  battleAct(snorlax, rewardTarget, field, replenish, gmaxLog);
  CHECK(gmaxLog.restoreLastItem);

  MoveId dragon = findMove("DRAGON CLAW");
  CHECK(dragon != MOVE_NONE);
  Combatant duraludon = mon(884, dragon);
  duraludon.gigantamax = true;
  duraludon.activeMechanic = BMECH_DYNAMAX;
  BattleMove depletion = battleMoveFor(duraludon, dragon);
  Combatant weakened = mon(9, flame);
  weakened.maxHp = weakened.hp = 1000;
  for (uint8_t i = 0; i < 5; i++)
    battleAct(duraludon, weakened, field, depletion, gmaxLog);
  CHECK(weakened.statPercent == 50 && gmaxLog.statsWeakened);

  Combatant cinderace = mon(815, flame);
  cinderace.gigantamax = true;
  cinderace.activeMechanic = BMECH_DYNAMAX;
  BattleMove fireball = battleMoveFor(cinderace, flame);
  Combatant flashFireTarget = mon(9, flame);
  flashFireTarget.maxHp = flashFireTarget.hp = 1000;
  flashFireTarget.ability = ABILITY_FLASH_FIRE;
  battleAct(cinderace, flashFireTarget, field, fireball, gmaxLog);
  CHECK(gmaxLog.damage && !gmaxLog.immune && !flashFireTarget.flashFireActive);

  field = BattleField();
  MoveId ice = findMove("ICE BEAM");
  CHECK(ice != MOVE_NONE);
  Combatant lapras = mon(131, ice);
  lapras.gigantamax = true;
  lapras.activeMechanic = BMECH_DYNAMAX;
  rewardTarget.hp = rewardTarget.maxHp;
  battleAct(lapras, rewardTarget, field, battleMoveFor(lapras, ice), gmaxLog);
  CHECK(field.sides[0].auroraVeilTurns == BATTLE_FIELD_TURNS &&
        gmaxLog.screenSet == BSCREEN_AURORA_VEIL);

  field = BattleField();
  MoveId psychic = findMove("PSYCHIC");
  CHECK(psychic != MOVE_NONE);
  Combatant orbeetle = mon(826, psychic);
  orbeetle.gigantamax = true;
  orbeetle.activeMechanic = BMECH_DYNAMAX;
  rewardTarget.hp = rewardTarget.maxHp;
  battleAct(orbeetle, rewardTarget, field, battleMoveFor(orbeetle, psychic), gmaxLog);
  CHECK(field.gravityTurns == BATTLE_FIELD_TURNS);
  MoveId ground = findMove("EARTHQUAKE");
  CHECK(ground != MOVE_NONE);
  Combatant airborne = mon(12, flame);
  airborne.maxHp = airborne.hp = 1000;
  airborne.type1 = T_FLYING;
  airborne.ability = ABILITY_LEVITATE;
  CHECK(battleGrounded(airborne, &field));
  battleAct(orbeetle, airborne, field, battleMove(ground), gmaxLog);
  CHECK(gmaxLog.damage && !gmaxLog.immune);
  Combatant arenaTrapper = mon(51, flame);
  arenaTrapper.ability = ABILITY_ARENA_TRAP;
  CHECK(battleCanSwitch(airborne, arenaTrapper));
  CHECK(!battleCanSwitch(airborne, arenaTrapper, &field));
  battleEndRound(field, orbeetle, rewardTarget, gmaxEndUser, gmaxEndTarget,
                 gmaxFieldLog);
  CHECK(field.gravityTurns == BATTLE_FIELD_TURNS - 1);

  field = BattleField();
  Combatant sandaconda = mon(844, ground);
  sandaconda.gigantamax = true;
  sandaconda.activeMechanic = BMECH_DYNAMAX;
  rewardTarget.hp = rewardTarget.maxHp;
  battleAct(sandaconda, rewardTarget, field,
            battleMoveFor(sandaconda, ground), gmaxLog);
  CHECK(rewardTarget.bindTurns == 4 &&
        !battleCanSwitch(rewardTarget, sandaconda));

  field = BattleField();
  MoveId steel = findMove("IRON HEAD");
  CHECK(steel != MOVE_NONE);
  Combatant copperajah = mon(879, steel);
  copperajah.gigantamax = true;
  copperajah.activeMechanic = BMECH_DYNAMAX;
  rewardTarget.hp = rewardTarget.maxHp;
  battleAct(copperajah, rewardTarget, field,
            battleMoveFor(copperajah, steel), gmaxLog, 100, 0);
  CHECK(field.sides[1].steelsurge);
  Combatant entrant = mon(9, flame);
  entrant.type1 = T_NORMAL;
  EntryLog entryLog;
  uint16_t beforeEntry = entrant.hp;
  battleOnEnter(entrant, copperajah, field, 1, entryLog);
  CHECK(beforeEntry - entrant.hp == entrant.maxHp / 8 && entryLog.hazardDamage);
  BattleMove maxMove = battleMoveFor(maxUser, flame);
  CHECK(maxMove.mechanic == BMECH_DYNAMAX && maxMove.entry.power > normal.entry.power);
  CHECK(maxMove.entry.effect == EF_SET_WEATHER &&
        maxMove.entry.param == BWEATHER_SUN);
  CHECK(battleDamage(maxUser, target, field, maxMove, false, 255) >
        battleDamage(maxUser, target, field, normal, false, 255));
  TurnLog maxLog;
  battleAct(maxUser, target, field, maxMove, maxLog);
  CHECK(field.weather == BWEATHER_SUN && field.weatherTurns == BATTLE_FIELD_TURNS &&
        maxLog.weatherSet == BWEATHER_SUN);
  field = BattleField();
  const uint8_t maxTypes[] = {
    T_FIRE, T_WATER, T_ROCK, T_ICE, T_ELECTRIC, T_GRASS, T_FAIRY, T_PSYCHIC,
  };
  const uint8_t maxEffects[] = {
    EF_SET_WEATHER, EF_SET_WEATHER, EF_SET_WEATHER, EF_SET_WEATHER,
    EF_SET_TERRAIN, EF_SET_TERRAIN, EF_SET_TERRAIN, EF_SET_TERRAIN,
  };
  const int8_t maxParams[] = {
    BWEATHER_SUN, BWEATHER_RAIN, BWEATHER_SAND, BWEATHER_SNOW,
    BTERRAIN_ELECTRIC, BTERRAIN_GRASSY, BTERRAIN_MISTY, BTERRAIN_PSYCHIC,
  };
  Combatant mappingUser = mon(9, flame);
  mappingUser.activeMechanic = BMECH_DYNAMAX;
  for (uint8_t i = 0; i < sizeof(maxTypes); i++) {
    MoveId typed = MOVE_NONE;
    for (MoveId move = 1; move < moveCount(); move++)
      if (moveEntry(move).type == maxTypes[i] && moveEntry(move).cat != MC_STATUS) {
        typed = move;
        break;
      }
    CHECK(typed != MOVE_NONE);
    BattleMove mapped = battleMoveFor(mappingUser, typed);
    CHECK(mapped.entry.effect == maxEffects[i] && mapped.entry.param == maxParams[i]);
  }
  battleAfterAction(maxUser);
  battleAfterAction(maxUser);
  CHECK(maxUser.activeMechanic == BMECH_DYNAMAX && maxUser.dynamaxTurns == 1);
  battleAfterAction(maxUser);
  CHECK(maxUser.activeMechanic == BMECH_NONE && maxUser.hp == 40 && maxUser.maxHp == 100);

  BattleSideMechanics switchSide;
  Combatant switched = mon(9, flame);
  Combatant switchOpponent = mon(133, flame);
  switched.hp = 25;
  switched.infatuated = true;
  switchOpponent.infatuated = true;
  CHECK(battleActivateMechanic(switchSide, switched, BMECH_DYNAMAX, flame));
  battleOnSwitchOut(switched, &switchOpponent);
  CHECK(switched.activeMechanic == BMECH_NONE && switched.hp == 25 &&
        switched.maxHp == 100 && !switched.infatuated &&
        !switchOpponent.infatuated);

  Combatant megaUser = mon(6, flame);
  CHECK(battleMegaEligible(megaUser.dex, MEGA_FORM_X));
  CHECK(battleMegaEligible(megaUser.dex, MEGA_FORM_Y));
  CHECK(!battleMegaEligible(megaUser.dex, MEGA_FORM_STANDARD));
  CHECK(battleMegaEligible(719));
  CHECK(battleMegaEligible(998));
  const MegaFormEntry *charizardMega = megaFormFor(megaUser.dex, MEGA_FORM_X);
  uint16_t expectedMegaAtk = dexValid(megaUser.dex)
      ? (uint16_t)(100 + charizardMega->bAtk - dexEntry(megaUser.dex).bAtk)
      : 120;
  CHECK(!battleActivateMechanic(side, megaUser, BMECH_MEGA, flame,
                                MEGA_FORM_STANDARD));
  CHECK(battleActivateMechanic(side, megaUser, BMECH_MEGA, flame, MEGA_FORM_X));
  CHECK(megaUser.activeMechanic == BMECH_MEGA &&
        megaUser.base[SI_ATK] == expectedMegaAtk);
  CHECK(megaUser.type1 == T_FIRE && megaUser.type2 == T_DRAGON);
  CHECK(megaUser.megaForm == MEGA_FORM_X);
  CHECK(side.used(BMECH_Z_MOVE) && side.used(BMECH_DYNAMAX) && side.used(BMECH_MEGA));

  BattleSideMechanics ySide;
  Combatant charizardY = mon(6, flame);
  CHECK(battleActivateMechanic(ySide, charizardY, BMECH_MEGA, flame, MEGA_FORM_Y));
  const MegaFormEntry *charizardYMega = megaFormFor(6, MEGA_FORM_Y);
  CHECK(charizardY.type1 == T_FIRE && charizardY.type2 == T_FLYING &&
        charizardYMega && charizardYMega->bSpA > charizardMega->bSpA);

  BattleSideMechanics singleSide;
  Combatant venusaur = mon(3, flame);
  CHECK(battleActivateMechanic(singleSide, venusaur, BMECH_MEGA, flame,
                               MEGA_FORM_STANDARD));
  CHECK(!battleMegaEligible(3, MEGA_FORM_X));

  BattleSideMechanics guardSide;
  Combatant guard = mon(9, recover);
  Combatant attacker = mon(6, flame);
  CHECK(battleActivateMechanic(guardSide, guard, BMECH_DYNAMAX, recover));
  BattleMove guardMove = battleMoveFor(guard, recover);
  CHECK(guardMove.entry.effect == EF_PROTECT);
  CHECK(battleMovesFirst(guard, guardMove, attacker, normal));
  TurnLog log;
  battleAct(guard, attacker, field, guardMove, log);
  uint16_t hpBefore = guard.hp;
  battleAct(attacker, guard, field, normal, log);
  CHECK(log.missed && guard.hp == hpBefore);

  if (failures) return 1;
  std::puts("PASS special battle mechanics");
  return 0;
}

#include "Arduino.h"
#include "Preferences.h"
#include "battle.h"
#include "content.h"
#include "types.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 0xF13D;
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

static Combatant mon(uint8_t type1, uint8_t type2 = T_NONE) {
  Combatant c;
  c.level = 50;
  c.maxHp = c.hp = 160;
  for (uint8_t i = 0; i < SI_COUNT; i++) c.base[i] = 100;
  c.type1 = type1;
  c.type2 = type2;
  return c;
}

static BattleMove move(uint8_t type, uint8_t category, uint8_t power,
                       uint8_t effect = EF_NONE, int8_t param = 0,
                       uint8_t fieldFlags = MF_NONE) {
  BattleMove result;
  result.source = 1;
  result.entry.type = type;
  result.entry.cat = category;
  result.entry.power = power;
  result.entry.acc = 0;
  result.entry.effect = effect;
  result.entry.param = param;
  result.entry.target = TG_FOE;
  result.entry.fieldFlags = fieldFlags;
  result.entry.tags = MT_NONE;
  return result;
}

static MoveId findMove(const char *name) {
  for (MoveId id = 1; id < moveCount(); id++)
    if (!std::strcmp(moveEntry(id).name, name)) return id;
  return MOVE_NONE;
}

int main() {
  TurnLog log;
  contentBegin();
  MoveId thunderId = findMove("THUNDER");
  MoveId blizzardId = findMove("BLIZZARD");
  MoveId solarId = findMove("SOLAR BEAM");
  MoveId quakeId = findMove("EARTHQUAKE");
  MoveId sunnyDayId = findMove("SUNNY DAY");
  MoveId closeCombatId = findMove("CLOSE COMBAT");
  MoveId emberId = findMove("EMBER");
  MoveId tackleId = findMove("TACKLE");
  MoveId growlId = findMove("GROWL");
  MoveId firePunchId = findMove("FIRE PUNCH");
  MoveId darkPulseId = findMove("DARK PULSE");
  MoveId shadowBallId = findMove("SHADOW BALL");
  MoveId razorLeafId = findMove("RAZOR LEAF");
  ck(thunderId && (moveEntry(thunderId).fieldFlags & MF_RAIN_ACCURATE),
     "the move pack marks Thunder as rain-accurate");
  ck(blizzardId && (moveEntry(blizzardId).fieldFlags & MF_SNOW_ACCURATE),
     "the move pack marks Blizzard as snow-accurate");
  ck(solarId && (moveEntry(solarId).fieldFlags & MF_SOLAR_CHARGE),
     "the move pack marks Solar Beam as weather-sensitive");
  ck(quakeId && (moveEntry(quakeId).fieldFlags & MF_GRASSY_WEAKENED),
     "the move pack marks Earthquake as weakened by Grassy Terrain");
  ck(tackleId && (moveEntry(tackleId).tags & MT_CONTACT),
     "the move pack marks contact moves");
  ck(growlId && (moveEntry(growlId).tags & MT_SOUND) &&
     (moveEntry(growlId).tags & MT_REFLECTABLE),
     "the move pack can combine sound and reflectable tags");
  ck(firePunchId && (moveEntry(firePunchId).tags & (MT_CONTACT | MT_PUNCH)) ==
     (MT_CONTACT | MT_PUNCH), "the move pack marks punching contact moves");
  ck(darkPulseId && (moveEntry(darkPulseId).tags & MT_PULSE),
     "the move pack marks pulse moves");
  ck(shadowBallId && (moveEntry(shadowBallId).tags & MT_BALLISTIC),
     "the move pack marks ballistic moves");
  ck(razorLeafId && (moveEntry(razorLeafId).tags & MT_SLICING),
     "the move pack includes supplemental slicing tags");
  ck(sunnyDayId > closeCombatId && moveEntry(sunnyDayId).effect == EF_SET_WEATHER,
     "field setters are appended after the stable move ABI boundary");
  BattleField setterField;
  Combatant setterUser = mon(T_FIRE), setterTarget = mon(T_WATER);
  battleAct(setterUser, setterTarget, setterField, sunnyDayId, log);
  ck(setterField.weather == BWEATHER_SUN &&
     setterField.weatherTurns == BATTLE_FIELD_TURNS &&
     log.weatherSet == BWEATHER_SUN,
     "a generated weather-setting move changes the battle field");

  Combatant ai = mon(T_FIRE), waterFoe = mon(T_WATER);
  ai.moves[0] = sunnyDayId;
  ai.moves[1] = emberId;
  BattleField aiField;
  ck(aiChooseMove(ai, waterFoe, aiField, true) == sunnyDayId,
     "smart AI can prefer weather setup when it improves a weak matchup");
  battleSetWeather(aiField, BWEATHER_SUN);
  ck(aiChooseMove(ai, waterFoe, aiField, true) == emberId,
     "smart AI avoids repeating an active weather setter");
  Combatant normal = mon(T_NORMAL), flying = mon(T_FLYING);
  Combatant levitating = normal;
  levitating.ability = ABILITY_LEVITATE;
  ck(battleGrounded(normal) && !battleGrounded(flying) &&
     !battleGrounded(levitating),
     "Flying type and Levitate are airborne");

  BattleField field;
  battleSetEnvironment(field, BWEATHER_RAIN, BTERRAIN_ELECTRIC);
  ck(field.weather == BWEATHER_RAIN && field.weatherTurns == 0 &&
     field.terrain == BTERRAIN_ELECTRIC && field.terrainTurns == 0,
     "wild environment is a persistent field baseline");
  battleSetWeather(field, BWEATHER_SUN);
  battleSetTerrain(field, BTERRAIN_GRASSY);
  ck(field.weather == BWEATHER_SUN && field.weatherTurns == 5 &&
     field.terrain == BTERRAIN_GRASSY && field.terrainTurns == 5,
     "move-created weather and terrain cover the baseline for five turns");

  TurnLog aLog, bLog;
  FieldLog fieldLog;
  Combatant a = normal, b = normal;
  for (int i = 0; i < 4; i++) battleEndRound(field, a, b, aLog, bLog, fieldLog);
  ck(field.weather == BWEATHER_SUN && field.weatherTurns == 1 &&
     field.terrain == BTERRAIN_GRASSY && field.terrainTurns == 1,
     "temporary effects remain through their fourth completed round");
  battleEndRound(field, a, b, aLog, bLog, fieldLog);
  ck(field.weather == BWEATHER_RAIN && !field.weatherTurns &&
     field.terrain == BTERRAIN_ELECTRIC && !field.terrainTurns &&
     fieldLog.weatherExpired == BWEATHER_SUN &&
     fieldLog.weatherRestored == BWEATHER_RAIN,
     "the fifth round expires the cover and restores the wild baseline");
  battleSetWeather(field, BWEATHER_RAIN);
  for (int i = 0; i < 5; i++) battleEndRound(field, a, b, aLog, bLog, fieldLog);
  ck(field.weather == BWEATHER_RAIN && !field.weatherTurns &&
     fieldLog.weatherExpired == BWEATHER_NONE &&
     fieldLog.weatherRestored == BWEATHER_NONE,
     "a temporary effect matching its baseline expires without a false transition");

  BattleField clear;
  Combatant fire = mon(T_FIRE), neutralTarget = mon(TYPE_COUNT);
  BattleMove flame = move(T_FIRE, MC_SPEC, 80);
  uint16_t plainFire = battleDamage(fire, neutralTarget, clear, flame, false, 255);
  battleSetEnvironment(clear, BWEATHER_SUN);
  uint16_t sunnyFire = battleDamage(fire, neutralTarget, clear, flame, false, 255);
  battleSetEnvironment(clear, BWEATHER_RAIN);
  uint16_t rainyFire = battleDamage(fire, neutralTarget, clear, flame, false, 255);
  ck(sunnyFire > plainFire && rainyFire < plainFire,
     "sun boosts Fire damage and rain weakens it");
  Combatant water = mon(T_WATER);
  BattleMove wave = move(T_WATER, MC_SPEC, 80);
  uint16_t rainyWater = battleDamage(water, neutralTarget, clear, wave, false, 255);
  battleSetEnvironment(clear, BWEATHER_SUN);
  uint16_t sunnyWater = battleDamage(water, neutralTarget, clear, wave, false, 255);
  battleSetEnvironment(clear, BWEATHER_NONE);
  uint16_t plainWater = battleDamage(water, neutralTarget, clear, wave, false, 255);
  ck(rainyWater > plainWater && sunnyWater < plainWater,
     "rain boosts Water damage and sun weakens it");

  BattleMove solar = move(T_GRASS, MC_SPEC, 120, EF_CHARGE, 0, MF_SOLAR_CHARGE);
  Combatant solarUser = mon(T_GRASS), solarTarget = neutralTarget;
  clear = BattleField();
  battleAct(solarUser, solarTarget, clear, solar, log);
  ck(log.charged && !log.damage, "Solar Beam charges in clear weather");
  solarUser.charging = 0;
  solarTarget.hp = solarTarget.maxHp;
  battleSetEnvironment(clear, BWEATHER_SUN);
  battleAct(solarUser, solarTarget, clear, solar, log);
  ck(!log.charged && log.damage, "sun lets Solar Beam attack immediately");

  BattleMove thunder = move(T_ELECTRIC, MC_SPEC, 40, EF_NONE, 0, MF_RAIN_ACCURATE);
  thunder.entry.acc = 1;
  clear = BattleField();
  bool clearMiss = false;
  for (int i = 0; i < 20 && !clearMiss; i++) {
    solarTarget.hp = solarTarget.maxHp;
    battleAct(normal, solarTarget, clear, thunder, log);
    clearMiss = log.missed;
  }
  battleSetEnvironment(clear, BWEATHER_RAIN);
  bool rainMiss = false;
  for (int i = 0; i < 20; i++) {
    solarTarget.hp = solarTarget.maxHp;
    battleAct(normal, solarTarget, clear, thunder, log);
    rainMiss |= log.missed;
  }
  ck(clearMiss && !rainMiss, "rain makes a weather-accurate move never miss");

  BattleMove blizzard = move(T_ICE, MC_SPEC, 40, EF_NONE, 0, MF_SNOW_ACCURATE);
  blizzard.entry.acc = 1;
  clear = BattleField();
  bool snowMoveClearMiss = false;
  for (int i = 0; i < 20 && !snowMoveClearMiss; i++) {
    solarTarget.hp = solarTarget.maxHp;
    battleAct(normal, solarTarget, clear, blizzard, log);
    snowMoveClearMiss = log.missed;
  }
  battleSetEnvironment(clear, BWEATHER_SNOW);
  bool snowMoveMiss = false;
  for (int i = 0; i < 20; i++) {
    solarTarget.hp = solarTarget.maxHp;
    battleAct(normal, solarTarget, clear, blizzard, log);
    snowMoveMiss |= log.missed;
  }
  ck(snowMoveClearMiss && !snowMoveMiss,
     "snow makes a snow-accurate move never miss");

  BattleField electric;
  battleSetEnvironment(electric, BWEATHER_NONE, BTERRAIN_ELECTRIC);
  BattleMove spark = move(T_ELECTRIC, MC_SPEC, 80);
  uint16_t electricGrounded = battleDamage(normal, neutralTarget, electric, spark, false, 255);
  uint16_t electricAirborne = battleDamage(flying, neutralTarget, electric, spark, false, 255);
  ck(electricGrounded > electricAirborne,
     "terrain damage boost applies only to a grounded attacker");

  BattleField mistyDamage;
  battleSetEnvironment(mistyDamage, BWEATHER_NONE, BTERRAIN_MISTY);
  BattleMove dragon = move(T_DRAGON, MC_SPEC, 80);
  uint16_t plainDragon = battleDamage(normal, neutralTarget, BattleField(), dragon, false, 255);
  uint16_t mistyDragon = battleDamage(normal, neutralTarget, mistyDamage, dragon, false, 255);
  ck(mistyDragon < plainDragon, "Misty Terrain halves Dragon damage to a grounded target");

  BattleField grassyDamage;
  battleSetEnvironment(grassyDamage, BWEATHER_NONE, BTERRAIN_GRASSY);
  BattleMove quake = move(T_GROUND, MC_PHYS, 100, EF_NONE, 0, MF_GRASSY_WEAKENED);
  uint16_t plainQuake = battleDamage(normal, neutralTarget, BattleField(), quake, false, 255);
  uint16_t grassyQuake = battleDamage(normal, neutralTarget, grassyDamage, quake, false, 255);
  ck(grassyQuake < plainQuake, "Grassy Terrain halves flagged ground-shaking damage");

  BattleField psychic;
  battleSetEnvironment(psychic, BWEATHER_NONE, BTERRAIN_PSYCHIC);
  BattleMove priority = move(T_NORMAL, MC_PHYS, 40, EF_PRIORITY, 1);
  battleAct(normal, a, psychic, priority, log);
  ck(log.blockedByField && !log.damage,
     "Psychic Terrain blocks a priority attack against a grounded target");
  a = flying;
  battleAct(normal, a, psychic, priority, log);
  ck(!log.blockedByField,
     "Psychic Terrain does not protect an airborne target");

  BattleField misty;
  battleSetEnvironment(misty, BWEATHER_NONE, BTERRAIN_MISTY);
  BattleMove burn = move(T_FIRE, MC_SPEC, 40);
  burn.entry.ailment = AIL_BURN;
  burn.entry.ailChance = 100;
  a = neutralTarget;
  battleAct(fire, a, misty, burn, log);
  ck(a.ailment == AIL_NONE,
     "Misty Terrain prevents a grounded target from receiving an ailment");

  BattleMove sleep = move(T_PSYCHIC, MC_SPEC, 40);
  sleep.entry.ailment = AIL_SLEEP;
  sleep.entry.ailChance = 100;
  a = neutralTarget;
  battleAct(normal, a, electric, sleep, log);
  ck(a.ailment == AIL_NONE, "Electric Terrain prevents grounded sleep");
  a = flying;
  battleAct(normal, a, electric, sleep, log);
  ck(a.ailment == AIL_SLEEP, "Electric Terrain does not prevent airborne sleep");

  BattleMove confuse = move(T_PSYCHIC, MC_SPEC, 40);
  confuse.entry.ailment = AIL_CONFUSE;
  confuse.entry.ailChance = 100;
  a = neutralTarget;
  battleAct(normal, a, misty, confuse, log);
  ck(!a.confuseTurns, "Misty Terrain prevents grounded confusion");

  BattleMove freeze = move(T_ICE, MC_SPEC, 40);
  freeze.entry.ailment = AIL_FREEZE;
  freeze.entry.ailChance = 100;
  BattleField sun;
  battleSetEnvironment(sun, BWEATHER_SUN);
  a = neutralTarget;
  battleAct(normal, a, sun, freeze, log);
  ck(a.ailment == AIL_NONE, "sun prevents a target from being frozen");

  BattleField grassy;
  battleSetEnvironment(grassy, BWEATHER_NONE, BTERRAIN_GRASSY);
  a = normal; b = normal;
  a.hp = b.hp = 80;
  battleEndRound(grassy, a, b, aLog, bLog, fieldLog);
  ck(a.hp == 90 && b.hp == 90 && aLog.healed && bLog.healed,
     "Grassy Terrain heals each grounded combatant by one sixteenth");
  a = flying; b = normal;
  a.hp = b.hp = 80;
  battleEndRound(grassy, a, b, aLog, bLog, fieldLog);
  ck(a.hp == 80 && b.hp == 90,
     "Grassy Terrain does not heal an airborne combatant");

  BattleField sand;
  battleSetEnvironment(sand, BWEATHER_SAND);
  a = normal; b = mon(T_ROCK);
  battleEndRound(sand, a, b, aLog, bLog, fieldLog);
  ck(a.hp == 150 && b.hp == 160,
     "sand chips ordinary combatants but not Rock types");
  a = mon(T_GROUND); b = mon(T_STEEL);
  battleEndRound(sand, a, b, aLog, bLog, fieldLog);
  ck(a.hp == 160 && b.hp == 160,
     "Ground and Steel types also ignore sand chip damage");

  BattleMove special = move(T_NORMAL, MC_SPEC, 80);
  BattleMove physical = move(T_NORMAL, MC_PHYS, 80);
  Combatant rock = mon(T_ROCK), ice = mon(T_ICE);
  uint16_t rockClear = battleDamage(normal, rock, BattleField(), special, false, 255);
  uint16_t iceClear = battleDamage(normal, ice, BattleField(), physical, false, 255);
  uint16_t rockSand = battleDamage(normal, rock, sand, special, false, 255);
  BattleField snowField;
  battleSetEnvironment(snowField, BWEATHER_SNOW);
  uint16_t iceSnow = battleDamage(normal, ice, snowField, physical, false, 255);
  ck(rockSand < rockClear, "sand raises a Rock type's special defense");
  ck(iceSnow < iceClear, "snow raises an Ice type's physical defense");

  return bad ? 1 : 0;
}

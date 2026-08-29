// Exercises the real battle engine: damage, type chart, STAB, stat stages,
// ailments, turn order, and a full fight to a KO.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "dex.h"
#include "types.h"
#include <cstdio>

uint32_t g_seed = 12345;
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
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static void mk(Combatant &c, int16_t dex, uint8_t lvl) {
  Pet p;
  p.dbgHatchAs(dex, false);
  p.ivAtk = p.ivDef = p.ivSpe = p.ivHp = 31;
  p.ageMinutes = (uint32_t)(lvl - 1) * MINUTES_PER_LEVEL;
  p.relearnFromLevel();
  combatantFromPet(c, p);
}

static uint8_t findMove(const char *name) {
  for (uint8_t i = 1; i < moveCount(); i++)
    if (!strcmp(moveEntry(i).name, name)) return i;
  return 0;
}

int main() {
  BattleField field;
  // --- stat stages: the series' own fractions
  ck(stagedStat(100, 0) == 100, "stage 0 is 1x");
  ck(stagedStat(100, 2) == 200, "stage +2 is 2x (SWORDS DANCE)");
  ck(stagedStat(100, 6) == 400, "stage +6 is 4x");
  ck(stagedStat(100, -1) == 66, "stage -1 is 2/3 (GROWL)");
  ck(stagedStat(100, -6) == 25, "stage -6 is 1/4");

  // --- type chart drives damage
  Combatant zard, blast, venu;
  mk(zard, 6, 50); mk(blast, 9, 50); mk(venu, 3, 50);
  uint8_t flame = findMove("FLAMETHROWER"), surf = findMove("SURF");
  uint16_t vsGrass = battleDamage(zard, venu, field, flame, false, 255);
  uint16_t vsWater = battleDamage(zard, blast, field, flame, false, 255);
  printf("     FLAMETHROWER: vs VENUSAUR %u, vs BLASTOISE %u\n", vsGrass, vsWater);
  ck(vsGrass > vsWater * 2, "fire hits grass far harder than water");

  // --- the local answer ratio scales the final move, not the base stat input
  Combatant fullAtk = zard, fullDef = venu, halfAtk = zard, halfDef = venu;
  TurnLog fullLog, halfLog, failedLog;
  g_seed = 0xB4771E;
  battleAct(fullAtk, fullDef, field, flame, fullLog, 100);
  g_seed = 0xB4771E;
  battleAct(halfAtk, halfDef, field, flame, halfLog, 50);
  ck(fullLog.damage > 0 && halfLog.damage == (fullLog.damage * 50 + 50) / 100,
     "a 50 percent answer scales final attack damage to 50 percent");
  Combatant failedAtk = zard, failedDef = venu;
  battleAct(failedAtk, failedDef, field, flame, failedLog, 0);
  ck(failedLog.missed && failedLog.damage == 0 && failedDef.hp == failedDef.maxHp,
     "a failed answer makes a damaging move fail completely");

  Combatant multiFullAtk, multiFullDef, multiThirdAtk, multiThirdDef;
  TurnLog multiFullLog, multiThirdLog;
  for (uint32_t seed = 1; seed < 100 && multiFullLog.hits < 2; seed++) {
    multiFullAtk = multiThirdAtk = zard;
    multiFullDef = multiThirdDef = venu;
    multiFullDef.maxHp = multiFullDef.hp = 60000;
    multiThirdDef.maxHp = multiThirdDef.hp = 60000;
    g_seed = seed;
    battleAct(multiFullAtk, multiFullDef, field, findMove("FURY ATTACK"), multiFullLog, 100);
    if (multiFullLog.hits < 2) continue;
    g_seed = seed;
    battleAct(multiThirdAtk, multiThirdDef, field, findMove("FURY ATTACK"), multiThirdLog, 33);
  }
  printf("     FURY ATTACK: full %u in %u hits, 33%% %u in %u hits\n",
         multiFullLog.damage, multiFullLog.hits, multiThirdLog.damage, multiThirdLog.hits);
  ck(multiFullLog.hits > 1 && multiThirdLog.hits == multiFullLog.hits &&
     multiThirdLog.damage == (multiFullLog.damage * 33 + 50) / 100,
     "a multi-hit move rounds the scaled final total only once");

  // --- SWORDS DANCE really doubles physical output
  Combatant m1, m2;
  mk(m1, 68, 50); mk(m2, 68, 50);
  uint8_t chop = findMove("KARATE CHOP");
  uint16_t before = battleDamage(m1, m2, field, chop, false, 255);
  TurnLog lg;
  battleAct(m1, m2, field, findMove("SWORDS DANCE"), lg, 0);
  ck(m1.stage[SI_ATK] == 0 && lg.missed,
     "a failed answer also blocks a status move");
  battleAct(m1, m2, field, findMove("SWORDS DANCE"), lg, 17);
  uint16_t after = battleDamage(m1, m2, field, chop, false, 255);
  printf("     KARATE CHOP before %u, after SWORDS DANCE %u (stage %d)\n",
         before, after, m1.stage[SI_ATK]);
  ck(m1.stage[SI_ATK] == 2, "SWORDS DANCE sets +2 ATK");
  ck(after > before * 18 / 10, "and roughly doubles damage");

  Combatant healer = zard, healTarget = venu;
  healer.hp = healer.maxHp / 4;
  uint16_t hurtHp = healer.hp;
  battleAct(healer, healTarget, field, findMove("RECOVER"), lg, 0);
  ck(healer.hp == hurtHp && lg.missed,
     "a failed answer also blocks a healing move");
  battleAct(healer, healTarget, field, findMove("RECOVER"), lg, 17);
  ck(healer.hp == hurtHp + healer.maxHp / 2 && lg.healed,
     "a correct healing move keeps its full effect at any positive stage");
  healer.hp = healer.maxHp;
  battleAct(healer, healTarget, field, findMove("RECOVER"), lg);
  ck(!lg.healed, "healing feedback is omitted when no HP changed");

  // --- GROWL lowers the FOE, not the user
  Combatant g1, g2;
  mk(g1, 6, 50); mk(g2, 9, 50);
  battleAct(g1, g2, field, findMove("GROWL"), lg);
  ck(g2.stage[SI_ATK] == -1 && g1.stage[SI_ATK] == 0, "GROWL lowers the target's ATK only");
  g2.stage[SI_ATK] = -6;
  battleAct(g1, g2, field, findMove("GROWL"), lg);
  ck(g2.stage[SI_ATK] == -6 && lg.stageMask == 0,
     "stage feedback is omitted when the target is already at the limit");

  // --- DRAGON DANCE moves two stats at once
  Combatant d1, d2;
  mk(d1, 6, 50); mk(d2, 9, 50);
  battleAct(d1, d2, field, findMove("DRAGON DANCE"), lg);
  ck(d1.stage[SI_ATK] == 1 && d1.stage[SI_SPE] == 1, "DRAGON DANCE raises ATK and SPE");

  // --- damaging EF_STAGE moves apply their secondary stat change
  Combatant sb1, sb2;
  mk(sb1, 12, 50); mk(sb2, 65, 50);
  battleAct(sb1, sb2, field, findMove("STRUGGLE BUG"), lg);
  ck(lg.damage > 0 && sb2.stage[SI_SPA] == -1 && sb1.stage[SI_SPA] == 0 &&
     lg.stageMask == ST_SPA && lg.stageDelta == -1,
     "STRUGGLE BUG damages and lowers the target's Sp. Atk");

  Combatant sn1, sn2;
  mk(sn1, 491, 50); mk(sn2, 65, 50);
  battleAct(sn1, sn2, field, findMove("SNARL"), lg);
  ck(lg.damage > 0 && sn2.stage[SI_SPA] == -1 && sn1.stage[SI_SPA] == 0,
     "SNARL damages and lowers the target's Sp. Atk");

  Combatant cc1, cc2;
  mk(cc1, 68, 50); mk(cc2, 143, 50);
  battleAct(cc1, cc2, field, findMove("CLOSE COMBAT"), lg);
  ck(lg.damage > 0 && cc1.stage[SI_DEF] == -1 && cc1.stage[SI_SPD] == -1 &&
     cc2.stage[SI_DEF] == 0 && cc2.stage[SI_SPD] == 0 &&
     lg.stageMask == (ST_DEF | ST_SPD) && lg.stageDelta == -1,
     "CLOSE COMBAT damages and lowers the user's defenses");

  // --- turn order follows speed, and priority beats it
  Combatant fast, slow;
  mk(fast, 65, 50);   // Alakazam, 120 base speed
  mk(slow, 143, 50);  // Snorlax, 30 base speed
  ck(battleMovesFirst(fast, flame, slow, flame), "the faster creature acts first");
  uint8_t quick = findMove("QUICK ATTACK");
  ck(battleMovesFirst(slow, quick, fast, flame), "QUICK ATTACK beats raw speed");

  // --- paralysis halves speed
  Combatant p1, p2;
  mk(p1, 65, 50); mk(p2, 65, 50);
  p1.ailment = AIL_PARA;
  ck(!battleMovesFirst(p1, flame, p2, flame), "paralysis loses the speed tie");

  // --- a fire type cannot be burned
  Combatant f1, f2;
  mk(f1, 9, 50); mk(f2, 6, 50);   // Blastoise attacking Charizard with fire
  bool burned = false;
  for (int i = 0; i < 400 && !burned; i++) {
    f2.ailment = AIL_NONE;
    f2.hp = f2.maxHp;
    battleAct(f1, f2, field, flame, lg);
    if (f2.ailment == AIL_BURN) burned = true;
  }
  ck(!burned, "a FIRE type never catches a burn");

  // --- burn chips and halves physical attack
  Combatant b1, b2;
  mk(b1, 68, 50); mk(b2, 68, 50);
  b1.ability = ABILITY_NONE;
  uint16_t clean = battleDamage(b1, b2, field, chop, false, 255);
  b1.ailment = AIL_BURN;
  uint16_t burnt = battleDamage(b1, b2, field, chop, false, 255);
  ck(burnt < clean * 6 / 10, "burn roughly halves physical damage");
  b1.ability = ABILITY_GUTS;
  uint16_t guts = battleDamage(b1, b2, field, chop, false, 255);
  ck(guts > clean, "Guts turns a burn into an Attack boost instead of a penalty");
  uint16_t hpWas = b1.hp;
  TurnLog endA, endB;
  FieldLog endField;
  battleEndRound(field, b1, b2, endA, endB, endField);
  ck(b1.hp < hpWas, "burn chips at end of turn");

  // --- immunity: no damage at all, not chip
  Combatant gh, norm;
  mk(gh, 94, 50);    // Gengar, Ghost
  mk(norm, 143, 50); // Snorlax, Normal
  uint8_t slam = findMove("BODY SLAM");
  ck(battleDamage(norm, gh, field, slam, false, 255) == 0, "NORMAL does nothing to a GHOST");

  // --- the first ability hook batch changes real resolver paths
  Combatant power = norm;
  uint16_t ordinaryAtk = battleEffectiveStat(power, SI_ATK);
  power.ability = ABILITY_HUGE_POWER;
  ck(battleEffectiveStat(power, SI_ATK) == ordinaryAtk * 2,
     "Huge Power doubles the resolver's physical Attack");

  Combatant earthAtk = norm, floating = venu;
  floating.ability = ABILITY_LEVITATE;
  uint8_t earthquake = findMove("EARTHQUAKE");
  ck(earthquake && battleDamage(earthAtk, floating, field, earthquake, false, 255) == 0,
     "Levitate grants Ground immunity in the type-effectiveness hook");

  Combatant waterAtk = blast, absorber = venu;
  absorber.ability = ABILITY_WATER_ABSORB;
  absorber.hp = absorber.maxHp / 2;
  uint16_t absorberHp = absorber.hp;
  battleAct(waterAtk, absorber, field, surf, lg);
  ck(lg.immune && lg.healed && absorber.hp > absorberHp,
     "Water Absorb nullifies a Water move and restores HP");

  Combatant blazeAtk = zard, blazeDef = venu;
  blazeAtk.hp = blazeAtk.maxHp / 3;
  blazeAtk.ability = ABILITY_NONE;
  uint16_t withoutBlaze = battleDamage(blazeAtk, blazeDef, field, flame, false, 255);
  blazeAtk.ability = ABILITY_BLAZE;
  uint16_t withBlaze = battleDamage(blazeAtk, blazeDef, field, flame, false, 255);
  ck(withBlaze > withoutBlaze * 14 / 10,
     "Blaze boosts Fire damage at one-third HP");

  Combatant sturdyAtk = zard, sturdyDef = venu;
  sturdyAtk.level = 100;
  sturdyAtk.base[SI_SPA] = 1000;
  sturdyDef.maxHp = sturdyDef.hp = 10;
  sturdyDef.ability = ABILITY_STURDY;
  BattleMove sureFlame = battleMove(flame);
  sureFlame.entry.acc = 0;
  battleAct(sturdyAtk, sturdyDef, field, sureFlame, lg);
  ck(sturdyDef.hp == 1,
     "Sturdy keeps a full-health target at one HP after a lethal single hit");

  Combatant statusAtk = blast, statusDef = norm;
  statusDef.ability = ABILITY_WATER_VEIL;
  BattleMove burning = battleMove(flame);
  burning.entry.acc = 0;
  burning.entry.ailment = AIL_BURN;
  burning.entry.ailChance = 100;
  battleAct(statusAtk, statusDef, field, burning, lg);
  ck(statusDef.ailment == AIL_NONE,
     "Water Veil blocks burn in the ailment hook");

  Combatant rainDish = blast, dry = venu;
  rainDish.ability = ABILITY_RAIN_DISH;
  rainDish.hp = rainDish.maxHp / 2;
  uint16_t beforeRain = rainDish.hp;
  BattleField rainy;
  battleSetEnvironment(rainy, BWEATHER_RAIN);
  battleEndRound(rainy, rainDish, dry, endA, endB, endField);
  ck(rainDish.hp > beforeRain && endA.healed,
     "Rain Dish heals through the centralized end-of-round hook");

  // --- ability effect families that reuse the existing resolver boundaries
  Combatant techAtk = zard, techDef = venu;
  BattleMove weakFire = battleMove(flame);
  weakFire.entry.power = 60;
  techAtk.ability = ABILITY_NONE;
  uint16_t plainWeak = battleDamage(techAtk, techDef, field, weakFire, false, 255);
  techAtk.ability = ABILITY_TECHNICIAN;
  ck(battleDamage(techAtk, techDef, field, weakFire, false, 255) > plainWeak * 14 / 10,
     "Technician boosts moves at the sixty-power boundary");

  Combatant wonderAtk = norm, wonderDef = venu;
  wonderDef.ability = ABILITY_WONDER_GUARD;
  ck(battleDamage(wonderAtk, wonderDef, field, slam, false, 255) == 0,
     "Wonder Guard rejects a damaging move that is not super effective");

  Combatant normalizer = zard;
  normalizer.ability = ABILITY_NORMALIZE;
  ck(battleMoveFor(normalizer, flame).entry.type == T_NORMAL,
     "Normalize changes the resolved move type");

  Combatant contraryAtk = zard, contraryDef = venu;
  contraryDef.ability = ABILITY_CONTRARY;
  battleAct(contraryAtk, contraryDef, field, findMove("GROWL"), lg);
  ck(contraryDef.stage[SI_ATK] == 1,
     "Contrary reverses an incoming stat drop");

  Combatant rockHead = zard, recoilDef = venu;
  rockHead.ability = ABILITY_ROCK_HEAD;
  uint16_t rockHeadHp = rockHead.hp;
  BattleMove recoil = weakFire;
  recoil.entry.effect = EF_RECOIL;
  recoil.entry.param = 4;
  battleAct(rockHead, recoilDef, field, recoil, lg);
  ck(rockHead.hp == rockHeadHp,
     "Rock Head prevents recoil damage");

  Combatant magicGuard = norm, endFoe = venu;
  magicGuard.ability = ABILITY_MAGIC_GUARD;
  magicGuard.ailment = AIL_BURN;
  uint16_t magicHp = magicGuard.hp;
  battleEndRound(field, magicGuard, endFoe, endA, endB, endField);
  ck(magicGuard.hp == magicHp,
     "Magic Guard prevents indirect ailment damage");

  Combatant speedBoost = norm, speedFoe = venu;
  speedBoost.ability = ABILITY_SPEED_BOOST;
  battleEndRound(field, speedBoost, speedFoe, endA, endB, endField);
  ck(speedBoost.stage[SI_SPE] == 1,
     "Speed Boost raises Speed at the end of the round");

  Combatant staminaDef = venu;
  staminaDef.ability = ABILITY_STAMINA;
  battleAct(zard, staminaDef, field, flame, lg);
  ck(staminaDef.stage[SI_DEF] == 1,
     "Stamina raises Defense after taking move damage");

  Combatant opportunistAtk = zard, opportunistStamina = venu;
  opportunistAtk.ability = ABILITY_OPPORTUNIST;
  opportunistStamina.ability = ABILITY_STAMINA;
  battleAct(opportunistAtk, opportunistStamina, field, flame, lg);
  ck(opportunistStamina.stage[SI_DEF] == 1 &&
         opportunistAtk.stage[SI_DEF] == 1,
     "Opportunist copies a hit-triggered stat boost");

  Combatant opportunistRound = norm, roundBooster = venu;
  opportunistRound.ability = ABILITY_OPPORTUNIST;
  roundBooster.ability = ABILITY_SPEED_BOOST;
  battleEndRound(field, opportunistRound, roundBooster, endA, endB, endField);
  ck(roundBooster.stage[SI_SPE] == 1 && opportunistRound.stage[SI_SPE] == 1,
     "Opportunist copies an end-of-round stat boost");

  Combatant moxieAtk = zard, fragile = venu;
  moxieAtk.ability = ABILITY_MOXIE;
  fragile.hp = 1;
  BattleMove sureWeak = weakFire;
  sureWeak.entry.acc = 0;
  battleAct(moxieAtk, fragile, field, sureWeak, lg);
  ck(fragile.fainted() && moxieAtk.stage[SI_ATK] == 1,
     "Moxie raises Attack after causing a knockout");

  Combatant natural = norm;
  natural.ability = ABILITY_NATURAL_CURE;
  natural.ailment = AIL_POISON;
  battleOnSwitchOut(natural);
  ck(natural.ailment == AIL_NONE,
     "Natural Cure removes status on switch-out");

  Combatant dreamer = norm, nightmare = venu;
  dreamer.ailment = AIL_SLEEP;
  dreamer.ailTurns = 3;
  nightmare.ability = ABILITY_BAD_DREAMS;
  uint16_t dreamHp = dreamer.hp;
  battleEndRound(field, dreamer, nightmare, endA, endB, endField);
  ck(dreamer.hp < dreamHp,
     "Bad Dreams damages a sleeping opponent at round end");

  Combatant simpleAtk = zard, simpleDef = venu;
  simpleDef.ability = ABILITY_SIMPLE;
  battleAct(simpleAtk, simpleDef, field, findMove("GROWL"), lg);
  ck(simpleDef.stage[SI_ATK] == -2,
     "Simple doubles a stat change");
  Combatant defiantDef = venu;
  defiantDef.ability = ABILITY_DEFIANT;
  battleAct(simpleAtk, defiantDef, field, findMove("GROWL"), lg);
  ck(defiantDef.stage[SI_ATK] == 1,
     "Defiant answers an opposing stat drop with two Attack stages");
  Combatant metalDef = venu;
  metalDef.ability = ABILITY_FULL_METAL_BODY;
  battleAct(simpleAtk, metalDef, field, findMove("GROWL"), lg);
  ck(metalDef.stage[SI_ATK] == 0,
     "Full Metal Body blocks an opposing stat drop");

  BattleField rainSpeed;
  battleSetEnvironment(rainSpeed, BWEATHER_RAIN);
  Combatant swimmer = slow, speedRival = fast;
  swimmer.base[SI_SPE] = 80;
  speedRival.base[SI_SPE] = 120;
  swimmer.ability = ABILITY_SWIFT_SWIM;
  ck(battleMovesFirst(swimmer, battleMove(flame), speedRival, battleMove(flame), rainSpeed),
     "Swift Swim doubles Speed in rain for real turn order");
  speedRival.ability = ABILITY_CLOUD_NINE;
  ck(!battleMovesFirst(swimmer, battleMove(flame), speedRival, battleMove(flame), rainSpeed),
     "Cloud Nine suppresses weather-dependent Speed");

  Combatant quickFeet = slow, quickRival = fast;
  quickFeet.base[SI_SPE] = 100;
  quickRival.base[SI_SPE] = 120;
  quickFeet.ability = ABILITY_QUICK_FEET;
  quickFeet.ailment = AIL_PARA;
  ck(battleMovesFirst(quickFeet, battleMove(flame), quickRival, battleMove(flame), field),
     "Quick Feet boosts statused Speed without paralysis halving it");

  Combatant scrappyAtk = norm, ghostDef;
  mk(ghostDef, 94, 50);
  scrappyAtk.ability = ABILITY_SCRAPPY;
  ck(battleDamage(scrappyAtk, ghostDef, field, slam, false, 255) > 0,
     "Scrappy lets Normal moves hit Ghost targets");

  Combatant furDef = venu;
  uint16_t ordinaryPhysical = battleDamage(norm, furDef, field, slam, false, 255);
  furDef.ability = ABILITY_FUR_COAT;
  ck(battleDamage(norm, furDef, field, slam, false, 255) < ordinaryPhysical * 6 / 10,
     "Fur Coat halves physical move damage");

  Combatant goldDef = venu;
  goldDef.ability = ABILITY_GOOD_AS_GOLD;
  battleAct(norm, goldDef, field, findMove("GROWL"), lg);
  ck(goldDef.stage[SI_ATK] == 0 && lg.immune,
     "Good as Gold blocks an incoming status move");

  Combatant corrosionAtk = norm, steelDef;
  mk(steelDef, 208, 50);
  corrosionAtk.ability = ABILITY_CORROSION;
  BattleMove poisonMove = weakFire;
  poisonMove.entry.acc = 0;
  poisonMove.entry.ailment = AIL_POISON;
  poisonMove.entry.ailChance = 100;
  battleAct(corrosionAtk, steelDef, field, poisonMove, lg);
  ck(steelDef.ailment == AIL_POISON,
     "Corrosion can poison a Steel target");

  Combatant charged = venu;
  charged.ability = ABILITY_ELECTROMORPHOSIS;
  battleAct(zard, charged, field, flame, lg);
  ck(charged.abilityCharged,
     "Electromorphosis charges after move damage");
  BattleMove electricMove = weakFire;
  electricMove.entry.type = T_ELECTRIC;
  uint16_t chargedDamage = battleDamage(charged, zard, field, electricMove, false, 255);
  charged.abilityCharged = false;
  uint16_t unchargedDamage = battleDamage(charged, zard, field, electricMove, false, 255);
  ck(chargedDamage > unchargedDamage * 19 / 10,
     "the charged state doubles the next Electric move");

  Combatant sower = venu;
  sower.ability = ABILITY_SEED_SOWER;
  BattleField seeded;
  battleAct(zard, sower, seeded, flame, lg);
  ck(seeded.terrain == BTERRAIN_GRASSY,
     "Seed Sower creates Grassy Terrain after move damage");

  Combatant proto = norm;
  proto.ability = ABILITY_PROTOSYNTHESIS;
  proto.base[SI_ATK] = 200;
  proto.base[SI_DEF] = proto.base[SI_SPA] = proto.base[SI_SPD] = proto.base[SI_SPE] = 100;
  BattleField harsh;
  battleSetEnvironment(harsh, BWEATHER_SUN);
  uint16_t protoDamage = battleDamage(proto, venu, harsh, slam, false, 255);
  proto.ability = ABILITY_NONE;
  ck(protoDamage > battleDamage(proto, venu, harsh, slam, false, 255) * 12 / 10,
     "Protosynthesis boosts the highest battle stat in sun");

  Combatant sheerAtk = zard, sheerDef = venu;
  BattleMove secondary = weakFire;
  secondary.entry.acc = 0;
  secondary.entry.ailment = AIL_POISON;
  secondary.entry.ailChance = 100;
  sheerAtk.ability = ABILITY_NONE;
  uint16_t secondaryDamage = battleDamage(sheerAtk, sheerDef, field, secondary, false, 217);
  sheerAtk.ability = ABILITY_SHEER_FORCE;
  battleAct(sheerAtk, sheerDef, field, secondary, lg);
  ck(lg.damage > secondaryDamage * 12 / 10 && sheerDef.ailment == AIL_NONE,
     "Sheer Force boosts a secondary-effect move and suppresses that effect");

  Combatant protean = norm, proteanDef = venu;
  protean.ability = ABILITY_PROTEAN;
  battleAct(protean, proteanDef, field, flame, lg);
  ck(protean.type1 == T_FIRE && protean.type2 == T_NONE && protean.abilityTriggered,
     "Protean changes type on its first move after entering");

  Combatant colorDef = venu;
  colorDef.ability = ABILITY_COLOR_CHANGE;
  battleAct(zard, colorDef, field, flame, lg);
  ck(colorDef.type1 == T_FIRE && colorDef.type2 == T_NONE,
     "Color Change adopts the type of a damaging move");

  Combatant dustDef = venu;
  dustDef.ability = ABILITY_SHIELD_DUST;
  battleAct(zard, dustDef, field, secondary, lg);
  ck(dustDef.ailment == AIL_NONE,
     "Shield Dust suppresses a move's secondary ailment");

  Combatant syncAtk = norm, syncDef = zard;
  syncAtk.ability = ABILITY_NONE;
  syncDef.ability = ABILITY_SYNCHRONIZE;
  battleAct(syncAtk, syncDef, field, secondary, lg);
  ck(syncDef.ailment == AIL_POISON && syncAtk.ailment == AIL_POISON,
     "Synchronize reflects poison to its source");

  Combatant poisonHeal = norm, poisonHealFoe = venu;
  poisonHeal.ability = ABILITY_POISON_HEAL;
  poisonHeal.ailment = AIL_POISON;
  poisonHeal.hp = poisonHeal.maxHp / 2;
  uint16_t poisonHealHp = poisonHeal.hp;
  battleEndRound(field, poisonHeal, poisonHealFoe, endA, endB, endField);
  ck(poisonHeal.hp > poisonHealHp && endA.healed,
     "Poison Heal replaces poison chip with healing");

  Combatant runner = norm;
  runner.ability = ABILITY_RUN_AWAY;
  ck(battleGuaranteedEscape(runner),
     "Run Away marks its bearer for guaranteed wild escape");

  BattleField sandForceField;
  battleSetEnvironment(sandForceField, BWEATHER_SAND);
  BattleMove rockMove = weakFire;
  rockMove.entry.type = T_ROCK;
  Combatant sandForce = norm;
  uint16_t plainRock = battleDamage(sandForce, venu, sandForceField,
                                    rockMove, false, 255);
  sandForce.ability = ABILITY_SAND_FORCE;
  ck(battleDamage(sandForce, venu, sandForceField, rockMove, false, 255) >
         plainRock * 12 / 10,
     "Sand Force strengthens Rock moves during sand");

  Combatant bubbleDef = norm;
  bubbleDef.ability = ABILITY_WATER_BUBBLE;
  BattleMove bubbleBurn = secondary;
  bubbleBurn.entry.ailment = AIL_BURN;
  battleAct(zard, bubbleDef, field, bubbleBurn, lg);
  ck(bubbleDef.ailment == AIL_NONE,
     "Water Bubble prevents burns");

  Combatant megaSol = zard;
  megaSol.ability = ABILITY_MEGA_SOL;
  BattleMove rainAccurate = weakFire;
  rainAccurate.entry.acc = 100;
  rainAccurate.entry.fieldFlags = MF_RAIN_ACCURATE;
  uint8_t megaSolMisses = 0;
  for (uint32_t seed = 1; seed <= 32; seed++) {
    g_seed = seed;
    Combatant target = venu;
    battleAct(megaSol, target, field, rainAccurate, lg);
    if (lg.missed) megaSolMisses++;
  }
  ck(megaSolMisses > 0,
     "Mega Sol applies sunlight to weather-sensitive move accuracy");

  Combatant confused = norm, confusedFoe = venu;
  confused.confuseTurns = 1;
  BattleMove sureMove = weakFire;
  sureMove.entry.acc = 0;
  g_seed = 3;
  battleAct(confused, confusedFoe, field, sureMove, lg);
  ck(confused.confuseTurns == 0 && !lg.hurtSelf,
     "the last confusion turn expires without underflowing");

  bool moodyValid = true;
  for (uint32_t seed = 1; seed <= 16; seed++) {
    g_seed = seed;
    Combatant moody = norm, moodyFoe = venu;
    moody.ability = ABILITY_MOODY;
    battleEndRound(field, moody, moodyFoe, endA, endB, endField);
    uint8_t raised = 0, lowered = 0;
    for (uint8_t i = 0; i < SI_COUNT; i++) {
      raised += moody.stage[i] == 2;
      lowered += moody.stage[i] == -1;
    }
    raised += moody.accuracyStage == 2;
    raised += moody.evasionStage == 2;
    lowered += moody.accuracyStage == -1;
    lowered += moody.evasionStage == -1;
    if (raised != 1 || lowered != 1) moodyValid = false;
  }
  ck(moodyValid,
     "Moody selects distinct valid stats, including accuracy and evasion");

  // --- accuracy and evasion are transient battle stages
  BattleMove aimed = battleMove(flame);
  aimed.entry.acc = 60;
  aimed.entry.effect = EF_NONE;
  Combatant aimAtk = norm, aimDef = venu;
  ck(battleAccuracy(aimAtk, aimDef, field, aimed) == 60,
     "neutral accuracy uses the authored move value");
  aimAtk.accuracyStage = 2;
  ck(battleAccuracy(aimAtk, aimDef, field, aimed) == 100,
     "two accuracy stages multiply accuracy by five thirds");
  aimAtk.accuracyStage = 0;
  aimDef.evasionStage = 2;
  ck(battleAccuracy(aimAtk, aimDef, field, aimed) == 36,
     "two evasion stages multiply opposing accuracy by three fifths");
  aimDef.evasionStage = 0;
  aimAtk.ability = ABILITY_NO_GUARD;
  ck(battleAccuracy(aimAtk, aimDef, field, aimed) == 0,
     "No Guard bypasses accuracy and evasion stages");

  aimAtk.ability = ABILITY_NONE;
  aimDef.ability = ABILITY_SAND_VEIL;
  BattleField obscured;
  battleSetEnvironment(obscured, BWEATHER_SAND);
  aimed.entry.acc = 80;
  ck(battleAccuracy(aimAtk, aimDef, obscured, aimed) == 64,
     "Sand Veil reduces incoming accuracy during sand");
  aimDef.ability = ABILITY_SNOW_CLOAK;
  battleSetEnvironment(obscured, BWEATHER_SNOW);
  ck(battleAccuracy(aimAtk, aimDef, obscured, aimed) == 64,
     "Snow Cloak reduces incoming accuracy during snow");
  aimDef.ability = ABILITY_TANGLED_FEET;
  aimDef.confuseTurns = 2;
  battleSetEnvironment(obscured, BWEATHER_NONE);
  ck(battleAccuracy(aimAtk, aimDef, obscured, aimed) == 40,
     "Tangled Feet halves incoming accuracy while confused");
  aimDef.ability = ABILITY_WONDER_SKIN;
  aimDef.confuseTurns = 0;
  aimed.entry.cat = MC_STATUS;
  ck(battleAccuracy(aimAtk, aimDef, obscured, aimed) == 50,
     "Wonder Skin caps incoming status-move accuracy at fifty percent");

  BattleMove sandAttack = aimed;
  sandAttack.entry.acc = 0;
  sandAttack.entry.effect = EF_STAGE;
  sandAttack.entry.statMask = ST_ACC;
  sandAttack.entry.stages = -1;
  sandAttack.entry.target = TG_FOE;
  aimDef = venu;
  aimDef.ability = ABILITY_KEEN_EYE;
  battleAct(aimAtk, aimDef, field, sandAttack, lg);
  ck(aimDef.accuracyStage == 0,
     "Keen Eye blocks an opposing accuracy drop");
  aimDef.ability = ABILITY_NONE;
  battleAct(aimAtk, aimDef, field, sandAttack, lg);
  ck(aimDef.accuracyStage == -1 && lg.stageMask == ST_ACC,
     "accuracy-lowering moves use the common stage resolver");
  aimDef.stage[SI_ATK] = 3;
  aimDef.evasionStage = 2;
  battleOnSwitchOut(aimDef);
  ck(aimDef.stage[SI_ATK] == 0 && aimDef.accuracyStage == 0 &&
     aimDef.evasionStage == 0,
     "switching out clears ordinary, accuracy and evasion stages");

  // --- move tags drive ability effects without move-name checks
  BattleMove tagged = battleMove(findMove("TACKLE"));
  tagged.entry.acc = 0;
  tagged.entry.power = 80;
  tagged.entry.type = T_NORMAL;
  tagged.entry.cat = MC_PHYS;
  tagged.entry.tags = MT_CONTACT;
  Combatant tagAtk = norm, tagDef = venu;
  tagAtk.ability = ABILITY_NONE;
  uint16_t plainTagged = battleDamage(tagAtk, tagDef, field, tagged, false, 255);
  tagAtk.ability = ABILITY_TOUGH_CLAWS;
  ck(battleDamage(tagAtk, tagDef, field, tagged, false, 255) > plainTagged * 12 / 10,
     "Tough Claws boosts any contact-tagged move");
  tagged.entry.tags = MT_PUNCH;
  tagAtk.ability = ABILITY_IRON_FIST;
  ck(battleDamage(tagAtk, tagDef, field, tagged, false, 255) > plainTagged * 11 / 10,
     "Iron Fist boosts any punch-tagged move");
  tagged.entry.tags = MT_BITE;
  tagAtk.ability = ABILITY_STRONG_JAW;
  ck(battleDamage(tagAtk, tagDef, field, tagged, false, 255) > plainTagged * 14 / 10,
     "Strong Jaw boosts any bite-tagged move");
  tagged.entry.tags = MT_PULSE;
  tagAtk.ability = ABILITY_MEGA_LAUNCHER;
  ck(battleDamage(tagAtk, tagDef, field, tagged, false, 255) > plainTagged * 14 / 10,
     "Mega Launcher boosts any pulse-tagged move");
  tagged.entry.tags = MT_SLICING;
  tagAtk.ability = ABILITY_SHARPNESS;
  ck(battleDamage(tagAtk, tagDef, field, tagged, false, 255) > plainTagged * 14 / 10,
     "Sharpness boosts any slicing-tagged move");

  tagged.entry.tags = MT_SOUND;
  tagAtk.ability = ABILITY_PUNK_ROCK;
  uint16_t punkDamage = battleDamage(tagAtk, tagDef, field, tagged, false, 255);
  tagAtk.ability = ABILITY_NONE;
  tagDef.ability = ABILITY_PUNK_ROCK;
  ck(punkDamage > plainTagged * 12 / 10 &&
     battleDamage(tagAtk, tagDef, field, tagged, false, 255) < plainTagged * 6 / 10,
     "Punk Rock strengthens outgoing sound and halves incoming sound damage");

  tagged.entry.tags = MT_CONTACT;
  tagDef.ability = ABILITY_FLUFFY;
  uint16_t fluffyContact = battleDamage(tagAtk, tagDef, field, tagged, false, 255);
  tagged.entry.tags = MT_NONE;
  tagged.entry.type = T_FIRE;
  uint16_t fluffyFire = battleDamage(tagAtk, tagDef, field, tagged, false, 255);
  tagDef.ability = ABILITY_NONE;
  uint16_t plainFireTag = battleDamage(tagAtk, tagDef, field, tagged, false, 255);
  ck(fluffyContact < plainTagged * 6 / 10 && fluffyFire > plainFireTag * 19 / 10,
     "Fluffy halves contact damage and doubles Fire damage");

  Combatant voice = norm;
  voice.ability = ABILITY_LIQUID_VOICE;
  MoveId snarl = findMove("SNARL");
  ck(snarl && (moveEntry(snarl).tags & MT_SOUND) &&
     battleMoveFor(voice, snarl).entry.type == T_WATER,
     "Liquid Voice turns sound-tagged moves into Water moves");

  Combatant soundDef = venu;
  soundDef.ability = ABILITY_SOUNDPROOF;
  battleAct(norm, soundDef, field, snarl, lg);
  ck(lg.immune && !lg.damage, "Soundproof grants immunity to sound-tagged moves");
  Combatant bulletDef = venu;
  bulletDef.ability = ABILITY_BULLETPROOF;
  battleAct(norm, bulletDef, field, findMove("SHADOW BALL"), lg);
  ck(lg.immune && !lg.damage, "Bulletproof grants immunity to ballistic-tagged moves");

  BattleMove powder = tagged;
  powder.entry.cat = MC_STATUS;
  powder.entry.type = T_GRASS;
  powder.entry.target = TG_FOE;
  powder.entry.ailment = AIL_SLEEP;
  powder.entry.ailChance = 100;
  powder.entry.tags = MT_POWDER;
  Combatant coatDef = venu;
  coatDef.ability = ABILITY_OVERCOAT;
  battleAct(norm, coatDef, field, powder, lg);
  ck(lg.immune && coatDef.ailment == AIL_NONE,
     "Overcoat grants immunity to powder-tagged moves");

  BattleMove wind = tagged;
  wind.entry.tags = MT_WIND;
  Combatant windRider = venu;
  windRider.ability = ABILITY_WIND_RIDER;
  battleAct(norm, windRider, field, wind, lg);
  ck(lg.immune && windRider.stage[SI_ATK] == 1,
     "Wind Rider nullifies wind-tagged moves and raises Attack");
  Combatant windPower = venu;
  windPower.ability = ABILITY_WIND_POWER;
  battleAct(norm, windPower, field, wind, lg);
  ck(lg.damage && windPower.abilityCharged,
     "Wind Power charges the next Electric move after a wind-tagged hit");

  Combatant rough = venu, contact = norm;
  rough.ability = ABILITY_ROUGH_SKIN;
  uint16_t contactHp = contact.hp;
  tagged.entry.type = T_NORMAL;
  tagged.entry.cat = MC_PHYS;
  tagged.entry.tags = MT_CONTACT;
  battleAct(contact, rough, field, tagged, lg);
  ck(contact.hp == contactHp - contact.maxHp / 8,
     "Rough Skin damages an attacker after contact");
  contact = norm;
  contact.ability = ABILITY_LONG_REACH;
  contactHp = contact.hp;
  battleAct(contact, rough, field, tagged, lg);
  ck(contact.hp == contactHp, "Long Reach suppresses contact-triggered retaliation");

  Combatant poisonTouch = norm, touchTarget = norm;
  poisonTouch.ability = ABILITY_POISON_TOUCH;
  touchTarget.ability = ABILITY_NONE;
  bool poisonTouchTriggered = false;
  for (uint32_t seed = 1; seed <= 128 && !poisonTouchTriggered; seed++) {
    g_seed = seed;
    poisonTouch.hp = poisonTouch.maxHp;
    touchTarget.hp = touchTarget.maxHp;
    touchTarget.ailment = AIL_NONE;
    battleAct(poisonTouch, touchTarget, field, tagged, lg);
    poisonTouchTriggered = touchTarget.ailment == AIL_POISON;
  }
  ck(poisonTouchTriggered, "Poison Touch can poison after a contact-tagged attack");

  Combatant protectedDef = venu, unseen = norm;
  protectedDef.protectedTurn = true;
  battleAct(unseen, protectedDef, field, tagged, lg);
  ck(lg.missed && !lg.damage, "Protect blocks ordinary contact moves");
  protectedDef.hp = protectedDef.maxHp;
  unseen.ability = ABILITY_UNSEEN_FIST;
  battleAct(unseen, protectedDef, field, tagged, lg);
  ck(lg.damage && !lg.missed, "Unseen Fist lets contact-tagged moves bypass Protect");

  Combatant dancerUser = norm, dancerFoe = venu;
  dancerFoe.ability = ABILITY_DANCER;
  battleAct(dancerUser, dancerFoe, field, findMove("SWORDS DANCE"), lg);
  ck(dancerUser.stage[SI_ATK] == 2 && dancerFoe.stage[SI_ATK] == 2 &&
     lg.dancerCopied,
     "Dancer copies a dance-tagged setup move exactly once");

  // --- a full fight terminates and someone wins
  Combatant A, B;
  mk(A, 6, 50); mk(B, 9, 50);
  int turn = 0;
  while (!A.fainted() && !B.fainted() && turn < 200) {
    turn++;
    uint8_t ma = A.moves[random(MOVE_SLOTS)], mb = B.moves[random(MOVE_SLOTS)];
    Combatant *first = &A, *second = &B;
    uint8_t mf = ma, ms = mb;
    if (!battleMovesFirst(A, ma, B, mb)) { first = &B; second = &A; mf = mb; ms = ma; }
    battleAct(*first, *second, field, mf, lg);
    if (!second->fainted()) battleAct(*second, *first, field, ms, lg);
    battleEndRound(field, A, B, endA, endB, endField);
  }
  printf("     fight ended on turn %d: CHARIZARD %u/%u, BLASTOISE %u/%u\n",
         turn, A.hp, A.maxHp, B.hp, B.maxHp);
  ck(turn < 200, "a fight reaches a conclusion");
  ck(A.fainted() || B.fainted(), "and somebody actually faints");

  // --- how long does a fight actually last? 6 matchups x 40 fights.
  {
    const int16_t roster[] = { 6, 9, 3, 65, 68, 143 };
    int total = 0, fights = 0, shortest = 999, longest = 0;
    for (int i = 0; i < 6; i++)
      for (int j = 0; j < 6; j++) {
        if (i == j) continue;
        for (int rep = 0; rep < 8; rep++) {
          Combatant X, Y;
          mk(X, roster[i], 50); mk(Y, roster[j], 50);
          int t = 0;
          while (!X.fainted() && !Y.fainted() && t < 200) {
            t++;
            uint8_t mx = X.moves[random(MOVE_SLOTS)], my = Y.moves[random(MOVE_SLOTS)];
            Combatant *f = &X, *sd = &Y; uint8_t mf = mx, ms = my;
            if (!battleMovesFirst(X, mx, Y, my)) { f = &Y; sd = &X; mf = my; ms = mx; }
            battleAct(*f, *sd, field, mf, lg);
            if (!sd->fainted()) battleAct(*sd, *f, field, ms, lg);
            battleEndRound(field, X, Y, endA, endB, endField);
          }
          total += t; fights++;
          if (t < shortest) shortest = t;
          if (t > longest) longest = t;
        }
      }
    printf("     %d fights at L50: average %.1f turns (shortest %d, longest %d)\n",
           fights, (double)total / fights, shortest, longest);
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}

// Covers battle abilities that are not exercised by the older focused suites.
// Every assertion goes through a public battle API from the real firmware.
#include "Arduino.h"
#include "Preferences.h"
#include "battle.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 0xAB1717;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
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

static Combatant mon(AbilityKey ability = ABILITY_NONE,
                     uint8_t type = T_NORMAL) {
  Combatant c;
  c.dex = 1;
  c.level = 50;
  c.maxHp = c.hp = 1000;
  for (uint8_t i = 0; i < SI_COUNT; i++)
    c.base[i] = c.nativeBase[i] = 100;
  c.type1 = c.nativeType1 = type;
  c.type2 = c.nativeType2 = T_NONE;
  c.ability = ability;
  return c;
}

static BattleMove attack(uint8_t type = T_NORMAL, uint8_t category = MC_PHYS,
                         uint8_t power = 80) {
  BattleMove result;
  result.source = 1;
  result.entry.type = type;
  result.entry.cat = category;
  result.entry.power = power;
  result.entry.acc = 0;
  result.entry.target = TG_FOE;
  return result;
}

static BattleMove status(uint8_t effect, int8_t param = 0,
                         uint8_t target = TG_FOE) {
  BattleMove result = attack(T_NORMAL, MC_STATUS, 0);
  result.entry.effect = effect;
  result.entry.param = param;
  result.entry.target = target;
  return result;
}

static MoveId findMove(const char *name) {
  for (MoveId i = 1; i < moveCount(); i++)
    if (!std::strcmp(moveEntry(i).name, name)) return i;
  return MOVE_NONE;
}

static uint16_t damage(AbilityKey attackerAbility, AbilityKey defenderAbility,
                       uint8_t type = T_NORMAL, uint8_t category = MC_PHYS,
                       BattleField field = BattleField(),
                       uint8_t attackerType = T_NORMAL,
                       uint8_t defenderType = T_NORMAL, bool crit = false) {
  Combatant attacker = mon(attackerAbility, attackerType);
  Combatant defender = mon(defenderAbility, defenderType);
  return battleDamage(attacker, defender, field, attack(type, category),
                      crit, 255);
}

static void expectAilmentBlocked(AbilityKey ability, uint8_t ailment,
                                 BattleField field, const char *what) {
  Combatant attacker = mon(), defender = mon(ability);
  BattleMove move = attack(T_NORMAL, MC_SPEC, 1);
  move.entry.ailment = ailment;
  move.entry.ailChance = 100;
  TurnLog log;
  battleAct(attacker, defender, field, move, log);
  ck(defender.ailment == AIL_NONE && !defender.confuseTurns, what);
}

static unsigned critCount(AbilityKey ability) {
  g_seed = 0xC817;
  unsigned count = 0;
  for (unsigned i = 0; i < 512; i++) {
    Combatant attacker = mon(ability), defender = mon();
    TurnLog log;
    BattleField field;
    battleAct(attacker, defender, field, attack(), log);
    count += log.crit;
  }
  return count;
}

static unsigned ailmentCount(AbilityKey ability) {
  g_seed = 0x5E8E;
  unsigned count = 0;
  for (unsigned i = 0; i < 256; i++) {
    Combatant attacker = mon(ability), defender = mon();
    BattleMove move = attack(T_NORMAL, MC_SPEC, 1);
    move.entry.ailment = AIL_BURN;
    move.entry.ailChance = 30;
    TurnLog log;
    BattleField field;
    battleAct(attacker, defender, field, move, log);
    count += defender.ailment == AIL_BURN;
  }
  return count;
}

int main() {
  BattleField clear;
  Combatant plain = mon(), foe = mon();

  // Effective stats and weather-dependent speed.
  ck(battleEffectiveStat(mon(ABILITY_PURE_POWER), SI_ATK) == 200,
     "Pure Power doubles Attack");
  ck(battleEffectiveStat(mon(ABILITY_HUSTLE), SI_ATK) == 150,
     "Hustle raises physical Attack");
  Combatant scaled = mon(ABILITY_MARVEL_SCALE);
  scaled.ailment = AIL_BURN;
  ck(battleEffectiveStat(scaled, SI_DEF) == 150,
     "Marvel Scale raises Defense while statused");
  scaled = mon(ABILITY_DEFEATIST); scaled.hp = 500;
  ck(battleEffectiveStat(scaled, SI_ATK) == 50 &&
     battleEffectiveStat(scaled, SI_SPA) == 50,
     "Defeatist halves both offenses at half HP");
  scaled = mon(ABILITY_TOXIC_BOOST); scaled.ailment = AIL_POISON;
  ck(battleEffectiveStat(scaled, SI_ATK) == 150,
     "Toxic Boost raises physical Attack while poisoned");
  scaled = mon(ABILITY_FLARE_BOOST); scaled.ailment = AIL_BURN;
  ck(battleEffectiveStat(scaled, SI_SPA) == 150,
     "Flare Boost raises Special Attack while burned");

  struct SpeedCase { AbilityKey ability; BattleWeather weather; const char *name; };
  const SpeedCase speedCases[] = {
    { ABILITY_CHLOROPHYLL, BWEATHER_SUN, "Chlorophyll doubles Speed in sun" },
    { ABILITY_SAND_RUSH, BWEATHER_SAND, "Sand Rush doubles Speed in sand" },
    { ABILITY_SLUSH_RUSH, BWEATHER_SNOW, "Slush Rush doubles Speed in snow" },
  };
  BattleMove basic = attack();
  for (const SpeedCase &test : speedCases) {
    BattleField field; field.weather = test.weather;
    Combatant fast = mon(test.ability), benchmark = mon();
    fast.base[SI_SPE] = 80; benchmark.base[SI_SPE] = 120;
    ck(battleMovesFirst(fast, basic, benchmark, basic, field), test.name);
  }
  BattleField electric; electric.terrain = BTERRAIN_ELECTRIC;
  Combatant quark = mon(ABILITY_QUARK_DRIVE), benchmark = mon();
  quark.base[SI_SPE] = 130; quark.nativeBase[SI_SPE] = 130;
  benchmark.base[SI_SPE] = 150;
  ck(battleMovesFirst(quark, basic, benchmark, basic, electric),
     "Quark Drive boosts the highest stat on Electric Terrain");

  // Accuracy and evasion hooks.
  BattleMove inaccurate = attack(); inaccurate.entry.acc = 80;
  ck(battleAccuracy(mon(ABILITY_COMPOUND_EYES), foe, clear, inaccurate) == 100,
     "Compound Eyes raises move accuracy");
  ck(battleAccuracy(mon(ABILITY_VICTORY_STAR), foe, clear, inaccurate) == 88,
     "Victory Star raises move accuracy");
  ck(battleAccuracy(mon(ABILITY_HUSTLE), foe, clear, inaccurate) == 64,
     "Hustle lowers physical move accuracy");
  Combatant unawareAtk = mon(ABILITY_UNAWARE), evasive = mon();
  evasive.evasionStage = 6;
  ck(battleAccuracy(unawareAtk, evasive, clear, inaccurate) == 80,
     "Unaware ignores the target's evasion stages");

  // Damage modifiers: each comparison uses the real damage resolver.
  uint16_t ordinary = damage(ABILITY_NONE, ABILITY_NONE);
  ck(damage(ABILITY_ADAPTABILITY, ABILITY_NONE) > ordinary,
     "Adaptability strengthens same-type attacks");
  Combatant low = mon(ABILITY_TORRENT, T_WATER); low.hp = 300;
  ck(battleDamage(low, foe, clear, attack(T_WATER), false, 255) >
     battleDamage(mon(ABILITY_NONE, T_WATER), foe, clear,
                  attack(T_WATER), false, 255),
     "Torrent strengthens Water attacks at low HP");
  low = mon(ABILITY_SWARM, T_BUG); low.hp = 300;
  ck(battleDamage(low, foe, clear, attack(T_BUG), false, 255) >
     battleDamage(mon(ABILITY_NONE, T_BUG), foe, clear,
                  attack(T_BUG), false, 255),
     "Swarm strengthens Bug attacks at low HP");
  BattleMove recoil = attack(); recoil.entry.effect = EF_RECOIL; recoil.entry.param = 4;
  ck(battleDamage(mon(ABILITY_RECKLESS), foe, clear, recoil, false, 255) >
     battleDamage(plain, foe, clear, recoil, false, 255),
     "Reckless strengthens recoil attacks");

  struct TypeBoostCase { AbilityKey ability; uint8_t type; const char *name; };
  const TypeBoostCase typeBoostCases[] = {
    { ABILITY_STEELWORKER, T_STEEL, "Steelworker strengthens Steel attacks" },
    { ABILITY_TRANSISTOR, T_ELECTRIC, "Transistor strengthens Electric attacks" },
    { ABILITY_DRAGONS_MAW, T_DRAGON, "Dragon's Maw strengthens Dragon attacks" },
    { ABILITY_ROCKY_PAYLOAD, T_ROCK, "Rocky Payload strengthens Rock attacks" },
    { ABILITY_FIRE_MANE, T_FIRE, "Fire Mane strengthens Fire attacks" },
  };
  for (const TypeBoostCase &test : typeBoostCases)
    ck(damage(test.ability, ABILITY_NONE, test.type) >
       damage(ABILITY_NONE, ABILITY_NONE, test.type), test.name);

  ck(damage(ABILITY_SNIPER, ABILITY_NONE, T_NORMAL, MC_PHYS,
            clear, T_NORMAL, T_NORMAL, true) >
     damage(ABILITY_NONE, ABILITY_NONE, T_NORMAL, MC_PHYS,
            clear, T_NORMAL, T_NORMAL, true),
     "Sniper strengthens critical hits");
  BattleField sun; sun.weather = BWEATHER_SUN;
  ck(damage(ABILITY_SOLAR_POWER, ABILITY_NONE, T_NORMAL, MC_SPEC, sun) >
     damage(ABILITY_NONE, ABILITY_NONE, T_NORMAL, MC_SPEC, sun),
     "Solar Power strengthens special attacks in sun");
  BattleField grassy; grassy.terrain = BTERRAIN_GRASSY;
  ck(damage(ABILITY_NONE, ABILITY_GRASS_PELT, T_NORMAL, MC_PHYS, grassy) < ordinary,
     "Grass Pelt raises Defense on Grassy Terrain");
  ck(damage(ABILITY_NONE, ABILITY_TABLETS_OF_RUIN) < ordinary,
     "Tablets of Ruin lowers opposing physical Attack");
  ck(damage(ABILITY_NONE, ABILITY_VESSEL_OF_RUIN, T_NORMAL, MC_SPEC) < ordinary,
     "Vessel of Ruin lowers opposing Special Attack");
  ck(damage(ABILITY_SWORD_OF_RUIN, ABILITY_NONE) > ordinary,
     "Sword of Ruin lowers opposing Defense");
  ck(damage(ABILITY_BEADS_OF_RUIN, ABILITY_NONE, T_NORMAL, MC_SPEC) > ordinary,
     "Beads of Ruin lowers opposing Special Defense");

  Combatant male = mon(ABILITY_RIVALRY), other = mon();
  male.gender = other.gender = GENDER_MALE;
  uint16_t sameGender = battleDamage(male, other, clear, basic, false, 255);
  other.gender = GENDER_FEMALE;
  ck(sameGender > battleDamage(male, other, clear, basic, false, 255),
     "Rivalry distinguishes same- and opposite-gender opponents");
  ck(damage(ABILITY_DARK_AURA, ABILITY_NONE, T_DARK) >
     damage(ABILITY_NONE, ABILITY_NONE, T_DARK),
     "Dark Aura strengthens Dark attacks");
  ck(damage(ABILITY_FAIRY_AURA, ABILITY_NONE, T_FAIRY) >
     damage(ABILITY_NONE, ABILITY_NONE, T_FAIRY),
     "Fairy Aura strengthens Fairy attacks");
  ck(damage(ABILITY_DARK_AURA, ABILITY_AURA_BREAK, T_DARK) <
     damage(ABILITY_NONE, ABILITY_NONE, T_DARK),
     "Aura Break reverses an opposing aura");

  ck(damage(ABILITY_NONE, ABILITY_THICK_FAT, T_FIRE, MC_SPEC) <
     damage(ABILITY_NONE, ABILITY_NONE, T_FIRE, MC_SPEC),
     "Thick Fat reduces Fire and Ice damage");
  ck(damage(ABILITY_NONE, ABILITY_DRY_SKIN, T_FIRE, MC_SPEC) >
     damage(ABILITY_NONE, ABILITY_NONE, T_FIRE, MC_SPEC),
     "Dry Skin increases incoming Fire damage");
  ck(damage(ABILITY_NONE, ABILITY_HEATPROOF, T_FIRE, MC_SPEC) <
     damage(ABILITY_NONE, ABILITY_NONE, T_FIRE, MC_SPEC),
     "Heatproof reduces incoming Fire damage");
  ck(damage(ABILITY_NONE, ABILITY_PURIFYING_SALT, T_GHOST, MC_SPEC,
            clear, T_NORMAL, T_PSYCHIC) <
     damage(ABILITY_NONE, ABILITY_NONE, T_GHOST, MC_SPEC,
            clear, T_NORMAL, T_PSYCHIC),
     "Purifying Salt reduces incoming Ghost damage");
  ck(damage(ABILITY_NONE, ABILITY_ICE_SCALES, T_NORMAL, MC_SPEC) <
     damage(ABILITY_NONE, ABILITY_NONE, T_NORMAL, MC_SPEC),
     "Ice Scales halves special damage");

  uint16_t resisted = damage(ABILITY_NONE, ABILITY_NONE, T_FIRE, MC_SPEC,
                             clear, T_NORMAL, T_WATER);
  ck(damage(ABILITY_TINTED_LENS, ABILITY_NONE, T_FIRE, MC_SPEC,
            clear, T_NORMAL, T_WATER) > resisted,
     "Tinted Lens strengthens resisted attacks");
  struct SuperReduction { AbilityKey ability; const char *name; };
  const SuperReduction reductions[] = {
    { ABILITY_FILTER, "Filter reduces super-effective damage" },
    { ABILITY_SOLID_ROCK, "Solid Rock reduces super-effective damage" },
    { ABILITY_PRISM_ARMOR, "Prism Armor reduces super-effective damage" },
  };
  uint16_t super = damage(ABILITY_NONE, ABILITY_NONE, T_FIRE, MC_SPEC,
                          clear, T_NORMAL, T_GRASS);
  for (const SuperReduction &test : reductions)
    ck(damage(ABILITY_NONE, test.ability, T_FIRE, MC_SPEC,
              clear, T_NORMAL, T_GRASS) < super, test.name);
  ck(damage(ABILITY_NONE, ABILITY_MULTISCALE) < ordinary,
     "Multiscale reduces damage at full HP");
  ck(damage(ABILITY_NONE, ABILITY_SHADOW_SHIELD) < ordinary,
     "Shadow Shield reduces damage at full HP");
  ck(damage(ABILITY_NONE, ABILITY_BATTLE_ARMOR, T_NORMAL, MC_PHYS,
            clear, T_NORMAL, T_NORMAL, true) ==
     damage(ABILITY_NONE, ABILITY_BATTLE_ARMOR),
     "Battle Armor prevents critical-hit damage");
  ck(damage(ABILITY_NONE, ABILITY_SHELL_ARMOR, T_NORMAL, MC_PHYS,
            clear, T_NORMAL, T_NORMAL, true) ==
     damage(ABILITY_NONE, ABILITY_SHELL_ARMOR),
     "Shell Armor prevents critical-hit damage");

  // Weather suppression and Normal-type conversion use resolved battle moves.
  uint16_t sunFire = damage(ABILITY_NONE, ABILITY_NONE, T_FIRE, MC_SPEC, sun);
  ck(damage(ABILITY_AIR_LOCK, ABILITY_NONE, T_FIRE, MC_SPEC, sun) < sunFire,
     "Air Lock suppresses active weather effects");
  MoveId tackle = findMove("TACKLE");
  ck(tackle != MOVE_NONE, "conversion fixture move exists");
  struct ConversionCase { AbilityKey ability; uint8_t type; const char *name; };
  const ConversionCase conversions[] = {
    { ABILITY_AERILATE, T_FLYING, "Aerilate converts Normal moves to Flying" },
    { ABILITY_PIXILATE, T_FAIRY, "Pixilate converts Normal moves to Fairy" },
    { ABILITY_REFRIGERATE, T_ICE, "Refrigerate converts Normal moves to Ice" },
    { ABILITY_DRAGONIZE, T_DRAGON, "Dragonize converts Normal moves to Dragon" },
  };
  for (const ConversionCase &test : conversions) {
    Combatant user = mon(test.ability);
    BattleMove converted = battleMoveFor(user, tackle);
    ck(converted.entry.type == test.type &&
       battleDamage(user, foe, clear, converted, false, 255) >
       battleDamage(plain, foe, clear, converted, false, 255),
       test.name);
  }

  // Priority and forced-switch rules.
  Combatant slow = mon(), fast = mon(); slow.base[SI_SPE] = 50; fast.base[SI_SPE] = 150;
  BattleMove selfStage = status(EF_STAGE, 0, TG_SELF);
  selfStage.entry.statMask = ST_ATK; selfStage.entry.stages = 1;
  slow.ability = ABILITY_PRANKSTER;
  ck(battleMovesFirst(slow, selfStage, fast, basic),
     "Prankster gives status moves priority");
  BattleMove healing = status(EF_HEAL, 50, TG_SELF);
  slow.ability = ABILITY_TRIAGE;
  ck(battleMovesFirst(slow, healing, fast, basic),
     "Triage gives healing moves priority");
  BattleMove flying = attack(T_FLYING);
  slow.ability = ABILITY_GALE_WINGS;
  ck(battleMovesFirst(slow, flying, fast, basic),
     "Gale Wings gives full-HP Flying moves priority");
  slow.ability = ABILITY_STALL;
  ck(!battleMovesFirst(slow, basic, fast, basic),
     "Stall makes the user act last in its priority bracket");
  slow.ability = ABILITY_MYCELIUM_MIGHT;
  ck(!battleMovesFirst(slow, selfStage, fast, basic),
     "Mycelium Might makes status moves act last");
  BattleMove priority = attack(); priority.entry.effect = EF_PRIORITY; priority.entry.param = 1;
  const AbilityKey blockers[] = {
    ABILITY_QUEENLY_MAJESTY, ABILITY_DAZZLING, ABILITY_ARMOR_TAIL,
  };
  const char *blockerNames[] = {
    "Queenly Majesty blocks priority attacks",
    "Dazzling blocks priority attacks",
    "Armor Tail blocks priority attacks",
  };
  for (uint8_t i = 0; i < 3; i++) {
    Combatant attacker = mon(), defender = mon(blockers[i]);
    TurnLog log; BattleField field;
    battleAct(attacker, defender, field, priority, log);
    ck(log.blockedByField && defender.hp == defender.maxHp, blockerNames[i]);
  }
  BattleMove roar = status(EF_FORCE_SWITCH);
  Combatant defender = mon(ABILITY_GUARD_DOG);
  TurnLog log;
  battleAct(plain, defender, clear, roar, log);
  ck(log.immune && log.switchRequest == BSWITCH_NONE,
     "Guard Dog prevents forced switching");

  // Externally caused stat drops and their defensive abilities.
  BattleMove drop = status(EF_STAGE); drop.entry.statMask = ST_ATK | ST_DEF;
  drop.entry.stages = -1;
  const AbilityKey fullDropBlocks[] = { ABILITY_CLEAR_BODY, ABILITY_WHITE_SMOKE };
  const char *fullDropNames[] = {
    "Clear Body blocks external stat drops", "White Smoke blocks external stat drops",
  };
  for (uint8_t i = 0; i < 2; i++) {
    Combatant target = mon(fullDropBlocks[i]); TurnLog dropLog; BattleField field;
    battleAct(plain, target, field, drop, dropLog);
    ck(target.stage[SI_ATK] == 0 && target.stage[SI_DEF] == 0, fullDropNames[i]);
  }
  defender = mon(ABILITY_HYPER_CUTTER);
  battleAct(plain, defender, clear, drop, log);
  ck(defender.stage[SI_ATK] == 0 && defender.stage[SI_DEF] == -1,
     "Hyper Cutter blocks only Attack drops");
  defender = mon(ABILITY_BIG_PECKS);
  battleAct(plain, defender, clear, drop, log);
  ck(defender.stage[SI_ATK] == -1 && defender.stage[SI_DEF] == 0,
     "Big Pecks blocks only Defense drops");
  defender = mon(ABILITY_COMPETITIVE);
  battleAct(plain, defender, clear, drop, log);
  ck(defender.stage[SI_SPA] == 2,
     "Competitive raises Special Attack after an external stat drop");

  // Ailment prevention.
  expectAilmentBlocked(ABILITY_LIMBER, AIL_PARA, clear,
                       "Limber prevents paralysis");
  expectAilmentBlocked(ABILITY_INSOMNIA, AIL_SLEEP, clear,
                       "Insomnia prevents sleep");
  expectAilmentBlocked(ABILITY_VITAL_SPIRIT, AIL_SLEEP, clear,
                       "Vital Spirit prevents sleep");
  expectAilmentBlocked(ABILITY_SWEET_VEIL, AIL_SLEEP, clear,
                       "Sweet Veil prevents sleep");
  expectAilmentBlocked(ABILITY_IMMUNITY, AIL_POISON, clear,
                       "Immunity prevents poison");
  expectAilmentBlocked(ABILITY_MAGMA_ARMOR, AIL_FREEZE, clear,
                       "Magma Armor prevents freezing");
  expectAilmentBlocked(ABILITY_THERMAL_EXCHANGE, AIL_BURN, clear,
                       "Thermal Exchange prevents burns");
  expectAilmentBlocked(ABILITY_OWN_TEMPO, AIL_CONFUSE, clear,
                       "Own Tempo prevents confusion");
  expectAilmentBlocked(ABILITY_PURIFYING_SALT, AIL_POISON, clear,
                       "Purifying Salt prevents status conditions");
  expectAilmentBlocked(ABILITY_LEAF_GUARD, AIL_POISON, sun,
                       "Leaf Guard prevents status conditions in sun");

  // Contact and damage-triggered abilities.
  BattleMove contact = attack(); contact.entry.tags = MT_CONTACT;
  struct ContactCase { AbilityKey ability; uint8_t ailment; const char *name; };
  const ContactCase contactCases[] = {
    { ABILITY_STATIC, AIL_PARA, "Static can paralyze a contact attacker" },
    { ABILITY_POISON_POINT, AIL_POISON, "Poison Point can poison a contact attacker" },
    { ABILITY_FLAME_BODY, AIL_BURN, "Flame Body can burn a contact attacker" },
    { ABILITY_EFFECT_SPORE, AIL_NONE, "Effect Spore can inflict a contact ailment" },
  };
  for (const ContactCase &test : contactCases) {
    bool triggered = false;
    g_seed = 0xC047AC7;
    for (uint16_t attempt = 0; attempt < 200 && !triggered; attempt++) {
      Combatant attacker = mon(), target = mon(test.ability); TurnLog hitLog; BattleField field;
      battleAct(attacker, target, field, contact, hitLog);
      triggered = test.ailment == AIL_NONE ? attacker.ailment != AIL_NONE
                                           : attacker.ailment == test.ailment;
    }
    ck(triggered, test.name);
  }
  Combatant attacker = mon(), target = mon(ABILITY_SAND_SPIT);
  battleAct(attacker, target, clear, basic, log);
  ck(clear.weather == BWEATHER_SAND && log.weatherSet == BWEATHER_SAND,
     "Sand Spit starts sand after being hit");
  target = mon(ABILITY_SPICY_SPRAY); attacker = mon(); clear = BattleField();
  battleAct(attacker, target, clear, basic, log);
  ck(attacker.ailment == AIL_BURN, "Spicy Spray burns an attacker after damage");
  attacker = mon(ABILITY_MERCILESS); target = mon(ABILITY_ANGER_POINT);
  target.ailment = AIL_POISON;
  battleAct(attacker, target, clear, attack(T_NORMAL, MC_PHYS, 10), log);
  ck(log.crit && target.stage[SI_ATK] == 6,
     "Anger Point maximizes Attack after a critical hit");
  target = mon(ABILITY_WEAK_ARMOR);
  battleAct(plain, target, clear, basic, log);
  ck(target.stage[SI_DEF] == -1 && target.stage[SI_SPE] == 2,
     "Weak Armor trades Defense for Speed after a physical hit");
  target = mon(ABILITY_WATER_COMPACTION);
  battleAct(plain, target, clear, attack(T_WATER), log);
  ck(target.stage[SI_DEF] == 2, "Water Compaction raises Defense after a Water hit");
  target = mon(ABILITY_JUSTIFIED);
  battleAct(plain, target, clear, attack(T_DARK), log);
  ck(target.stage[SI_ATK] == 1, "Justified raises Attack after a Dark hit");
  target = mon(ABILITY_RATTLED);
  battleAct(plain, target, clear, attack(T_DARK), log);
  ck(target.stage[SI_SPE] == 1, "Rattled raises Speed after a triggering hit");
  target = mon(ABILITY_STEAM_ENGINE);
  battleAct(plain, target, clear, attack(T_FIRE), log);
  ck(target.stage[SI_SPE] == 6, "Steam Engine maximizes Speed after a Fire hit");
  target = mon(ABILITY_THERMAL_EXCHANGE);
  battleAct(plain, target, clear, attack(T_FIRE), log);
  ck(target.stage[SI_ATK] == 1, "Thermal Exchange raises Attack after a Fire hit");

  BattleMove threshold = attack(T_NORMAL, MC_PHYS, 80);
  target = mon(ABILITY_BERSERK); target.maxHp = 200; target.hp = 110;
  battleAct(plain, target, clear, threshold, log);
  ck(target.hp <= 100 && target.stage[SI_SPA] == 1,
     "Berserk raises Special Attack when crossing half HP");
  target = mon(ABILITY_ANGER_SHELL); target.maxHp = 200; target.hp = 110;
  battleAct(plain, target, clear, threshold, log);
  ck(target.hp <= 100 && target.stage[SI_ATK] == 1 &&
     target.stage[SI_DEF] == -1 && target.stage[SI_SPE] == 1,
     "Anger Shell changes all five stats when crossing half HP");
  target = mon(ABILITY_WIMP_OUT); target.maxHp = 200; target.hp = 110;
  battleAct(plain, target, clear, threshold, log);
  ck(target.hp <= 100 && log.switchRequest == BSWITCH_TARGET,
     "Wimp Out requests a switch when crossing half HP");

  // Knockout hooks.
  struct KnockoutCase { AbilityKey ability; uint8_t stat; const char *name; };
  const KnockoutCase knockoutCases[] = {
    { ABILITY_CHILLING_NEIGH, SI_ATK, "Chilling Neigh raises Attack after a knockout" },
    { ABILITY_GRIM_NEIGH, SI_SPA, "Grim Neigh raises Special Attack after a knockout" },
    { ABILITY_SOUL_HEART, SI_SPA, "Soul-Heart raises Special Attack after a knockout" },
  };
  BattleMove knockout = attack(T_NORMAL, MC_PHYS, 250);
  for (const KnockoutCase &test : knockoutCases) {
    Combatant winner = mon(test.ability), victim = mon(); victim.hp = 20;
    battleAct(winner, victim, clear, knockout, log);
    ck(victim.fainted() && winner.stage[test.stat] == 1, test.name);
  }
  Combatant beast = mon(ABILITY_BEAST_BOOST), victim = mon();
  beast.base[SI_SPE] = beast.nativeBase[SI_SPE] = 140; victim.hp = 20;
  battleAct(beast, victim, clear, knockout, log);
  ck(victim.fainted() && beast.stage[SI_SPE] == 1,
     "Beast Boost raises the user's highest base stat after a knockout");
  attacker = mon(); victim = mon(ABILITY_INNARDS_OUT); victim.hp = 20;
  uint16_t attackerBefore = attacker.hp;
  battleAct(attacker, victim, clear, knockout, log);
  ck(victim.fainted() && attacker.hp == attackerBefore - 20,
     "Innards Out damages the attacker by the victim's remaining HP");

  // Entry, switching and type-absorption hooks.
  struct EntryWeather { AbilityKey ability; BattleWeather weather; const char *name; };
  const EntryWeather entryWeathers[] = {
    { ABILITY_DROUGHT, BWEATHER_SUN, "Drought starts sun on entry" },
    { ABILITY_SAND_STREAM, BWEATHER_SAND, "Sand Stream starts sand on entry" },
    { ABILITY_SNOW_WARNING, BWEATHER_SNOW, "Snow Warning starts snow on entry" },
  };
  EntryLog entryLog;
  for (const EntryWeather &test : entryWeathers) {
    Combatant entrant = mon(test.ability), opponent = mon(); BattleField field;
    battleOnEnter(entrant, opponent, field, 0, entryLog);
    ck(field.weather == test.weather && entryLog.weatherSet == test.weather, test.name);
  }
  struct EntryTerrain { AbilityKey ability; BattleTerrain terrain; const char *name; };
  const EntryTerrain entryTerrains[] = {
    { ABILITY_PSYCHIC_SURGE, BTERRAIN_PSYCHIC, "Psychic Surge starts Psychic Terrain" },
    { ABILITY_MISTY_SURGE, BTERRAIN_MISTY, "Misty Surge starts Misty Terrain" },
    { ABILITY_GRASSY_SURGE, BTERRAIN_GRASSY, "Grassy Surge starts Grassy Terrain" },
  };
  for (const EntryTerrain &test : entryTerrains) {
    Combatant entrant = mon(test.ability), opponent = mon(); BattleField field;
    battleOnEnter(entrant, opponent, field, 0, entryLog);
    ck(field.terrain == test.terrain && entryLog.terrainSet == test.terrain, test.name);
  }
  Combatant regenerating = mon(ABILITY_REGENERATOR); regenerating.hp = 400;
  battleOnSwitchOut(regenerating);
  ck(regenerating.hp == 733, "Regenerator restores one third HP on switch-out");

  struct AbsorbCase { AbilityKey ability; uint8_t type; uint8_t stat; const char *name; };
  const AbsorbCase absorbers[] = {
    { ABILITY_VOLT_ABSORB, T_ELECTRIC, SI_COUNT, "Volt Absorb heals from Electric moves" },
    { ABILITY_LIGHTNING_ROD, T_ELECTRIC, SI_SPA, "Lightning Rod raises Special Attack" },
    { ABILITY_MOTOR_DRIVE, T_ELECTRIC, SI_SPE, "Motor Drive raises Speed" },
    { ABILITY_WELL_BAKED_BODY, T_FIRE, SI_DEF, "Well-Baked Body sharply raises Defense" },
    { ABILITY_SAP_SIPPER, T_GRASS, SI_ATK, "Sap Sipper raises Attack" },
    { ABILITY_EARTH_EATER, T_GROUND, SI_COUNT, "Earth Eater heals from Ground moves" },
    { ABILITY_STORM_DRAIN, T_WATER, SI_SPA, "Storm Drain raises Special Attack" },
  };
  for (const AbsorbCase &test : absorbers) {
    Combatant source = mon(), absorbed = mon(test.ability); absorbed.hp = 500;
    BattleField field; TurnLog absorbLog;
    battleAct(source, absorbed, field, attack(test.type), absorbLog);
    bool effect = test.stat == SI_COUNT ? absorbed.hp > 500
                                       : absorbed.stage[test.stat] > 0;
    ck(absorbLog.immune && effect, test.name);
  }
  defender = mon(ABILITY_FLASH_FIRE);
  battleAct(plain, defender, clear, attack(T_FIRE), log);
  ck(log.immune && defender.flashFireActive,
     "Flash Fire absorbs Fire and arms its damage boost");
  Combatant drainer = mon(); drainer.hp = 800; defender = mon(ABILITY_LIQUID_OOZE);
  BattleMove drain = attack(); drain.entry.effect = EF_DRAIN; drain.entry.param = 50;
  battleAct(drainer, defender, clear, drain, log);
  ck(drainer.hp < 800, "Liquid Ooze turns drain healing into damage");

  // Action lifecycle, secondary effects, critical rate and end of round.
  Combatant truant = mon(ABILITY_TRUANT); defender = mon();
  battleAct(truant, defender, clear, basic, log);
  uint16_t afterFirst = defender.hp;
  battleAct(truant, defender, clear, basic, log);
  ck(afterFirst < defender.maxHp && log.skipped && defender.hp == afterFirst,
     "Truant alternates an acting turn with a loafing turn");
  Combatant early = mon(ABILITY_EARLY_BIRD); early.ailment = AIL_SLEEP; early.ailTurns = 2;
  defender = mon();
  battleAct(early, defender, clear, basic, log);
  ck(early.ailment == AIL_NONE && !log.skipped && defender.hp < defender.maxHp,
     "Early Bird counts down two sleep turns at once");
  ck(ailmentCount(ABILITY_SERENE_GRACE) > ailmentCount(ABILITY_NONE),
     "Serene Grace increases secondary-effect frequency");
  ck(critCount(ABILITY_SUPER_LUCK) > critCount(ABILITY_NONE),
     "Super Luck increases critical-hit frequency");
  Combatant linker = mon(ABILITY_SKILL_LINK); defender = mon();
  BattleMove multi = attack(T_NORMAL, MC_PHYS, 5); multi.entry.effect = EF_MULTI;
  battleAct(linker, defender, clear, multi, log);
  ck(log.hits == 5, "Skill Link makes multi-hit attacks strike five times");
  attacker = mon(ABILITY_MERCILESS); defender = mon(); defender.ailment = AIL_POISON;
  battleAct(attacker, defender, clear, basic, log);
  ck(log.crit, "Merciless guarantees a critical hit against poisoned targets");
  attacker = mon(ABILITY_POISON_PUPPETEER); defender = mon();
  BattleMove poison = attack(T_NORMAL, MC_SPEC, 1);
  poison.entry.ailment = AIL_POISON; poison.entry.ailChance = 100;
  battleAct(attacker, defender, clear, poison, log);
  ck(defender.ailment == AIL_POISON && defender.confuseTurns,
     "Poison Puppeteer also confuses a newly poisoned target");
  attacker = mon(ABILITY_LIBERO); defender = mon();
  battleAct(attacker, defender, clear, attack(T_FIRE), log);
  ck(attacker.type1 == T_FIRE && attacker.type2 == T_NONE,
     "Libero changes the user to the selected move's type once per entry");

  TurnLog aLog, bLog; FieldLog fieldLog;
  Combatant ender = mon(ABILITY_HYDRATION), endFoe = mon();
  ender.ailment = AIL_BURN; BattleField rain; rain.weather = BWEATHER_RAIN;
  battleEndRound(rain, ender, endFoe, aLog, bLog, fieldLog);
  ck(ender.ailment == AIL_NONE, "Hydration cures status in rain");
  ender = mon(ABILITY_SHED_SKIN); ender.ailment = AIL_BURN; g_seed = 0x5EED;
  for (uint8_t i = 0; i < 30 && ender.ailment != AIL_NONE; i++) {
    ender.hp = ender.maxHp;
    battleEndRound(clear, ender, endFoe, aLog, bLog, fieldLog);
  }
  ck(ender.ailment == AIL_NONE, "Shed Skin can cure a status at end of turn");
  ender = mon(ABILITY_ICE_BODY); ender.hp = 500;
  BattleField snow; snow.weather = BWEATHER_SNOW;
  battleEndRound(snow, ender, endFoe, aLog, bLog, fieldLog);
  ck(ender.hp > 500, "Ice Body heals in snow");
  ender = mon(ABILITY_DRY_SKIN); ender.hp = 500;
  battleEndRound(rain, ender, endFoe, aLog, bLog, fieldLog);
  ck(ender.hp == 625, "Dry Skin heals in rain");
  ender = mon(ABILITY_HEATPROOF); ender.ailment = AIL_BURN;
  battleEndRound(clear, ender, endFoe, aLog, bLog, fieldLog);
  ck(aLog.damage == ender.maxHp / 32, "Heatproof halves burn damage");
  ender = mon(ABILITY_SOLAR_POWER); ender.hp = 1000;
  battleEndRound(sun, ender, endFoe, aLog, bLog, fieldLog);
  ck(ender.hp == 875, "Solar Power costs one eighth HP in sun");

  std::printf("\nability gap assertions failed: %d\n", bad);
  return bad ? 1 : 0;
}

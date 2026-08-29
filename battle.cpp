#include "battle.h"
#include "dex.h"
#include "types.h"

// ---------- building a combatant ----------

static uint16_t bondedBattleStat(uint16_t value, uint8_t bond) {
  uint8_t boundedBond = bond > 100 ? 100 : bond;
  uint32_t scaled = (uint32_t)value * (70u + boundedBond / 2u) / 100u;
  return scaled > UINT16_MAX ? UINT16_MAX : (uint16_t)scaled;
}

static void fill(Combatant &c, int16_t dex, uint8_t lvl, uint16_t hp,
                 uint16_t a, uint16_t d, uint16_t sa, uint16_t sd, uint16_t sp,
                 uint8_t bond) {
  c = Combatant();
  c.dex = dex;
  c.level = lvl;
  c.maxHp = bondedBattleStat(hp, bond);
  if (!c.maxHp) c.maxHp = 1;
  c.hp = c.maxHp;
  c.base[SI_ATK] = bondedBattleStat(a, bond);
  c.base[SI_DEF] = bondedBattleStat(d, bond);
  c.base[SI_SPA] = bondedBattleStat(sa, bond);
  c.base[SI_SPD] = bondedBattleStat(sd, bond);
  c.base[SI_SPE] = bondedBattleStat(sp, bond);
  if (dex >= 1 && dex <= dexCount()) {
    c.type1 = dexEntry(dex).type1;
    c.type2 = dexEntry(dex).type2;
  }
  c.nativeType1 = c.type1;
  c.nativeType2 = c.type2;
  for (uint8_t i = 0; i < SI_COUNT; i++) c.nativeBase[i] = c.base[i];
}

void combatantFromPet(Combatant &c, const Pet &p) {
  fill(c, p.speciesId, p.level(), p.vitStat(), p.atkStat(), p.defStat(),
       p.spaStat(), p.spdStat(), p.speStat(), p.bond);
  for (int i = 0; i < MOVE_SLOTS; i++) c.moves[i] = p.moves[i];
  c.shiny = p.shiny;
  c.ability = speciesAbility(p.speciesId, p.abilitySlot);
  battleInitializeForm(c);
  c.gender = p.gender;
  c.gigantamaxFactor = p.gigantamaxFactor;
  const char *nm = p.nick[0] ? p.nick : dexEntry(p.speciesId).name;
  snprintf(c.name, sizeof(c.name), "%s", nm);
}

void combatantFromParty(Combatant &c, const PartyMon &m) {
  fill(c, m.dex, (uint8_t)m.level, party.vitOf(m), party.atkOf(m), party.defOf(m),
       party.spaOf(m), party.spdOf(m), party.speOf(m), m.bond);
  for (int i = 0; i < MOVE_SLOTS; i++) c.moves[i] = m.moves[i];
  c.shiny = m.shiny != 0 || m.sparkle != 0;
  c.ability = speciesAbility(m.dex, m.abilitySlot());
  battleInitializeForm(c);
  c.gender = m.gender;
  c.gigantamaxFactor = m.gigantamaxFactor();
  const char *nm = m.nick[0] ? m.nick : dexEntry(m.dex).name;
  snprintf(c.name, sizeof(c.name), "%s", nm);
}

// ---------- special battle mechanics ----------

static bool combatantHasType(const Combatant &c, uint8_t type) {
  return c.type1 == type || c.type2 == type;
}

static bool combatantHasAbility(const Combatant &c, AbilityKey ability) {
  return c.ability == ability;
}

static bool exclusiveAbility(const Combatant &c, SpeciesId species,
                             AbilityKey ability) {
  return c.dex == species && combatantHasAbility(c, ability);
}

static bool weatherSuppressed(const Combatant &a, const Combatant &b);

struct ExclusiveFormStats {
  BattleForm form;
  uint8_t base[SI_COUNT];
  uint8_t changed[SI_COUNT];
  uint8_t type1, type2;
};

static const ExclusiveFormStats EXCLUSIVE_FORM_STATS[] = {
  { BFORM_DARMANITAN_ZEN, {140,55,30,55,95}, {30,105,140,105,55}, T_FIRE, T_PSYCHIC },
  { BFORM_AEGISLASH_BLADE, {50,140,50,140,60}, {140,50,140,50,60}, T_STEEL, T_GHOST },
  { BFORM_WISHIWASHI_SCHOOL, {20,20,25,25,40}, {140,130,140,135,30}, T_WATER, T_NONE },
  { BFORM_MINIOR_CORE, {60,100,60,100,60}, {100,60,100,60,120}, T_ROCK, T_FLYING },
  { BFORM_EISCUE_NOICE, {80,110,65,90,50}, {80,70,65,50,130}, T_ICE, T_NONE },
  { BFORM_PALAFIN_HERO, {70,72,53,62,100}, {160,97,106,87,100}, T_WATER, T_NONE },
};

static bool setBattleForm(Combatant &combatant, BattleForm form) {
  if (combatant.form == form) return false;
  for (uint8_t i = 0; i < SI_COUNT; i++) combatant.base[i] = combatant.nativeBase[i];
  combatant.type1 = combatant.nativeType1;
  combatant.type2 = combatant.nativeType2;
  for (const ExclusiveFormStats &rule : EXCLUSIVE_FORM_STATS) {
    if (rule.form != form) continue;
    for (uint8_t i = 0; i < SI_COUNT; i++) {
      int32_t value = (int32_t)combatant.nativeBase[i] +
                      rule.changed[i] - rule.base[i];
      combatant.base[i] = value < 1 ? 1 : value > UINT16_MAX
                                      ? UINT16_MAX : (uint16_t)value;
    }
    combatant.type1 = rule.type1;
    combatant.type2 = rule.type2;
    break;
  }
  if (form == BFORM_CASTFORM_SUN) combatant.type1 = T_FIRE;
  else if (form == BFORM_CASTFORM_RAIN) combatant.type1 = T_WATER;
  else if (form == BFORM_CASTFORM_SNOW) combatant.type1 = T_ICE;
  combatant.form = form;
  return true;
}

void battleInitializeForm(Combatant &combatant) {
  combatant.nativeType1 = combatant.type1;
  combatant.nativeType2 = combatant.type2;
  for (uint8_t i = 0; i < SI_COUNT; i++) combatant.nativeBase[i] = combatant.base[i];
  if (exclusiveAbility(combatant, 778, ABILITY_DISGUISE))
    combatant.form = BFORM_MIMIKYU_DISGUISED;
  else if (exclusiveAbility(combatant, 875, ABILITY_ICE_FACE))
    combatant.form = BFORM_EISCUE_ICE;
  else if (exclusiveAbility(combatant, 877, ABILITY_HUNGER_SWITCH))
    combatant.form = BFORM_MORPEKO_FULL;
  else
    combatant.form = BFORM_BASE;
}

static BattleForm weatherForm(const Combatant &combatant,
                              const BattleField &field) {
  if (exclusiveAbility(combatant, 351, ABILITY_FORECAST)) {
    if (field.weather == BWEATHER_SUN) return BFORM_CASTFORM_SUN;
    if (field.weather == BWEATHER_RAIN) return BFORM_CASTFORM_RAIN;
    if (field.weather == BWEATHER_SNOW) return BFORM_CASTFORM_SNOW;
  }
  if (exclusiveAbility(combatant, 421, ABILITY_FLOWER_GIFT) &&
      field.weather == BWEATHER_SUN) return BFORM_CHERRIM_SUN;
  return BFORM_BASE;
}

static bool refreshForm(Combatant &combatant, const BattleField &field) {
  if (combatant.fainted()) return false;
  if (exclusiveAbility(combatant, 351, ABILITY_FORECAST) ||
      exclusiveAbility(combatant, 421, ABILITY_FLOWER_GIFT))
    return setBattleForm(combatant, weatherForm(combatant, field));
  if (exclusiveAbility(combatant, 555, ABILITY_ZEN_MODE))
    return setBattleForm(combatant, combatant.hp * 2u <= combatant.maxHp
                                      ? BFORM_DARMANITAN_ZEN : BFORM_BASE);
  if (exclusiveAbility(combatant, 746, ABILITY_SCHOOLING))
    return setBattleForm(combatant, combatant.level >= 20 &&
                                      combatant.hp * 4u > combatant.maxHp
                                      ? BFORM_WISHIWASHI_SCHOOL : BFORM_BASE);
  if (exclusiveAbility(combatant, 774, ABILITY_SHIELDS_DOWN))
    return setBattleForm(combatant, combatant.hp * 2u <= combatant.maxHp
                                      ? BFORM_MINIOR_CORE : BFORM_BASE);
  if (exclusiveAbility(combatant, 875, ABILITY_ICE_FACE) &&
      combatant.form == BFORM_EISCUE_NOICE && field.weather == BWEATHER_SNOW)
    return setBattleForm(combatant, BFORM_EISCUE_ICE);
  if (exclusiveAbility(combatant, 964, ABILITY_ZERO_TO_HERO) &&
      combatant.formPrimed)
    return setBattleForm(combatant, BFORM_PALAFIN_HERO);
  return false;
}

void battleRefreshForms(BattleField &field, Combatant &a, Combatant &b) {
  BattleField effective = field;
  if (weatherSuppressed(a, b)) effective.weather = BWEATHER_NONE;
  refreshForm(a, effective);
  refreshForm(b, effective);
}

static bool moveMakesContact(const Combatant &attacker, const MoveEntry &move) {
  return (move.tags & MT_CONTACT) &&
         !combatantHasAbility(attacker, ABILITY_LONG_REACH);
}

static uint16_t typeEffVsCombatant(uint8_t attack, const Combatant &defender,
                                   const Combatant *attacker = nullptr,
                                   bool gravity = false) {
  if (attack == T_GROUND && !gravity &&
      (combatantHasAbility(defender, ABILITY_LEVITATE) ||
       combatantHasAbility(defender, ABILITY_EELEVATE))) return 0;
  uint16_t effect;
  if (attack == T_GROUND && gravity) {
    uint16_t first = defender.type1 == T_FLYING
        ? 10 : typeEffectTenth(attack, defender.type1);
    uint16_t second = defender.type2 == T_FLYING
        ? 10 : defender.type2 < TYPE_COUNT
            ? typeEffectTenth(attack, defender.type2) : 10;
    effect = first * second;
  } else {
    effect = typeEffPct(attack, defender.type1, defender.type2);
  }
  if (attacker && (attack == T_NORMAL || attack == T_FIGHTING) && effect == 0 &&
      combatantHasAbility(*attacker, ABILITY_SCRAPPY)) {
    uint8_t other = defender.type1 == T_GHOST ? defender.type2 : defender.type1;
    effect = other < TYPE_COUNT ? typeEffPct(attack, other, T_NONE) : 100;
  }
  if (attacker && combatantHasAbility(defender, ABILITY_WONDER_GUARD) && effect <= 100)
    effect = 0;
  return effect;
}

void battleSetEnvironment(BattleField &field, BattleWeather weather,
                          BattleTerrain terrain) {
  field = BattleField();
  if (weather > BWEATHER_SNOW) weather = BWEATHER_NONE;
  if (terrain > BTERRAIN_PSYCHIC) terrain = BTERRAIN_NONE;
  field.baseWeather = field.weather = weather;
  field.baseTerrain = field.terrain = terrain;
}

void battleSetWeather(BattleField &field, BattleWeather weather) {
  if (weather > BWEATHER_SNOW) return;
  field.weather = weather == BWEATHER_NONE ? field.baseWeather : weather;
  field.weatherTurns = weather == BWEATHER_NONE ? 0 : BATTLE_FIELD_TURNS;
}

void battleSetTerrain(BattleField &field, BattleTerrain terrain) {
  if (terrain > BTERRAIN_PSYCHIC) return;
  field.terrain = terrain == BTERRAIN_NONE ? field.baseTerrain : terrain;
  field.terrainTurns = terrain == BTERRAIN_NONE ? 0 : BATTLE_FIELD_TURNS;
}

bool battleGrounded(const Combatant &combatant, const BattleField *field) {
  if (field && field->gravityTurns) return true;
  return !combatantHasType(combatant, T_FLYING) &&
         !combatantHasAbility(combatant, ABILITY_LEVITATE) &&
         !combatantHasAbility(combatant, ABILITY_EELEVATE);
}

bool battleGuaranteedEscape(const Combatant &combatant) {
  return combatantHasAbility(combatant, ABILITY_RUN_AWAY);
}

bool battleCanSwitch(const Combatant &combatant, const Combatant &opponent,
                     const BattleField *field) {
  if (combatantHasType(combatant, T_GHOST)) return true;
  if (combatant.trapped || combatant.bindTurns) return false;
  if (combatantHasAbility(opponent, ABILITY_SHADOW_TAG)) return false;
  if (combatantHasAbility(opponent, ABILITY_ARENA_TRAP) &&
      battleGrounded(combatant, field)) return false;
  if (combatantHasAbility(opponent, ABILITY_MAGNET_PULL) &&
      combatantHasType(combatant, T_STEEL)) return false;
  return true;
}

BattleMove battleMove(MoveId move) {
  BattleMove result;
  if (!moveValid(move)) return result;
  result.source = move;
  result.entry = moveEntry(move);
  return result;
}

static uint8_t zPower(uint8_t power) {
  if (power <= 55) return 100;
  if (power <= 60) return 120;
  if (power <= 70) return 140;
  if (power <= 85) return 160;
  if (power <= 95) return 175;
  if (power <= 100) return 180;
  if (power <= 110) return 185;
  if (power <= 120) return 190;
  if (power <= 130) return 195;
  return 200;
}

static uint8_t maxPower(uint8_t power, uint8_t type) {
  uint8_t result = power <= 40 ? 90 : power <= 50 ? 100 : power <= 60 ? 110
                   : power <= 70 ? 120 : power <= 100 ? 130
                   : power <= 140 ? 140 : 150;
  if (type == T_FIGHTING || type == T_POISON)
    result = result <= 100 ? 70 : result <= 120 ? 80 : result <= 140 ? 90 : 100;
  return result;
}

static void setMaxStageEffect(MoveEntry &move) {
  move.statMask = 0;
  move.stages = 0;
  move.target = TG_SELF;
  switch (move.type) {
    case T_FIRE:     move.effect = EF_SET_WEATHER; move.param = BWEATHER_SUN; break;
    case T_WATER:    move.effect = EF_SET_WEATHER; move.param = BWEATHER_RAIN; break;
    case T_ROCK:     move.effect = EF_SET_WEATHER; move.param = BWEATHER_SAND; break;
    case T_ICE:      move.effect = EF_SET_WEATHER; move.param = BWEATHER_SNOW; break;
    case T_ELECTRIC: move.effect = EF_SET_TERRAIN; move.param = BTERRAIN_ELECTRIC; break;
    case T_GRASS:    move.effect = EF_SET_TERRAIN; move.param = BTERRAIN_GRASSY; break;
    case T_FAIRY:    move.effect = EF_SET_TERRAIN; move.param = BTERRAIN_MISTY; break;
    case T_PSYCHIC:  move.effect = EF_SET_TERRAIN; move.param = BTERRAIN_PSYCHIC; break;
    case T_NORMAL:   move.statMask = ST_SPE; move.stages = -1; move.target = TG_FOE; break;
    case T_FIGHTING: move.statMask = ST_ATK; move.stages = 1; break;
    case T_FLYING:   move.statMask = ST_SPE; move.stages = 1; break;
    case T_POISON:   move.statMask = ST_SPA; move.stages = 1; break;
    case T_GROUND:   move.statMask = ST_SPD; move.stages = 1; break;
    case T_BUG:      move.statMask = ST_SPA; move.stages = -1; move.target = TG_FOE; break;
    case T_GHOST:    move.statMask = ST_DEF; move.stages = -1; move.target = TG_FOE; break;
    case T_DRAGON:   move.statMask = ST_ATK; move.stages = -1; move.target = TG_FOE; break;
    case T_DARK:     move.statMask = ST_SPD; move.stages = -1; move.target = TG_FOE; break;
    case T_STEEL:    move.statMask = ST_DEF; move.stages = 1; break;
    default: break;
  }
}

BattleMove battleMoveFor(const Combatant &attacker, MoveId move,
                         BattleMechanic requested) {
  BattleMove result = battleMove(move);
  if (!result.valid()) return result;
  if ((result.entry.fieldFlags & MF_AURA_WHEEL) &&
      attacker.form == BFORM_MORPEKO_HANGRY) {
    result.entry.type = T_DARK;
  } else if ((result.entry.tags & MT_SOUND) &&
      combatantHasAbility(attacker, ABILITY_LIQUID_VOICE)) {
    result.entry.type = T_WATER;
  } else if (combatantHasAbility(attacker, ABILITY_NORMALIZE)) {
    result.entry.type = T_NORMAL;
  } else if (result.entry.type == T_NORMAL) {
    if (combatantHasAbility(attacker, ABILITY_REFRIGERATE)) result.entry.type = T_ICE;
    else if (combatantHasAbility(attacker, ABILITY_PIXILATE)) result.entry.type = T_FAIRY;
    else if (combatantHasAbility(attacker, ABILITY_AERILATE)) result.entry.type = T_FLYING;
    else if (combatantHasAbility(attacker, ABILITY_DRAGONIZE)) result.entry.type = T_DRAGON;
  }
  if (requested == BMECH_Z_MOVE && result.entry.cat != MC_STATUS) {
    result.mechanic = BMECH_Z_MOVE;
    result.entry.power = zPower(result.entry.power);
    result.entry.acc = 0;
    result.entry.effect = EF_NONE;
    result.entry.param = 0;
    result.entry.statMask = 0;
    result.entry.stages = 0;
    result.entry.ailment = AIL_NONE;
    result.entry.ailChance = 0;
    result.entry.fieldFlags = MF_NONE;
  } else if (attacker.activeMechanic == BMECH_DYNAMAX ||
             requested == BMECH_DYNAMAX) {
    result.mechanic = BMECH_DYNAMAX;
    result.entry.acc = 0;
    result.entry.ailment = AIL_NONE;
    result.entry.ailChance = 0;
    result.entry.fieldFlags = MF_NONE;
    if (result.entry.cat == MC_STATUS) {
      result.entry.effect = EF_PROTECT;
      result.entry.param = 4;  // Max Guard priority
      result.entry.statMask = 0;
      result.entry.stages = 0;
    } else {
      result.entry.power = maxPower(result.entry.power, result.entry.type);
      result.entry.effect = EF_NONE;
      result.entry.param = 0;
      bool gigantamax = attacker.gigantamax ||
          (requested == BMECH_DYNAMAX && attacker.gigantamaxFactor &&
           battleGigantamaxEligible(attacker.dex));
      const GmaxMoveEntry *signature = gigantamax
          ? gmaxMoveFor(attacker.dex, result.entry.type) : nullptr;
      if (signature) {
        result.gmaxMove = signature->id;
        result.gmaxEffect = signature->effect;
        if (signature->power) result.entry.power = signature->power;
      } else {
        setMaxStageEffect(result.entry);
      }
    }
  }
  return result;
}

bool battleMegaEligible(SpeciesId species, MegaFormKind form) {
  return megaFormFor(species, form) != nullptr;
}

bool battleDynamaxEligible(SpeciesId species) {
  return dexValid(species) && species != 888 && species != 889 && species != 890;
}

bool battleGigantamaxEligible(SpeciesId species) {
  return contentGigantamaxEligible(species);
}

static bool hasDamagingMove(const Combatant &combatant) {
  for (uint8_t i = 0; i < MOVE_SLOTS; i++)
    if (moveValid(combatant.moves[i]) && moveEntry(combatant.moves[i]).cat != MC_STATUS)
      return true;
  return false;
}

bool battleMechanicAvailable(const BattleSideMechanics &side,
                             const Combatant &combatant,
                             BattleMechanic mechanic, MoveId move,
                             MegaFormKind megaForm) {
  if (mechanic == BMECH_NONE || combatant.fainted() ||
      combatant.usedMechanic != BMECH_NONE ||
      side.used(mechanic)) return false;
  if (mechanic == BMECH_MEGA) return battleMegaEligible(combatant.dex, megaForm);
  if (mechanic == BMECH_Z_MOVE) {
    if (moveValid(move)) return moveEntry(move).cat != MC_STATUS;
    return hasDamagingMove(combatant);
  }
  return mechanic == BMECH_DYNAMAX && battleDynamaxEligible(combatant.dex);
}

static void applyMegaForm(Combatant &combatant, MegaFormKind requested) {
  const MegaFormEntry *form = megaFormFor(combatant.dex, requested);
  if (!form) return;
  if (dexValid(combatant.dex)) {
    const DexEntry &baseSpecies = dexEntry(combatant.dex);
    const uint8_t oldBase[SI_COUNT] = {
      baseSpecies.bAtk, baseSpecies.bDef, baseSpecies.bSpA,
      baseSpecies.bSpD, baseSpecies.bSpe,
    };
    const uint8_t newBase[SI_COUNT] = {
      form->bAtk, form->bDef, form->bSpA, form->bSpD, form->bSpe,
    };
    for (uint8_t i = 0; i < SI_COUNT; i++) {
      int32_t changed = (int32_t)combatant.base[i] + newBase[i] - oldBase[i];
      combatant.base[i] = changed < 1 ? 1
                           : changed > UINT16_MAX ? UINT16_MAX : (uint16_t)changed;
    }
  } else {
    // Recovery/test environments may have the move pack without a region pack.
    // Eligibility still comes from real form data; this fallback keeps the
    // battle object valid until species base stats are available again.
    for (uint8_t i = 0; i < SI_COUNT; i++)
      combatant.base[i] = (uint16_t)((uint32_t)combatant.base[i] * 6u / 5u);
  }
  combatant.type1 = form->type1;
  combatant.type2 = form->type2;
  if (form->ability) combatant.ability = form->ability;
  combatant.megaForm = form->form;
}

bool battleActivateMechanic(BattleSideMechanics &side, Combatant &combatant,
                            BattleMechanic mechanic, MoveId move,
                            MegaFormKind megaForm) {
  if (!battleMechanicAvailable(side, combatant, mechanic, move, megaForm)) return false;
  side.usedMask |= battleMechanicBit(mechanic);
  combatant.usedMechanic = mechanic;
  if (mechanic == BMECH_DYNAMAX) {
    combatant.activeMechanic = mechanic;
    combatant.gigantamax = combatant.gigantamaxFactor &&
                           battleGigantamaxEligible(combatant.dex);
    combatant.dynamaxTurns = 3;
    combatant.normalMaxHp = combatant.maxHp;
    combatant.maxHp = combatant.maxHp > UINT16_MAX / 2 ? UINT16_MAX
                                                       : (uint16_t)(combatant.maxHp * 2u);
    combatant.hp = combatant.hp > UINT16_MAX / 2 ? UINT16_MAX
                                                 : (uint16_t)(combatant.hp * 2u);
  } else if (mechanic == BMECH_MEGA) {
    combatant.activeMechanic = mechanic;
    applyMegaForm(combatant, megaForm);
  }
  return true;
}

static void endDynamax(Combatant &combatant) {
  if (combatant.activeMechanic != BMECH_DYNAMAX) return;
  uint16_t oldMax = combatant.maxHp;
  uint16_t normal = combatant.normalMaxHp ? combatant.normalMaxHp
                                          : (uint16_t)((oldMax + 1u) / 2u);
  if (combatant.hp && oldMax) {
    uint32_t hp = ((uint32_t)combatant.hp * normal + oldMax - 1u) / oldMax;
    combatant.hp = hp ? (uint16_t)hp : 1;
  }
  combatant.maxHp = normal ? normal : 1;
  combatant.normalMaxHp = 0;
  combatant.dynamaxTurns = 0;
  combatant.activeMechanic = BMECH_NONE;
  combatant.gigantamax = false;
}

void battleAfterAction(Combatant &combatant) {
  if (combatant.activeMechanic != BMECH_DYNAMAX) return;
  if (combatant.dynamaxTurns) combatant.dynamaxTurns--;
  if (!combatant.dynamaxTurns || combatant.fainted()) endDynamax(combatant);
}

void battleOnSwitchOut(Combatant &combatant, Combatant *opponent) {
  endDynamax(combatant);
  if (exclusiveAbility(combatant, 964, ABILITY_ZERO_TO_HERO))
    combatant.formPrimed = true;
  if (combatantHasAbility(combatant, ABILITY_NATURAL_CURE)) {
    combatant.ailment = AIL_NONE;
    combatant.ailTurns = 0;
  }
  if (combatantHasAbility(combatant, ABILITY_REGENERATOR) && !combatant.fainted()) {
    uint16_t amount = combatant.maxHp / 3u;
    if (!amount) amount = 1;
    uint32_t hp = (uint32_t)combatant.hp + amount;
    combatant.hp = hp > combatant.maxHp ? combatant.maxHp : (uint16_t)hp;
  }
  combatant.protectedTurn = false;
  combatant.bindTurns = 0;
  combatant.drowsyTurns = 0;
  combatant.trapped = false;
  combatant.tormented = false;
  combatant.infatuated = false;
  if (opponent) opponent->infatuated = false;
  combatant.lastMove = MOVE_NONE;
  combatant.abilityTriggered = false;
  combatant.abilityCharged = false;
  for (uint8_t i = 0; i < SI_COUNT; i++) combatant.stage[i] = 0;
  combatant.accuracyStage = 0;
  combatant.evasionStage = 0;
}

BattleMechanic wildBattleMechanic(uint8_t eventRoll, uint8_t choiceRoll,
                                  bool hard, bool megaEligible, bool zEligible,
                                  bool dynamaxEligible) {
  if (eventRoll >= (hard ? 20 : 5)) return BMECH_NONE;
  BattleMechanic choices[3];
  uint8_t count = 0;
  if (zEligible) choices[count++] = BMECH_Z_MOVE;
  if (dynamaxEligible) choices[count++] = BMECH_DYNAMAX;
  if (megaEligible) choices[count++] = BMECH_MEGA;
  if (!count) return BMECH_NONE;
  return choices[choiceRoll % count];
}

// ---------- stat stages ----------

// The series' own table, as a fraction so it stays integer: +1 is 3/2, -1 is
// 2/3, and so on out to +6 = 4x and -6 = 1/4.
uint16_t stagedStat(uint16_t base, int8_t stage) {
  if (stage > 6) stage = 6;
  if (stage < -6) stage = -6;
  uint16_t num = 2 + (stage > 0 ? stage : 0);
  uint16_t den = 2 + (stage < 0 ? -stage : 0);
  uint32_t v = (uint32_t)base * num / den;
  return v < 1 ? 1 : (v > 65535 ? 65535 : (uint16_t)v);
}

static uint16_t battleBaseStat(const Combatant &c, uint8_t idx) {
  if (idx >= SI_COUNT) return 1;
  uint32_t value = c.base[idx];
  if (c.angry) value = value * BATTLE_ANGRY_STAT_PERCENT / 100u;
  return value < 1 ? 1 : value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}

static uint8_t highestBaseStat(const Combatant &c) {
  uint8_t best = 0;
  for (uint8_t i = 1; i < SI_COUNT; i++)
    if (c.base[i] > c.base[best]) best = i;
  return best;
}

static bool weatherSuppressed(const Combatant &a, const Combatant &b) {
  return combatantHasAbility(a, ABILITY_CLOUD_NINE) ||
         combatantHasAbility(a, ABILITY_AIR_LOCK) ||
         combatantHasAbility(b, ABILITY_CLOUD_NINE) ||
         combatantHasAbility(b, ABILITY_AIR_LOCK);
}

static uint16_t battleEffectiveStatFor(const Combatant &c, uint8_t idx,
                                       const BattleField *field,
                                       bool ignoreStages = false) {
  if (idx >= SI_COUNT) return 1;
  uint16_t v = ignoreStages ? battleBaseStat(c, idx)
                            : stagedStat(battleBaseStat(c, idx), c.stage[idx]);
  if (idx == SI_ATK && (combatantHasAbility(c, ABILITY_HUGE_POWER) ||
                       combatantHasAbility(c, ABILITY_PURE_POWER)))
    v = v > UINT16_MAX / 2u ? UINT16_MAX : (uint16_t)(v * 2u);
  if (idx == SI_ATK && combatantHasAbility(c, ABILITY_HUSTLE))
    v = (uint16_t)((uint32_t)v * 3u / 2u);
  if (idx == SI_ATK && c.ailment != AIL_NONE && combatantHasAbility(c, ABILITY_GUTS))
    v = v > UINT16_MAX * 2u / 3u ? UINT16_MAX : (uint16_t)((uint32_t)v * 3u / 2u);
  if (idx == SI_DEF && c.ailment != AIL_NONE && combatantHasAbility(c, ABILITY_MARVEL_SCALE))
    v = v > UINT16_MAX * 2u / 3u ? UINT16_MAX : (uint16_t)((uint32_t)v * 3u / 2u);
  if ((idx == SI_ATK || idx == SI_SPA) && c.hp * 2u <= c.maxHp &&
      combatantHasAbility(c, ABILITY_DEFEATIST)) v /= 2u;
  if (idx == SI_ATK && c.ailment == AIL_POISON &&
      combatantHasAbility(c, ABILITY_TOXIC_BOOST))
    v = (uint16_t)((uint32_t)v * 3u / 2u);
  if (idx == SI_SPA && c.ailment == AIL_BURN &&
      combatantHasAbility(c, ABILITY_FLARE_BOOST))
    v = (uint16_t)((uint32_t)v * 3u / 2u);
  if (idx == SI_SPE && c.ailment != AIL_NONE &&
      combatantHasAbility(c, ABILITY_QUICK_FEET))
    v = (uint16_t)((uint32_t)v * 3u / 2u);
  if (field) {
    bool doubleSpeed =
        (field->weather == BWEATHER_RAIN && combatantHasAbility(c, ABILITY_SWIFT_SWIM)) ||
        (field->weather == BWEATHER_SUN && combatantHasAbility(c, ABILITY_CHLOROPHYLL)) ||
        (field->weather == BWEATHER_SAND && combatantHasAbility(c, ABILITY_SAND_RUSH)) ||
        (field->weather == BWEATHER_SNOW && combatantHasAbility(c, ABILITY_SLUSH_RUSH));
    if (idx == SI_SPE && doubleSpeed)
      v = v > UINT16_MAX / 2u ? UINT16_MAX : (uint16_t)(v * 2u);
    bool paradox = (field->weather == BWEATHER_SUN &&
                    combatantHasAbility(c, ABILITY_PROTOSYNTHESIS)) ||
                   (field->terrain == BTERRAIN_ELECTRIC &&
                    combatantHasAbility(c, ABILITY_QUARK_DRIVE));
    if (paradox && idx == highestBaseStat(c))
      v = (uint16_t)((uint32_t)v * (idx == SI_SPE ? 3u : 13u) /
                     (idx == SI_SPE ? 2u : 10u));
  }
  // burn halves physical attack, paralysis halves speed -- the two ailments
  // that do something beyond chip damage
  if (idx == SI_ATK && c.ailment == AIL_BURN &&
      !combatantHasAbility(c, ABILITY_GUTS)) v = v / 2 ? v / 2 : 1;
  if (idx == SI_SPE && c.ailment == AIL_PARA &&
      !combatantHasAbility(c, ABILITY_QUICK_FEET)) v = v / 2 ? v / 2 : 1;
  if ((idx == SI_ATK || idx == SI_SPD) && c.form == BFORM_CHERRIM_SUN)
    v = v > UINT16_MAX * 2u / 3u ? UINT16_MAX
                                  : (uint16_t)((uint32_t)v * 3u / 2u);
  v = (uint16_t)((uint32_t)v * c.statPercent / 100u);
  return v ? v : 1;
}

uint16_t battleEffectiveStat(const Combatant &c, uint8_t idx) {
  return battleEffectiveStatFor(c, idx, nullptr);
}

uint8_t battleAccuracy(const Combatant &atk, const Combatant &def,
                       const BattleField &field, const BattleMove &move) {
  if (!move.valid() || !move.entry.acc || move.entry.effect == EF_NEVER_MISS ||
      combatantHasAbility(atk, ABILITY_NO_GUARD) ||
      combatantHasAbility(def, ABILITY_NO_GUARD)) return 0;
  const MoveEntry &m = move.entry;
  BattleField effectiveField = field;
  if (weatherSuppressed(atk, def)) effectiveField.weather = BWEATHER_NONE;
  if (combatantHasAbility(atk, ABILITY_MEGA_SOL)) effectiveField.weather = BWEATHER_SUN;
  uint16_t accuracy = m.acc;
  if ((m.fieldFlags & MF_RAIN_ACCURATE) && effectiveField.weather == BWEATHER_RAIN)
    return 0;
  if ((m.fieldFlags & MF_RAIN_ACCURATE) && effectiveField.weather == BWEATHER_SUN)
    accuracy = 50;
  if ((m.fieldFlags & MF_SNOW_ACCURATE) && effectiveField.weather == BWEATHER_SNOW)
    return 0;

  int8_t accuracyStage = combatantHasAbility(def, ABILITY_UNAWARE)
      ? 0 : atk.accuracyStage;
  int8_t evasionStage = combatantHasAbility(atk, ABILITY_UNAWARE)
      ? 0 : def.evasionStage;
  int8_t stage = accuracyStage - evasionStage;
  if (stage > 6) stage = 6;
  if (stage < -6) stage = -6;
  accuracy = stage >= 0 ? accuracy * (3u + stage) / 3u
                        : accuracy * 3u / (3u - stage);
  if (combatantHasAbility(atk, ABILITY_COMPOUND_EYES)) accuracy = accuracy * 13u / 10u;
  if (combatantHasAbility(atk, ABILITY_VICTORY_STAR)) accuracy = accuracy * 11u / 10u;
  if (m.cat == MC_PHYS && combatantHasAbility(atk, ABILITY_HUSTLE))
    accuracy = accuracy * 4u / 5u;
  if (m.cat == MC_STATUS && combatantHasAbility(def, ABILITY_WONDER_SKIN) &&
      accuracy > 50) accuracy = 50;
  if ((effectiveField.weather == BWEATHER_SAND &&
       combatantHasAbility(def, ABILITY_SAND_VEIL)) ||
      (effectiveField.weather == BWEATHER_SNOW &&
       combatantHasAbility(def, ABILITY_SNOW_CLOAK)))
    accuracy = accuracy * 4u / 5u;
  if (def.confuseTurns && combatantHasAbility(def, ABILITY_TANGLED_FEET))
    accuracy /= 2u;
  if (field.gravityTurns) accuracy = accuracy * 5u / 3u;
  return accuracy > 100 ? 100 : (uint8_t)accuracy;
}

// ---------- damage ----------

static bool gmaxIgnoresTargetAbility(const BattleMove &move) {
  return move.gmaxEffect == GMAX_EFFECT_DRUM_SOLO ||
         move.gmaxEffect == GMAX_EFFECT_FIREBALL ||
         move.gmaxEffect == GMAX_EFFECT_HYDROSNIPE;
}

static bool gmaxBypassesProtect(const BattleMove &move) {
  return move.gmaxEffect == GMAX_EFFECT_ONE_BLOW ||
         move.gmaxEffect == GMAX_EFFECT_RAPID_FLOW;
}

// roll is 217..255, the series' damage spread, passed in so tests can pin it.
uint16_t battleDamage(const Combatant &atk, const Combatant &def,
                      const BattleField &field, MoveId mv, bool crit, uint8_t roll,
                      const BattleSideConditions *defendingSide) {
  return battleDamage(atk, def, field, battleMove(mv), crit, roll, defendingSide);
}

uint16_t battleDamage(const Combatant &atk, const Combatant &def,
                      const BattleField &field, const BattleMove &move,
                      bool crit, uint8_t roll,
                      const BattleSideConditions *defendingSide) {
  if (!move.valid()) return 0;
  const MoveEntry &m = move.entry;
  if (m.cat == MC_STATUS) return 0;
  Combatant abilitylessDef = def;
  if (gmaxIgnoresTargetAbility(move)) abilitylessDef.ability = ABILITY_NONE;
  const Combatant &defRules = abilitylessDef;

  if (m.effect == EF_FIXED_LVL) return atk.level ? atk.level : 1;
  if (m.effect == EF_FIXED) return m.param > 0 ? (uint16_t)m.param : 1;

  if (combatantHasAbility(defRules, ABILITY_BATTLE_ARMOR) ||
      combatantHasAbility(defRules, ABILITY_SHELL_ARMOR)) crit = false;

  BattleField effectiveField = field;
  if (weatherSuppressed(atk, defRules)) effectiveField.weather = BWEATHER_NONE;
  if (combatantHasAbility(atk, ABILITY_MEGA_SOL)) effectiveField.weather = BWEATHER_SUN;
  uint8_t atkStat = m.cat == MC_PHYS ? SI_ATK : SI_SPA;
  uint8_t defStat = m.cat == MC_PHYS ? SI_DEF : SI_SPD;
  uint16_t A = battleEffectiveStatFor(
      atk, atkStat, &effectiveField, combatantHasAbility(defRules, ABILITY_UNAWARE));
  uint16_t D = battleEffectiveStatFor(
      defRules, defStat, &effectiveField, combatantHasAbility(atk, ABILITY_UNAWARE));
  // A critical hit ignores the defender's positive stages and the attacker's
  // negative ones, so a Barrier cannot make you immune to a lucky roll.
  if (crit) {
    A = battleEffectiveStatFor(atk, atkStat, &effectiveField, true);
    D = battleEffectiveStatFor(defRules, defStat, &effectiveField, true);
  }
  if (m.cat == MC_PHYS && effectiveField.weather == BWEATHER_SNOW &&
      combatantHasType(defRules, T_ICE))
    D = (uint16_t)((uint32_t)D * 3u / 2u);
  if (m.cat == MC_SPEC && effectiveField.weather == BWEATHER_SAND &&
      combatantHasType(defRules, T_ROCK))
    D = (uint16_t)((uint32_t)D * 3u / 2u);
  if (m.cat == MC_PHYS && combatantHasAbility(defRules, ABILITY_FUR_COAT))
    D = D > UINT16_MAX / 2u ? UINT16_MAX : (uint16_t)(D * 2u);
  if (m.cat == MC_PHYS && effectiveField.terrain == BTERRAIN_GRASSY &&
      combatantHasAbility(defRules, ABILITY_GRASS_PELT))
    D = (uint16_t)((uint32_t)D * 3u / 2u);
  if (m.cat == MC_PHYS && combatantHasAbility(defRules, ABILITY_TABLETS_OF_RUIN))
    A = (uint16_t)((uint32_t)A * 3u / 4u);
  if (m.cat == MC_SPEC && combatantHasAbility(defRules, ABILITY_VESSEL_OF_RUIN))
    A = (uint16_t)((uint32_t)A * 3u / 4u);
  if (m.cat == MC_PHYS && combatantHasAbility(atk, ABILITY_SWORD_OF_RUIN))
    D = (uint16_t)((uint32_t)D * 3u / 4u);
  if (m.cat == MC_SPEC && combatantHasAbility(atk, ABILITY_BEADS_OF_RUIN))
    D = (uint16_t)((uint32_t)D * 3u / 4u);
  if (!D) D = 1;

  uint32_t dmg = (2UL * atk.level / 5 + 2) * m.power * A / D / 50 + 2;
  dmg = dmg * move.abilityPowerPercent / 100u;
  if (crit) dmg *= combatantHasAbility(atk, ABILITY_SNIPER) ? 3u : 2u;
  if ((effectiveField.weather == BWEATHER_SUN && m.type == T_FIRE) ||
      (effectiveField.weather == BWEATHER_RAIN && m.type == T_WATER))
    dmg = dmg * 3u / 2u;
  if ((effectiveField.weather == BWEATHER_SUN && m.type == T_WATER) ||
      (effectiveField.weather == BWEATHER_RAIN && m.type == T_FIRE))
    dmg /= 2u;
  if ((m.fieldFlags & MF_SOLAR_CHARGE) && effectiveField.weather != BWEATHER_NONE &&
      effectiveField.weather != BWEATHER_SUN)
    dmg /= 2u;
  if (combatantHasType(atk, m.type))
    dmg = combatantHasAbility(atk, ABILITY_ADAPTABILITY) ? dmg * 2u : dmg * 3u / 2u;
  bool lowHp = (uint32_t)atk.hp * 3u <= atk.maxHp;
  if (lowHp && ((m.type == T_GRASS && combatantHasAbility(atk, ABILITY_OVERGROW)) ||
                (m.type == T_FIRE && combatantHasAbility(atk, ABILITY_BLAZE)) ||
                (m.type == T_WATER && combatantHasAbility(atk, ABILITY_TORRENT)) ||
                (m.type == T_BUG && combatantHasAbility(atk, ABILITY_SWARM))))
    dmg = dmg * 3u / 2u;
  if (m.type == T_FIRE && atk.flashFireActive) dmg = dmg * 3u / 2u;
  if (m.cat == MC_SPEC && effectiveField.weather == BWEATHER_SUN &&
      combatantHasAbility(atk, ABILITY_SOLAR_POWER)) dmg = dmg * 3u / 2u;
  if (effectiveField.weather == BWEATHER_SAND &&
      (m.type == T_ROCK || m.type == T_GROUND || m.type == T_STEEL) &&
      combatantHasAbility(atk, ABILITY_SAND_FORCE)) dmg = dmg * 13u / 10u;
  if (move.mechanic == BMECH_NONE && m.power <= 60 &&
      combatantHasAbility(atk, ABILITY_TECHNICIAN)) dmg = dmg * 3u / 2u;
  if ((m.tags & MT_PUNCH) && combatantHasAbility(atk, ABILITY_IRON_FIST))
    dmg = dmg * 6u / 5u;
  if ((m.tags & MT_BITE) && combatantHasAbility(atk, ABILITY_STRONG_JAW))
    dmg = dmg * 3u / 2u;
  if ((m.tags & MT_PULSE) && combatantHasAbility(atk, ABILITY_MEGA_LAUNCHER))
    dmg = dmg * 3u / 2u;
  if (moveMakesContact(atk, m) && combatantHasAbility(atk, ABILITY_TOUGH_CLAWS))
    dmg = dmg * 13u / 10u;
  if ((m.tags & MT_SLICING) && combatantHasAbility(atk, ABILITY_SHARPNESS))
    dmg = dmg * 3u / 2u;
  if ((m.tags & MT_SOUND) && combatantHasAbility(atk, ABILITY_PUNK_ROCK))
    dmg = dmg * 13u / 10u;
  if (m.effect == EF_RECOIL && combatantHasAbility(atk, ABILITY_RECKLESS))
    dmg = dmg * 6u / 5u;
  if (move.source && moveEntry(move.source).type == T_NORMAL && m.type != T_NORMAL &&
      (combatantHasAbility(atk, ABILITY_REFRIGERATE) ||
       combatantHasAbility(atk, ABILITY_PIXILATE) ||
       combatantHasAbility(atk, ABILITY_AERILATE))) dmg = dmg * 6u / 5u;
  if (move.source && moveEntry(move.source).type == T_NORMAL && m.type == T_DRAGON &&
      combatantHasAbility(atk, ABILITY_DRAGONIZE)) dmg = dmg * 6u / 5u;
  if (m.type == T_ELECTRIC && atk.abilityCharged) dmg *= 2u;
  if (m.type == T_WATER && combatantHasAbility(atk, ABILITY_WATER_BUBBLE)) dmg *= 2u;
  if (m.type == T_STEEL && combatantHasAbility(atk, ABILITY_STEELWORKER)) dmg = dmg * 3u / 2u;
  if (m.type == T_ELECTRIC && combatantHasAbility(atk, ABILITY_TRANSISTOR)) dmg = dmg * 13u / 10u;
  if (m.type == T_DRAGON && combatantHasAbility(atk, ABILITY_DRAGONS_MAW)) dmg = dmg * 3u / 2u;
  if (m.type == T_ROCK && combatantHasAbility(atk, ABILITY_ROCKY_PAYLOAD)) dmg = dmg * 3u / 2u;
  if (m.type == T_FIRE && combatantHasAbility(atk, ABILITY_FIRE_MANE)) dmg = dmg * 3u / 2u;
  if (m.type == T_DARK && (combatantHasAbility(atk, ABILITY_DARK_AURA) ||
                          combatantHasAbility(defRules, ABILITY_DARK_AURA)))
    dmg = dmg * (combatantHasAbility(atk, ABILITY_AURA_BREAK) ||
                 combatantHasAbility(defRules, ABILITY_AURA_BREAK) ? 3u : 4u) /
          (combatantHasAbility(atk, ABILITY_AURA_BREAK) ||
           combatantHasAbility(defRules, ABILITY_AURA_BREAK) ? 4u : 3u);
  if (m.type == T_FAIRY && (combatantHasAbility(atk, ABILITY_FAIRY_AURA) ||
                           combatantHasAbility(defRules, ABILITY_FAIRY_AURA)))
    dmg = dmg * (combatantHasAbility(atk, ABILITY_AURA_BREAK) ||
                 combatantHasAbility(defRules, ABILITY_AURA_BREAK) ? 3u : 4u) /
          (combatantHasAbility(atk, ABILITY_AURA_BREAK) ||
           combatantHasAbility(defRules, ABILITY_AURA_BREAK) ? 4u : 3u);
  if (atk.gender != GENDER_UNKNOWN && atk.gender != GENDER_NONE &&
      def.gender != GENDER_UNKNOWN && def.gender != GENDER_NONE &&
      combatantHasAbility(atk, ABILITY_RIVALRY))
    dmg = atk.gender == def.gender ? dmg * 5u / 4u : dmg * 3u / 4u;
  if ((m.type == T_FIRE || m.type == T_ICE) &&
      combatantHasAbility(defRules, ABILITY_THICK_FAT)) dmg /= 2u;
  if (m.type == T_FIRE && combatantHasAbility(defRules, ABILITY_WATER_BUBBLE)) dmg /= 2u;
  if (m.type == T_FIRE && combatantHasAbility(defRules, ABILITY_DRY_SKIN)) dmg = dmg * 5u / 4u;
  if (m.type == T_FIRE && combatantHasAbility(defRules, ABILITY_HEATPROOF)) dmg /= 2u;
  if (m.type == T_GHOST && combatantHasAbility(defRules, ABILITY_PURIFYING_SALT)) dmg /= 2u;
  if (m.cat == MC_SPEC && combatantHasAbility(defRules, ABILITY_ICE_SCALES)) dmg /= 2u;
  if ((m.tags & MT_SOUND) && combatantHasAbility(defRules, ABILITY_PUNK_ROCK)) dmg /= 2u;
  if (moveMakesContact(atk, m) && combatantHasAbility(defRules, ABILITY_FLUFFY)) dmg /= 2u;
  if (m.type == T_FIRE && combatantHasAbility(defRules, ABILITY_FLUFFY)) dmg *= 2u;
  if (battleGrounded(atk, &field) &&
      ((field.terrain == BTERRAIN_ELECTRIC && m.type == T_ELECTRIC) ||
       (field.terrain == BTERRAIN_GRASSY && m.type == T_GRASS) ||
       (field.terrain == BTERRAIN_PSYCHIC && m.type == T_PSYCHIC)))
    dmg = dmg * 13u / 10u;
  if (field.terrain == BTERRAIN_MISTY && m.type == T_DRAGON &&
      battleGrounded(defRules, &field))
    dmg /= 2u;
  if (field.terrain == BTERRAIN_GRASSY &&
      (m.fieldFlags & MF_GRASSY_WEAKENED) && battleGrounded(defRules, &field))
    dmg /= 2u;
  uint16_t eff = typeEffVsCombatant(
      m.type, defRules, &atk, field.gravityTurns != 0);
  dmg = dmg * eff / 100;
  if (eff == 0) return 0;               // immune: no chip, no minimum
  if (eff < 100 && combatantHasAbility(atk, ABILITY_TINTED_LENS)) dmg *= 2u;
  if (eff > 100 && (combatantHasAbility(defRules, ABILITY_FILTER) ||
                    combatantHasAbility(defRules, ABILITY_SOLID_ROCK) ||
                    combatantHasAbility(defRules, ABILITY_PRISM_ARMOR)))
    dmg = dmg * 3u / 4u;
  if (def.hp == def.maxHp && (combatantHasAbility(defRules, ABILITY_MULTISCALE) ||
                             combatantHasAbility(defRules, ABILITY_SHADOW_SHIELD)))
    dmg /= 2u;
  if (!crit && defendingSide &&
      (defendingSide->auroraVeilTurns ||
       (m.cat == MC_PHYS && defendingSide->reflectTurns) ||
       (m.cat == MC_SPEC && defendingSide->lightScreenTurns)))
    dmg /= 2u;
  dmg = dmg * roll / 255;
  return dmg < 1 ? 1 : (dmg > 65535 ? 65535 : (uint16_t)dmg);
}

// ---------- turn order ----------

bool battleMovesFirst(const Combatant &a, MoveId ma,
                      const Combatant &b, MoveId mb) {
  return battleMovesFirst(a, battleMove(ma), b, battleMove(mb));
}

bool battleMovesFirst(const Combatant &a, const BattleMove &ma,
                      const Combatant &b, const BattleMove &mb) {
  BattleField field;
  return battleMovesFirst(a, ma, b, mb, field);
}

static int battlePriority(const Combatant &c, const BattleMove &move) {
  int priority = move.valid() &&
      (move.entry.effect == EF_PRIORITY || move.entry.effect == EF_PROTECT)
      ? move.entry.param : 0;
  if (move.valid() && move.entry.cat == MC_STATUS &&
      combatantHasAbility(c, ABILITY_PRANKSTER)) priority++;
  if (move.valid() && move.entry.effect == EF_HEAL &&
      combatantHasAbility(c, ABILITY_TRIAGE)) priority += 3;
  if (move.valid() && move.entry.type == T_FLYING && c.hp == c.maxHp &&
      combatantHasAbility(c, ABILITY_GALE_WINGS)) priority++;
  return priority;
}

bool battleMovesFirst(const Combatant &a, const BattleMove &ma,
                      const Combatant &b, const BattleMove &mb,
                      const BattleField &field) {
  int pa = battlePriority(a, ma);
  int pb = battlePriority(b, mb);
  if (pa != pb) return pa > pb;
  bool aLast = combatantHasAbility(a, ABILITY_STALL) ||
               (ma.valid() && ma.entry.cat == MC_STATUS &&
                combatantHasAbility(a, ABILITY_MYCELIUM_MIGHT));
  bool bLast = combatantHasAbility(b, ABILITY_STALL) ||
               (mb.valid() && mb.entry.cat == MC_STATUS &&
                combatantHasAbility(b, ABILITY_MYCELIUM_MIGHT));
  if (aLast != bLast) return !aLast;
  BattleField effectiveField = field;
  if (weatherSuppressed(a, b)) effectiveField.weather = BWEATHER_NONE;
  uint16_t sa = battleEffectiveStatFor(a, SI_SPE, &effectiveField);
  uint16_t sb = battleEffectiveStatFor(b, SI_SPE, &effectiveField);
  if (sa != sb) return sa > sb;
  return random(2) == 0;               // a genuine speed tie is a coin flip
}

// ---------- one action ----------

static uint8_t applyStagesRaw(Combatant &c, uint8_t mask, int8_t delta) {
  static const uint8_t BIT[SI_COUNT] = { ST_ATK, ST_DEF, ST_SPA, ST_SPD, ST_SPE };
  uint8_t changed = 0;
  for (int i = 0; i < SI_COUNT; i++) {
    if (!(mask & BIT[i])) continue;
    int8_t before = c.stage[i];
    int v = c.stage[i] + delta;
    c.stage[i] = v > 6 ? 6 : (v < -6 ? -6 : (int8_t)v);
    if (c.stage[i] != before) changed |= BIT[i];
  }
  if (mask & ST_ACC) {
    int8_t before = c.accuracyStage;
    int value = before + delta;
    c.accuracyStage = value > 6 ? 6 : (value < -6 ? -6 : (int8_t)value);
    if (c.accuracyStage != before) changed |= ST_ACC;
  }
  if (mask & ST_EVA) {
    int8_t before = c.evasionStage;
    int value = before + delta;
    c.evasionStage = value > 6 ? 6 : (value < -6 ? -6 : (int8_t)value);
    if (c.evasionStage != before) changed |= ST_EVA;
  }
  return changed;
}

static uint8_t applyStages(Combatant &c, uint8_t mask, int8_t delta,
                           const Combatant *source = nullptr,
                           bool ignoreAbility = false) {
  bool external = source && source != &c;
  if (!ignoreAbility && combatantHasAbility(c, ABILITY_CONTRARY)) delta = (int8_t)-delta;
  if (!ignoreAbility && combatantHasAbility(c, ABILITY_SIMPLE)) delta = (int8_t)(delta * 2);
  if (!ignoreAbility && delta < 0 && external) {
    if (combatantHasAbility(c, ABILITY_CLEAR_BODY) ||
        combatantHasAbility(c, ABILITY_WHITE_SMOKE) ||
        combatantHasAbility(c, ABILITY_FULL_METAL_BODY)) return 0;
    if (combatantHasAbility(c, ABILITY_HYPER_CUTTER)) mask &= ~ST_ATK;
    if (combatantHasAbility(c, ABILITY_BIG_PECKS)) mask &= ~ST_DEF;
    if (combatantHasAbility(c, ABILITY_KEEN_EYE)) mask &= ~ST_ACC;
  }
  uint8_t changed = applyStagesRaw(c, mask, delta);
  if (!ignoreAbility && changed && delta < 0 && external) {
    if (combatantHasAbility(c, ABILITY_DEFIANT))
      applyStagesRaw(c, ST_ATK, 2);
    if (combatantHasAbility(c, ABILITY_COMPETITIVE))
      applyStagesRaw(c, ST_SPA, 2);
  }
  return changed;
}

static int8_t abilityStageDelta(const Combatant &c, int8_t delta,
                                bool ignoreAbility = false) {
  if (ignoreAbility) return delta;
  if (combatantHasAbility(c, ABILITY_CONTRARY)) delta = (int8_t)-delta;
  if (combatantHasAbility(c, ABILITY_SIMPLE)) delta = (int8_t)(delta * 2);
  return delta;
}

static void hurt(Combatant &c, uint16_t amount) {
  c.hp = (amount >= c.hp) ? 0 : c.hp - amount;
}

static uint16_t heal(Combatant &c, uint16_t amount) {
  uint16_t before = c.hp;
  uint32_t v = (uint32_t)c.hp + amount;
  c.hp = v > c.maxHp ? c.maxHp : (uint16_t)v;
  return c.hp - before;
}

static bool abilityPreventsAilment(const Combatant &c, uint8_t ailment,
                                   const BattleField &field) {
  if (combatantHasAbility(c, ABILITY_PURIFYING_SALT)) return true;
  if (exclusiveAbility(c, 774, ABILITY_SHIELDS_DOWN) &&
      c.form == BFORM_BASE) return true;
  if (field.weather == BWEATHER_SUN && combatantHasAbility(c, ABILITY_LEAF_GUARD))
    return true;
  if (ailment == AIL_PARA) return combatantHasAbility(c, ABILITY_LIMBER);
  if (ailment == AIL_SLEEP)
    return combatantHasAbility(c, ABILITY_INSOMNIA) ||
           combatantHasAbility(c, ABILITY_VITAL_SPIRIT) ||
           combatantHasAbility(c, ABILITY_SWEET_VEIL);
  if (ailment == AIL_POISON) return combatantHasAbility(c, ABILITY_IMMUNITY);
  if (ailment == AIL_FREEZE) return combatantHasAbility(c, ABILITY_MAGMA_ARMOR);
  if (ailment == AIL_BURN)
    return combatantHasAbility(c, ABILITY_WATER_VEIL) ||
           combatantHasAbility(c, ABILITY_WATER_BUBBLE) ||
           combatantHasAbility(c, ABILITY_THERMAL_EXCHANGE);
  if (ailment == AIL_CONFUSE) return combatantHasAbility(c, ABILITY_OWN_TEMPO);
  return false;
}

static bool ailmentTypeImmune(const Combatant &target, uint8_t ailment,
                              const Combatant *source = nullptr) {
  return (ailment == AIL_BURN && combatantHasType(target, T_FIRE)) ||
         (ailment == AIL_FREEZE && combatantHasType(target, T_ICE)) ||
         (ailment == AIL_POISON &&
          (combatantHasType(target, T_POISON) || combatantHasType(target, T_STEEL)) &&
          (!source || !combatantHasAbility(*source, ABILITY_CORROSION))) ||
         (ailment == AIL_PARA && combatantHasType(target, T_ELECTRIC));
}

static bool tryInflictTriggeredAilment(Combatant &source, Combatant &target,
                                       uint8_t ailment,
                                       const BattleField &field) {
  if (target.ailment != AIL_NONE || abilityPreventsAilment(target, ailment, field) ||
      ailmentTypeImmune(target, ailment, &source) ||
      (battleGrounded(target, &field) &&
       (field.terrain == BTERRAIN_MISTY ||
        (field.terrain == BTERRAIN_ELECTRIC && ailment == AIL_SLEEP))))
    return false;
  target.ailment = ailment;
  if (ailment == AIL_SLEEP) target.ailTurns = 2 + random(3);
  return true;
}

static uint8_t statBit(uint8_t index) {
  static const uint8_t bits[SI_COUNT] = { ST_ATK, ST_DEF, ST_SPA, ST_SPD, ST_SPE };
  return index < SI_COUNT ? bits[index] : 0;
}

static uint8_t battleStageBit(uint8_t index) {
  static const uint8_t bits[BATTLE_STAGE_COUNT] = {
    ST_ATK, ST_DEF, ST_SPA, ST_SPD, ST_SPE, ST_ACC, ST_EVA,
  };
  return index < BATTLE_STAGE_COUNT ? bits[index] : 0;
}

static int8_t battleStageValue(const Combatant &combatant, uint8_t index) {
  if (index < SI_COUNT) return combatant.stage[index];
  if (index == BSI_ACC) return combatant.accuracyStage;
  if (index == BSI_EVA) return combatant.evasionStage;
  return 0;
}

struct StageSnapshot {
  int8_t values[BATTLE_STAGE_COUNT] = {};
};

static void rememberStages(const Combatant &combatant, StageSnapshot &out) {
  for (uint8_t i = 0; i < BATTLE_STAGE_COUNT; i++)
    out.values[i] = battleStageValue(combatant, i);
}

static void copyPositiveStageChanges(Combatant &observer,
                                     const Combatant &source,
                                     const StageSnapshot &before) {
  if (!combatantHasAbility(observer, ABILITY_OPPORTUNIST) || observer.fainted())
    return;
  for (uint8_t i = 0; i < BATTLE_STAGE_COUNT; i++) {
    int8_t gained = battleStageValue(source, i) - before.values[i];
    if (gained > 0) applyStages(observer, battleStageBit(i), gained, &observer);
  }
}

static void abilityAfterHit(Combatant &atk, Combatant &def, BattleField &field,
                            const MoveEntry &move, bool crit, uint16_t hpBefore,
                            TurnLog &log) {
  StageSnapshot stagesBefore;
  rememberStages(def, stagesBefore);
  bool tookDamage = def.hp < hpBefore;
  BattleField statusField = field;
  if (weatherSuppressed(atk, def)) statusField.weather = BWEATHER_NONE;
  if (combatantHasAbility(def, ABILITY_ELECTROMORPHOSIS)) def.abilityCharged = true;
  if (tookDamage && (move.tags & MT_WIND) &&
      combatantHasAbility(def, ABILITY_WIND_POWER)) def.abilityCharged = true;
  if (combatantHasAbility(def, ABILITY_COLOR_CHANGE) && move.type < TYPE_COUNT &&
      !combatantHasType(def, move.type)) {
    def.type1 = move.type;
    def.type2 = T_NONE;
  }
  if (combatantHasAbility(def, ABILITY_SAND_SPIT)) {
    battleSetWeather(field, BWEATHER_SAND);
    log.weatherSet = BWEATHER_SAND;
  }
  if (combatantHasAbility(def, ABILITY_SEED_SOWER)) {
    battleSetTerrain(field, BTERRAIN_GRASSY);
    log.terrainSet = BTERRAIN_GRASSY;
  }
  if (tookDamage && combatantHasAbility(def, ABILITY_SPICY_SPRAY) &&
      atk.ailment == AIL_NONE &&
      !combatantHasType(atk, T_FIRE) &&
      !abilityPreventsAilment(atk, AIL_BURN, statusField)) {
    atk.ailment = AIL_BURN;
  }
  if (tookDamage && moveMakesContact(atk, move)) {
    if (combatantHasAbility(def, ABILITY_ROUGH_SKIN) &&
        !combatantHasAbility(atk, ABILITY_MAGIC_GUARD))
      hurt(atk, atk.maxHp / 8u ? atk.maxHp / 8u : 1u);
    bool mayInflict = combatantHasAbility(def, ABILITY_STATIC) ||
        combatantHasAbility(def, ABILITY_POISON_POINT) ||
        combatantHasAbility(def, ABILITY_FLAME_BODY) ||
        combatantHasAbility(def, ABILITY_EFFECT_SPORE);
    if (mayInflict && random(100) < 30) {
      if (combatantHasAbility(def, ABILITY_STATIC))
        tryInflictTriggeredAilment(def, atk, AIL_PARA, statusField);
      else if (combatantHasAbility(def, ABILITY_POISON_POINT))
        tryInflictTriggeredAilment(def, atk, AIL_POISON, statusField);
      else if (combatantHasAbility(def, ABILITY_FLAME_BODY))
        tryInflictTriggeredAilment(def, atk, AIL_BURN, statusField);
      else if (combatantHasAbility(def, ABILITY_EFFECT_SPORE) &&
               !combatantHasType(atk, T_GRASS) &&
               !combatantHasAbility(atk, ABILITY_OVERCOAT)) {
        static const uint8_t EFFECT_SPORE_AILMENTS[3] = {
          AIL_POISON, AIL_PARA, AIL_SLEEP,
        };
        tryInflictTriggeredAilment(
            def, atk, EFFECT_SPORE_AILMENTS[random(3)], statusField);
      }
    }
    if (combatantHasAbility(atk, ABILITY_POISON_TOUCH) && random(100) < 30 &&
        tryInflictTriggeredAilment(atk, def, AIL_POISON, statusField))
      log.inflicted = AIL_POISON;
  }
  if (def.fainted()) return;
  if (crit && combatantHasAbility(def, ABILITY_ANGER_POINT)) {
    def.stage[SI_ATK] = 6;
    log.stageMask = ST_ATK;
    log.stageDelta = 6;
  }
  if (move.cat == MC_PHYS && combatantHasAbility(def, ABILITY_WEAK_ARMOR)) {
    applyStages(def, ST_DEF, -1, &atk);
    applyStages(def, ST_SPE, 2, &def);
    log.stageMask = ST_DEF | ST_SPE;
  }
  if (tookDamage && combatantHasAbility(def, ABILITY_STAMINA)) {
    applyStages(def, ST_DEF, 1, &def);
    log.stageMask |= ST_DEF;
  }
  if (move.type == T_WATER && combatantHasAbility(def, ABILITY_WATER_COMPACTION)) {
    applyStages(def, ST_DEF, 2, &def);
    log.stageMask |= ST_DEF;
  }
  if (tookDamage && move.type == T_DARK &&
      combatantHasAbility(def, ABILITY_JUSTIFIED)) {
    applyStages(def, ST_ATK, 1, &def);
    log.stageMask |= ST_ATK;
  }
  if ((move.type == T_DARK || move.type == T_GHOST || move.type == T_BUG) &&
      combatantHasAbility(def, ABILITY_RATTLED)) {
    applyStages(def, ST_SPE, 1, &def);
    log.stageMask |= ST_SPE;
  }
  if ((move.type == T_FIRE || move.type == T_WATER) &&
      combatantHasAbility(def, ABILITY_STEAM_ENGINE)) {
    applyStages(def, ST_SPE, 6, &def);
    log.stageMask |= ST_SPE;
  }
  if (move.type == T_FIRE && combatantHasAbility(def, ABILITY_THERMAL_EXCHANGE)) {
    applyStages(def, ST_ATK, 1, &def);
    log.stageMask |= ST_ATK;
  }
  if (tookDamage &&
      (def.form == BFORM_CRAMORANT_GULPING ||
       def.form == BFORM_CRAMORANT_GORGING) &&
      exclusiveAbility(def, 845, ABILITY_GULP_MISSILE)) {
    BattleForm prey = def.form;
    uint16_t before = atk.hp;
    if (!combatantHasAbility(atk, ABILITY_MAGIC_GUARD))
      hurt(atk, atk.maxHp / 4u ? atk.maxHp / 4u : 1u);
    log.counterDamage += before - atk.hp;
    if (prey == BFORM_CRAMORANT_GULPING)
      applyStages(atk, ST_DEF, -1, &def);
    else
      tryInflictTriggeredAilment(def, atk, AIL_PARA, statusField);
    setBattleForm(def, BFORM_BASE);
    log.formChanged = BFORM_BASE;
  }
  if (hpBefore * 2u > def.maxHp && def.hp * 2u <= def.maxHp) {
    if (combatantHasAbility(def, ABILITY_BERSERK)) {
      applyStages(def, ST_SPA, 1, &def);
      log.stageMask |= ST_SPA;
    }
    if (combatantHasAbility(def, ABILITY_ANGER_SHELL) && !def.abilityTriggered) {
      def.abilityTriggered = true;
      applyStages(def, ST_DEF | ST_SPD, -1, &def);
      applyStages(def, ST_ATK | ST_SPA | ST_SPE, 1, &def);
      log.stageMask = ST_ATK | ST_DEF | ST_SPA | ST_SPD | ST_SPE;
    }
    if ((combatantHasAbility(def, ABILITY_WIMP_OUT) ||
         combatantHasAbility(def, ABILITY_EMERGENCY_EXIT)) &&
        !def.abilityTriggered) {
      def.abilityTriggered = true;
      log.switchRequest = BSWITCH_TARGET;
    }
  }
  copyPositiveStageChanges(atk, def, stagesBefore);
}

static void abilityAfterKnockout(Combatant &winner) {
  if (combatantHasAbility(winner, ABILITY_MOXIE) ||
      combatantHasAbility(winner, ABILITY_CHILLING_NEIGH))
    applyStages(winner, ST_ATK, 1, &winner);
  if (combatantHasAbility(winner, ABILITY_GRIM_NEIGH) ||
      combatantHasAbility(winner, ABILITY_SOUL_HEART))
    applyStages(winner, ST_SPA, 1, &winner);
  if (combatantHasAbility(winner, ABILITY_BEAST_BOOST) ||
      combatantHasAbility(winner, ABILITY_EELEVATE))
    applyStages(winner, statBit(highestBaseStat(winner)), 1, &winner);
}

static bool battleSetScreen(BattleSideConditions &side, BattleScreen screen,
                            const BattleField &field) {
  uint8_t *turns = screen == BSCREEN_REFLECT ? &side.reflectTurns
                   : screen == BSCREEN_LIGHT_SCREEN ? &side.lightScreenTurns
                   : screen == BSCREEN_AURORA_VEIL ? &side.auroraVeilTurns
                   : nullptr;
  if (!turns || (screen == BSCREEN_AURORA_VEIL && field.weather != BWEATHER_SNOW))
    return false;
  *turns = BATTLE_FIELD_TURNS;
  return true;
}

static bool battleSetHazard(BattleSideConditions &side, BattleHazard hazard) {
  if (hazard == BHAZARD_SPIKES && side.spikesLayers < 3) {
    side.spikesLayers++;
    return true;
  }
  if (hazard == BHAZARD_TOXIC_SPIKES && side.toxicSpikesLayers < 2) {
    side.toxicSpikesLayers++;
    return true;
  }
  if (hazard == BHAZARD_STEALTH_ROCK && !side.stealthRock) {
    side.stealthRock = true;
    return true;
  }
  if (hazard == BHAZARD_STICKY_WEB && !side.stickyWeb) {
    side.stickyWeb = true;
    return true;
  }
  return false;
}

static bool battleClearHazards(BattleSideConditions &side) {
  bool changed = side.spikesLayers || side.toxicSpikesLayers ||
                 side.stealthRock || side.stickyWeb || side.steelsurge;
  side.spikesLayers = side.toxicSpikesLayers = 0;
  side.stealthRock = side.stickyWeb = side.steelsurge = false;
  return changed;
}

static bool battleClearAll(BattleField &field) {
  bool changed = false;
  for (uint8_t sideIndex = 0; sideIndex < 2; sideIndex++) {
    BattleSideConditions &side = field.sides[sideIndex];
    changed |= battleClearHazards(side);
    changed |= side.reflectTurns || side.lightScreenTurns || side.auroraVeilTurns;
    side.reflectTurns = side.lightScreenTurns = side.auroraVeilTurns = 0;
  }
  return changed;
}

static bool gmaxConfuse(Combatant &target, const BattleField &field) {
  if (target.fainted() || target.confuseTurns ||
      abilityPreventsAilment(target, AIL_CONFUSE, field) ||
      (battleGrounded(target, &field) && field.terrain == BTERRAIN_MISTY)) return false;
  target.confuseTurns = 2 + random(3);
  return true;
}

static void applyGmaxEffect(Combatant &atk, Combatant &def, BattleField &field,
                            const BattleMove &move, uint8_t attackerSide,
                            TurnLog &log) {
  GmaxEffect effect = move.gmaxEffect;
  BattleSideConditions &own = field.sides[attackerSide & 1u];
  BattleSideConditions &foe = field.sides[(attackerSide & 1u) ^ 1u];
  if (effect == GMAX_EFFECT_NONE) return;
  if (effect == GMAX_EFFECT_VINE_LASH || effect == GMAX_EFFECT_WILDFIRE ||
      effect == GMAX_EFFECT_CANNONADE || effect == GMAX_EFFECT_VOLCALITH) {
    foe.gmaxResidualEffect = effect;
    foe.gmaxResidualTurns = 4;
    return;
  }
  if (effect == GMAX_EFFECT_BEFUDDLE || effect == GMAX_EFFECT_STUN_SHOCK) {
    static const uint8_t BEFUDDLE[] = { AIL_PARA, AIL_POISON, AIL_SLEEP };
    static const uint8_t STUN_SHOCK[] = { AIL_PARA, AIL_POISON };
    const uint8_t *choices = effect == GMAX_EFFECT_BEFUDDLE ? BEFUDDLE : STUN_SHOCK;
    uint8_t count = effect == GMAX_EFFECT_BEFUDDLE ? 3 : 2;
    uint8_t ailment = choices[random(count)];
    if (!def.fainted() && tryInflictTriggeredAilment(atk, def, ailment, field))
      log.inflicted = ailment;
    return;
  }
  if (effect == GMAX_EFFECT_VOLT_CRASH || effect == GMAX_EFFECT_MALODOR) {
    uint8_t ailment = effect == GMAX_EFFECT_VOLT_CRASH ? AIL_PARA : AIL_POISON;
    if (!def.fainted() && tryInflictTriggeredAilment(atk, def, ailment, field))
      log.inflicted = ailment;
    return;
  }
  if (effect == GMAX_EFFECT_GOLD_RUSH) {
    log.bonusRewardItems = 1;
    if (gmaxConfuse(def, field)) log.inflicted = AIL_CONFUSE;
    return;
  }
  if (effect == GMAX_EFFECT_CHI_STRIKE) {
    if (own.critStages < 3) own.critStages++;
    return;
  }
  if (effect == GMAX_EFFECT_TERROR) {
    if (!def.fainted()) def.trapped = true;
    return;
  }
  if (effect == GMAX_EFFECT_FOAM_BURST || effect == GMAX_EFFECT_TARTNESS) {
    uint8_t mask = effect == GMAX_EFFECT_FOAM_BURST ? ST_SPE : ST_EVA;
    int8_t delta = effect == GMAX_EFFECT_FOAM_BURST ? -2 : -1;
    if (!def.fainted()) {
      log.stageMask = applyStages(def, mask, delta, &atk);
      if (log.stageMask) log.stageDelta = abilityStageDelta(def, delta);
    }
    return;
  }
  if (effect == GMAX_EFFECT_RESONANCE) {
    own.auroraVeilTurns = BATTLE_FIELD_TURNS;
    log.screenSet = BSCREEN_AURORA_VEIL;
    return;
  }
  if (effect == GMAX_EFFECT_CUDDLE) {
    bool known = atk.gender != GENDER_UNKNOWN && atk.gender != GENDER_NONE &&
                 def.gender != GENDER_UNKNOWN && def.gender != GENDER_NONE;
    if (!def.fainted() && known && atk.gender != def.gender) def.infatuated = true;
    return;
  }
  if (effect == GMAX_EFFECT_REPLENISH) {
    log.restoreLastItem = true;
    return;
  }
  if (effect == GMAX_EFFECT_MELTDOWN) {
    if (!def.fainted()) def.tormented = true;
    return;
  }
  if (effect == GMAX_EFFECT_WIND_RAGE) {
    log.fieldCleared = battleClearAll(field);
    return;
  }
  if (effect == GMAX_EFFECT_GRAVITAS) {
    field.gravityTurns = BATTLE_FIELD_TURNS;
    return;
  }
  if (effect == GMAX_EFFECT_STONESURGE) {
    if (!foe.stealthRock) {
      foe.stealthRock = true;
      log.hazardSet = BHAZARD_STEALTH_ROCK;
    }
    return;
  }
  if (effect == GMAX_EFFECT_SWEETNESS) {
    atk.ailment = AIL_NONE;
    atk.ailTurns = 0;
    atk.confuseTurns = 0;
    return;
  }
  if (effect == GMAX_EFFECT_SANDBLAST || effect == GMAX_EFFECT_CENTIFERNO) {
    if (!def.fainted()) def.bindTurns = 4;
    return;
  }
  if (effect == GMAX_EFFECT_SMITE) {
    if (gmaxConfuse(def, field)) log.inflicted = AIL_CONFUSE;
    return;
  }
  if (effect == GMAX_EFFECT_SNOOZE) {
    if (!def.fainted() && def.ailment == AIL_NONE) def.drowsyTurns = 2;
    return;
  }
  if (effect == GMAX_EFFECT_FINALE) {
    uint16_t amount = atk.maxHp / 6u;
    if (!amount) amount = 1;
    log.healed = heal(atk, amount) != 0;
    return;
  }
  if (effect == GMAX_EFFECT_STEELSURGE) {
    foe.steelsurge = true;
    return;
  }
  if (effect == GMAX_EFFECT_DEPLETION && !def.fainted()) {
    uint8_t before = def.statPercent;
    def.statPercent = before > 50 ? (uint8_t)(before - 10) : 50;
    if (def.statPercent < 50) def.statPercent = 50;
    log.statsWeakened = def.statPercent != before;
  }
}

void battleOnEnter(Combatant &combatant, Combatant &opponent,
                   BattleField &field, uint8_t sideIndex, EntryLog &log) {
  log = EntryLog();
  sideIndex &= 1u;
  BattleSideConditions &side = field.sides[sideIndex];
  if (!combatantHasAbility(combatant, ABILITY_EELEVATE)) {
    if (side.stealthRock && !combatantHasAbility(combatant, ABILITY_MAGIC_GUARD)) {
      uint16_t effect = typeEffPct(T_ROCK, combatant.type1, combatant.type2);
      uint16_t damage = (uint32_t)combatant.maxHp * effect / 800u;
      if (effect && !damage) damage = 1;
      uint16_t before = combatant.hp;
      hurt(combatant, damage);
      log.hazardDamage += before - combatant.hp;
    }
    if (!combatant.fainted() && side.steelsurge &&
        !combatantHasAbility(combatant, ABILITY_MAGIC_GUARD)) {
      uint16_t effect = typeEffPct(T_STEEL, combatant.type1, combatant.type2);
      uint16_t damage = (uint32_t)combatant.maxHp * effect / 800u;
      if (effect && !damage) damage = 1;
      uint16_t before = combatant.hp;
      hurt(combatant, damage);
      log.hazardDamage += before - combatant.hp;
    }
    if (!combatant.fainted() && battleGrounded(combatant, &field)) {
      if (side.spikesLayers && !combatantHasAbility(combatant, ABILITY_MAGIC_GUARD)) {
        static const uint8_t DENOMINATOR[] = { 0, 8, 6, 4 };
        uint16_t damage = combatant.maxHp / DENOMINATOR[side.spikesLayers];
        if (!damage) damage = 1;
        uint16_t before = combatant.hp;
        hurt(combatant, damage);
        log.hazardDamage += before - combatant.hp;
      }
      if (!combatant.fainted() && side.toxicSpikesLayers) {
        if (combatantHasType(combatant, T_POISON)) {
          side.toxicSpikesLayers = 0;
          log.toxicSpikesAbsorbed = true;
        } else if (tryInflictTriggeredAilment(opponent, combatant, AIL_POISON, field)) {
          log.inflicted = AIL_POISON;
        }
      }
      if (!combatant.fainted() && side.stickyWeb) {
        log.stageMask = applyStages(combatant, ST_SPE, -1, &opponent);
        if (log.stageMask) log.stageDelta = -1;
      }
    }
  }
  if (combatant.fainted()) return;

  if (combatantHasAbility(combatant, ABILITY_SCREEN_CLEANER)) {
    bool hadScreens = false;
    for (uint8_t i = 0; i < 2; i++)
      hadScreens |= field.sides[i].reflectTurns || field.sides[i].lightScreenTurns ||
                    field.sides[i].auroraVeilTurns;
    for (uint8_t i = 0; i < 2; i++)
      field.sides[i].reflectTurns = field.sides[i].lightScreenTurns =
          field.sides[i].auroraVeilTurns = 0;
    log.screensCleared = hadScreens;
  }
  BattleWeather weather = BWEATHER_NONE;
  if (combatantHasAbility(combatant, ABILITY_DRIZZLE)) weather = BWEATHER_RAIN;
  else if (combatantHasAbility(combatant, ABILITY_DROUGHT)) weather = BWEATHER_SUN;
  else if (combatantHasAbility(combatant, ABILITY_SAND_STREAM)) weather = BWEATHER_SAND;
  else if (combatantHasAbility(combatant, ABILITY_SNOW_WARNING)) weather = BWEATHER_SNOW;
  if (weather != BWEATHER_NONE) {
    battleSetWeather(field, weather);
    log.weatherSet = weather;
  }
  BattleTerrain terrain = BTERRAIN_NONE;
  if (combatantHasAbility(combatant, ABILITY_ELECTRIC_SURGE))
    terrain = BTERRAIN_ELECTRIC;
  else if (combatantHasAbility(combatant, ABILITY_PSYCHIC_SURGE))
    terrain = BTERRAIN_PSYCHIC;
  else if (combatantHasAbility(combatant, ABILITY_MISTY_SURGE))
    terrain = BTERRAIN_MISTY;
  else if (combatantHasAbility(combatant, ABILITY_GRASSY_SURGE))
    terrain = BTERRAIN_GRASSY;
  if (terrain != BTERRAIN_NONE) {
    battleSetTerrain(field, terrain);
    log.terrainSet = terrain;
  }
  if (combatantHasAbility(combatant, ABILITY_INTIMIDATE)) {
    log.stageMask = applyStages(opponent, ST_ATK, -1, &combatant);
    if (log.stageMask) log.stageDelta = -1;
  } else if (combatantHasAbility(combatant, ABILITY_DOWNLOAD)) {
    uint8_t stat = battleEffectiveStat(opponent, SI_DEF) <
                           battleEffectiveStat(opponent, SI_SPD)
                       ? ST_ATK : ST_SPA;
    log.stageMask = applyStages(combatant, stat, 1, &combatant);
    if (log.stageMask) log.stageDelta = 1;
  } else if (combatantHasAbility(combatant, ABILITY_TRACE) &&
             opponent.ability != ABILITY_NONE && opponent.ability != ABILITY_TRACE) {
    combatant.ability = opponent.ability;
    log.traced = true;
  }
  battleRefreshForms(field, combatant, opponent);
}

static void battleActImpl(Combatant &atk, Combatant &def, BattleField &field,
                          const BattleMove &selected, TurnLog &log,
                          uint8_t effectPercent, uint8_t attackerSide,
                          bool allowDancer, bool allowBounce);

void battleAct(Combatant &atk, Combatant &def, BattleField &field, MoveId mv,
               TurnLog &log, uint8_t effectPercent, uint8_t attackerSide) {
  battleActImpl(atk, def, field, battleMoveFor(atk, mv), log, effectPercent,
                attackerSide, true, true);
}

void battleAct(Combatant &atk, Combatant &def, BattleField &field,
               const BattleMove &selected, TurnLog &log, uint8_t effectPercent,
               uint8_t attackerSide) {
  battleActImpl(atk, def, field, selected, log, effectPercent,
                attackerSide, true, true);
}

static void battleActImpl(Combatant &atk, Combatant &def, BattleField &field,
                          const BattleMove &selected, TurnLog &log,
                          uint8_t effectPercent, uint8_t attackerSide,
                          bool allowDancer, bool allowBounce) {
  attackerSide &= 1u;
  BattleMove move = selected;
  log = TurnLog();
  log.move = move.source;
  log.mechanic = move.mechanic;
  log.gmaxMove = move.gmaxMove;
  log.moveType = move.entry.type;
  if (effectPercent > 100) effectPercent = 100;
  if (atk.fainted() || def.fainted()) { log.skipped = true; return; }
  if (selected.valid() && atk.tormented && selected.source == atk.lastMove) {
    log.skipped = true;
    return;
  }
  if (atk.infatuated && random(100) < 50) { log.skipped = true; return; }
  BattleField effectiveField = field;
  if (weatherSuppressed(atk, def)) effectiveField.weather = BWEATHER_NONE;
  if (combatantHasAbility(atk, ABILITY_MEGA_SOL))
    effectiveField.weather = BWEATHER_SUN;

  // --- things that can cost the turn before a move is even chosen
  if (combatantHasAbility(atk, ABILITY_TRUANT)) {
    if (atk.abilityTriggered) {
      atk.abilityTriggered = false;
      log.skipped = true;
      return;
    }
    atk.abilityTriggered = true;
  }
  if (atk.recharge) { atk.recharge = false; log.skipped = true; return; }
  if (atk.ailment == AIL_FREEZE) {
    if (random(100) < 20) atk.ailment = AIL_NONE;   // thaws
    else { log.skipped = true; return; }
  }
  if (atk.ailment == AIL_SLEEP) {
    uint8_t ticks = combatantHasAbility(atk, ABILITY_EARLY_BIRD) ? 2 : 1;
    while (ticks-- && atk.ailTurns) atk.ailTurns--;
    if (atk.ailTurns == 0) atk.ailment = AIL_NONE;
    else { log.skipped = true; return; }
  }
  if (atk.ailment == AIL_PARA && random(100) < 25) { log.skipped = true; return; }
  if (atk.confuseTurns) {
    atk.confuseTurns--;
    if (atk.confuseTurns && random(100) < 33) {  // hits itself instead
      uint16_t self = (2UL * atk.level / 5 + 2) * 40 *
                          battleBaseStat(atk, SI_ATK) /
                          battleBaseStat(atk, SI_DEF) / 50 + 2;
      if (!combatantHasAbility(atk, ABILITY_MAGIC_GUARD)) hurt(atk, self);
      log.hurtSelf = true;
      log.damage = combatantHasAbility(atk, ABILITY_MAGIC_GUARD) ? 0 : self;
      return;
    }
  }

  // A wound-up EF_CHARGE move fires this turn instead of whatever was picked.
  // The answer gates the move before a fresh charge is stored, so a failed
  // answer cannot bank an attack for a later turn.
  bool firingCharge = atk.charging != 0 && move.mechanic == BMECH_NONE;
  if (firingCharge) { move = battleMove(atk.charging); atk.charging = 0; }
  log.move = move.source;
  log.mechanic = move.mechanic;
  log.gmaxMove = move.gmaxMove;
  log.moveType = move.entry.type;
  if (!effectPercent) { log.missed = true; return; }
  bool sunnyCharge = move.valid() && (move.entry.fieldFlags & MF_SOLAR_CHARGE) &&
                     (effectiveField.weather == BWEATHER_SUN ||
                      combatantHasAbility(atk, ABILITY_MEGA_SOL));
  if (!firingCharge && !sunnyCharge && move.valid() && move.entry.effect == EF_CHARGE) {
    atk.charging = move.source;
    log.charged = true;
    return;
  }

  if (!move.valid()) { log.skipped = true; return; }
  atk.lastMove = move.source;
  if (exclusiveAbility(atk, 681, ABILITY_STANCE_CHANGE)) {
    BattleForm next = (move.entry.fieldFlags & MF_STANCE_SHIELD)
        ? BFORM_BASE : move.entry.cat != MC_STATUS
        ? BFORM_AEGISLASH_BLADE : atk.form;
    if (setBattleForm(atk, next)) log.formChanged = next;
  }
  if (!atk.abilityTriggered &&
      (combatantHasAbility(atk, ABILITY_PROTEAN) ||
       combatantHasAbility(atk, ABILITY_LIBERO))) {
    atk.type1 = move.entry.type;
    atk.type2 = T_NONE;
    atk.abilityTriggered = true;
  }
  bool sheerForce = combatantHasAbility(atk, ABILITY_SHEER_FORCE) &&
      (move.entry.ailChance ||
       (move.entry.target == TG_FOE && move.entry.statMask && move.entry.stages < 0));
  if (sheerForce) {
    move.abilityPowerPercent = 130;
    move.entry.ailment = AIL_NONE;
    move.entry.ailChance = 0;
    if (move.entry.target == TG_FOE && move.entry.stages < 0) {
      move.entry.statMask = 0;
      move.entry.stages = 0;
    }
  }
  const MoveEntry &m = move.entry;
  bool ignoreTargetAbility = gmaxIgnoresTargetAbility(move);
  log.move = move.source;
  log.gmaxMove = move.gmaxMove;

  int priority = battlePriority(atk, move);
  bool priorityBlocked = priority > 0 && m.target == TG_FOE &&
      ((field.terrain == BTERRAIN_PSYCHIC && battleGrounded(def, &field)) ||
       (!ignoreTargetAbility &&
        (combatantHasAbility(def, ABILITY_QUEENLY_MAJESTY) ||
         combatantHasAbility(def, ABILITY_DAZZLING) ||
         combatantHasAbility(def, ABILITY_ARMOR_TAIL))));
  bool pranksterBlocked = m.cat == MC_STATUS && m.target == TG_FOE &&
      combatantHasAbility(atk, ABILITY_PRANKSTER) && combatantHasType(def, T_DARK);
  if (priorityBlocked || pranksterBlocked) {
    log.blockedByField = true;
    return;
  }

  if (!ignoreTargetAbility && m.target == TG_FOE &&
      (((m.tags & MT_SOUND) && combatantHasAbility(def, ABILITY_SOUNDPROOF)) ||
       ((m.tags & MT_BALLISTIC) && combatantHasAbility(def, ABILITY_BULLETPROOF)) ||
       ((m.tags & MT_POWDER) && (combatantHasAbility(def, ABILITY_OVERCOAT) ||
                                combatantHasType(def, T_GRASS))))) {
    log.immune = true;
    return;
  }
  if (!ignoreTargetAbility && m.target == TG_FOE && (m.tags & MT_WIND) &&
      combatantHasAbility(def, ABILITY_WIND_RIDER)) {
    log.stageMask = applyStages(def, ST_ATK, 1, &atk);
    log.stageDelta = 1;
    log.immune = true;
    return;
  }
  if (allowBounce && m.cat == MC_STATUS && m.target == TG_FOE &&
      (m.tags & MT_REFLECTABLE) && combatantHasAbility(def, ABILITY_MAGIC_BOUNCE)) {
    TurnLog bounced;
    battleActImpl(def, atk, field, move, bounced, 100,
                  attackerSide ^ 1u, false, false);
    log.immune = true;
    log.screenSet = bounced.screenSet;
    log.hazardSet = bounced.hazardSet;
    log.fieldCleared = bounced.fieldCleared;
    return;
  }

  // --- accuracy. A zero result means the move bypasses the accuracy roll.
  uint8_t accuracy = battleAccuracy(atk, def, field, move);
  if (accuracy && random(100) >= accuracy) {
    log.missed = true;
    return;
  }

  if ((m.fieldFlags & MF_GULP_MISSILE) &&
      exclusiveAbility(atk, 845, ABILITY_GULP_MISSILE) &&
      atk.form == BFORM_BASE) {
    BattleForm next = atk.hp * 2u > atk.maxHp
        ? BFORM_CRAMORANT_GULPING : BFORM_CRAMORANT_GORGING;
    setBattleForm(atk, next);
    log.formChanged = next;
  }

  if (m.cat == MC_STATUS) {
    bool mycelium = m.target == TG_FOE &&
        combatantHasAbility(atk, ABILITY_MYCELIUM_MIGHT);
    bool targetsCombatant = m.effect != EF_SET_HAZARD && m.effect != EF_CLEAR_FIELD;
    if (!mycelium && targetsCombatant && m.target == TG_FOE &&
        combatantHasAbility(def, ABILITY_GOOD_AS_GOLD)) {
      log.immune = true;
      return;
    }
    if (m.effect == EF_PROTECT) {
      atk.protectedTurn = true;
    } else if (m.effect == EF_HEAL) {
      log.healed = heal(atk, (uint32_t)atk.maxHp * (m.param > 0 ? m.param : 50) / 100) != 0;
    } else if (m.effect == EF_STAGE) {
      Combatant &t = (m.target == TG_SELF) ? atk : def;
      bool ignoreAbility = mycelium && &t == &def;
      log.stageMask = applyStages(t, m.statMask, m.stages, &atk, ignoreAbility);
      log.stageDelta = abilityStageDelta(t, m.stages, ignoreAbility);
      if (&t == &atk && log.stageMask && log.stageDelta > 0 &&
          combatantHasAbility(def, ABILITY_OPPORTUNIST))
        applyStages(def, log.stageMask, log.stageDelta, &def);
    } else if (m.effect == EF_SET_WEATHER && m.param > BWEATHER_NONE &&
               m.param <= BWEATHER_SNOW) {
      battleSetWeather(field, (BattleWeather)m.param);
      log.weatherSet = (BattleWeather)m.param;
    } else if (m.effect == EF_SET_TERRAIN && m.param > BTERRAIN_NONE &&
               m.param <= BTERRAIN_PSYCHIC) {
      battleSetTerrain(field, (BattleTerrain)m.param);
      log.terrainSet = (BattleTerrain)m.param;
    } else if (m.effect == EF_SET_SCREEN && m.param > BSCREEN_NONE &&
               m.param <= BSCREEN_AURORA_VEIL &&
               battleSetScreen(field.sides[attackerSide], (BattleScreen)m.param,
                               effectiveField)) {
      log.screenSet = (BattleScreen)m.param;
    } else if (m.effect == EF_SET_HAZARD && m.param > BHAZARD_NONE &&
               m.param <= BHAZARD_STICKY_WEB &&
               battleSetHazard(field.sides[attackerSide ^ 1u],
                               (BattleHazard)m.param)) {
      log.hazardSet = (BattleHazard)m.param;
    } else if (m.effect == EF_CLEAR_FIELD) {
      if (m.param == BCLEAR_OWN_HAZARDS)
        log.fieldCleared = battleClearHazards(field.sides[attackerSide]);
      else if (m.param == BCLEAR_ALL)
        log.fieldCleared = battleClearAll(field);
      if (m.statMask && m.stages) {
        Combatant &target = m.target == TG_SELF ? atk : def;
        log.stageMask = applyStages(target, m.statMask, m.stages, &atk);
        if (log.stageMask) log.stageDelta = abilityStageDelta(target, m.stages);
      }
    } else if (m.effect == EF_FORCE_SWITCH) {
      if (combatantHasAbility(def, ABILITY_SUCTION_CUPS) ||
          combatantHasAbility(def, ABILITY_GUARD_DOG))
        log.immune = true;
      else
        log.switchRequest = BSWITCH_TARGET;
    }
    if (allowDancer && (m.tags & MT_DANCE) && move.source &&
        !def.fainted() && combatantHasAbility(def, ABILITY_DANCER)) {
      TurnLog copied;
      battleActImpl(def, atk, field, battleMoveFor(def, move.source), copied, 100,
                    attackerSide ^ 1u, false, true);
      log.dancerCopied = true;
      log.counterDamage = copied.damage;
    }
    battleRefreshForms(field, atk, def);
    return;
  }

  if (def.protectedTurn && !gmaxBypassesProtect(move) &&
      !(moveMakesContact(atk, m) && combatantHasAbility(atk, ABILITY_UNSEEN_FIST))) {
    log.missed = true;
    return;
  }

  StageSnapshot absorbedStagesBefore;
  rememberStages(def, absorbedStagesBefore);
  bool absorbed = false;
  if (!ignoreTargetAbility &&
      m.type == T_WATER && (combatantHasAbility(def, ABILITY_WATER_ABSORB) ||
                            combatantHasAbility(def, ABILITY_DRY_SKIN))) {
    log.healed = heal(def, def.maxHp / 4u ? def.maxHp / 4u : 1u) != 0;
    absorbed = true;
  } else if (!ignoreTargetAbility && m.type == T_WATER && combatantHasAbility(def, ABILITY_STORM_DRAIN)) {
    log.stageMask = applyStages(def, ST_SPA, 1, &atk);
    log.stageDelta = 1;
    absorbed = true;
  } else if (!ignoreTargetAbility && m.type == T_ELECTRIC && combatantHasAbility(def, ABILITY_VOLT_ABSORB)) {
    log.healed = heal(def, def.maxHp / 4u ? def.maxHp / 4u : 1u) != 0;
    absorbed = true;
  } else if (!ignoreTargetAbility && m.type == T_ELECTRIC && combatantHasAbility(def, ABILITY_LIGHTNING_ROD)) {
    log.stageMask = applyStages(def, ST_SPA, 1, &atk);
    log.stageDelta = 1;
    absorbed = true;
  } else if (!ignoreTargetAbility && m.type == T_ELECTRIC && combatantHasAbility(def, ABILITY_MOTOR_DRIVE)) {
    log.stageMask = applyStages(def, ST_SPE, 1, &atk);
    log.stageDelta = 1;
    absorbed = true;
  } else if (!ignoreTargetAbility && m.type == T_FIRE && combatantHasAbility(def, ABILITY_FLASH_FIRE)) {
    def.flashFireActive = true;
    absorbed = true;
  } else if (!ignoreTargetAbility && m.type == T_FIRE && combatantHasAbility(def, ABILITY_WELL_BAKED_BODY)) {
    log.stageMask = applyStages(def, ST_DEF, 2, &atk);
    log.stageDelta = 2;
    absorbed = true;
  } else if (!ignoreTargetAbility && m.type == T_GRASS && combatantHasAbility(def, ABILITY_SAP_SIPPER)) {
    log.stageMask = applyStages(def, ST_ATK, 1, &atk);
    log.stageDelta = 1;
    absorbed = true;
  } else if (!ignoreTargetAbility && m.type == T_GROUND && combatantHasAbility(def, ABILITY_EARTH_EATER)) {
    log.healed = heal(def, def.maxHp / 4u ? def.maxHp / 4u : 1u) != 0;
    absorbed = true;
  }
  if (absorbed) {
    copyPositiveStageChanges(atk, def, absorbedStagesBefore);
    log.immune = true;
    return;
  }

  // --- damage, including multi-hit
  uint8_t hits = (m.effect == EF_MULTI)
      ? (combatantHasAbility(atk, ABILITY_SKILL_LINK) ? 5 : (uint8_t)(2 + random(4)))
      : 1;
  uint32_t rawTotal = 0;
  uint16_t total = 0;
  uint16_t knockoutHp = 0;
  Combatant abilitylessDef = def;
  if (ignoreTargetAbility) abilitylessDef.ability = ABILITY_NONE;
  log.effPct = typeEffVsCombatant(
      m.type, abilitylessDef, &atk, field.gravityTurns != 0);
  if (log.effPct == 0) { log.immune = true; return; }
  bool spendCharge = m.type == T_ELECTRIC && atk.abilityCharged;
  for (uint8_t h = 0; h < hits; h++) {
    uint8_t critStage = field.sides[attackerSide].critStages +
        (combatantHasAbility(atk, ABILITY_SUPER_LUCK) ? 1u : 0u);
    if (critStage > 3) critStage = 3;
    static const uint8_t CRIT_RATE[] = { 16, 8, 2, 1 };
    uint8_t critRate = CRIT_RATE[critStage];
    bool crit = (combatantHasAbility(atk, ABILITY_MERCILESS) &&
                 def.ailment == AIL_POISON) ||
        (random(critRate) == 0 &&
        (ignoreTargetAbility ||
         (!combatantHasAbility(def, ABILITY_BATTLE_ARMOR) &&
          !combatantHasAbility(def, ABILITY_SHELL_ARMOR))));
    uint16_t d = battleDamage(atk, def, field, move, crit,
                              (uint8_t)(217 + random(39)),
                              &field.sides[attackerSide ^ 1u]);
    if (h == 0 && d >= def.hp && def.hp == def.maxHp && def.hp > 1 &&
        !ignoreTargetAbility && combatantHasAbility(def, ABILITY_STURDY))
      d = (uint16_t)(def.hp - 1u);
    bool formBlocked = false;
    if (!ignoreTargetAbility && def.form == BFORM_MIMIKYU_DISGUISED &&
        exclusiveAbility(def, 778, ABILITY_DISGUISE)) {
      setBattleForm(def, BFORM_MIMIKYU_BUSTED);
      log.formChanged = BFORM_MIMIKYU_BUSTED;
      uint16_t disguiseDamage = def.maxHp / 8u ? def.maxHp / 8u : 1u;
      hurt(def, disguiseDamage);
      d = 0;
      formBlocked = true;
    } else if (!ignoreTargetAbility && def.form == BFORM_EISCUE_ICE && m.cat == MC_PHYS &&
               exclusiveAbility(def, 875, ABILITY_ICE_FACE)) {
      setBattleForm(def, BFORM_EISCUE_NOICE);
      log.formChanged = BFORM_EISCUE_NOICE;
      d = 0;
      formBlocked = true;
    }
    rawTotal += d;
    uint32_t scaledTotal = (rawTotal * effectPercent + 50u) / 100u;
    if (scaledTotal > UINT16_MAX) scaledTotal = UINT16_MAX;
    d = (uint16_t)(scaledTotal - total);
    total = (uint16_t)scaledTotal;
    if (crit) log.crit = true;
    uint16_t hpBefore = def.hp;
    hurt(def, d);
    if (!formBlocked && !ignoreTargetAbility)
      abilityAfterHit(atk, def, field, m, crit, hpBefore, log);
    if (def.fainted()) { knockoutHp = hpBefore; hits = h + 1; break; }
  }
  log.hits = hits;
  log.damage = total;
  if (spendCharge) atk.abilityCharged = false;

  if (m.effect == EF_RECOIL && m.param > 0 && total &&
      !combatantHasAbility(atk, ABILITY_ROCK_HEAD) &&
      !combatantHasAbility(atk, ABILITY_MAGIC_GUARD))
    hurt(atk, total / m.param ? total / m.param : 1);
  if (m.effect == EF_DRAIN && m.param > 0) {
    uint16_t amount = total * m.param / 100;
    if (combatantHasAbility(def, ABILITY_LIQUID_OOZE)) {
      if (!combatantHasAbility(atk, ABILITY_MAGIC_GUARD)) hurt(atk, amount ? amount : 1);
    }
    else heal(atk, amount);
  }
  if (m.effect == EF_RECHARGE) atk.recharge = true;
  if (m.effect == EF_SET_WEATHER && m.param > BWEATHER_NONE &&
      m.param <= BWEATHER_SNOW) {
    battleSetWeather(field, (BattleWeather)m.param);
    log.weatherSet = (BattleWeather)m.param;
  }
  if (m.effect == EF_SET_TERRAIN && m.param > BTERRAIN_NONE &&
      m.param <= BTERRAIN_PSYCHIC) {
    battleSetTerrain(field, (BattleTerrain)m.param);
    log.terrainSet = (BattleTerrain)m.param;
  }
  if (total) applyGmaxEffect(atk, def, field, move, attackerSide, log);
  battleRefreshForms(field, atk, def);
  if (m.effect == EF_CLEAR_FIELD && total) {
    if (m.param == BCLEAR_OWN_HAZARDS)
      log.fieldCleared = battleClearHazards(field.sides[attackerSide]);
    else if (m.param == BCLEAR_ALL)
      log.fieldCleared = battleClearAll(field);
  }
  if (total && !def.fainted() && m.effect == EF_FORCE_SWITCH &&
      log.switchRequest == BSWITCH_NONE) {
    if (combatantHasAbility(def, ABILITY_SUCTION_CUPS) ||
        combatantHasAbility(def, ABILITY_GUARD_DOG))
      log.immune = true;
    else
      log.switchRequest = BSWITCH_TARGET;
  }
  if (total && !atk.fainted() && m.effect == EF_PIVOT &&
      battleCanSwitch(atk, def, &field))
    log.switchRequest = BSWITCH_USER;

  if (m.statMask && m.stages &&
      (m.target == TG_SELF || !def.fainted()) &&
      !(m.target == TG_FOE && combatantHasAbility(def, ABILITY_SHIELD_DUST))) {
    Combatant &stageTarget = m.target == TG_SELF ? atk : def;
    applyStages(stageTarget, m.statMask, m.stages, &atk);
    log.stageMask = m.statMask;
    log.stageDelta = abilityStageDelta(stageTarget, m.stages);
    if (&stageTarget == &atk && log.stageMask && log.stageDelta > 0 &&
        combatantHasAbility(def, ABILITY_OPPORTUNIST))
      applyStages(def, log.stageMask, log.stageDelta, &def);
  }

  if (def.fainted()) {
    abilityAfterKnockout(atk);
    if (combatantHasAbility(def, ABILITY_INNARDS_OUT) && knockoutHp &&
        !combatantHasAbility(atk, ABILITY_MAGIC_GUARD)) hurt(atk, knockoutHp);
  }

  // --- secondary ailment. Never overwrites an existing one, and confusion is
  // tracked separately so it can stack with a real status, as in the games.
  uint16_t ailChance = m.ailChance;
  if (combatantHasAbility(atk, ABILITY_SERENE_GRACE)) ailChance *= 2u;
  if (ailChance > 100) ailChance = 100;
  if (m.ailment != AIL_NONE && ailChance && !def.fainted() &&
      !combatantHasAbility(def, ABILITY_SHIELD_DUST) &&
      random(100) < ailChance) {
    bool blocked = (effectiveField.weather == BWEATHER_SUN && m.ailment == AIL_FREEZE) ||
                   (battleGrounded(def, &field) &&
                    ((field.terrain == BTERRAIN_ELECTRIC && m.ailment == AIL_SLEEP) ||
                     field.terrain == BTERRAIN_MISTY));
    bool mycelium = m.cat == MC_STATUS && m.target == TG_FOE &&
        combatantHasAbility(atk, ABILITY_MYCELIUM_MIGHT);
    if (blocked || (!mycelium && abilityPreventsAilment(def, m.ailment, effectiveField)))
      return;
    if (m.ailment == AIL_CONFUSE) {
      if (!def.confuseTurns) {
        def.confuseTurns = 2 + random(3);
        log.inflicted = AIL_CONFUSE;
      }
    } else if (def.ailment == AIL_NONE) {
      // a type cannot be given the status it is made of
      bool immune = (m.ailment == AIL_BURN && combatantHasType(def, T_FIRE)) ||
                    (m.ailment == AIL_FREEZE && combatantHasType(def, T_ICE)) ||
                    (m.ailment == AIL_POISON &&
                     (combatantHasType(def, T_POISON) || combatantHasType(def, T_STEEL)) &&
                     !combatantHasAbility(atk, ABILITY_CORROSION)) ||
                    (m.ailment == AIL_PARA && combatantHasType(def, T_ELECTRIC));
      if (!immune) {
        def.ailment = m.ailment;
        if (m.ailment == AIL_SLEEP) def.ailTurns = 2 + random(3);
        log.inflicted = m.ailment;
        if (m.ailment == AIL_POISON &&
            combatantHasAbility(atk, ABILITY_POISON_PUPPETEER) &&
            !def.confuseTurns) {
          def.confuseTurns = 2 + random(3);
        }
        if ((m.ailment == AIL_BURN || m.ailment == AIL_POISON ||
             m.ailment == AIL_PARA) &&
            combatantHasAbility(def, ABILITY_SYNCHRONIZE) &&
            atk.ailment == AIL_NONE &&
            !abilityPreventsAilment(atk, m.ailment, effectiveField)) {
          bool typeImmune = (m.ailment == AIL_BURN && combatantHasType(atk, T_FIRE)) ||
              (m.ailment == AIL_POISON &&
               (combatantHasType(atk, T_POISON) || combatantHasType(atk, T_STEEL))) ||
              (m.ailment == AIL_PARA && combatantHasType(atk, T_ELECTRIC));
          if (!typeImmune) atk.ailment = m.ailment;
        }
      }
    }
  }
  log.targetFainted = def.fainted();
}

// ---------- end of turn ----------

static void battleEndCombatant(const BattleField &field,
                               const BattleSideConditions &side,
                               Combatant &c, TurnLog &log) {
  log = TurnLog();
  if (c.fainted()) return;
  if ((c.ailment == AIL_BURN || c.ailment == AIL_POISON) &&
      !(c.ailment == AIL_POISON && combatantHasAbility(c, ABILITY_POISON_HEAL)) &&
      !combatantHasAbility(c, ABILITY_MAGIC_GUARD)) {
    uint16_t chip = c.maxHp / 16;
    if (c.ailment == AIL_BURN && combatantHasAbility(c, ABILITY_HEATPROOF)) chip /= 2u;
    if (!chip) chip = 1;
    uint16_t before = c.hp;
    hurt(c, chip);
    log.damage = before - c.hp;
    log.inflicted = c.ailment;
  }
  if (!c.fainted() && field.weather == BWEATHER_SAND &&
      !combatantHasType(c, T_ROCK) && !combatantHasType(c, T_GROUND) &&
      !combatantHasType(c, T_STEEL) &&
      !combatantHasAbility(c, ABILITY_MAGIC_GUARD) &&
      !combatantHasAbility(c, ABILITY_OVERCOAT) &&
      !combatantHasAbility(c, ABILITY_SAND_RUSH) &&
      !combatantHasAbility(c, ABILITY_SAND_FORCE)) {
    uint16_t chip = c.maxHp / 16;
    if (!chip) chip = 1;
    uint16_t before = c.hp;
    hurt(c, chip);
    log.damage += before - c.hp;
    log.weatherDamage = BWEATHER_SAND;
  }
  if (!c.fainted() && field.weather == BWEATHER_RAIN &&
      (combatantHasAbility(c, ABILITY_RAIN_DISH) ||
       combatantHasAbility(c, ABILITY_DRY_SKIN))) {
    uint16_t amount = combatantHasAbility(c, ABILITY_DRY_SKIN)
        ? c.maxHp / 8u : c.maxHp / 16u;
    if (!amount) amount = 1;
    log.healed = heal(c, amount) != 0 || log.healed;
  }
  if (!c.fainted() && c.ailment == AIL_POISON &&
      combatantHasAbility(c, ABILITY_POISON_HEAL)) {
    uint16_t amount = c.maxHp / 8u;
    if (!amount) amount = 1;
    log.healed = heal(c, amount) != 0 || log.healed;
  }
  if (!c.fainted() && field.weather == BWEATHER_SNOW &&
      combatantHasAbility(c, ABILITY_ICE_BODY)) {
    uint16_t amount = c.maxHp / 16u;
    if (!amount) amount = 1;
    log.healed = heal(c, amount) != 0 || log.healed;
  }
  if (!c.fainted() && field.weather == BWEATHER_SUN &&
      (combatantHasAbility(c, ABILITY_DRY_SKIN) ||
       combatantHasAbility(c, ABILITY_SOLAR_POWER))) {
    uint16_t chip = c.maxHp / 8u;
    if (!chip) chip = 1;
    uint16_t before = c.hp;
    hurt(c, chip);
    log.damage += before - c.hp;
  }
  if (!c.fainted() && side.gmaxResidualTurns &&
      !combatantHasAbility(c, ABILITY_MAGIC_GUARD)) {
    uint8_t immuneType = side.gmaxResidualEffect == GMAX_EFFECT_VINE_LASH ? T_GRASS
        : side.gmaxResidualEffect == GMAX_EFFECT_WILDFIRE ? T_FIRE
        : side.gmaxResidualEffect == GMAX_EFFECT_CANNONADE ? T_WATER
        : side.gmaxResidualEffect == GMAX_EFFECT_VOLCALITH ? T_ROCK : T_NONE;
    if (immuneType != T_NONE && !combatantHasType(c, immuneType)) {
      uint16_t chip = c.maxHp / 6u;
      if (!chip) chip = 1;
      uint16_t before = c.hp;
      hurt(c, chip);
      log.damage += before - c.hp;
    }
  }
  if (!c.fainted() && c.bindTurns) {
    if (!combatantHasAbility(c, ABILITY_MAGIC_GUARD)) {
      uint16_t chip = c.maxHp / 8u;
      if (!chip) chip = 1;
      uint16_t before = c.hp;
      hurt(c, chip);
      log.damage += before - c.hp;
    }
    c.bindTurns--;
  }
  if (!c.fainted() && c.drowsyTurns && --c.drowsyTurns == 0 &&
      c.ailment == AIL_NONE &&
      !abilityPreventsAilment(c, AIL_SLEEP, field) &&
      !(battleGrounded(c, &field) &&
        (field.terrain == BTERRAIN_ELECTRIC || field.terrain == BTERRAIN_MISTY))) {
    c.ailment = AIL_SLEEP;
    c.ailTurns = 2 + random(3);
    log.inflicted = AIL_SLEEP;
  }
  if (!c.fainted() && field.terrain == BTERRAIN_GRASSY &&
      battleGrounded(c, &field)) {
    uint16_t amount = c.maxHp / 16;
    if (!amount) amount = 1;
    log.healed = heal(c, amount) != 0;
    if (log.healed) log.terrainHeal = BTERRAIN_GRASSY;
  }
  if (!c.fainted() && field.weather == BWEATHER_RAIN &&
      combatantHasAbility(c, ABILITY_HYDRATION)) {
    c.ailment = AIL_NONE;
    c.ailTurns = 0;
  } else if (!c.fainted() && c.ailment != AIL_NONE &&
             combatantHasAbility(c, ABILITY_SHED_SKIN) && random(100) < 33) {
    c.ailment = AIL_NONE;
    c.ailTurns = 0;
  }
  if (!c.fainted() && combatantHasAbility(c, ABILITY_SPEED_BOOST)) {
    log.stageMask |= applyStages(c, ST_SPE, 1, &c);
    log.stageDelta = 1;
  }
  if (!c.fainted() && combatantHasAbility(c, ABILITY_MOODY)) {
    uint8_t upCandidates[BATTLE_STAGE_COUNT], upCount = 0;
    for (uint8_t i = 0; i < BATTLE_STAGE_COUNT; i++)
      if (battleStageValue(c, i) < 6) upCandidates[upCount++] = i;
    if (upCount) {
      uint8_t up = upCandidates[random(upCount)];
      uint8_t downCandidates[BATTLE_STAGE_COUNT], downCount = 0;
      for (uint8_t i = 0; i < BATTLE_STAGE_COUNT; i++)
        if (i != up && battleStageValue(c, i) > -6) downCandidates[downCount++] = i;
      applyStages(c, battleStageBit(up), 2, &c);
      log.stageMask |= battleStageBit(up);
      if (downCount) {
        uint8_t down = downCandidates[random(downCount)];
        applyStages(c, battleStageBit(down), -1, &c);
        log.stageMask |= battleStageBit(down);
      }
    }
  }
  log.targetFainted = c.fainted();
}

void battleEndRound(BattleField &field, Combatant &a, Combatant &b,
                    TurnLog &aLog, TurnLog &bLog, FieldLog &fieldLog) {
  bool aWasAlive = !a.fainted(), bWasAlive = !b.fainted();
  StageSnapshot aStagesBefore, bStagesBefore;
  rememberStages(a, aStagesBefore);
  rememberStages(b, bStagesBefore);
  BattleField effectiveField = field;
  if (weatherSuppressed(a, b)) effectiveField.weather = BWEATHER_NONE;
  battleEndCombatant(effectiveField, field.sides[0], a, aLog);
  battleEndCombatant(effectiveField, field.sides[1], b, bLog);
  copyPositiveStageChanges(a, b, bStagesBefore);
  copyPositiveStageChanges(b, a, aStagesBefore);
  if (!a.fainted() && combatantHasAbility(a, ABILITY_BAD_DREAMS) &&
      b.ailment == AIL_SLEEP && !combatantHasAbility(b, ABILITY_MAGIC_GUARD)) {
    uint16_t before = b.hp;
    hurt(b, b.maxHp / 8u ? b.maxHp / 8u : 1u);
    bLog.damage += before - b.hp;
  }
  if (!b.fainted() && combatantHasAbility(b, ABILITY_BAD_DREAMS) &&
      a.ailment == AIL_SLEEP && !combatantHasAbility(a, ABILITY_MAGIC_GUARD)) {
    uint16_t before = a.hp;
    hurt(a, a.maxHp / 8u ? a.maxHp / 8u : 1u);
    aLog.damage += before - a.hp;
  }
  if (aWasAlive && a.fainted() && !b.fainted() &&
      combatantHasAbility(b, ABILITY_SOUL_HEART))
    applyStages(b, ST_SPA, 1, &b);
  if (bWasAlive && b.fainted() && !a.fainted() &&
      combatantHasAbility(a, ABILITY_SOUL_HEART))
    applyStages(a, ST_SPA, 1, &a);
  if (refreshForm(a, effectiveField)) aLog.formChanged = a.form;
  if (refreshForm(b, effectiveField)) bLog.formChanged = b.form;
  if (!a.fainted() && exclusiveAbility(a, 877, ABILITY_HUNGER_SWITCH)) {
    BattleForm next = a.form == BFORM_MORPEKO_HANGRY
        ? BFORM_MORPEKO_FULL : BFORM_MORPEKO_HANGRY;
    setBattleForm(a, next);
    aLog.formChanged = next;
  }
  if (!b.fainted() && exclusiveAbility(b, 877, ABILITY_HUNGER_SWITCH)) {
    BattleForm next = b.form == BFORM_MORPEKO_HANGRY
        ? BFORM_MORPEKO_FULL : BFORM_MORPEKO_HANGRY;
    setBattleForm(b, next);
    bLog.formChanged = next;
  }
  aLog.targetFainted = a.fainted();
  bLog.targetFainted = b.fainted();
  fieldLog = FieldLog();
  if (field.weatherTurns && --field.weatherTurns == 0) {
    BattleWeather expired = field.weather;
    field.weather = field.baseWeather;
    if (expired != field.weather) {
      fieldLog.weatherExpired = expired;
      fieldLog.weatherRestored = field.weather;
    }
  }
  if (field.terrainTurns && --field.terrainTurns == 0) {
    BattleTerrain expired = field.terrain;
    field.terrain = field.baseTerrain;
    if (expired != field.terrain) {
      fieldLog.terrainExpired = expired;
      fieldLog.terrainRestored = field.terrain;
    }
  }
  if (field.gravityTurns) field.gravityTurns--;
  BattleField refreshedField = field;
  if (weatherSuppressed(a, b)) refreshedField.weather = BWEATHER_NONE;
  if (refreshForm(a, refreshedField)) aLog.formChanged = a.form;
  if (refreshForm(b, refreshedField)) bLog.formChanged = b.form;
  for (uint8_t sideIndex = 0; sideIndex < 2; sideIndex++) {
    BattleSideConditions &side = field.sides[sideIndex];
    if (side.reflectTurns && --side.reflectTurns == 0)
      fieldLog.screensExpired[sideIndex] |= 1u << (BSCREEN_REFLECT - 1u);
    if (side.lightScreenTurns && --side.lightScreenTurns == 0)
      fieldLog.screensExpired[sideIndex] |= 1u << (BSCREEN_LIGHT_SCREEN - 1u);
    if (side.auroraVeilTurns && --side.auroraVeilTurns == 0)
      fieldLog.screensExpired[sideIndex] |= 1u << (BSCREEN_AURORA_VEIL - 1u);
    if (side.gmaxResidualTurns && --side.gmaxResidualTurns == 0)
      side.gmaxResidualEffect = GMAX_EFFECT_NONE;
  }
}

// ---------- move choice ----------

MoveId aiChooseMove(const Combatant &self, const Combatant &foe,
                    const BattleField &field, bool smart) {
  MoveId legal[MOVE_SLOTS];
  uint8_t n = 0;
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (self.moves[i] && self.moves[i] < moveCount()) legal[n++] = self.moves[i];
  if (!n) return 0;
  if (!smart) return legal[random(n)];

  int16_t bestScore = -32768;
  MoveId best = legal[0];
  for (uint8_t i = 0; i < n; i++) {
    MoveId mv = legal[i];
    const MoveEntry &m = moveEntry(mv);
    int32_t sc;
    if (m.cat == MC_STATUS) {
      // A status move costs a whole turn, so it has to buy more than chip
      // damage would. Healing is worth it only when actually hurt; a boost is
      // worth it early and worthless once the stage is already stacked.
      if (m.effect == EF_HEAL) {
        int missing = (int)self.maxHp - self.hp;
        sc = (missing * 100 / (self.maxHp ? self.maxHp : 1)) - 30;
      } else if (m.effect == EF_STAGE) {
        static const uint8_t BIT[SI_COUNT] = { ST_ATK, ST_DEF, ST_SPA, ST_SPD, ST_SPE };
        int stacked = 0, hit = 0;
        const Combatant &t = (m.target == TG_SELF) ? self : foe;
        for (int k = 0; k < SI_COUNT; k++)
          if (m.statMask & BIT[k]) { stacked += t.stage[k] * (m.stages > 0 ? 1 : -1); hit++; }
        if (!hit) hit = 1;
        // diminishing: +2 ATK is strong at stage 0, pointless at +6
        sc = 26 - (stacked * 12 / hit);
        // and never set up when one more hit would finish you
        if (self.hp * 3 < self.maxHp) sc -= 40;
      } else if (m.effect == EF_SET_WEATHER) {
        BattleWeather wanted = (BattleWeather)m.param;
        if (wanted <= BWEATHER_NONE || wanted > BWEATHER_SNOW) sc = -100;
        else if (field.weather == wanted && field.weatherTurns >= 3) sc = -50;
        else {
          sc = 18;
          for (uint8_t k = 0; k < MOVE_SLOTS; k++) {
            if (!moveValid(self.moves[k])) continue;
            uint8_t type = moveEntry(self.moves[k]).type;
            if ((wanted == BWEATHER_SUN && type == T_FIRE) ||
                (wanted == BWEATHER_RAIN && type == T_WATER)) sc += 8;
          }
        }
      } else if (m.effect == EF_SET_TERRAIN) {
        BattleTerrain wanted = (BattleTerrain)m.param;
        if (wanted <= BTERRAIN_NONE || wanted > BTERRAIN_PSYCHIC) sc = -100;
        else if (field.terrain == wanted && field.terrainTurns >= 3) sc = -50;
        else {
          sc = 18;
          uint8_t boosted = wanted == BTERRAIN_ELECTRIC ? T_ELECTRIC
                            : wanted == BTERRAIN_GRASSY ? T_GRASS
                            : wanted == BTERRAIN_PSYCHIC ? T_PSYCHIC : T_NONE;
          for (uint8_t k = 0; boosted != T_NONE && k < MOVE_SLOTS; k++)
            if (moveValid(self.moves[k]) && moveEntry(self.moves[k]).type == boosted) sc += 8;
        }
      } else {
        sc = 5;
      }
    } else {
      uint16_t dmg = battleDamage(self, foe, field, mv, false, 236);  // average roll
      sc = dmg;
      bool blocked = field.terrain == BTERRAIN_PSYCHIC &&
          battleGrounded(foe, &field) &&
                     m.effect == EF_PRIORITY && m.param > 0;
      if (dmg >= foe.hp) sc += 1000;              // a kill this turn beats all
      uint8_t acc = m.acc ? m.acc : 100;
      if ((m.fieldFlags & MF_RAIN_ACCURATE) && field.weather == BWEATHER_RAIN) acc = 100;
      else if ((m.fieldFlags & MF_RAIN_ACCURATE) && field.weather == BWEATHER_SUN) acc = 50;
      if ((m.fieldFlags & MF_SNOW_ACCURATE) && field.weather == BWEATHER_SNOW) acc = 100;
      sc = sc * acc / 100;                        // discount what tends to miss
      if (blocked) sc = -1000;
      if (m.effect == EF_RECHARGE) sc -= dmg / 4; // a free turn for the foe
      if (m.effect == EF_RECOIL) sc -= dmg / 6;
      if (m.effect == EF_CHARGE) sc -= dmg / 3;   // a turn spent winding up
      if (m.ailment != AIL_NONE && foe.ailment == AIL_NONE)
        sc += m.ailChance / 4;
    }
    if (sc > bestScore) { bestScore = (int16_t)sc; best = mv; }
  }
  return best;
}

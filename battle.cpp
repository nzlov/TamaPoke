#include "battle.h"
#include "dex.h"
#include "types.h"

// ---------- building a combatant ----------

static void fill(Combatant &c, int16_t dex, uint8_t lvl, uint16_t hp,
                 uint16_t a, uint16_t d, uint16_t sa, uint16_t sd, uint16_t sp) {
  c = Combatant();
  c.dex = dex;
  c.level = lvl;
  c.maxHp = hp ? hp : 1;
  c.hp = c.maxHp;
  c.base[SI_ATK] = a; c.base[SI_DEF] = d;
  c.base[SI_SPA] = sa; c.base[SI_SPD] = sd; c.base[SI_SPE] = sp;
  if (dex >= 1 && dex <= dexCount()) {
    c.type1 = dexEntry(dex).type1;
    c.type2 = dexEntry(dex).type2;
  }
}

void combatantFromPet(Combatant &c, const Pet &p) {
  fill(c, p.speciesId, p.level(), p.vitStat(), p.atkStat(), p.defStat(),
       p.spaStat(), p.spdStat(), p.speStat());
  for (int i = 0; i < MOVE_SLOTS; i++) c.moves[i] = p.moves[i];
  c.shiny = p.shiny;
  c.sparkle = p.sparkle;
  const char *nm = p.nick[0] ? p.nick : dexEntry(p.speciesId).name;
  snprintf(c.name, sizeof(c.name), "%s", nm);
}

void combatantFromParty(Combatant &c, const PartyMon &m) {
  fill(c, m.dex, (uint8_t)m.level, party.vitOf(m), party.atkOf(m), party.defOf(m),
       party.spaOf(m), party.spdOf(m), party.speOf(m));
  for (int i = 0; i < MOVE_SLOTS; i++) c.moves[i] = m.moves[i];
  c.shiny = m.shiny != 0;
  c.sparkle = m.sparkle != 0;
  const char *nm = m.nick[0] ? m.nick : dexEntry(m.dex).name;
  snprintf(c.name, sizeof(c.name), "%s", nm);
}

// ---------- special battle mechanics ----------

static bool combatantHasType(const Combatant &c, uint8_t type) {
  return c.type1 == type || c.type2 == type;
}

static uint16_t typeEffVsCombatant(uint8_t attack, const Combatant &defender) {
  return typeEffPct(attack, defender.type1, defender.type2);
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

bool battleGrounded(const Combatant &combatant) {
  return !combatantHasType(combatant, T_FLYING);
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
  } else if (attacker.activeMechanic == BMECH_DYNAMAX) {
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
      setMaxStageEffect(result.entry);
    }
  }
  return result;
}

bool battleMegaEligible(SpeciesId species) {
  return megaFormFor(species) != nullptr;
}

static bool hasDamagingMove(const Combatant &combatant) {
  for (uint8_t i = 0; i < MOVE_SLOTS; i++)
    if (moveValid(combatant.moves[i]) && moveEntry(combatant.moves[i]).cat != MC_STATUS)
      return true;
  return false;
}

bool battleMechanicAvailable(const BattleSideMechanics &side,
                             const Combatant &combatant,
                             BattleMechanic mechanic, MoveId move) {
  if (mechanic == BMECH_NONE || combatant.fainted() ||
      combatant.usedMechanic != BMECH_NONE ||
      side.used(mechanic)) return false;
  if (mechanic == BMECH_MEGA) return battleMegaEligible(combatant.dex);
  if (mechanic == BMECH_Z_MOVE) {
    if (moveValid(move)) return moveEntry(move).cat != MC_STATUS;
    return hasDamagingMove(combatant);
  }
  return mechanic == BMECH_DYNAMAX;
}

static void applyMegaForm(Combatant &combatant) {
  const MegaFormEntry *form = megaFormFor(combatant.dex);
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
}

bool battleActivateMechanic(BattleSideMechanics &side, Combatant &combatant,
                            BattleMechanic mechanic, MoveId move) {
  if (!battleMechanicAvailable(side, combatant, mechanic, move)) return false;
  side.usedMask |= battleMechanicBit(mechanic);
  combatant.usedMechanic = mechanic;
  if (mechanic == BMECH_DYNAMAX) {
    combatant.activeMechanic = mechanic;
    combatant.dynamaxTurns = 3;
    combatant.normalMaxHp = combatant.maxHp;
    combatant.maxHp = combatant.maxHp > UINT16_MAX / 2 ? UINT16_MAX
                                                       : (uint16_t)(combatant.maxHp * 2u);
    combatant.hp = combatant.hp > UINT16_MAX / 2 ? UINT16_MAX
                                                 : (uint16_t)(combatant.hp * 2u);
  } else if (mechanic == BMECH_MEGA) {
    combatant.activeMechanic = mechanic;
    applyMegaForm(combatant);
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
}

void battleAfterAction(Combatant &combatant) {
  if (combatant.activeMechanic != BMECH_DYNAMAX) return;
  if (combatant.dynamaxTurns) combatant.dynamaxTurns--;
  if (!combatant.dynamaxTurns || combatant.fainted()) endDynamax(combatant);
}

void battleOnSwitchOut(Combatant &combatant) {
  endDynamax(combatant);
  combatant.protectedTurn = false;
}

BattleMechanic wildBattleMechanic(uint8_t eventRoll, uint8_t choiceRoll,
                                  bool hard, bool megaEligible, bool zEligible) {
  if (eventRoll >= (hard ? 20 : 5)) return BMECH_NONE;
  BattleMechanic choices[3];
  uint8_t count = 0;
  if (zEligible) choices[count++] = BMECH_Z_MOVE;
  choices[count++] = BMECH_DYNAMAX;
  if (megaEligible) choices[count++] = BMECH_MEGA;
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

static uint16_t effStat(const Combatant &c, uint8_t idx) {
  uint16_t v = stagedStat(c.base[idx], c.stage[idx]);
  // burn halves physical attack, paralysis halves speed -- the two ailments
  // that do something beyond chip damage
  if (idx == SI_ATK && c.ailment == AIL_BURN) v = v / 2 ? v / 2 : 1;
  if (idx == SI_SPE && c.ailment == AIL_PARA) v = v / 2 ? v / 2 : 1;
  return v;
}

// ---------- damage ----------

// roll is 217..255, the series' damage spread, passed in so tests can pin it.
uint16_t battleDamage(const Combatant &atk, const Combatant &def,
                      const BattleField &field, MoveId mv, bool crit, uint8_t roll) {
  return battleDamage(atk, def, field, battleMove(mv), crit, roll);
}

uint16_t battleDamage(const Combatant &atk, const Combatant &def,
                      const BattleField &field, const BattleMove &move,
                      bool crit, uint8_t roll) {
  if (!move.valid()) return 0;
  const MoveEntry &m = move.entry;
  if (m.cat == MC_STATUS) return 0;

  if (m.effect == EF_FIXED_LVL) return atk.level ? atk.level : 1;
  if (m.effect == EF_FIXED) return m.param > 0 ? (uint16_t)m.param : 1;

  uint16_t A = (m.cat == MC_PHYS) ? effStat(atk, SI_ATK) : effStat(atk, SI_SPA);
  uint16_t D = (m.cat == MC_PHYS) ? effStat(def, SI_DEF) : effStat(def, SI_SPD);
  // A critical hit ignores the defender's positive stages and the attacker's
  // negative ones, so a Barrier cannot make you immune to a lucky roll.
  if (crit) {
    A = (m.cat == MC_PHYS) ? atk.base[SI_ATK] : atk.base[SI_SPA];
    D = (m.cat == MC_PHYS) ? def.base[SI_DEF] : def.base[SI_SPD];
  }
  if (m.cat == MC_PHYS && field.weather == BWEATHER_SNOW &&
      combatantHasType(def, T_ICE))
    D = (uint16_t)((uint32_t)D * 3u / 2u);
  if (m.cat == MC_SPEC && field.weather == BWEATHER_SAND &&
      combatantHasType(def, T_ROCK))
    D = (uint16_t)((uint32_t)D * 3u / 2u);
  if (!D) D = 1;

  uint32_t dmg = (2UL * atk.level / 5 + 2) * m.power * A / D / 50 + 2;
  if (crit) dmg *= 2;
  if ((field.weather == BWEATHER_SUN && m.type == T_FIRE) ||
      (field.weather == BWEATHER_RAIN && m.type == T_WATER))
    dmg = dmg * 3u / 2u;
  if ((field.weather == BWEATHER_SUN && m.type == T_WATER) ||
      (field.weather == BWEATHER_RAIN && m.type == T_FIRE))
    dmg /= 2u;
  if ((m.fieldFlags & MF_SOLAR_CHARGE) && field.weather != BWEATHER_NONE &&
      field.weather != BWEATHER_SUN)
    dmg /= 2u;
  if (combatantHasType(atk, m.type)) dmg = dmg * 3 / 2;
  if (battleGrounded(atk) &&
      ((field.terrain == BTERRAIN_ELECTRIC && m.type == T_ELECTRIC) ||
       (field.terrain == BTERRAIN_GRASSY && m.type == T_GRASS) ||
       (field.terrain == BTERRAIN_PSYCHIC && m.type == T_PSYCHIC)))
    dmg = dmg * 13u / 10u;
  if (field.terrain == BTERRAIN_MISTY && m.type == T_DRAGON && battleGrounded(def))
    dmg /= 2u;
  if (field.terrain == BTERRAIN_GRASSY &&
      (m.fieldFlags & MF_GRASSY_WEAKENED) && battleGrounded(def))
    dmg /= 2u;
  uint16_t eff = typeEffVsCombatant(m.type, def);
  dmg = dmg * eff / 100;
  if (eff == 0) return 0;               // immune: no chip, no minimum
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
  int pa = ma.valid() && (ma.entry.effect == EF_PRIORITY || ma.entry.effect == EF_PROTECT)
               ? ma.entry.param : 0;
  int pb = mb.valid() && (mb.entry.effect == EF_PRIORITY || mb.entry.effect == EF_PROTECT)
               ? mb.entry.param : 0;
  if (pa != pb) return pa > pb;
  uint16_t sa = effStat(a, SI_SPE), sb = effStat(b, SI_SPE);
  if (sa != sb) return sa > sb;
  return random(2) == 0;               // a genuine speed tie is a coin flip
}

// ---------- one action ----------

static uint8_t applyStages(Combatant &c, uint8_t mask, int8_t delta) {
  static const uint8_t BIT[SI_COUNT] = { ST_ATK, ST_DEF, ST_SPA, ST_SPD, ST_SPE };
  uint8_t changed = 0;
  for (int i = 0; i < SI_COUNT; i++) {
    if (!(mask & BIT[i])) continue;
    int8_t before = c.stage[i];
    int v = c.stage[i] + delta;
    c.stage[i] = v > 6 ? 6 : (v < -6 ? -6 : (int8_t)v);
    if (c.stage[i] != before) changed |= BIT[i];
  }
  return changed;
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

void battleAct(Combatant &atk, Combatant &def, BattleField &field, MoveId mv,
               TurnLog &log, uint8_t effectPercent) {
  battleAct(atk, def, field, battleMove(mv), log, effectPercent);
}

void battleAct(Combatant &atk, Combatant &def, BattleField &field,
               const BattleMove &selected, TurnLog &log, uint8_t effectPercent) {
  BattleMove move = selected;
  log = TurnLog();
  log.move = move.source;
  log.mechanic = move.mechanic;
  log.moveType = move.entry.type;
  if (effectPercent > 100) effectPercent = 100;
  if (atk.fainted() || def.fainted()) { log.skipped = true; return; }

  // --- things that can cost the turn before a move is even chosen
  if (atk.recharge) { atk.recharge = false; log.skipped = true; return; }
  if (atk.ailment == AIL_FREEZE) {
    if (random(100) < 20) atk.ailment = AIL_NONE;   // thaws
    else { log.skipped = true; return; }
  }
  if (atk.ailment == AIL_SLEEP) {
    if (atk.ailTurns) atk.ailTurns--;
    if (atk.ailTurns == 0) atk.ailment = AIL_NONE;
    else { log.skipped = true; return; }
  }
  if (atk.ailment == AIL_PARA && random(100) < 25) { log.skipped = true; return; }
  if (atk.confuseTurns) {
    atk.confuseTurns--;
    if (random(100) < 33) {               // hits itself instead
      uint16_t self = (2UL * atk.level / 5 + 2) * 40 *
                          atk.base[SI_ATK] / (atk.base[SI_DEF] ? atk.base[SI_DEF] : 1) / 50 + 2;
      hurt(atk, self);
      log.hurtSelf = true;
      log.damage = self;
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
  log.moveType = move.entry.type;
  if (!effectPercent) { log.missed = true; return; }
  bool sunnyCharge = move.valid() && (move.entry.fieldFlags & MF_SOLAR_CHARGE) &&
                     field.weather == BWEATHER_SUN;
  if (!firingCharge && !sunnyCharge && move.valid() && move.entry.effect == EF_CHARGE) {
    atk.charging = move.source;
    log.charged = true;
    return;
  }

  if (!move.valid()) { log.skipped = true; return; }
  const MoveEntry &m = move.entry;
  log.move = move.source;

  if (m.target == TG_FOE && m.effect == EF_PRIORITY && m.param > 0 &&
      field.terrain == BTERRAIN_PSYCHIC && battleGrounded(def)) {
    log.blockedByField = true;
    return;
  }

  // --- accuracy. acc 0 means it cannot miss (SWIFT, and every status move)
  uint8_t accuracy = m.acc;
  if ((m.fieldFlags & MF_RAIN_ACCURATE) && field.weather == BWEATHER_RAIN) accuracy = 0;
  else if ((m.fieldFlags & MF_RAIN_ACCURATE) && field.weather == BWEATHER_SUN) accuracy = 50;
  if ((m.fieldFlags & MF_SNOW_ACCURATE) && field.weather == BWEATHER_SNOW) accuracy = 0;
  if (accuracy && m.effect != EF_NEVER_MISS && random(100) >= accuracy) {
    log.missed = true;
    return;
  }

  if (m.cat == MC_STATUS) {
    if (m.effect == EF_PROTECT) {
      atk.protectedTurn = true;
    } else if (m.effect == EF_HEAL) {
      log.healed = heal(atk, (uint32_t)atk.maxHp * (m.param > 0 ? m.param : 50) / 100) != 0;
    } else if (m.effect == EF_STAGE) {
      Combatant &t = (m.target == TG_SELF) ? atk : def;
      log.stageMask = applyStages(t, m.statMask, m.stages);
      log.stageDelta = m.stages;
    } else if (m.effect == EF_SET_WEATHER && m.param > BWEATHER_NONE &&
               m.param <= BWEATHER_SNOW) {
      battleSetWeather(field, (BattleWeather)m.param);
      log.weatherSet = (BattleWeather)m.param;
    } else if (m.effect == EF_SET_TERRAIN && m.param > BTERRAIN_NONE &&
               m.param <= BTERRAIN_PSYCHIC) {
      battleSetTerrain(field, (BattleTerrain)m.param);
      log.terrainSet = (BattleTerrain)m.param;
    }
    return;
  }

  if (def.protectedTurn) {
    log.missed = true;
    return;
  }

  // --- damage, including multi-hit
  uint8_t hits = (m.effect == EF_MULTI) ? (uint8_t)(2 + random(4)) : 1;
  uint32_t rawTotal = 0;
  uint16_t total = 0;
  log.effPct = typeEffVsCombatant(m.type, def);
  if (log.effPct == 0) { log.immune = true; return; }
  for (uint8_t h = 0; h < hits; h++) {
    bool crit = random(16) == 0;                 // ~6%, the series' base rate
    uint16_t d = battleDamage(atk, def, field, move, crit,
                              (uint8_t)(217 + random(39)));
    rawTotal += d;
    uint32_t scaledTotal = (rawTotal * effectPercent + 50u) / 100u;
    if (scaledTotal > UINT16_MAX) scaledTotal = UINT16_MAX;
    d = (uint16_t)(scaledTotal - total);
    total = (uint16_t)scaledTotal;
    if (crit) log.crit = true;
    hurt(def, d);
    if (def.fainted()) { hits = h + 1; break; }
  }
  log.hits = hits;
  log.damage = total;

  if (m.effect == EF_RECOIL && m.param > 0 && total)
    hurt(atk, total / m.param ? total / m.param : 1);
  if (m.effect == EF_DRAIN && m.param > 0) heal(atk, total * m.param / 100);
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

  if (m.statMask && m.stages &&
      (m.target == TG_SELF || !def.fainted())) {
    Combatant &stageTarget = m.target == TG_SELF ? atk : def;
    applyStages(stageTarget, m.statMask, m.stages);
    log.stageMask = m.statMask;
    log.stageDelta = m.stages;
  }

  // --- secondary ailment. Never overwrites an existing one, and confusion is
  // tracked separately so it can stack with a real status, as in the games.
  if (m.ailment != AIL_NONE && m.ailChance && !def.fainted() &&
      random(100) < m.ailChance) {
    bool blocked = (field.weather == BWEATHER_SUN && m.ailment == AIL_FREEZE) ||
                   (battleGrounded(def) &&
                    ((field.terrain == BTERRAIN_ELECTRIC && m.ailment == AIL_SLEEP) ||
                     field.terrain == BTERRAIN_MISTY));
    if (blocked) return;
    if (m.ailment == AIL_CONFUSE) {
      if (!def.confuseTurns) {
        def.confuseTurns = 2 + random(3);
        log.inflicted = AIL_CONFUSE;
      }
    } else if (def.ailment == AIL_NONE) {
      // a type cannot be given the status it is made of
      bool immune = (m.ailment == AIL_BURN && combatantHasType(def, T_FIRE)) ||
                    (m.ailment == AIL_FREEZE && combatantHasType(def, T_ICE)) ||
                    (m.ailment == AIL_POISON && combatantHasType(def, T_POISON)) ||
                    (m.ailment == AIL_PARA && combatantHasType(def, T_ELECTRIC));
      if (!immune) {
        def.ailment = m.ailment;
        if (m.ailment == AIL_SLEEP) def.ailTurns = 2 + random(3);
        log.inflicted = m.ailment;
      }
    }
  }
  log.targetFainted = def.fainted();
}

// ---------- end of turn ----------

static void battleEndCombatant(const BattleField &field, Combatant &c, TurnLog &log) {
  log = TurnLog();
  if (c.fainted()) return;
  if (c.ailment == AIL_BURN || c.ailment == AIL_POISON) {
    uint16_t chip = c.maxHp / 16;
    if (!chip) chip = 1;
    uint16_t before = c.hp;
    hurt(c, chip);
    log.damage = before - c.hp;
    log.inflicted = c.ailment;
  }
  if (!c.fainted() && field.weather == BWEATHER_SAND &&
      !combatantHasType(c, T_ROCK) && !combatantHasType(c, T_GROUND) &&
      !combatantHasType(c, T_STEEL)) {
    uint16_t chip = c.maxHp / 16;
    if (!chip) chip = 1;
    uint16_t before = c.hp;
    hurt(c, chip);
    log.damage += before - c.hp;
    log.weatherDamage = BWEATHER_SAND;
  }
  if (!c.fainted() && field.terrain == BTERRAIN_GRASSY && battleGrounded(c)) {
    uint16_t amount = c.maxHp / 16;
    if (!amount) amount = 1;
    log.healed = heal(c, amount) != 0;
    if (log.healed) log.terrainHeal = BTERRAIN_GRASSY;
  }
  log.targetFainted = c.fainted();
}

void battleEndRound(BattleField &field, Combatant &a, Combatant &b,
                    TurnLog &aLog, TurnLog &bLog, FieldLog &fieldLog) {
  battleEndCombatant(field, a, aLog);
  battleEndCombatant(field, b, bLog);
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
      bool blocked = field.terrain == BTERRAIN_PSYCHIC && battleGrounded(foe) &&
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

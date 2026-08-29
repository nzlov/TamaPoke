#include "breeding.h"
#include "content.h"
#include "wild.h"

static bool isDitto(const PartyMon &mon) {
  return (speciesEggGroups(mon.dex) & EGG_GROUP_DITTO) != 0;
}

bool breedingEligible(const PartyMon &mon) {
  if (!mon.battleReady() || mon.dead() || !dexValid(mon.dex)) return false;
  uint16_t groups = speciesEggGroups(mon.dex);
  return groups && !(groups & EGG_GROUP_UNDISCOVERED) &&
         genderValid(mon.gender);
}

bool breedingCompatible(const PartyMon &first, const PartyMon &second) {
  if (!breedingEligible(first) || !breedingEligible(second)) return false;
  bool firstDitto = isDitto(first), secondDitto = isDitto(second);
  if (firstDitto || secondDitto) return firstDitto != secondDitto;
  if (first.gender == GENDER_NONE || second.gender == GENDER_NONE ||
      first.gender == second.gender) return false;
  return (speciesEggGroups(first.dex) & speciesEggGroups(second.dex) &
          ~(EGG_GROUP_DITTO | EGG_GROUP_UNDISCOVERED)) != 0;
}

static const PartyMon &offspringParent(const PartyMon &first,
                                       const PartyMon &second) {
  if (isDitto(first)) return second;
  if (isDitto(second)) return first;
  return first.gender == GENDER_FEMALE ? first : second;
}

SpeciesId breedingOffspringSpecies(const PartyMon &first,
                                   const PartyMon &second, uint8_t roll) {
  if (!breedingCompatible(first, second)) return SPECIES_NONE;
  SpeciesId source = offspringParent(first, second).dex;
  uint8_t count = speciesOffspringCount(source);
  return count ? speciesOffspring(source, (uint8_t)(roll % count)) : SPECIES_NONE;
}

uint32_t breedingRareThreshold(uint8_t wildBonus, uint8_t shinyParents) {
  uint32_t threshold = wildRareThreshold(wildBonus);
  if (shinyParents > 2) shinyParents = 2;
  threshold += (uint32_t)shinyParents * BREEDING_SHINY_PARENT_BONUS *
               WILD_RARE_BONUS_THRESHOLD;
  return threshold > WILD_RARE_ROLL_SCALE ? WILD_RARE_ROLL_SCALE : threshold;
}

bool breedingRareForRoll(uint32_t roll, uint8_t wildBonus, uint8_t shinyParents) {
  return roll < breedingRareThreshold(wildBonus, shinyParents);
}

AbilitySlot breedingAbilityForRoll(SpeciesId child, AbilitySlot inherited,
                                   uint8_t roll) {
  if (inherited == ABILITY_SLOT_HIDDEN &&
      speciesAbility(child, ABILITY_SLOT_HIDDEN) && roll < 60)
    return ABILITY_SLOT_HIDDEN;
  if (inherited != ABILITY_SLOT_HIDDEN && abilitySlotValid(inherited) &&
      speciesAbility(child, inherited) && roll < 80)
    return inherited;
  AbilitySlot first = speciesAbility(child, ABILITY_SLOT_ONE)
      ? ABILITY_SLOT_ONE : ABILITY_SLOT_UNKNOWN;
  AbilitySlot second = speciesAbility(child, ABILITY_SLOT_TWO)
      ? ABILITY_SLOT_TWO : ABILITY_SLOT_UNKNOWN;
  if (first == ABILITY_SLOT_UNKNOWN) return second;
  if (second == ABILITY_SLOT_UNKNOWN) return first;
  if (inherited == ABILITY_SLOT_ONE) return second;
  if (inherited == ABILITY_SLOT_TWO) return first;
  return (roll & 1u) ? second : first;
}

static bool addMove(PartyMon &child, MoveId move) {
  if (!moveValid(move)) return false;
  for (MoveId known : child.moves) if (known == move) return false;
  for (MoveId known : child.reserveMoves) if (known == move) return false;
  for (MoveId &slot : child.moves)
    if (!slot) { slot = move; return true; }
  for (MoveId &slot : child.reserveMoves)
    if (!slot) { slot = move; return true; }
  return false;
}

static void inheritEggMoves(PartyMon &child, const PartyMon &parent) {
  for (MoveId move : parent.moves)
    if (speciesHasEggMove(child.dex, move)) addMove(child, move);
  for (MoveId move : parent.reserveMoves)
    if (speciesHasEggMove(child.dex, move)) addMove(child, move);
}

PartyMon breedingCreateOffspring(const PartyMon &first, const PartyMon &second,
                                 uint8_t wildBonus) {
  PartyMon child;
  child.dex = breedingOffspringSpecies(first, second, (uint8_t)random(256));
  if (!child.dex) return PartyMon();
  child.level = 1;
  child.ageMinutes = 0;
  child.raisedMinutes = 0;
  child.shiny = breedingRareForRoll(
      (uint32_t)random((long)WILD_RARE_ROLL_SCALE), wildBonus,
      (uint8_t)(!!first.shiny + !!second.shiny));
  child.sparkle = child.shiny;
  child.nature = (NatureId)random(NATURE_COUNT);
  child.gender = genderFromRate(dexEntry(child.dex).femaleRate,
                                (uint8_t)random(8));

  const PartyMon &abilityParent = offspringParent(first, second);
  child.setAbilitySlot(breedingAbilityForRoll(
      child.dex, abilityParent.abilitySlot(), (uint8_t)random(100)));

  uint8_t *childIvs[] = { &child.ivAtk, &child.ivDef, &child.ivSpe, &child.ivHp };
  const uint8_t firstIvs[] = { first.ivAtk, first.ivDef, first.ivSpe, first.ivHp };
  const uint8_t secondIvs[] = { second.ivAtk, second.ivDef, second.ivSpe, second.ivHp };
  for (uint8_t stat = 0; stat < 4; stat++) *childIvs[stat] = 8 + random(24);
  uint8_t order[] = { 0, 1, 2, 3 };
  for (uint8_t i = 3; i > 0; i--) {
    uint8_t swap = (uint8_t)random(i + 1);
    uint8_t value = order[i]; order[i] = order[swap]; order[swap] = value;
  }
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t stat = order[i];
    *childIvs[stat] = random(2) ? firstIvs[stat] : secondIvs[stat];
  }
  if (child.shiny)
    for (uint8_t stat = 0; stat < 4; stat++)
      if (*childIvs[stat] < 20) *childIvs[stat] = 20;

  const PartyMon &primary = offspringParent(first, second);
  const PartyMon &secondary = &primary == &first ? second : first;
  inheritEggMoves(child, primary);
  inheritEggMoves(child, secondary);
  for (uint16_t i = 0; i < learnCount(child.dex); i++)
    if (learnMethod(child.dex, i) == LM_LEVEL_UP && learnLevel(child.dex, i) <= 1)
      addMove(child, learnMove(child.dex, i));
  child.lastLearnLevel = 1;
  return child;
}

#pragma once
#include <stdint.h>
#include "party.h"

constexpr uint16_t EGG_GROUP_DITTO = 1u << 12;
constexpr uint16_t EGG_GROUP_UNDISCOVERED = 1u << 14;
constexpr uint32_t BREEDING_MIN_SECONDS = 60UL * 60UL;
constexpr uint32_t BREEDING_MAX_SECONDS = 2UL * 60UL * 60UL;
constexpr uint8_t BREEDING_SHINY_PARENT_BONUS = 5;

bool breedingEligible(const PartyMon &mon);
bool breedingCompatible(const PartyMon &first, const PartyMon &second);
SpeciesId breedingOffspringSpecies(const PartyMon &first,
                                   const PartyMon &second, uint8_t roll);
uint32_t breedingRareThreshold(uint8_t wildBonus, uint8_t shinyParents);
bool breedingRareForRoll(uint32_t roll, uint8_t wildBonus, uint8_t shinyParents);
AbilitySlot breedingAbilityForRoll(SpeciesId child, AbilitySlot inherited,
                                   uint8_t roll);
PartyMon breedingCreateOffspring(const PartyMon &first, const PartyMon &second,
                                 uint8_t wildBonus);

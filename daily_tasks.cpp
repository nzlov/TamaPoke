#include "daily_tasks.h"
#include "content.h"
#include "wild.h"

static bool taskSpeciesRegistered(SpeciesId species,
                                  const uint8_t *registered) {
  return registered[(species - 1) >> 3] & (1u << ((species - 1) & 7));
}

static bool taskSpeciesEligible(SpeciesId species, const uint8_t *registered,
                                bool registeredPool) {
  return dexValid(species) && dexEntry(species).rarity != R_LEGENDARIO &&
         taskSpeciesRegistered(species, registered) == registeredPool;
}

static uint32_t nextTaskRoll(uint32_t &state) {
  if (!state) state = 0x9E3779B9UL;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

static bool alreadyPicked(const DailyTaskState &state, uint8_t count,
                          SpeciesId species) {
  for (uint8_t i = 0; i < count; i++)
    if (state.entries[i].species == species) return true;
  return false;
}

bool dailyTasksRefresh(
    DailyTaskState &state, uint32_t day,
    const uint8_t registered[(CONTENT_MAX_SPECIES + 7) / 8], uint32_t seed) {
  if (!day || state.day == day) return false;
  state = DailyTaskState();
  state.day = day;
  uint32_t rollState = seed ^ day ^ 0xA341316CUL;
  for (uint8_t slot = 0; slot < DAILY_TASK_COUNT; slot++) {
    bool registeredPool = nextTaskRoll(rollState) % 100u <
                          DAILY_TASK_REGISTERED_CHANCE;
    auto candidateCount = [&](bool pool) {
      uint16_t count = 0;
      for (uint8_t region = 0; region < regionAll(); region++) {
        const RegionInfo &info = regionInfo(region);
        for (SpeciesId species = info.lo;
             species <= info.hi && species <= dexCount(); species++)
          if (taskSpeciesEligible(species, registered, pool) &&
              !alreadyPicked(state, slot, species)) count++;
      }
      return count;
    };
    uint16_t candidates = candidateCount(registeredPool);
    if (!candidates) {
      registeredPool = !registeredPool;
      candidates = candidateCount(registeredPool);
    }
    if (!candidates) break;
    uint16_t pick = (uint16_t)(nextTaskRoll(rollState) % candidates);
    for (uint8_t region = 0; region < regionAll(); region++) {
      const RegionInfo &info = regionInfo(region);
      for (SpeciesId species = info.lo;
           species <= info.hi && species <= dexCount(); species++) {
        if (!taskSpeciesEligible(species, registered, registeredPool) ||
            alreadyPicked(state, slot, species)) continue;
        if (!pick--) {
          state.entries[slot].species = species;
          region = regionAll();
          break;
        }
      }
    }
  }
  return true;
}

SpeciesId dailyTaskEncounter(const DailyTaskState &state, uint8_t region,
                             bool night, uint8_t chanceRoll,
                             uint32_t pickRoll) {
  if (region >= regionAll() || chanceRoll >= DAILY_TASK_ENCOUNTER_CHANCE)
    return SPECIES_NONE;
  const RegionInfo &info = regionInfo(region);
  uint8_t matches = 0;
  for (const DailyTask &task : state.entries)
    if (!task.completed && task.species >= info.lo && task.species <= info.hi &&
        dexValid(task.species) && wildEncounterAllowed(task.species, night))
      matches++;
  if (!matches) return SPECIES_NONE;
  uint8_t pick = (uint8_t)(pickRoll % matches);
  for (const DailyTask &task : state.entries) {
    if (task.completed || task.species < info.lo || task.species > info.hi ||
        !dexValid(task.species) || !wildEncounterAllowed(task.species, night))
      continue;
    if (!pick--) return task.species;
  }
  return SPECIES_NONE;
}

bool dailyTaskHardReward(uint16_t submittedLevel, uint16_t partyAverageLevel) {
  return submittedLevel >= (uint16_t)(partyAverageLevel + 10);
}

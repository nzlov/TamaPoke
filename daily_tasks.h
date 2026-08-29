#pragma once
#include <stdint.h>
#include "dex.h"

constexpr uint8_t DAILY_TASK_COUNT = 3;
constexpr uint8_t DAILY_TASK_ENCOUNTER_CHANCE = 30;
constexpr uint8_t DAILY_TASK_REGISTERED_CHANCE = 70;

struct DailyTask {
  SpeciesId species = SPECIES_NONE;
  uint8_t completed = 0;
  uint8_t reserved = 0;
};

struct DailyTaskState {
  uint32_t day = 0;
  DailyTask entries[DAILY_TASK_COUNT];
};
static_assert(sizeof(DailyTaskState) == 16,
              "daily task state is part of the persistent player snapshot");

bool dailyTasksRefresh(
    DailyTaskState &state, uint32_t day,
    const uint8_t registered[(CONTENT_MAX_SPECIES + 7) / 8], uint32_t seed);
SpeciesId dailyTaskEncounter(const DailyTaskState &state, uint8_t region,
                             bool night, uint8_t chanceRoll,
                             uint32_t pickRoll);
bool dailyTaskHardReward(uint16_t submittedLevel, uint16_t partyAverageLevel);

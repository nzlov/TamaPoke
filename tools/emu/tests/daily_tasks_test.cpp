#include "Arduino.h"
#include "Preferences.h"
#include "daily_tasks.h"
#include "content.h"
#include <cstdio>

uint32_t g_seed = 0xD411u;
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
static void check(bool ok, const char *message) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", message);
  if (!ok) bad++;
}

int main() {
  contentBegin();
  uint8_t registered[(CONTENT_MAX_SPECIES + 7) / 8] = {};
  for (SpeciesId species : { (SpeciesId)1, (SpeciesId)4, (SpeciesId)6 })
    registered[(species - 1) >> 3] |= 1u << ((species - 1) & 7);
  DailyTaskState state;
  check(dailyTasksRefresh(state, 100, registered, 12345),
        "a new RTC day creates all daily tasks");
  bool distinct = true, eligible = true;
  for (uint8_t i = 0; i < DAILY_TASK_COUNT; i++) {
    SpeciesId species = state.entries[i].species;
    eligible = eligible && dexValid(species) &&
               dexEntry(species).rarity != R_LEGENDARIO;
    for (uint8_t j = 0; j < i; j++)
      if (state.entries[j].species == species) distinct = false;
  }
  check(distinct && eligible,
        "three distinct normally encounterable species are selected");
  DailyTaskState unchanged = state;
  check(!dailyTasksRefresh(state, 100, registered, 999) &&
        !memcmp(&state, &unchanged, sizeof(state)),
        "opening again on the same day does not reroll tasks");
  state.entries[0].completed = 1;
  check(dailyTasksRefresh(state, 101, registered, 12345) &&
        !state.entries[0].completed && !state.entries[1].completed &&
        !state.entries[2].completed,
        "midnight replaces and resets all three tasks");

  bool excludesLegendary = true;
  DailyTaskState nonLegendary;
  for (uint32_t day = 200; day < 264; day++) {
    dailyTasksRefresh(nonLegendary, day, registered, day * 7919u);
    for (const DailyTask &task : nonLegendary.entries)
      excludesLegendary = excludesLegendary && dexValid(task.species) &&
                          dexEntry(task.species).rarity != R_LEGENDARIO;
  }
  check(excludesLegendary,
        "daily refresh never selects a legendary species");
  check(DAILY_TASK_REGISTERED_CHANCE == 70,
        "registered species own exactly seventy percent of the pool roll");
  uint32_t registeredTargets = 0, totalTargets = 0;
  DailyTaskState weighted;
  for (uint32_t day = 300; day < 800; day++) {
    dailyTasksRefresh(weighted, day, registered, day * 104729u);
    for (const DailyTask &task : weighted.entries) {
      bool known = registered[(task.species - 1) >> 3] &
                   (1u << ((task.species - 1) & 7));
      registeredTargets += known;
      totalTargets++;
    }
  }
  check(registeredTargets * 100u >= totalTargets * 66u &&
        registeredTargets * 100u <= totalTargets * 74u,
        "daily targets follow the deterministic seventy-thirty split");

  DailyTaskState encounter;
  encounter.day = 1;
  encounter.entries[0].species = 1;
  encounter.entries[1].species = 4;
  encounter.entries[2].species = 152;
  check(dailyTaskEncounter(encounter, 0, false, 29, 0) == 1 &&
        dailyTaskEncounter(encounter, 0, false, 29, 1) == 4,
        "the thirty-percent branch chooses uniformly among regional tasks");
  check(dailyTaskEncounter(encounter, 0, false, 30, 0) == SPECIES_NONE,
        "the task encounter branch stops at the exact thirty-percent boundary");
  encounter.entries[0].completed = 1;
  check(dailyTaskEncounter(encounter, 0, false, 0, 0) == 4,
        "completed tasks no longer boost encounters");
  DexEntry &daySpecies = const_cast<DexEntry &>(dexEntry(1));
  DexEntry &nightSpecies = const_cast<DexEntry &>(dexEntry(4));
  uint8_t oldDayPeriods = daySpecies.encounterPeriods;
  uint8_t oldNightPeriods = nightSpecies.encounterPeriods;
  daySpecies.encounterPeriods = ENCOUNTER_DAY;
  nightSpecies.encounterPeriods = ENCOUNTER_NIGHT;
  encounter.entries[0].completed = 0;
  check(dailyTaskEncounter(encounter, 0, false, 0, 0) == 1 &&
        dailyTaskEncounter(encounter, 0, true, 0, 0) == 4,
        "task encounters respect the shared day and night species pools");
  daySpecies.encounterPeriods = oldDayPeriods;
  nightSpecies.encounterPeriods = oldNightPeriods;
  check(dailyTaskHardReward(30, 20) && !dailyTaskHardReward(29, 20),
        "hard rewards start at party average plus ten levels");
  return bad ? 1 : 0;
}

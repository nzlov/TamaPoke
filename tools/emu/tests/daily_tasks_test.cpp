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
  DailyTaskState state;
  check(dailyTasksRefresh(state, 100, 0, 12345),
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
  check(!dailyTasksRefresh(state, 100, 0, 999) &&
        !memcmp(&state, &unchanged, sizeof(state)),
        "opening again on the same day does not reroll tasks");
  state.entries[0].completed = 1;
  check(dailyTasksRefresh(state, 101, 0, 12345) &&
        !state.entries[0].completed && !state.entries[1].completed &&
        !state.entries[2].completed,
        "midnight replaces and resets all three tasks");

  DailyTaskState encounter;
  encounter.day = 1;
  encounter.entries[0].species = 1;
  encounter.entries[1].species = 4;
  encounter.entries[2].species = 152;
  check(dailyTaskEncounter(encounter, 0, 29, 0) == 1 &&
        dailyTaskEncounter(encounter, 0, 29, 1) == 4,
        "the thirty-percent branch chooses uniformly among regional tasks");
  check(dailyTaskEncounter(encounter, 0, 30, 0) == SPECIES_NONE,
        "the task encounter branch stops at the exact thirty-percent boundary");
  encounter.entries[0].completed = 1;
  check(dailyTaskEncounter(encounter, 0, 0, 0) == 4,
        "completed tasks no longer boost encounters");
  check(dailyTaskHardReward(30, 20) && !dailyTaskHardReward(29, 20),
        "hard rewards start at party average plus ten levels");
  return bad ? 1 : 0;
}

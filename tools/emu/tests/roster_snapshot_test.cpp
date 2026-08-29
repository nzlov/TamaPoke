// The six cultivation slots and their RTC baseline are one persistence unit.
// Saving a newer roster with an older `seen` replays live decay after reboot.
#include "Arduino.h"
#include "Preferences.h"
#include "party.h"
#include "pet.h"
#include "save.h"
#include "perf.h"
#include <cstdio>

uint32_t g_seed = 211;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
static uint32_t g_ms = 0;
uint32_t millis() { return g_ms; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

int main() {
  Pet active;
  active.begin();
  active.dbgHatchAs(1, false);
  active.fullness = 80;

  Party roster;
  roster.begin();
  roster.attach(active);
  active.dbgSetSeen(1000);
  uint32_t teamWrites = perfSample(PERF_TEAM_SAVE).nvsWrites;
  active.saveNow();
  bool singleCommit = perfSample(PERF_TEAM_SAVE).nvsWrites == teamWrites + 1;

  for (int i = 0; i < 10; i++) active.dbgTick();
  active.lastSeenEpoch = 1600;
  roster.captureActive(active);  // state and epoch must commit together

  Pet rebooted;
  rebooted.begin();
  Party rebootedRoster;
  rebootedRoster.begin();
  rebootedRoster.attach(rebooted);
  rebootedRoster.syncClock(rebooted, 1600);

  bool ok = singleCommit && rebooted.fullness == 60 && nvs().count("team2") == 1;
  std::printf("%s  roster snapshot keeps state and seen atomic\n", ok ? "PASS" : "FAIL");

  nvs().clear();
  PartyMon old[PARTY_SLOTS] = {};
  old[0].dex = 1;
  old[0].stateVersion = 4;
  old[0].fullness = 70;
  old[0].joy = old[0].energy = old[0].hygiene = 80;
  old[0].ivAtk = old[0].ivDef = old[0].ivSpe = old[0].ivHp = 20;
  old[0].nature = NATURE_HARDY;
  old[0].gender = GENDER_MALE;
  Preferences seed;
  seed.begin("tamapoke", false);
  seed.putBool("init", true);
  seed.putUShort("savev", SAVE_STATE_VERSION_LEGACY);
  seed.putBytes("team1", old, sizeof(old));
  seed.putUChar("active", 0);
  seed.putUInt("seen", 2000);
  seed.putUShort("rostv", 2);

  Pet migratedPet;
  migratedPet.begin();
  Party migratedRoster;
  migratedRoster.begin();
  migratedRoster.attach(migratedPet);
  migratedRoster.syncClock(migratedPet, 2000);
  bool migrated = migratedPet.fullness == 70 && nvs().count("team2") == 1 &&
                  seed.getUShort("rostv", 0) == 6;
  std::printf("%s  separate team1/seen save migrates to team2\n",
              migrated ? "PASS" : "FAIL");
  ok = ok && migrated;

  nvs().clear();
  struct RosterSnapshotV3 {
    uint32_t magic;
    uint32_t seenEpoch;
    uint8_t active;
    uint8_t reserved[3];
    PartyMon slots[PARTY_SLOTS];
  } oldSnapshot = {};
  oldSnapshot.magic = 0x33534B54UL;
  oldSnapshot.seenEpoch = 3000;
  oldSnapshot.slots[0] = old[0];
  oldSnapshot.slots[0].gymIvRewards[0] = GYM_IV_REWARD_DEF;
  seed.putBool("init", true);
  seed.putUShort("savev", SAVE_STATE_VERSION_LEGACY);
  seed.putUShort("badg", 0x0005);
  seed.putBytes("team2", &oldSnapshot, sizeof(oldSnapshot));
  seed.putUShort("rostv", 3);

  PlayerProgress v3Player;
  Pet v3Pet(v3Player);
  Party v3Roster;
  v3Pet.begin();
  v3Roster.begin();
  v3Roster.attach(v3Pet);
  bool v3Migrated = v3Player.badges == 0x0005 &&
                    v3Pet.gymIvRewardAt(0, 0) == GYM_IV_REWARD_DEF &&
                    seed.getUShort("rostv", 0) == 6 &&
                    seed.getBytesLength("team2") > sizeof(oldSnapshot);
  std::printf("%s  v3 roster and legacy player keys migrate to v6 snapshot\n",
              v3Migrated ? "PASS" : "FAIL");
  ok = ok && v3Migrated;

  nvs().clear();
  struct RosterSnapshotV4 {
    uint32_t magic;
    uint32_t seenEpoch;
    uint8_t active;
    uint8_t lead;
    uint8_t reserved[2];
    uint8_t player[612];
    PartyMon slots[PARTY_SLOTS];
  } v4Snapshot = {};
  v4Snapshot.magic = 0x34534B54UL;
  v4Snapshot.seenEpoch = 4000;
  v4Snapshot.slots[0] = old[0];
  seed.putBool("init", true);
  seed.putUShort("savev", SAVE_STATE_VERSION);
  seed.putBytes("team2", &v4Snapshot, sizeof(v4Snapshot));
  seed.putUShort("rostv", 4);

  Pet v4Pet;
  v4Pet.begin();
  Party v4Roster;
  v4Roster.begin();
  v4Roster.attach(v4Pet);
  bool v4Migrated = v4Pet.speciesId == 1 &&
                    v4Roster.breeding.status == BREEDING_IDLE &&
                    v4Roster.breeding.parents[0].empty() &&
                    seed.getUShort("rostv", 0) == 6;
  std::printf("%s  v4 roster migrates with an empty breeding center\n",
              v4Migrated ? "PASS" : "FAIL");
  ok = ok && v4Migrated;

  nvs().clear();
  struct RosterSnapshotV5 {
    uint32_t magic;
    uint32_t seenEpoch;
    uint8_t active;
    uint8_t lead;
    uint8_t reserved[2];
    uint8_t player[612];
    PartyMon slots[PARTY_SLOTS];
    BreedingCenterState breeding;
  } v5Snapshot = {};
  v5Snapshot.magic = 0x35534B54UL;
  v5Snapshot.seenEpoch = 5000;
  v5Snapshot.slots[0] = old[0];
  v5Snapshot.breeding.parents[0] = old[0];
  seed.putBool("init", true);
  seed.putUShort("savev", SAVE_STATE_VERSION);
  seed.putBytes("team2", &v5Snapshot, sizeof(v5Snapshot));
  seed.putUShort("rostv", 5);

  Pet v5Pet;
  v5Pet.begin();
  Party v5Roster;
  v5Roster.begin();
  v5Roster.attach(v5Pet);
  bool v5Migrated = v5Pet.speciesId == 1 &&
                    v5Roster.breeding.parents[0].dex == 1 &&
                    v5Pet.playerProgress().dailyTasks.day == 0 &&
                    seed.getUShort("rostv", 0) == 6;
  std::printf("%s  v5 roster gains empty daily tasks without losing state\n",
              v5Migrated ? "PASS" : "FAIL");
  ok = ok && v5Migrated;
  return ok ? 0 : 1;
}

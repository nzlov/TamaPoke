// The six cultivation slots and their RTC baseline are one persistence unit.
// Saving a newer roster with an older `seen` replays live decay after reboot.
#include "Arduino.h"
#include "Preferences.h"
#include "party.h"
#include "pet.h"
#include "save.h"
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
  roster.captureActive(active);

  for (int i = 0; i < 10; i++) active.dbgTick();
  active.lastSeenEpoch = 1600;
  roster.captureActive(active);  // state and epoch must commit together

  Pet rebooted;
  rebooted.begin();
  Party rebootedRoster;
  rebootedRoster.begin();
  rebootedRoster.attach(rebooted);
  rebootedRoster.syncClock(rebooted, 1600);

  bool ok = rebooted.fullness == 60 && nvs().count("team2") == 1;
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
  seed.putUShort("savev", SAVE_STATE_VERSION);
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
                  seed.getUShort("rostv", 0) == 3;
  std::printf("%s  separate team1/seen save migrates to team2\n",
              migrated ? "PASS" : "FAIL");
  ok = ok && migrated;
  return ok ? 0 : 1;
}

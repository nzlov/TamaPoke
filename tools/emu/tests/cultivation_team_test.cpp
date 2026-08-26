// Six cultivation slots advance together; Box records stay frozen until they
// are exchanged back into the team. Switching must preserve every care field.
#include "Arduino.h"
#include "Preferences.h"
#include "dex.h"
#include "party.h"
#include "pet.h"
#include <cstdio>

uint32_t g_seed = 131;
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

static int bad = 0;
static void ck(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static PartyMon raised(int16_t dex, uint32_t age, uint8_t fullness,
                       uint8_t joy, uint8_t bond) {
  PartyMon m;
  m.dex = dex;
  m.ageMinutes = age;
  m.level = (uint16_t)(1 + age / MINUTES_PER_LEVEL);
  m.fullness = fullness;
  m.joy = joy;
  m.bond = bond;
  m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
  return m;
}

int main() {
  if (dexCount() < 7) {
    std::puts("FAIL content packs are required for cultivation-team coverage");
    return 1;
  }
  Pet active;
  active.begin();
  active.dbgHatchAs(1, false);
  active.fullness = 88;
  active.joy = 77;
  active.bond = 12;

  Party roster;
  roster.begin();
  roster.attach(active);
  roster.replaceAt(1, raised(4, 30, 66, 55, 23));
  roster.box[0] = raised(7, 90, 44, 33, 34);
  roster.boxSave();

  uint32_t activeAge = active.ageMinutes;
  uint32_t teamAge = roster.slots[1].ageMinutes;
  uint32_t boxAge = roster.box[0].ageMinutes;
  g_ms = PET_TICK_MS;
  active.update(g_ms);
  roster.update(active, g_ms);
  ck(active.ageMinutes == activeAge + 1 &&
     roster.slots[1].ageMinutes == teamAge + 1,
     "all occupied cultivation slots advance each live minute");
  ck(roster.box[0].ageMinutes == boxAge,
     "Box state stays frozen during live progression");

  active.fullness = 37;
  ck(roster.activate(1, active) && active.speciesId == 4 && active.bond == 23,
     "switching imports the selected slot's full state");
  active.joy = 19;
  ck(roster.activate(0, active) && active.speciesId == 1 && active.fullness == 37,
     "switching back restores edits made to the previous active slot");
  ck(roster.slots[1].joy == 19,
     "leaving a slot captures its latest care state");

  roster.swapPartyBox(1, 0);
  ck(roster.slots[1].dex == 7 && roster.slots[1].fullness == 44 &&
     roster.box[0].dex == 4 && roster.box[0].joy == 19,
     "team and Box exchange complete cultivation records");

  Preferences clock;
  clock.begin("tamapoke", false);
  clock.putUInt("seen", 1000);
  clock.end();
  uint32_t teamBeforeOffline = roster.slots[1].ageMinutes;
  uint32_t boxBeforeOffline = roster.box[0].ageMinutes;
  roster.syncClock(active, 1600);
  ck(roster.slots[1].ageMinutes == teamBeforeOffline + 10,
     "inactive cultivation slots receive offline progress");
  ck(roster.box[0].ageMinutes == boxBeforeOffline,
     "Box remains frozen during offline progress");

  std::puts(bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}

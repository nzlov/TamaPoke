#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>

uint32_t g_seed = 47;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
void sfxPlay(uint8_t) {}
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

static int bad = 0;
static void check(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static uint16_t trainingTotal(const PartyMon &m) {
  return (uint16_t)m.trAtk + m.trDef + m.trSpe;
}

static PartyMon readyMon(int16_t dex) {
  PartyMon m;
  m.dex = dex;
  m.level = 50;
  m.ageMinutes = 49UL * MINUTES_PER_LEVEL;
  m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 31;
  m.trAtk = m.trDef = m.trSpe = 10;
  return m;
}

int main() {
  Pet active;
  active.speciesId = 6;
  active.ageMinutes = 49UL * MINUTES_PER_LEVEL;
  active.ivAtk = active.ivDef = active.ivSpe = active.ivHp = 31;
  active.trAtk = active.trDef = active.trSpe = 10;

  for (PartyMon &slot : party.slots) slot = PartyMon();
  active.exportState(party.slots[0]);
  party.slots[1] = readyMon(25);
  party.slots[2] = readyMon(39);

  uint16_t activeBefore = (uint16_t)active.trAtk + active.trDef + active.trSpe;
  uint16_t reserveBefore = trainingTotal(party.slots[1]);
  uint16_t benchBefore = trainingTotal(party.slots[2]);
  party.rewardRandomTraining(0x03, active, 10);

  check((uint16_t)active.trAtk + active.trDef + active.trSpe == activeBefore + 10,
        "the active participant receives ten random current-training points");
  check(trainingTotal(party.slots[1]) == reserveBefore + 10,
        "another entered participant receives ten random current-training points");
  check(trainingTotal(party.slots[2]) == benchBefore,
        "a team member outside the participation mask receives no training");

  active.trAtk = active.trDef = active.trSpe = active.trMaxAtk();
  party.captureActive(active, false);
  uint16_t capped = (uint16_t)active.trAtk + active.trDef + active.trSpe;
  party.rewardRandomTraining(0x01, active, 10);
  check((uint16_t)active.trAtk + active.trDef + active.trSpe == capped,
        "training rewards stop at each current-value cap");

  uint16_t beforeNoReward = (uint16_t)active.trAtk + active.trDef + active.trSpe;
  party.rewardRandomTraining(0, active, 10);
  check((uint16_t)active.trAtk + active.trDef + active.trSpe == beforeNoReward,
        "an empty participation mask grants no training");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}

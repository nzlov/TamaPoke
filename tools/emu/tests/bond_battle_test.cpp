// Bond modifies the complete player combatant at the battle boundary without
// changing the creature's persisted IVs, training, or displayed base stats.
#include "Arduino.h"
#include "Preferences.h"
#include "battle.h"
#include "content.h"
#include "party.h"
#include "pet.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 61;
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
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static bool scaledBy(const Combatant &got, const Combatant &base, uint16_t percent) {
  if (got.maxHp != (uint32_t)base.maxHp * percent / 100u) return false;
  for (uint8_t i = 0; i < SI_COUNT; i++)
    if (got.base[i] != (uint32_t)base.base[i] * percent / 100u) return false;
  return true;
}

int main() {
  contentBegin();
  Pet pet;
  pet.dbgHatchAs(65, false);
  pet.ageMinutes = 49UL * MINUTES_PER_LEVEL;
  pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = 20;
  pet.trAtk = pet.trDef = pet.trSpe = 20;
  pet.nature = NATURE_UNKNOWN;
  pet.gender = GENDER_NONE;

  pet.bond = 60;
  Combatant neutral;
  combatantFromPet(neutral, pet);
  ck(neutral.maxHp == pet.vitStat() && neutral.base[SI_ATK] == pet.atkStat() &&
     neutral.base[SI_DEF] == pet.defStat() && neutral.base[SI_SPA] == pet.spaStat() &&
     neutral.base[SI_SPD] == pet.spdStat() && neutral.base[SI_SPE] == pet.speStat(),
     "bond 60 keeps all six battle stats at 100 percent");

  pet.bond = 0;
  Combatant low;
  combatantFromPet(low, pet);
  ck(scaledBy(low, neutral, 70), "bond 0 scales all six battle stats to 70 percent");

  pet.bond = 100;
  Combatant high;
  combatantFromPet(high, pet);
  ck(scaledBy(high, neutral, 120), "bond 100 scales all six battle stats to 120 percent");
  ck(!battleObservesMove(0, 0) &&
     battleObservesMove(50, 14) && !battleObservesMove(50, 15) &&
     battleObservesMove(100, 29) && !battleObservesMove(100, 30),
     "bond scales observed-move learning linearly from 0 to 30 percent");
  ck(high.bond == 100,
     "the combatant keeps bond for battle-time observation checks");

  PartyMon banked;
  pet.exportState(banked);
  Combatant fromParty;
  combatantFromParty(fromParty, banked);
  ck(fromParty.maxHp == high.maxHp &&
     !memcmp(fromParty.base, high.base, sizeof(high.base)),
     "banked party members use the same bond multiplier");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}

#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>

uint32_t g_seed = 113;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
static uint32_t nowMs = 1;
uint32_t millis() { return nowMs; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static PartyMon mon(int16_t dex, uint8_t level) {
  PartyMon m;
  m.dex = dex;
  m.level = level;
  m.ageMinutes = (uint32_t)(level - 1) * MINUTES_PER_LEVEL;
  m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
  return m;
}

static void finish(Pet &pet) {
  nowMs += CEREMONY_MS + 1;
  pet.update(nowMs);
}

static void fresh(Pet &pet, Party &party, int16_t dex) {
  Preferences reset;
  reset.begin("tamapoke", false);
  reset.clear();
  reset.end();
  pet.begin();
  pet.dbgHatchAs(dex, false);
  party.begin();
  party.attach(pet);
}

int main() {
  {
    Pet pet; Party roster; fresh(pet, roster, 6);
    pet.ageMinutes = FAREWELL_AGE_MIN * 3;
    pet.raisedMinutes = FAREWELL_AGE_MIN - 1;
    ck(!pet.canFarewellNow(), "wild encounter age cannot fake three days of cultivation");
    pet.raisedMinutes++;
    ck(pet.canFarewellNow(), "a final form becomes farewell-ready after three raised days");
  }

  {
    Pet pet; Party roster; fresh(pet, roster, 6);
    pet.raisedMinutes = FAREWELL_AGE_MIN;
    pet.ageMinutes = 98UL * MINUTES_PER_LEVEL;
    roster.add(mon(25, 30));
    pet.startFarewell(); finish(pet);
    ck(player.wildRareBonus == 1, "a normal farewell adds one shared percentage point");
    ck(roster.count() == 1 && pet.speciesId == 25,
       "farewell removes the active creature and activates the remaining member");
    ck(!pet.isEgg(), "farewell does not generate an egg while the roster has a member");
  }

  {
    Pet pet; Party roster; fresh(pet, roster, 6);
    pet.raisedMinutes = FAREWELL_AGE_MIN;
    pet.ageMinutes = 99UL * MINUTES_PER_LEVEL;
    player.wildRareBonus = 14;
    pet.startFarewell(); finish(pet);
    ck(player.wildRareBonus == 15, "a level-one-hundred farewell adds two but respects the cap");
    ck(roster.count() == 1 && pet.isEgg(),
       "the last creature leaving an empty team and Box creates one safety egg");
    ck(!pet.shiny, "the safety egg does not carry the wild rare state");
  }

  {
    Pet pet; Party roster; fresh(pet, roster, 6);
    g_seed = 424242;
    pet.lastEnd = CER_FAREWELL;
    int16_t afterFarewell = pet.pickEggSpecies();
    g_seed = 424242;
    pet.lastEnd = CER_RUNAWAY;
    ck(afterFarewell == pet.pickEggSpecies(),
       "the ending no longer blesses or curses safety-egg rarity");
  }

  {
    Pet pet; Party roster; fresh(pet, roster, 4);
    player.wildRareBonus = 7;
    roster.add(mon(25, 30));
    pet.release(); finish(pet);
    ck(player.wildRareBonus == 7, "release has no probability effect");
    ck(roster.count() == 1 && pet.speciesId == 25,
       "release removes rather than banks the creature");
  }

  {
    Pet pet; Party roster; fresh(pet, roster, 4);
    player.wildRareBonus = 1;
    roster.add(mon(25, 30));
    pet.startRunaway(); finish(pet);
    ck(player.wildRareBonus == 0, "runaway subtracts two without crossing below zero");
  }

  {
    Pet pet; Party roster; fresh(pet, roster, 4);
    roster.box[5] = mon(9, 40);
    roster.boxSave();
    pet.release(); finish(pet);
    ck(roster.count() == 1 && pet.speciesId == 9 && roster.box[5].empty(),
       "a Box member is withdrawn before considering a safety egg");
  }

  return bad ? 1 : 0;
}

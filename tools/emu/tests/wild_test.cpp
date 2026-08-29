#include "Arduino.h"
#include "Preferences.h"
#include <cstdio>
#include "wild.h"
#include "dex.h"
#include "items.h"

uint32_t g_seed = 73;
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
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

int main() {
  ck(!encounterIsNightHour(18) && encounterIsNightHour(19) &&
     encounterIsNightHour(5) && !encounterIsNightHour(6),
     "day and night split at 19:00 and 06:00 exactly");
  ck(wildEncounterAllowed(16, false) && !wildEncounterAllowed(16, true),
     "a day species is excluded from the night encounter pool");
  ck(!wildEncounterAllowed(41, false) && wildEncounterAllowed(41, true),
     "a night species is excluded from the day encounter pool");
  ck(wildEncounterAllowed(1, false) && wildEncounterAllowed(1, true),
     "an unrestrictive species remains available in both encounter pools");
  BattleField meadowClear = wildBattleField(0, 49);
  BattleField meadowSun = wildBattleField(0, 50);
  BattleField meadowRain = wildBattleField(0, 80);
  BattleField meadowStorm = wildBattleField(0, 90);
  ck(meadowClear.weather == BWEATHER_NONE && meadowClear.terrain == BTERRAIN_NONE,
     "the first half of meadow encounters has no environment");
  ck(meadowSun.baseWeather == BWEATHER_SUN && meadowSun.baseTerrain == BTERRAIN_NONE,
     "meadow rolls 50-79 start in sun");
  ck(meadowRain.baseWeather == BWEATHER_RAIN && meadowRain.baseTerrain == BTERRAIN_NONE,
     "meadow rolls 80-89 start in ordinary rain");
  ck(meadowStorm.baseWeather == BWEATHER_RAIN &&
     meadowStorm.baseTerrain == BTERRAIN_ELECTRIC,
     "meadow rolls 90-99 start a rain and Electric Terrain thunderstorm");

  BattleField cave = wildBattleField(3, 90);
  BattleField snow = wildBattleField(5, 90);
  ck(cave.baseWeather == BWEATHER_SAND && cave.baseTerrain == BTERRAIN_NONE,
     "cave gives the thunderstorm share to its secondary sand weather");
  ck(snow.baseWeather == BWEATHER_SUN && snow.baseTerrain == BTERRAIN_NONE,
     "snow gives the thunderstorm share to its secondary sun weather");
  ck(wildBattleField(1, 50).baseWeather == BWEATHER_RAIN &&
     wildBattleField(1, 80).baseWeather == BWEATHER_SUN,
     "beach uses rain as primary weather and sun as secondary");
  ck(wildBattleField(2, 50).baseWeather == BWEATHER_RAIN &&
     wildBattleField(2, 80).baseWeather == BWEATHER_SUN,
     "forest uses rain as primary weather and sun as secondary");
  ck(wildBattleField(4, 50).baseWeather == BWEATHER_SAND &&
     wildBattleField(4, 80).baseWeather == BWEATHER_SNOW,
     "mountain uses sand as primary weather and snow as secondary");
  ck(wildBattleField(6, 99).weather == BWEATHER_NONE,
     "an invalid biome safely produces a clear field");

  Combatant squad[3];
  squad[0].level = 20;
  squad[1].level = 60;
  squad[2].level = 35;
  ck(wildEncounterMaxLevel(squad, 3, false) == 70,
     "normal encounters stop ten levels above the party's highest level");
  ck(wildEncounterMaxLevel(squad, 3, true) == 80,
     "hard encounters stop twenty levels above the party's highest level");
  squad[1].level = 95;
  ck(wildEncounterMaxLevel(squad, 3, false) == 105 &&
     wildEncounterMaxLevel(squad, 3, true) == 115,
     "wild encounters can exceed the cultivation level cap");
  squad[1].level = 100;
  ck(wildEncounterMaxLevel(squad, 3, false) == 110 &&
     wildEncounterMaxLevel(squad, 3, true) == 120,
     "wild encounter levels stop at 120");
  ck(wildWeightedDropCount(false, 29) == 2 &&
     wildWeightedDropCount(false, 30) == 1,
     "normal wild rewards add exactly one item below the thirty-percent boundary");
  ck(wildWeightedDropCount(true, 29) == 3 &&
     wildWeightedDropCount(true, 30) == 2,
     "hard wild rewards add exactly one item below the thirty-percent boundary");
  AbilitySlot bulbasaurNormal = wildAbilitySlotForRoll(1, true, 5, 0);
  ck(wildAbilitySlotForRoll(1, true, 4, 0) == ABILITY_SLOT_HIDDEN &&
     bulbasaurNormal == ABILITY_SLOT_ONE,
     "hard wild hidden abilities use the exact five-percent boundary");
  ck(wildAbilitySlotForRoll(1, false, 0, 0) == ABILITY_SLOT_ONE,
     "easy wild encounters never roll a hidden ability");
  ck(wildAbilitySlotForRoll(151, true, 0, 0) != ABILITY_SLOT_HIDDEN,
     "a species without a hidden slot safely keeps a normal ability");
  ck(wildBattleMechanic(4, 0, false, false) == BMECH_Z_MOVE &&
     wildBattleMechanic(4, 1, false, false) == BMECH_DYNAMAX &&
     wildBattleMechanic(4, 2, false, true) == BMECH_MEGA,
     "normal encounters allow every available mechanic below five percent");
  ck(wildBattleMechanic(5, 0, false, true) == BMECH_NONE,
     "normal encounter mechanics stop at five percent");
  ck(wildBattleMechanic(19, 0, true, false) == BMECH_Z_MOVE &&
     wildBattleMechanic(19, 1, true, false) == BMECH_DYNAMAX &&
     wildBattleMechanic(19, 2, true, true) == BMECH_MEGA,
     "hard encounters allow every available mechanic below twenty percent");
  ck(wildBattleMechanic(20, 0, true, true) == BMECH_NONE,
     "hard encounter mechanics stop at twenty percent");
  ck(wildBattleMechanic(0, 0, false, false, false) == BMECH_DYNAMAX,
     "wild encounters omit unavailable Z-Moves and Mega Evolution");
  ck(wildBattleMechanic(0, 0, false, false, false, false) == BMECH_NONE,
     "a species with no officially supported mechanic receives none");
  ck(wildEscapeChance(50, 50) == 90 && wildEscapeChance(80, 50) == 90,
     "escape defaults to ninety percent when not under-levelled");
  ck(wildEscapeChance(50, 100) == 45,
     "a lower level scales escape chance by the level ratio");
  ck(wildEscapeChance(1, 100) == 10,
     "escape chance never falls below ten percent");
  ck(wildFoeEscapeChance(41, 100) == 0,
     "a wild foe does not flee above forty percent HP");
  ck(wildFoeEscapeChance(40, 100) == 10,
     "a wild foe has ten percent escape chance at forty percent HP");
  ck(wildFoeEscapeChance(30, 100) == 15 &&
     wildFoeEscapeChance(20, 100) == 20,
     "wild foe escape chance rises linearly from forty to twenty percent HP");
  ck(wildFoeEscapeChance(15, 100) == 15 &&
     wildFoeEscapeChance(10, 100) == 10 &&
     wildFoeEscapeChance(1, 100) == 10,
     "wild foe escape chance falls linearly to ten percent and stays there");
  ck(wildFoeEscapeChance(0, 100) == 0 && wildFoeEscapeChance(10, 0) == 0,
     "fainted foes and invalid HP cannot escape");
  ck(wildFoeEscapeChance(41, 100, true) == 5 &&
     wildFoeEscapeChance(40, 100, true) == 15 &&
     wildFoeEscapeChance(20, 100, true) == 25 &&
     wildFoeEscapeChance(10, 100, true) == 15,
     "anger adds five percentage points to wild escape chance");
  ck(wildFoeEscapeChance(0, 100, true) == 0 &&
     wildFoeEscapeChance(10, 0, true) == 0,
     "anger cannot make fainted or invalid foes escape");

  uint8_t commonFull = wildCaptureChance(R_COMUN, 100, 100, false, 100);
  uint8_t commonLow = wildCaptureChance(R_COMUN, 1, 100, false, 100);
  uint8_t statusLow = wildCaptureChance(R_COMUN, 1, 100, true, 100);
  uint8_t betterBall = wildCaptureChance(R_COMUN, 1, 100, true, 200);
  uint8_t legend = wildCaptureChance(R_LEGENDARIO, 1, 100, true, 100);
  ck(commonFull > 0, "a healthy common creature remains catchable");
  ck(commonLow > commonFull, "low HP improves capture odds");
  ck(statusLow > commonLow, "status improves capture odds");
  ck(betterBall > statusLow, "the pack-provided ball multiplier matters");
  ck(legend < statusLow, "rarity lowers derived capture odds");
  ck(wildCaptureChance(R_LEGENDARIO, 100, 100, false,
                       ITEM_CATCH_GUARANTEED) == 100,
     "a guaranteed-catch item bypasses rarity, HP, and status odds");
  ck(wildCaptureChance(R_COMUN, 0, 100, false,
                       ITEM_CATCH_GUARANTEED) == 0,
     "a guaranteed-catch item still rejects a fainted target");
  ck(wildCaptureChance(R_COMUN, 0, 0, false, 100) == 0,
     "invalid HP cannot be captured");
  return bad ? 1 : 0;
}

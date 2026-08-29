#include "Arduino.h"
#include "Preferences.h"
#include "breeding.h"
#include "content.h"
#include "party.h"
#include "pet.h"
#include "wild.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 0xBEEFu;
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
static void check(bool ok, const char *message) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", message);
  if (!ok) bad++;
}

static PartyMon parent(SpeciesId dex, PetGender gender, uint8_t ivBase) {
  PartyMon mon;
  mon.dex = dex;
  mon.level = 20;
  mon.ageMinutes = 19UL * MINUTES_PER_LEVEL;
  mon.gender = gender;
  mon.nature = NATURE_HARDY;
  mon.stateVersion = 6;
  mon.ivAtk = ivBase;
  mon.ivDef = ivBase + 2;
  mon.ivSpe = ivBase + 4;
  mon.ivHp = ivBase + 6;
  mon.setAbilitySlot(ABILITY_SLOT_ONE);
  return mon;
}

static MoveId firstEggMove(SpeciesId species) {
  for (MoveId move = 1; move < moveCount(); move++)
    if (speciesHasEggMove(species, move)) return move;
  return MOVE_NONE;
}

static bool knows(const PartyMon &mon, MoveId wanted) {
  for (MoveId move : mon.moves) if (move == wanted) return true;
  for (MoveId move : mon.reserveMoves) if (move == wanted) return true;
  return false;
}

int main() {
  check(contentBegin() && contentHasBreeding(),
        "the core pack exposes breeding metadata");

  PartyMon female = parent(6, GENDER_FEMALE, 0);
  PartyMon male = parent(1, GENDER_MALE, 1);
  PartyMon sameSex = male;
  sameSex.gender = GENDER_FEMALE;
  PartyMon otherGroup = parent(65, GENDER_MALE, 1);
  PartyMon ditto = parent(132, GENDER_NONE, 1);
  PartyMon undiscovered = parent(144, GENDER_NONE, 1);
  check(breedingCompatible(female, male),
        "opposite-sex parents with a shared Egg Group are compatible");
  check(!breedingCompatible(female, sameSex) &&
        !breedingCompatible(female, otherGroup),
        "same-sex and disjoint Egg Group pairs are rejected");
  check(breedingCompatible(female, ditto) &&
        !breedingCompatible(ditto, ditto) && !breedingEligible(undiscovered),
        "Ditto is the sole gender exception and the Undiscovered group is rejected");
  check(breedingOffspringSpecies(female, male, 0) == 4,
        "the female/non-Ditto family determines the base offspring species");

  check(breedingRareThreshold(0, 0) == wildRareThreshold(0) &&
        breedingRareThreshold(3, 1) == wildRareThreshold(3) +
            5UL * WILD_RARE_BONUS_THRESHOLD &&
        breedingRareThreshold(3, 2) == wildRareThreshold(3) +
            10UL * WILD_RARE_BONUS_THRESHOLD,
        "breeding adds five shiny percentage points per shiny parent");

  check(breedingAbilityForRoll(65, ABILITY_SLOT_ONE, 79) == ABILITY_SLOT_ONE &&
        breedingAbilityForRoll(65, ABILITY_SLOT_ONE, 80) == ABILITY_SLOT_TWO &&
        breedingAbilityForRoll(65, ABILITY_SLOT_HIDDEN, 59) == ABILITY_SLOT_HIDDEN &&
        breedingAbilityForRoll(65, ABILITY_SLOT_HIDDEN, 60) != ABILITY_SLOT_HIDDEN,
        "Gen-IX normal and hidden ability inheritance thresholds are exact");

  check(breedingGigantamaxFactorForRoll(0, 4) &&
        !breedingGigantamaxFactorForRoll(0, 5) &&
        breedingGigantamaxFactorForRoll(1, 14) &&
        !breedingGigantamaxFactorForRoll(1, 15) &&
        breedingGigantamaxFactorForRoll(2, 24) &&
        !breedingGigantamaxFactorForRoll(2, 25),
        "Gigantamax Factor chance is 5 percent plus 10 per factor parent");

  MoveId eggMove = firstEggMove(4);
  check(moveValid(eggMove) && speciesHasEggMove(4, eggMove),
        "the generated catalogue identifies a canonical Egg Move");
  male.moves[0] = eggMove;
  randomSeed(77);
  PartyMon child = breedingCreateOffspring(female, male, 0);
  const uint8_t childIvs[] = {child.ivAtk, child.ivDef, child.ivSpe, child.ivHp};
  const uint8_t firstIvs[] = {female.ivAtk, female.ivDef, female.ivSpe, female.ivHp};
  const uint8_t secondIvs[] = {male.ivAtk, male.ivDef, male.ivSpe, male.ivHp};
  uint8_t inherited = 0;
  for (uint8_t stat = 0; stat < 4; stat++)
    if (childIvs[stat] == firstIvs[stat] || childIvs[stat] == secondIvs[stat])
      inherited++;
  check(child.dex == 4 && child.level == 1 && inherited == 3,
        "a level-one offspring inherits exactly three of TamaPoke's four IV stats");
  check(knows(child, eggMove), "Egg Moves can be inherited from either parent");
  female.setGigantamaxFactor(true);
  male.setGigantamaxFactor(true);
  randomSeed(211);
  PartyMon factorChild;
  for (uint8_t attempt = 0; attempt < 100 && !factorChild.gigantamaxFactor(); attempt++)
    factorChild = breedingCreateOffspring(female, male, 0);
  check(factorChild.gigantamaxFactor(),
        "the breeding path persists a rolled Gigantamax Factor on the offspring");
  female.setGigantamaxFactor(false);
  male.setGigantamaxFactor(false);
  female.shiny = male.shiny = 1;
  randomSeed(103);
  PartyMon shinyChild;
  for (uint8_t attempt = 0; attempt < 100 && !shinyChild.shiny; attempt++)
    shinyChild = breedingCreateOffspring(female, male, WILD_RARE_BONUS_MAX);
  check(shinyChild.shiny && shinyChild.ivAtk >= 20 && shinyChild.ivDef >= 20 &&
        shinyChild.ivSpe >= 20 && shinyChild.ivHp >= 20,
        "a shiny bred offspring floors all four IVs at 20");
  female.shiny = male.shiny = 0;

  Pet active;
  active.begin();
  active.dbgHatchAs(9, false);
  Party roster;
  roster.begin();
  roster.attach(active);
  roster.replaceAt(1, female);
  roster.replaceAt(2, male);
  check(roster.breedingSwapParty(0, 1, active) &&
        roster.breedingSwapParty(1, 2, active),
        "parents move from cultivation slots into dedicated frozen slots");
  randomSeed(91);
  check(roster.breedingStart(1000) &&
        roster.breeding.readyEpoch >= 1000 + BREEDING_MIN_SECONDS &&
        roster.breeding.readyEpoch <= 1000 + BREEDING_MAX_SECONDS,
        "Start schedules a durable random wait from one to two hours");
  PartyMon replacement = parent(9, GENDER_MALE, 2);
  roster.box[0] = replacement;
  check(!roster.breedingSwapBox(0, 0, active) &&
        roster.breedingRemoveParent(0, active) == PARTY_STORE_FULL,
        "running jobs lock both parents against removal and replacement");

  uint32_t readyAt = roster.breeding.readyEpoch;
  Pet rebooted;
  rebooted.begin();
  Party restored;
  restored.begin();
  restored.attach(rebooted);
  check(restored.breeding.status == BREEDING_RUNNING &&
        restored.breeding.readyEpoch == readyAt,
        "an in-progress breeding job survives restart");
  restored.syncClock(rebooted, readyAt - 1);
  check(restored.breeding.status == BREEDING_RUNNING,
        "the offspring is not created before the persisted deadline");
  restored.syncClock(rebooted, readyAt);
  check(restored.breeding.status == BREEDING_READY &&
        restored.breeding.offspring.dex == 4,
        "RTC catch-up creates the offspring exactly when the deadline passes");
  PartyStoreResult taken = restored.breedingTakeOffspring(rebooted);
  check(taken == PARTY_STORE_PARTY && restored.breeding.status == BREEDING_IDLE &&
        restored.breeding.offspring.empty() &&
        !restored.breeding.parents[0].empty() &&
        !restored.breeding.parents[1].empty(),
        "Take stores the child, keeps both parents, and returns the center to idle");
  for (uint8_t slot = 0; slot < PARTY_SLOTS; slot++)
    if (restored.slots[slot].empty())
      restored.slots[slot] = parent(1, GENDER_MALE, 2);
  for (uint8_t slot = 0; slot < BOX_SLOTS; slot++)
    restored.box[slot] = parent(1, GENDER_MALE, 3);
  uint8_t oldParentIv = restored.breeding.parents[1].ivAtk;
  uint8_t selectedIv = restored.box[0].ivAtk;
  check(restored.breedingRemoveParent(1, rebooted) == PARTY_STORE_FULL &&
        restored.breedingSwapBox(1, 0, rebooted) &&
        restored.breeding.parents[1].ivAtk == selectedIv &&
        restored.box[0].ivAtk == oldParentIv,
        "a full roster exchanges the chosen party or Box member into the parent slot");
  check(restored.breedingStart(readyAt + 1),
        "the same parents can start another breeding cycle after collection");

  return bad ? 1 : 0;
}

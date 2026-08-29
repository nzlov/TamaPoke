#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "party.h"
#include "pet.h"
#include <cstdio>

uint32_t g_seed = 0xBEE5u;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void breedingTap(int16_t x, int16_t y);
void onSwipe(int dir);
extern Pet pet;
extern bool breedingOpen, breedingPickSwapRequired;
extern uint8_t breedingView, breedingParent, breedingPickPage;
extern uint8_t breedingDetailPage, breedingDetailTarget;

static int bad = 0;
static void check(bool ok, const char *message) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", message);
  if (!ok) bad++;
}

static PartyMon mon(SpeciesId dex, PetGender gender, uint8_t iv) {
  PartyMon value;
  value.dex = dex;
  value.gender = gender;
  value.nature = NATURE_HARDY;
  value.stateVersion = 6;
  value.level = 20;
  value.ageMinutes = 19UL * MINUTES_PER_LEVEL;
  value.ivAtk = value.ivDef = value.ivSpe = value.ivHp = iv;
  value.setAbilitySlot(ABILITY_SLOT_ONE);
  return value;
}

int main() {
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(1, false);
  party.captureActive(pet, false);
  party.breeding = BreedingCenterState();
  party.breeding.parents[0] = mon(6, GENDER_FEMALE, 12);
  party.breeding.parents[1] = mon(1, GENDER_MALE, 13);
  breedingOpen = true;
  breedingView = 0;

  breedingTap(100, 110);
  check(breedingView == 2 && breedingParent == 1,
        "the left parent card maps to the male and opens its two-button menu");
  breedingTap(233, 184);
  check(breedingView == 3 && breedingDetailTarget == 1 && breedingDetailPage == 0,
        "the Details button opens the selected parent's introduction");
  onSwipe(-1);
  check(breedingView == 3 && breedingDetailPage == 1,
        "the parent detail pages horizontally without closing");
  breedingTap(233, 400);
  check(breedingView == 2, "leaving parent details returns to its click menu");
  breedingTap(233, 246);
  check(breedingView == 0 && party.breeding.parents[1].empty(),
        "Take out removes the parent when normal storage has space");
  breedingTap(100, 110);
  check(breedingView == 1 && breedingParent == 1 && !breedingPickSwapRequired,
        "clicking the now-empty left slot opens the parent picker");

  breedingView = 0;
  PartyMon child = mon(4, GENDER_FEMALE, 20);
  child.level = 1;
  child.ageMinutes = 0;
  party.breeding.offspring = child;
  party.breeding.status = BREEDING_READY;
  breedingTap(233, 280);
  check(breedingView == 3 && breedingDetailTarget == 2,
        "the centered ready offspring opens its own detail introduction");

  breedingTap(233, 400);
  party.breeding.offspring = PartyMon();
  party.breeding.status = BREEDING_IDLE;
  party.breeding.parents[1] = mon(1, GENDER_MALE, 13);
  for (uint8_t slot = 0; slot < PARTY_SLOTS; slot++)
    party.slots[slot] = mon(1, GENDER_MALE, (uint8_t)(20 + slot));
  for (uint8_t slot = 0; slot < BOX_SLOTS; slot++)
    party.box[slot] = mon(1, GENDER_MALE, (uint8_t)(10 + slot));
  breedingView = 2;
  breedingParent = 1;
  breedingTap(233, 246);
  check(breedingView == 1 && breedingPickPage == 0 && breedingPickSwapRequired,
        "a full team and Box continue Take out into replacement selection");
  uint8_t outgoingIv = party.breeding.parents[1].ivAtk;
  uint8_t selectedIv = party.slots[0].ivAtk;
  breedingTap(100, 110);
  check(breedingView == 0 && !breedingPickSwapRequired &&
        party.breeding.parents[1].ivAtk == selectedIv &&
        party.slots[0].ivAtk == outgoingIv,
        "the chosen full-roster member swaps into the vacated parent slot");

  party.breeding.status = BREEDING_RUNNING;
  party.breeding.readyEpoch = 999999;
  breedingView = 2;
  breedingParent = 1;
  PartyMon locked = party.breeding.parents[1];
  breedingTap(233, 246);
  check(breedingView == 2 &&
        party.breeding.parents[1].ivAtk == locked.ivAtk,
        "the Take out button stays locked while breeding is running");

  return bad ? 1 : 0;
}

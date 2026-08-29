#include "Arduino.h"
#include "Preferences.h"
#include "party.h"
#include "pet.h"
#include <cstdio>

uint32_t g_seed = 109;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void onSwipe(int dir);
void onSwipeV(int dir);
bool petNavTap(int16_t x, int16_t y);
void petNavPoints(int *slot0X, int *slotGap, int *y);
uint8_t petNavCount();
bool navMenuTap(int16_t x, int16_t y);
void navMenuButtonPoint(uint8_t index, int *x, int *y);
void onTap(int16_t x, int16_t y);
void boxTap(int16_t x, int16_t y);
extern bool bagOpen, boxOpen, breedingOpen, gymOpen, gymPick, navMenuOpen, playerOpen;
extern uint8_t breedingView, breedingPickPage;
extern uint8_t playerPage, boxPage, boxSel, boxDetailPage;
extern Pet pet;

int main() {
  pet.speciesId = 1;
  pet.chooseStarter(1);
  party.slots[0] = pet.toPartyMon();
  PartyMon second;
  second.dex = 4;
  second.ageMinutes = 9 * MINUTES_PER_LEVEL;
  second.level = 10;
  party.slots[1] = second;

  bagOpen = boxOpen = breedingOpen = gymOpen = gymPick = navMenuOpen = playerOpen = false;
  onSwipe(-1);
  if (party.activeIndex() != 1 || bagOpen || gymOpen) {
    std::puts("FAIL left swipe did not select the next cultivation slot");
    return 1;
  }
  onSwipe(1);
  if (party.activeIndex() != 0 || bagOpen || gymOpen) {
    std::puts("FAIL right swipe did not select the previous cultivation slot");
    return 1;
  }

  int slot0X, slotGap, navY;
  petNavPoints(&slot0X, &slotGap, &navY);
  if (petNavCount() != 2 || slot0X != 221) {
    std::puts("FAIL slot dots do not follow and centre the occupied count");
    return 1;
  }
  if (petNavTap(94, navY) || petNavTap(372, navY) || bagOpen || gymOpen) {
    std::puts("FAIL removed main-screen icons still accept taps");
    return 1;
  }
  if (!petNavTap(slot0X + slotGap, navY) || !boxOpen || party.activeIndex() != 0) {
    std::puts("FAIL cultivation dots do not open Box without switching");
    return 1;
  }
  boxOpen = false;

  int buttonX, buttonY;
  onSwipeV(1);
  if (!navMenuOpen) {
    std::puts("FAIL swipe down did not open the navigation menu");
    return 1;
  }
  navMenuButtonPoint(0, &buttonX, &buttonY);
  if (!navMenuTap(buttonX, buttonY) || !bagOpen || navMenuOpen) {
    std::puts("FAIL navigation menu bag button");
    return 1;
  }
  bagOpen = false;
  navMenuOpen = true;
  navMenuButtonPoint(1, &buttonX, &buttonY);
  if (!navMenuTap(buttonX, buttonY) || !gymOpen || !gymPick || bagOpen) {
    std::puts("FAIL navigation menu battle-centre button");
    return 1;
  }
  gymPick = false;
  onTap(327, 61);
  if (boxOpen || !gymOpen) {
    std::puts("FAIL removed battle-centre team area still opens storage");
    return 1;
  }
  gymOpen = gymPick = false;

  PartyMon boxed;
  boxed.dex = 7;
  boxed.level = 12;
  party.box[0] = boxed;
  boxOpen = true;
  boxPage = boxSel = 0;
  boxTap(100, 110);  // occupied Box slot 0
  boxTap(100, 110);  // outside the action menu: must not exchange immediately
  if (party.slots[0].dex != 1 || party.box[0].dex != 7 || boxSel) {
    std::puts("FAIL occupied Box slot did not open an action menu first");
    return 1;
  }
  boxTap(100, 110);  // occupied Box slot 0
  boxTap(233, 172);  // VIEW
  onSwipe(-1);
  if (boxDetailPage != 1 || party.box[0].dex != 7) {
    std::puts("FAIL Box VIEW did not page through the stored creature details");
    return 1;
  }
  boxTap(233, 400);  // detail back to actions
  boxTap(233, 230);  // WITHDRAW into the first free cultivation slot
  if (party.count() != 3 || party.slots[2].dex != 7 || !party.box[0].empty() || boxSel) {
    std::puts("FAIL Box WITHDRAW did not use the first free cultivation slot");
    return 1;
  }
  party.box[0] = boxed;
  for (uint8_t slot = 3; slot < PARTY_SLOTS; slot++) {
    PartyMon filler;
    filler.dex = 20 + slot;
    filler.level = 20;
    party.replaceAt(slot, filler);
  }
  boxTap(100, 110);  // occupied Box slot 0
  boxTap(233, 230);  // WITHDRAW from a full cultivation roster
  boxTap(100, 110);  // exchange with the first cultivation member
  if (party.slots[0].dex != 7 || party.box[0].dex != 1 || boxSel) {
    std::puts("FAIL full-roster WITHDRAW did not continue into exchange selection");
    return 1;
  }
  boxTap(100, 110);  // occupied Box slot 0
  boxTap(233, 288);  // RELEASE
  boxTap(233, 300);  // NO
  if (party.box[0].empty()) {
    std::puts("FAIL Box RELEASE ignored cancellation");
    return 1;
  }
  boxTap(233, 288);  // RELEASE again
  boxTap(233, 240);  // YES
  if (!party.box[0].empty() || boxSel) {
    std::puts("FAIL confirmed Box RELEASE kept the stored creature");
    return 1;
  }
  for (uint8_t slot = 2; slot < PARTY_SLOTS; slot++) party.releaseAt(slot);
  boxTap(260, 110);  // empty Box slot 1
  boxTap(260, 110);  // second cultivation member
  if (party.count() != 1 || party.box[1].dex != 4 || boxSel) {
    std::puts("FAIL empty Box slot did not accept selected member");
    return 1;
  }
  boxTap(100, 188);  // empty Box slot 2
  boxTap(100, 110);  // sole remaining member: must be denied
  if (party.count() != 1 || !party.box[2].empty()) {
    std::puts("FAIL Box accepted the sole remaining cultivation member");
    return 1;
  }
  boxTap(200, 398);  // back to the Box grid after the denied deposit
  boxTap(260, 110);  // occupied Box slot 1: opens actions
  boxTap(233, 230);  // WITHDRAW
  if (party.count() != 2 || party.slots[1].dex != 4 || !party.box[1].empty() || boxSel) {
    std::puts("FAIL Box member was not withdrawn after using its action menu");
    return 1;
  }
  boxOpen = false;
  navMenuOpen = true;
  playerPage = 2;
  navMenuButtonPoint(2, &buttonX, &buttonY);
  if (!navMenuTap(buttonX, buttonY) || !playerOpen || playerPage != 0) {
    std::puts("FAIL navigation menu badges button");
    return 1;
  }
  playerOpen = false;
  navMenuOpen = true;
  breedingView = 2;
  breedingPickPage = 3;
  navMenuButtonPoint(3, &buttonX, &buttonY);
  if (!navMenuTap(buttonX, buttonY) || !breedingOpen || navMenuOpen ||
      breedingView != 0 || breedingPickPage != 0) {
    std::puts("FAIL navigation menu breeding-centre button");
    return 1;
  }
  std::puts("PASS dynamic slot switcher, Box management, and four-button navigation menu");
  return 0;
}

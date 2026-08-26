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
extern bool bagOpen, boxOpen, gymOpen, gymPick, navMenuOpen, playerOpen;
extern uint8_t playerPage, boxPage, boxSel;
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

  bagOpen = boxOpen = gymOpen = gymPick = navMenuOpen = playerOpen = false;
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
  if (boxSel != 1) {
    std::puts("FAIL occupied Box slot did not open cultivation selection");
    return 1;
  }
  boxTap(100, 110);  // first cultivation member
  if (party.slots[0].dex != 7 || party.box[0].dex != 1 || boxSel) {
    std::printf("FAIL occupied Box slot did not exchange with selected member "
                "(team=%d box=%d selected=%u)\n",
                party.slots[0].dex, party.box[0].dex, boxSel);
    return 1;
  }
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
  boxOpen = false;
  navMenuOpen = true;
  playerPage = 2;
  navMenuButtonPoint(2, &buttonX, &buttonY);
  if (!navMenuTap(buttonX, buttonY) || !playerOpen || playerPage != 0) {
    std::puts("FAIL navigation menu badges button");
    return 1;
  }
  std::puts("PASS dynamic slot switcher, Box management, and navigation menu");
  return 0;
}

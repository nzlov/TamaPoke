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
bool navMenuTap(int16_t x, int16_t y);
void navMenuButtonPoint(uint8_t index, int *x, int *y);
bool battleCenterTeamTap(int16_t x, int16_t y);
extern bool bagOpen, partyOpen, gymOpen, gymPick, navMenuOpen, playerOpen;
extern uint8_t playerPage;
extern Pet pet;

int main() {
  pet.dbgHatchAs(1, false);
  party.slots[0] = pet.toPartyMon();
  PartyMon second;
  second.dex = 4;
  second.ageMinutes = 9 * MINUTES_PER_LEVEL;
  second.level = 10;
  party.slots[1] = second;

  bagOpen = partyOpen = gymOpen = gymPick = navMenuOpen = playerOpen = false;
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
  if (petNavTap(94, navY) || petNavTap(372, navY) || bagOpen || gymOpen) {
    std::puts("FAIL removed main-screen icons still accept taps");
    return 1;
  }
  if (!petNavTap(slot0X + slotGap, navY) || party.activeIndex() != 1) {
    std::puts("FAIL cultivation slot tap navigation");
    return 1;
  }

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
  if (!battleCenterTeamTap(327, 61) || !partyOpen || !gymOpen) {
    std::puts("FAIL battle-centre party overlay");
    return 1;
  }
  partyOpen = gymOpen = gymPick = false;
  navMenuOpen = true;
  playerPage = 2;
  navMenuButtonPoint(2, &buttonX, &buttonY);
  if (!navMenuTap(buttonX, buttonY) || !playerOpen || playerPage != 0) {
    std::puts("FAIL navigation menu badges button");
    return 1;
  }
  std::puts("PASS top six-slot switcher and swipe-down navigation menu");
  return 0;
}

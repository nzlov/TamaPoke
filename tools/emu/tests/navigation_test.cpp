#include "Arduino.h"
#include "Preferences.h"
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
bool battleCenterTeamTap(int16_t x, int16_t y);
extern bool bagOpen, partyOpen, gymOpen, gymPick;

int main() {
  bagOpen = partyOpen = gymOpen = gymPick = false;
  onSwipe(1);
  if (!bagOpen || partyOpen || gymOpen) {
    std::puts("FAIL right swipe navigation");
    return 1;
  }
  bagOpen = false;
  onSwipe(-1);
  if (!gymOpen || !gymPick || bagOpen) {
    std::puts("FAIL left swipe navigation");
    return 1;
  }
  if (!battleCenterTeamTap(327, 61) || !partyOpen || !gymOpen) {
    std::puts("FAIL battle-centre party overlay");
    return 1;
  }
  std::puts("PASS right=bag, left=battle centre, team icon preserves context");
  return 0;
}

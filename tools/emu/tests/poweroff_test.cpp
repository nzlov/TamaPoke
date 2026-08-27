// The menu power-off is confirmed, saves the live roster at the current RTC,
// then asks the PMU to cut power. Cancellation must do neither.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include <cstdio>

uint32_t g_seed = 223;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void onTap(int16_t x, int16_t y);
extern Pet pet;
extern bool menuOpen;
extern uint8_t choiceKind;
extern bool gPowerOffRequested;

int main() {
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  while (pet.hasLearnOffer()) pet.declineLearn();

  menuOpen = true;
  onTap(233, 343);  // fifth menu row
  if (choiceKind != 4 || gPowerOffRequested) {
    std::puts("FAIL  POWER OFF row did not open a confirmation");
    return 1;
  }
  onTap(233, 304);  // cancel
  if (choiceKind || gPowerOffRequested) {
    std::puts("FAIL  cancelling POWER OFF requested PMU shutdown");
    return 1;
  }

  pet.fullness = 73;
  menuOpen = true;
  onTap(233, 343);
  onTap(233, 242);  // confirm
  if (!gPowerOffRequested || nvs().count("team2") != 1) {
    std::puts("FAIL  confirmed POWER OFF did not save before PMU shutdown");
    return 1;
  }
  std::puts("PASS  confirmed POWER OFF saves then requests PMU shutdown");
  return 0;
}

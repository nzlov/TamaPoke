// The menu power-off is confirmed, saves the live roster at the current RTC,
// then asks the PMU to cut power. Cancellation must do neither.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
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
void btlFinish(bool won);
extern Pet pet;
extern Party party;
extern bool menuOpen;
extern uint8_t choiceKind;
extern bool gPowerOffRequested;
extern uint8_t btlRegion;
extern int8_t btlTrainer;
extern bool btlHard, btlPetIn;

int main() {
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);

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
  btlRegion = 0;
  btlTrainer = 0;
  btlHard = false;
  btlPetIn = true;
  btlFinish(true);
  if (!player.hasBadge(0, 0, false) || !pet.gymIvClaimed(0, 0)) {
    std::puts("FAIL  gym victory did not award progress before power off");
    return 1;
  }
  onTap(233, 390);  // dismiss the victory settlement before opening the menu
  menuOpen = true;
  onTap(233, 343);
  onTap(233, 242);  // confirm
  if (!gPowerOffRequested || nvs().count("team2") != 1) {
    std::puts("FAIL  confirmed POWER OFF did not save before PMU shutdown");
    return 1;
  }
  PlayerProgress rebootedPlayer;
  Pet rebooted(rebootedPlayer);
  Party rebootedParty;
  rebooted.begin();
  rebootedParty.begin();
  rebootedParty.attach(rebooted);
  if (!rebootedPlayer.hasBadge(0, 0, false) || !rebooted.gymIvClaimed(0, 0) ||
      rebooted.fullness != 73) {
    std::puts("FAIL  confirmed POWER OFF lost gym progress after reboot");
    return 1;
  }
  std::puts("PASS  confirmed POWER OFF preserves gym progress across reboot");
  return 0;
}

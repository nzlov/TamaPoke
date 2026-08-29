// A gym victory changes two domains: the player's badge ladder and the active
// creature's one-time IV reward. Roster snapshot v4 is their shared commit.
#include "Arduino.h"
#include "Preferences.h"
#include "party.h"
#include "pet.h"
#include "player.h"
#include <cstdio>

uint32_t g_seed = 41;
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

static void settle(PlayerProgress &progress, Pet &pet) {
  if (!progress.hasBadge(0, 0, false)) progress.winBadge(0, 0, false);
  uint8_t which = 0;
  pet.rewardGymIv(0, 0, which);
  pet.saveNow();
}

int main() {
  PlayerProgress firstPlayer;
  Pet first(firstPlayer);
  Party firstParty;
  first.begin();
  if (first.awaitingStarter()) first.chooseStarter(4);
  if (first.isEgg()) first.dbgHatchAs(6, false);
  firstParty.begin();
  firstParty.attach(first);
  first.saveNow();

  nvsFailWritesFor("team2", 1);
  settle(firstPlayer, first);
  ck(firstPlayer.hasBadge(0, 0, false) && first.gymIvClaimed(0, 0),
     "the completed victory is visible before an interrupted commit");

  PlayerProgress rolledBackPlayer;
  Pet rolledBack(rolledBackPlayer);
  Party rolledBackParty;
  rolledBack.begin();
  rolledBackParty.begin();
  rolledBackParty.attach(rolledBack);
  bool badgeRolledBack = !rolledBackPlayer.hasBadge(0, 0, false);
  bool ivRolledBack = !rolledBack.gymIvClaimed(0, 0);
  ck(badgeRolledBack, "an interrupted commit does not expose the new badge");
  ck(ivRolledBack, "an interrupted commit does not expose the new IV reward");

  settle(rolledBackPlayer, rolledBack);
  uint16_t ivSum = rolledBack.ivAtk + rolledBack.ivDef +
                   rolledBack.ivSpe + rolledBack.ivHp;
  settle(rolledBackPlayer, rolledBack);
  ck(rolledBack.ivAtk + rolledBack.ivDef + rolledBack.ivSpe +
             rolledBack.ivHp == ivSum,
     "replaying settlement cannot grant the IV twice");

  PlayerProgress committedPlayer;
  Pet committed(committedPlayer);
  Party committedParty;
  committed.begin();
  committedParty.begin();
  committedParty.attach(committed);
  ck(committedPlayer.hasBadge(0, 0, false) && committed.gymIvClaimed(0, 0),
     "a successful commit restores the badge and IV reward together");

  return bad ? 1 : 0;
}

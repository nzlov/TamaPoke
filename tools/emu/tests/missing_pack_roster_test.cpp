#include "Arduino.h"
#include "Preferences.h"
#include "content.h"
#include "party.h"
#include "save.h"
#include <cstdio>

uint32_t g_seed = 1;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
extern bool recoveryMode;
extern Party party;

int main() {
  Preferences seed;
  seed.begin("tamapoke", false);
  seed.putUShort("savev", SAVE_STATE_VERSION);
  seed.putBool("init", true);
  seed.putShort("dexn", 1);

  PartyMon team[PARTY_SLOTS];
  PartyMon boxPage[BOX_PAGE_SLOTS];
  team[0].dex = 1;
  team[0].stateVersion = 4;
  boxPage[0].dex = 1025;  // Paldea save; fixture catalogue ends at Galar (905)
  boxPage[0].level = 40;
  boxPage[0].stateVersion = 4;
  boxPage[0].nature = NATURE_HARDY;
  boxPage[0].gender = GENDER_MALE;
  seed.putBytes("team1", team, sizeof(team));
  seed.putBytes("box10", boxPage, sizeof(boxPage));
  seed.putUShort("rostv", 2);
  seed.putUChar("active", 0);
  seed.end();

  setup();

  PartyMon stored[BOX_PAGE_SLOTS];
  Preferences verify;
  verify.begin("tamapoke", true);
  bool rawKept = verify.getBytes("box10", stored, sizeof(stored)) == sizeof(stored) &&
                 stored[0].dex == 1025;
  verify.end();
  bool ok = contentReady() && recoveryMode && party.box[0].dex == 1025 && rawKept;
  printf("%s  missing regional content enters recovery without changing the roster\n",
         ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

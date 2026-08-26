#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include <chrono>
#include <cstdio>
#include <thread>

uint32_t g_seed = 17;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void loadUserBrightness();
void clockTap(int16_t x, int16_t y);
void handleTouch();
void onSwipeV(int dir);
void updateBrightness(uint32_t now);
void setUserBrightness(uint8_t level, bool persist);
extern uint8_t userBrightness;
extern uint32_t lastInteract;
extern bool screenOff;
extern bool clockOpen;
extern bool cardOpen;
extern bool navMenuOpen;
extern volatile bool gTouchIrq;
extern Pet pet;
extern Arduino_CO5300 *panel;

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static void pollTouch() {
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  handleTouch();
}

int main() {
  {
    Preferences prefs;
    prefs.begin("tamapoke", false);
    prefs.putUChar("bright", 3);
    prefs.end();
  }
  loadUserBrightness();
  ck(userBrightness == 3, "saved brightness is loaded at startup");

  clockTap(132, 283);
  Preferences prefs;
  prefs.begin("tamapoke", true);
  ck(userBrightness == 1 && prefs.getUChar("bright", 0) == 1,
     "the left end selects and persists minimum brightness");
  prefs.end();

  clockOpen = true;
  g_touchX = 156;
  g_touchY = 283;
  g_touchDown = true;
  gTouchIrq = true;
  pollTouch();
  g_touchX = 336;
  pollTouch();
  g_touchDown = false;
  pollTouch();
  clockOpen = false;
  prefs.begin("tamapoke", true);
  ck(userBrightness == 10 && prefs.getUChar("bright", 0) == 10,
     "dragging to the right end selects and persists maximum brightness");
  prefs.end();

  pet.sleeping = false;
  screenOff = false;
  lastInteract = 1000;
  updateBrightness(1000);
  ck(panel->brightness == 250, "maximum normal brightness reaches the panel");

  pet.speciesId = 1;
  if (pet.awaitingStarter()) pet.chooseStarter(1);
  pet.sleeping = true;
  updateBrightness(1000);
  ck(panel->brightness == 25, "sleep dims only the main screen");

  onSwipeV(-1);
  updateBrightness(1000);
  ck(cardOpen && panel->brightness == 250,
     "swiping up restores brightness on the pet card");
  lastInteract = 0;
  updateBrightness(90001);
  ck(panel->brightness == 60,
     "sleep does not change idle protection away from the main screen");
  lastInteract = 1000;
  onSwipeV(-1);
  updateBrightness(1000);
  ck(!cardOpen && panel->brightness == 25,
     "returning from the pet card dims the sleeping main screen again");

  onSwipeV(1);
  updateBrightness(1000);
  ck(navMenuOpen && panel->brightness == 250,
     "swiping down restores brightness on the navigation menu");
  onSwipeV(1);
  updateBrightness(1000);
  ck(!navMenuOpen && panel->brightness == 25,
     "returning from the navigation menu dims the sleeping main screen again");

  pet.sleeping = false;

  lastInteract = 0;
  updateBrightness(90001);
  ck(panel->brightness == 60, "idle protection still dims a bright setting");

  setUserBrightness(1, false);
  lastInteract = 1000;
  updateBrightness(1000);
  ck(panel->brightness == 39, "idle policy never brightens a low manual setting");

  screenOff = true;
  updateBrightness(1000);
  ck(panel->brightness == 0, "screen-off still overrides manual brightness");
  return bad ? 1 : 0;
}

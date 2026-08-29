// Fast taps must survive the firmware's touch poll gate in all three games.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <chrono>
#include <cstdio>
#include <thread>

uint32_t g_seed = 19;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void loop();
void startGame();
void startSack();
void startSpeedGame();
extern Pet pet;
extern bool gameOpen, sackOpen, spdOpen;
extern uint8_t gameScore;
extern uint16_t sackHits, spdHits;
extern float ballX, ballY;
extern int spdX, spdY;

static void waitMs(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

static void resetPollWindow() {
  g_touchDown = false;
  emuFireInterrupt();
  waitMs(25);
  loop();
}

static void rapidTap(int x, int y) {
  g_touchX = x;
  g_touchY = y;
  g_touchDown = true;
  emuFireInterrupt();
  waitMs(6);
  loop();

  g_touchDown = false;
  emuFireInterrupt();
  waitMs(6);
  loop();

  // Let the normal 20 ms window elapse too: a missed tap must not be rescued
  // by leaving a pending interrupt behind after the finger is already up.
  waitMs(15);
  loop();
}

static bool check(bool condition, const char *message) {
  std::printf("%s  %s\n", condition ? "PASS" : "FAIL", message);
  return condition;
}

int main() {
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);

  int bad = 0;

  startGame();
  resetPollWindow();
  int ballTapX = (int)ballX, ballTapY = (int)ballY;
  rapidTap(ballTapX, ballTapY);
  if (!check(gameScore == 1, "a 12 ms ball tap is sampled")) bad++;
  gameOpen = false;

  startSack();
  resetPollWindow();
  rapidTap(233, 220);
  if (!check(sackHits == 1, "a 12 ms punching-bag tap is sampled")) bad++;
  sackOpen = false;

  startSpeedGame();
  resetPollWindow();
  rapidTap(spdX, spdY);
  if (!check(spdHits == 1, "a 12 ms reaction-target tap is sampled")) bad++;
  spdOpen = false;

  std::printf("%s\n", bad ? "FAILURES" : "all rapid taps sampled");
  return bad ? 1 : 0;
}

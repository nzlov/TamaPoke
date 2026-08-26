#include "Arduino.h"
#include "Arduino_GFX_Library.h"

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

void battleTap(int16_t x, int16_t y);
extern bool battleOpen, btlTapDebounceArmed;
extern uint8_t btlMenu, btlMsgCount;

static int failures = 0;

static void check(bool condition, const char *message) {
  std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
  if (!condition) failures++;
}

int main() {
  battleOpen = true;
  btlTapDebounceArmed = false;
  btlMenu = 0;
  btlMsgCount = 0;

  battleTap(0, 0);                       // blank root area is not an operation
  btlMenu = 2;
  battleTap(0, 0);                       // off-grid switch tap backs out
  check(btlMenu == 0, "a blank tap does not arm battle debounce");

  btlMenu = 2;
  battleTap(0, 0);
  check(btlMenu == 2, "an immediate battle operation is ignored");
  std::this_thread::sleep_for(std::chrono::milliseconds(310));
  battleTap(0, 0);
  check(btlMenu == 0, "battle operations resume after 300 ms");

  std::this_thread::sleep_for(std::chrono::milliseconds(310));
  btlMsgCount = 1;
  battleTap(0, 0);                       // dismiss narration
  battleTap(69 + 10, 286 + 10);         // same burst must not open FIGHT
  check(!btlMsgCount && btlMenu == 0,
        "a rapid tap cannot cross from narration into the next action");

  return failures ? 1 : 0;
}

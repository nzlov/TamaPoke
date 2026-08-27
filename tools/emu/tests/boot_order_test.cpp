#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "content.h"
#include "sdmon.h"
#include <cstdio>
#include <filesystem>

uint32_t g_seed = 1;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void renderBootSplash();
extern Arduino_Canvas *gfx;

int main() {
  namespace fs = std::filesystem;
  fs::create_directories(CONTENT_DIR);

  gfx->begin(80000000);
  gfx->frameReady = false;
  renderBootSplash();
  size_t lit = 0;
  for (size_t i = 0; i < 466UL * 466UL; i++)
    if (gfx->buffer()[i] != RGB565_BLACK) lit++;
  bool splashReady = gfx->frameReady && lit >= 10000;

  for (const auto &entry : fs::directory_iterator(BOOT_PACK_SOURCE)) {
    if (!entry.is_regular_file()) continue;
    fs::copy_file(entry.path(), fs::path(CONTENT_DIR) / entry.path().filename(),
                  fs::copy_options::overwrite_existing);
  }

  bool contentReadyAfterMount = sdBegin() && contentReady();
  printf("%s  boot splash draws and flushes before storage is mounted\n",
         splashReady ? "PASS" : "FAIL");
  printf("%s  boot splash leaves content loading until storage is mounted\n",
         contentReadyAfterMount ? "PASS" : "FAIL");
  bool ok = splashReady && contentReadyAfterMount;
  return ok ? 0 : 1;
}

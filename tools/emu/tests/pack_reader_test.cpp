#include "Arduino.h"
#include "Preferences.h"
#include "content.h"
#include <cstdio>

uint32_t g_seed = 1;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

extern "C" size_t __real_fread(void *, size_t, size_t, FILE *);
extern "C" size_t __wrap_fread(void *buffer, size_t size, size_t count, FILE *stream) {
  if (count > 7) count = 7;
  return __real_fread(buffer, size, count, stream);
}

static int seekCount = 0;
extern "C" int __real_fseek(FILE *, long, int);
extern "C" int __wrap_fseek(FILE *stream, long offset, int origin) {
  seekCount++;
  return __real_fseek(stream, offset, origin);
}

int main() {
  bool ok = contentValidatePackFile(PACK_READER_FIXTURE);
  bool sequential = seekCount == 5;
  printf("%s  pack validation tolerates short filesystem reads\n", ok ? "PASS" : "FAIL");
  printf("%s  payload CRC uses one sequential filesystem scan\n",
         sequential ? "PASS" : "FAIL");
  return ok && sequential ? 0 : 1;
}

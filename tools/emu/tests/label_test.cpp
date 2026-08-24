#include "Arduino.h"
#include "Preferences.h"
// linked against the same core as every other suite, so it needs the same
// hardware stubs even though it only exercises the string table
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
#include "i18n.h"
#include "font_cjk.h"
#include <cstdio>
#include <cstring>

static int textWidth2(const char *s, bool cjk) {
  if (cjk) return emuCjkTextWidth(s, 1) + 1;  // UI size 2 maps to bold Unifont x1
  int width = 0;
  while (*s) {
    unsigned char c = (unsigned char)*s++;
    if (c < 0x80) { width += 12; continue; }
    while (((unsigned char)*s & 0xC0) == 0x80) s++;
    width += 12;
  }
  return width;
}

int main(){
  const StrId ids[] = { S_VIN, S_STAT_ATK, S_STAT_DEF, S_STAT_SPE, S_STAT_VIT, S_STAT_WGT };
  const char *ln[] = {"ES","EN","FR","DE","IT","PT","ZH"};
  int worst = 0, bad = 0;
  for (int l=0;l<LANG_COUNT;l++){ setLang((Lang)l);
    for (auto id : ids){ int w = 70 + textWidth2(T(id), l == LANG_ZH);
      if (w > 132) { printf("COLLIDES %s \"%s\" ends at x=%d (bar starts 132)\n", ln[l], T(id), w); bad++; }
      if (w > worst) worst = w; } }
  printf("widest label ends at x=%d\n", worst);
  return bad ? 1 : 0;
}

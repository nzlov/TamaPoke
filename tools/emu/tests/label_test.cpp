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
#include <cstdio>
#include <cstring>

static uint32_t nextUtf8(const char *&at) {
  uint8_t first = (uint8_t)*at++;
  if (first < 0x80) return first;
  uint32_t value;
  uint8_t remaining;
  if ((first & 0xE0) == 0xC0) { value = first & 0x1F; remaining = 1; }
  else if ((first & 0xF0) == 0xE0) { value = first & 0x0F; remaining = 2; }
  else return '?';
  while (remaining--) value = (value << 6) | ((uint8_t)*at++ & 0x3F);
  return value;
}

static int textWidth2(const char *s) {
  int width = 0;
  while (*s) {
    const UiFontGlyph *glyph = uiFontGlyph(nextUtf8(s));
    width += glyph ? glyph->advance * 2 : 12;
  }
  return width;
}

int main(){
  const StrId ids[] = { S_VIN, S_STAT_ATK, S_STAT_DEF, S_STAT_SPE, S_STAT_VIT, S_STAT_WGT };
  int worst = 0, bad = 0;
  for (int l=0;l<langCount();l++){ setLang((Lang)l);
    int labelX = uiLayoutMetric(UI_LAYOUT_STAT_LABEL_X, 70);
    int barX = uiLayoutMetric(UI_LAYOUT_STAT_BAR_X, 132);
    int gap = uiLayoutMetric(UI_LAYOUT_MIN_TOUCH_GAP, 8);
    for (auto id : ids){ int w = labelX + textWidth2(T(id));
      if (w + gap > barX) { printf("COLLIDES %s \"%s\" ends at x=%d (bar starts %d)\n", langCode((Lang)l), T(id), w, barX); bad++; }
      if (w > worst) worst = w; } }
  printf("widest label ends at x=%d\n", worst);
  return bad ? 1 : 0;
}

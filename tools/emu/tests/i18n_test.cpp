// Checks the positional STRINGS table: a language row with too few entries is
// zero-padded by the compiler with no diagnostic, which silently shifts every
// string after the gap. Latin translations stay ASCII; Chinese must be valid
// UTF-8 for the hardware CJK renderer.
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
#include <string>

// STRINGS is file-static, so go through the same accessor the firmware uses.
static const char *LANG[] = { "ES", "EN", "FR", "DE", "IT", "PT", "ZH" };

static bool validUtf8(const unsigned char *s) {
  while (*s) {
    if (*s < 0x80) { s++; continue; }
    int continuation = (*s >= 0xC2 && *s <= 0xDF) ? 1
                     : (*s >= 0xE0 && *s <= 0xEF) ? 2 : -1;
    if (continuation < 0) return false;
    s++;
    while (continuation--)
      if ((*s & 0xC0) != 0x80) return false;
      else s++;
  }
  return true;
}

static std::string formatSpecifiers(const char *s) {
  std::string out;
  while (*s) {
    if (*s++ != '%') continue;
    if (*s == '%') { s++; continue; }
    while (*s && strchr("-+ #0.0123456789", *s)) s++;
    while (*s && strchr("hljztL", *s)) out += *s++;
    if (*s) out += *s++;
    out += ',';
  }
  return out;
}

int main() {
  int bad = 0;
  for (int l = 0; l < LANG_COUNT; l++)
    for (int s = 0; s < STR_COUNT; s++) {
      setLang((Lang)l);
      const char *v = T((StrId)s);
      if (!v) { printf("NULL  %s index %d\n", LANG[l], s); bad++; continue; }
      const unsigned char *p = (const unsigned char *)v;
      if (l == LANG_ZH) {
        if (!validUtf8(p)) { printf("BAD UTF-8  ZH index %d\n", s); bad++; }
        const char *scan = v;
        while (*scan) {
          uint32_t codepoint = emuNextUtf8(scan);
          if (!emuCjkGlyph(codepoint)) {
            printf("MISSING GLYPH  ZH index %d: U+%04X\n", s, (unsigned)codepoint);
            bad++;
          }
        }
      } else {
        for (; *p; p++)
          if (*p > 0x7F) { printf("NON-ASCII  %s index %d: \"%s\"\n", LANG[l], s, v); bad++; break; }
      }
      setLang(LANG_EN);
      if (formatSpecifiers(v) != formatSpecifiers(T((StrId)s))) {
        printf("FORMAT MISMATCH  %s index %d: \"%s\"\n", LANG[l], s, v);
        bad++;
      }
    }
  printf("%s: %d languages x %d strings\n", bad ? "FAIL" : "PASS", LANG_COUNT, STR_COUNT);
  return bad ? 1 : 0;
}

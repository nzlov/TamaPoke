// Checks the positional STRINGS table: a language row with too few entries is
// zero-padded by the compiler with no diagnostic, which silently shifts every
// string after the gap. Every pack must be valid UTF-8 and provide every glyph
// its strings require.
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
#include "font_engine.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
  const int8_t english = uiFindLocale("en-US");
  if (english < 0) { printf("FAIL: en-US pack missing\n"); return 1; }
  std::vector<std::string> englishFormats(STR_COUNT);
  setLang((Lang)english);
  for (int s = 0; s < STR_COUNT; s++) englishFormats[s] = formatSpecifiers(T((StrId)s));

  for (int l = 0; l < langCount(); l++) {
    setLang((Lang)l);
    bool vectorFont = uiFontFormat() == UI_FONT_OPENTYPE;
    uint8_t *fontData = nullptr;
    uint32_t fontSize = 0;
    if (vectorFont && (!uiFontLoadData(&fontData, &fontSize) ||
                       !runtimeFontBegin(fontData, fontSize, uiFontFaceIndex()))) {
      printf("FONT LOAD FAILED  %s\n", langCode((Lang)l));
      bad++;
      vectorFont = false;
    } else if (!vectorFont) {
      runtimeFontEnd();
    }
    for (int s = 0; s < STR_COUNT; s++) {
      const char *v = T((StrId)s);
      const char *code = langCode((Lang)l);
      if (!v) { printf("NULL  %s index %d\n", code, s); bad++; continue; }
      const unsigned char *p = (const unsigned char *)v;
      if (!validUtf8(p)) { printf("BAD UTF-8  %s index %d\n", code, s); bad++; }
      const char *scan = v;
      while (*scan) {
        uint32_t codepoint = nextUtf8(scan);
        bool glyphPresent = vectorFont
            ? runtimeFontGlyph(codepoint, uiFontPixelSize(1)) != nullptr
            : uiFontGlyph(codepoint) != nullptr;
        if (!glyphPresent) {
          printf("MISSING GLYPH  %s index %d: U+%04X\n", code, s, (unsigned)codepoint);
          bad++;
        }
      }
      if (formatSpecifiers(v) != englishFormats[s]) {
        printf("FORMAT MISMATCH  %s index %d: \"%s\"\n", code, s, v);
        bad++;
      }
    }
  }
  setLang((Lang)english);
  if (strcmp(T(S_RETIRE), "RELEASE")) {
    printf("WRONG EXIT LABEL  en-US: %s\n", T(S_RETIRE));
    bad++;
  }
  if (strcmp(T(S_BOX_WITHDRAW), "WITHDRAW")) {
    printf("WRONG BOX WITHDRAW LABEL  en-US: %s\n", T(S_BOX_WITHDRAW));
    bad++;
  }
  int8_t chinese = uiFindLocale("zh-CN");
  if (chinese >= 0) {
    setLang((Lang)chinese);
    if (strcmp(T(S_RETIRE), "放生")) {
      printf("WRONG EXIT LABEL  zh-CN: %s\n", T(S_RETIRE));
      bad++;
    }
    if (strcmp(T(S_BOX_WITHDRAW), "取出")) {
      printf("WRONG BOX WITHDRAW LABEL  zh-CN: %s\n", T(S_BOX_WITHDRAW));
      bad++;
    }
  }
  runtimeFontEnd();
  printf("%s: %u languages x %d strings\n", bad ? "FAIL" : "PASS", langCount(), STR_COUNT);
  return bad ? 1 : 0;
}

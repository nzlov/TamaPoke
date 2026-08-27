// First boot must choose and persist a language before region and starter.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "content.h"
#include "i18n.h"
#include "pet.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 31;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void render();
void onTap(int16_t x, int16_t y);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static int languageX(uint8_t index) { return 74 + (index % 2) * 168 + 75; }
static int languageY(uint8_t index) { return 104 + (index / 2) * 70 + 27; }

int main() {
  setup();
  render();
  ck(pet.awaitingStarter(), "a new save enters first boot");
  ck(!strcmp(SCREEN_NAME[uiCurrentScreen()], "language"),
     "language selection is the first screen");
  ck(gLang == 0, "the first installed language pack is shown by default");
  ck(!strcmp(langDisplayName(gLang), uiLocaleInfo(gLang).displayName),
     "a language button uses its pack display name");

  int8_t chosen = uiFindLocale("zh-CN");
  ck(chosen >= 0, "the fixture offers Chinese");
  if (chosen >= 0)
    ck(!strcmp(langDisplayName((Lang)chosen), "Chinese"),
       "English text backs up a self-name missing from the active bitmap font");
  if (chosen >= 0) onTap(languageX((uint8_t)chosen), languageY((uint8_t)chosen));
  ck(gLang == (Lang)chosen, "tapping a language activates it");
  ck(!strcmp(SCREEN_NAME[uiCurrentScreen()], "region"),
     "language selection advances to region selection");

  Preferences prefs;
  prefs.begin("tamapoke", true);
  char saved[16] = {};
  prefs.getString("locale", saved, sizeof(saved));
  prefs.end();
  ck(!strcmp(saved, "zh-CN"), "the first-boot language is persisted");

  onTap(233, 108 + 30);
  ck(!strcmp(SCREEN_NAME[uiCurrentScreen()], "starter"),
     "region selection advances to starter selection");
  onTap(233, 110 + 35);
  ck(!pet.awaitingStarter(), "starter selection finishes first boot");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}

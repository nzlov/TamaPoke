#include "Arduino.h"
#include "Preferences.h"
#include "badges.h"
#include "content.h"
#include "font_engine.h"
#include "i18n.h"
#include "trainers.h"
#include <chrono>
#include <cstdio>
#include <cstring>

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

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
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

static bool fontCovers(const char *text) {
  while (text && *text) {
    uint32_t codepoint = nextUtf8(text);
    bool found = uiFontFormat() == UI_FONT_OPENTYPE
                   ? runtimeFontGlyph(codepoint, uiFontPixelSize(2)) != nullptr
                   : uiFontGlyph(codepoint) != nullptr;
    if (!found) {
      printf("      missing U+%04X\n", (unsigned)codepoint);
      return false;
    }
  }
  return true;
}

static bool fontHasGray(const RuntimeFontGlyph *glyph) {
  if (!glyph) return false;
  for (uint32_t pixel = 0; pixel < (uint32_t)glyph->width * glyph->height; pixel++)
    if (glyph->alpha[pixel] != 0 && glyph->alpha[pixel] != 255) return true;
  return false;
}

int main() {
  ck(contentBegin() && contentReady(), "the installed pack set is bootable");
  ck(contentPackCount() >= 3, "UI, move and region packs were discovered");
  ck(contentMechanicsHash() != 0, "loaded mechanics have a stable link hash");
  ck(!moveValid(MOVE_NONE), "the empty move slot is not a usable move");

  int8_t english = uiFindLocale("en-US"), chinese = uiFindLocale("zh-CN");
  ck(english >= 0 && chinese >= 0, "languages come from installed UI packs");
  ck(uiFindLocale("xx-XX") < 0, "an uninstalled language is not invented by firmware");

  bool englishDescriptions = english >= 0 && uiActivateLocale((uint8_t)english);
  MoveId thunderbolt = MOVE_NONE;
  MoveId growl = MOVE_NONE, dragonDance = MOVE_NONE, recover = MOVE_NONE;
  for (MoveId move = 1; move < moveCount(); move++)
    if (!strcmp(moveEntry(move).name, "THUNDERBOLT")) thunderbolt = move;
    else if (!strcmp(moveEntry(move).name, "GROWL")) growl = move;
    else if (!strcmp(moveEntry(move).name, "DRAGON DANCE")) dragonDance = move;
    else if (!strcmp(moveEntry(move).name, "RECOVER")) recover = move;
  ck(englishDescriptions && !strcmp(speciesName(25), "PIKACHU") &&
     thunderbolt && !strcmp(moveName(thunderbolt), "THUNDERBOLT") &&
     !strcmp(packedTypeName(T_NORMAL), "NORMAL") &&
     !strcmp(regionName(0), "KANTO") && !strcmp(trainerName(0, 0), "BROCK") &&
     !strcmp(trainerPlace(0, 0), "PEWTER"),
     "English names use the canonical pack fallback");
  for (SpeciesId species = 1; englishDescriptions && species <= dexCount(); species++)
    englishDescriptions = speciesDescription(species, "en-US") != nullptr;
  for (MoveId move = 1; englishDescriptions && move < moveCount(); move++)
    englishDescriptions = moveDescription(move, "en-US") != nullptr;
  ck(englishDescriptions, "species and move descriptions load from their own packs");
  ck(growl && dragonDance && recover &&
     strstr(moveDescription(growl, "en-US"), "Lowers foe's Attack by 1 stage.") &&
     strstr(moveDescription(dragonDance, "en-US"), "Attack and Speed by 1 stage.") &&
     strstr(moveDescription(recover, "en-US"), "Restores 50%"),
     "English move descriptions explain targets, stats and healing without internal codes");
  ck(speciesDescription(25, "es-ES") != nullptr &&
     speciesDescription(25, "fr-FR") != nullptr &&
     speciesDescription(25, "pt-PT") != nullptr,
     "species descriptions follow every installed UI language");
  ck(moveDescription(1, "es-ES") == nullptr,
     "content without a translated move description stays hidden");

  bool chineseGlyphs = chinese >= 0 && uiActivateLocale((uint8_t)chinese);
  ck(chineseGlyphs && !strcmp(speciesName(25), "皮卡丘") &&
     !strcmp(speciesName(778), "谜拟丘") &&
     thunderbolt && !strcmp(moveName(thunderbolt), "十万伏特") &&
     !strcmp(packedTypeName(T_NORMAL), "一般") &&
     !strcmp(regionName(0), "关都") && !strcmp(trainerName(0, 0), "小刚") &&
     !strcmp(trainerPlace(0, 0), "深灰市"),
     "Chinese species, move, type and regional names resolve from their content packs");
  ck(growl && strstr(moveDescription(growl, "zh-CN"), "使对手的攻击-1级。") &&
     !strstr(moveDescription(growl, "zh-CN"), "效果") &&
     !strstr(moveDescription(growl, "zh-CN"), "参数"),
     "Chinese move descriptions present player-facing effects instead of ABI fields");
  uint8_t *fontData = nullptr;
  uint32_t fontSize = 0;
  bool vectorReady = uiFontFormat() == UI_FONT_OPENTYPE &&
                     uiFontLoadData(&fontData, &fontSize) &&
                     runtimeFontBegin(fontData, fontSize, uiFontFaceIndex());
  const RuntimeFontGlyph *small = vectorReady ? runtimeFontGlyph(0x4E2D, uiFontPixelSize(1)) : nullptr;
  uint16_t smallHeight = small ? small->height : 0;
  const RuntimeFontGlyph *title = vectorReady ? runtimeFontGlyph(0x4E2D, uiFontPixelSize(3)) : nullptr;
  ck(vectorReady && title && title->height > smallHeight && fontHasGray(title),
     "the Chinese UI pack provides hinted, antialiased OpenType size tiers");
  RuntimeFontStats before = runtimeFontStats();
  runtimeFontGlyph(0x4E2D, uiFontPixelSize(3));
  RuntimeFontStats after = runtimeFontStats();
  ck(after.cacheHits > before.cacheHits && after.cacheBytes > 0,
     "rasterized glyphs are reused from the runtime cache");
  auto firstStart = std::chrono::steady_clock::now();
  runtimeFontGlyph(0x5999, uiFontPixelSize(6));
  auto firstEnd = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000; i++) runtimeFontGlyph(0x5999, uiFontPixelSize(6));
  auto hitsEnd = std::chrono::steady_clock::now();
  printf("      first glyph %lldus; 1000 cache hits %lldus\n",
         (long long)std::chrono::duration_cast<std::chrono::microseconds>(firstEnd - firstStart).count(),
         (long long)std::chrono::duration_cast<std::chrono::microseconds>(hitsEnd - firstEnd).count());
  for (SpeciesId species = 1; chineseGlyphs && species <= dexCount(); species++)
    chineseGlyphs = fontCovers(speciesName(species)) &&
                    fontCovers(speciesDescription(species, "zh-CN"));
  for (MoveId move = 1; chineseGlyphs && move < moveCount(); move++)
    chineseGlyphs = fontCovers(moveName(move)) &&
                    fontCovers(moveDescription(move, "zh-CN"));
  for (uint16_t item = 0; chineseGlyphs && item < itemCount(); item++) {
    const ItemEntry *entry = itemAt(item);
    chineseGlyphs = entry && fontCovers(itemName(entry->key)) &&
                    fontCovers(itemDescription(entry->key, "zh-CN"));
  }
  for (uint8_t type = 0; chineseGlyphs && type < TYPE_COUNT; type++)
    chineseGlyphs = fontCovers(packedTypeName(type));
  for (uint8_t region = 0; chineseGlyphs && region < regionAll(); region++) {
    chineseGlyphs = fontCovers(regionName(region));
    for (uint8_t trainer = 0;
         chineseGlyphs && trainer < regionBattleInfo(region).trainerCount; trainer++)
      chineseGlyphs = fontCovers(trainerName(region, trainer)) &&
                      fontCovers(trainerPlace(region, trainer));
  }
  ck(chineseGlyphs, "the UI font covers names and descriptions supplied by content packs");
  int stressGlyphs = 0;
  for (uint32_t codepoint = 0x4E00; codepoint <= 0x9FFF && stressGlyphs < 300; codepoint++)
    if (runtimeFontGlyph(codepoint, uiFontPixelSize(6))) stressGlyphs++;
  RuntimeFontStats stressed = runtimeFontStats();
  ck(stressGlyphs == 300 && stressed.cacheBytes <= 192u * 1024u && stressed.cachedGlyphs <= 96,
     "the PSRAM glyph cache stays inside its fixed budget under pressure");

  bool layoutsFit = true;
  for (uint8_t locale = 0; layoutsFit && locale < uiLocaleCount(); locale++) {
    layoutsFit = uiActivateLocale(locale);
    int detailSize = uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_TEXT_SIZE, 1);
    int moveSize = uiLayoutMetric(UI_LAYOUT_MOVE_DESCRIPTION_TEXT_SIZE, 1);
    auto lineHeight = [](int logicalSize) {
      if (uiFontFormat() == UI_FONT_OPENTYPE) return (int)uiFontPixelSize(logicalSize) + 2;
      int scale = (logicalSize * uiFontDesignHeight() + uiFontLineHeight() / 2) /
                  uiFontLineHeight();
      return (int)uiFontLineHeight() * (scale ? scale : 1);
    };
    int detailBottom = uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_Y, 282) +
                       uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_LINES, 5) * lineHeight(detailSize);
    int moveBottom = uiLayoutMetric(UI_LAYOUT_MOVE_DESCRIPTION_Y, 158) +
                     uiLayoutMetric(UI_LAYOUT_MOVE_DESCRIPTION_LINES, 8) * lineHeight(moveSize);
    layoutsFit = detailBottom < uiLayoutMetric(UI_LAYOUT_DETAIL_BACK_Y, 408) &&
                 moveBottom < uiLayoutMetric(UI_LAYOUT_MOVE_CHANGE_Y, 320) &&
                 uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_X, 78) >= 0 &&
                 uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_X, 78) +
                     uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_WIDTH, 310) <= 466 &&
                 uiLayoutMetric(UI_LAYOUT_MOVE_DESCRIPTION_X, 78) >= 0 &&
                 uiLayoutMetric(UI_LAYOUT_MOVE_DESCRIPTION_X, 78) +
                     uiLayoutMetric(UI_LAYOUT_MOVE_DESCRIPTION_WIDTH, 310) <= 466;
    if (!strcmp(uiActiveLocaleCode(), "zh-CN"))
      layoutsFit = layoutsFit && detailSize == 2 && moveSize == 2;
  }
  ck(layoutsFit, "UI packs control readable description sizes without colliding with controls");

  bool regionalBattles = regionAll() > 0;
  for (uint8_t region = 0; regionalBattles && region < regionAll(); region++) {
    const RegionBattleInfo &battle = regionBattleInfo(region);
    regionalBattles = regionBattleAvailable(region) && battle.trainerCount > battle.gymCount;
    for (uint8_t trainer = 0; regionalBattles && trainer < battle.trainerCount; trainer++)
      regionalBattles = trainerInfo(region, trainer).count > 0;
    for (uint8_t badge = 0; regionalBattles && badge < battle.gymCount; badge++) {
      const BadgeArt &art = badgeArt(region, badge);
      regionalBattles = art.pal && art.idx && art.width && art.height;
    }
  }
  ck(regionalBattles, "regional trainers, battles and badges come from region packs");

  runtimeFontEnd();

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}

#pragma once
#include <stdint.h>
#include <stddef.h>

#include "dex.h"
#include "moves.h"

constexpr uint16_t CONTENT_PACK_ABI = 2;
constexpr uint8_t CONTENT_MAX_UI_LOCALES = 16;

enum UiLayoutMetric : uint16_t {
  UI_LAYOUT_COMPACT_TEXT_HEIGHT = 1,
  UI_LAYOUT_MIN_TOUCH_TARGET = 2,
  UI_LAYOUT_MIN_TOUCH_GAP = 3,
  UI_LAYOUT_STAT_LABEL_X = 4,
  UI_LAYOUT_STAT_BAR_X = 5,
  UI_LAYOUT_STAT_BAR_WIDTH = 6,
  UI_LAYOUT_STAT_VALUE_X = 7,
  UI_LAYOUT_DETAIL_SPRITE_GROUND = 8,
  UI_LAYOUT_DETAIL_DESCRIPTION_Y = 9,
  UI_LAYOUT_DETAIL_DESCRIPTION_LINES = 10,
  UI_LAYOUT_DETAIL_BACK_Y = 11,
  UI_LAYOUT_MOVE_DESCRIPTION_Y = 12,
  UI_LAYOUT_MOVE_DESCRIPTION_LINES = 13,
  UI_LAYOUT_MOVE_CHANGE_Y = 14,
  UI_LAYOUT_DETAIL_DESCRIPTION_TEXT_SIZE = 15,
  UI_LAYOUT_MOVE_DESCRIPTION_TEXT_SIZE = 16,
  UI_LAYOUT_DETAIL_DESCRIPTION_X = 17,
  UI_LAYOUT_DETAIL_DESCRIPTION_WIDTH = 18,
  UI_LAYOUT_MOVE_DESCRIPTION_X = 19,
  UI_LAYOUT_MOVE_DESCRIPTION_WIDTH = 20,
};

enum ContentPackKind : uint8_t {
  CONTENT_PACK_UI = 1,
  CONTENT_PACK_REGION = 2,
  CONTENT_PACK_MOVE = 3,
};

enum ContentPackValidation : uint8_t {
  CONTENT_PACK_VALID = 0,
  CONTENT_PACK_OPEN_FAILED,
  CONTENT_PACK_READ_FAILED,
  CONTENT_PACK_HEADER_INVALID,
  CONTENT_PACK_ABI_MISMATCH,
  CONTENT_PACK_SIZE_MISMATCH,
  CONTENT_PACK_CHECKSUM_MISMATCH,
  CONTENT_PACK_DIRECTORY_INVALID,
};

struct ContentPackInfo {
  char id[21];
  uint32_t revision;
};

struct ContentPackSource {
  void *context;
  uint32_t size;
  bool (*seek)(void *context, uint32_t offset);
  size_t (*read)(void *context, uint8_t *out, size_t length);
};

struct UiLocaleInfo {
  char locale[16];
  char shortLabel[8];
  char displayName[32];
  bool isDefault;
  bool isCjk;
};

struct UiFontGlyph {
  uint32_t codepoint;
  uint8_t width, height, advance;
  int8_t xOffset, yOffset;
  // Two row-major pixels per byte, low nibble first; 0 is transparent, 15 opaque.
  const uint8_t *alpha4;
};

enum UiFontFormat : uint8_t {
  UI_FONT_BITMAP = 1,
  UI_FONT_OPENTYPE = 2,
};

// Mount and validate every compatible pack. On desktop builds the first
// catalogue access lazily calls this against CONTENT_DIR.
bool contentBegin();
ContentPackValidation contentValidatePackFile(const char *path);
ContentPackValidation contentValidatePackSource(ContentPackSource &source);
bool contentReadPackInfo(const char *path, ContentPackInfo &out);
bool contentReadPackInfo(ContentPackSource &source, ContentPackInfo &out);
bool contentReady();
bool contentHasUi();
bool contentHasPets();
bool contentHasMoves();
uint32_t contentMechanicsHash();

uint8_t uiLocaleCount();
const UiLocaleInfo &uiLocaleInfo(uint8_t index);
int8_t uiFindLocale(const char *locale);
bool uiActivateLocale(uint8_t index);
uint8_t uiActiveLocale();
const char *uiActiveLocaleCode();
const char *uiString(uint16_t id);
int16_t uiLayoutMetric(uint16_t id, int16_t fallback);
uint8_t uiFontLineHeight();
uint8_t uiFontDesignHeight();
const UiFontGlyph *uiFontGlyph(uint32_t codepoint);
UiFontFormat uiFontFormat();
uint8_t uiFontPixelSize(uint8_t logicalSize);
uint8_t uiFontFaceIndex();
uint32_t uiFontDataSize();
// The caller owns the returned PSRAM/malloc buffer and frees it with free().
bool uiFontLoadData(uint8_t **out, uint32_t *size);

const char *speciesDescription(SpeciesId species, const char *locale);
const char *moveDescription(MoveId move, const char *locale);
const char *speciesName(SpeciesId species);
const char *moveName(MoveId move);

uint8_t typeEffectTenth(uint8_t attack, uint8_t defense);
const char *packedTypeName(uint8_t type);
uint16_t packedTypeColor(uint8_t type);
bool packedTypeColorIsLight(uint8_t type);

// The caller owns the returned PSRAM/malloc buffer and frees it with free().
bool contentLoadSprite(SpeciesId species, bool shiny, uint8_t **out, uint32_t *size);
bool contentLoadThumbs(uint8_t **out, uint32_t *size);

// Serial/Web diagnostics. The text is one line per installed pack and ends in
// an empty string when index is out of range.
uint8_t contentPackCount();
const char *contentPackSummary(uint8_t index);

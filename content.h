#pragma once
#include <stdint.h>
#include <stddef.h>

#include "abilities.h"
#include "dex.h"
#include "items.h"
#include "moves.h"

constexpr uint16_t CONTENT_PACK_ABI = 8;
constexpr uint8_t CONTENT_MAX_UI_LOCALES = 16;
constexpr uint8_t CONTENT_MAX_QUIZ_OPTIONS = 4;
constexpr uint16_t CONTENT_MAX_QUESTION_ID_BYTES = 40;
constexpr uint16_t CONTENT_MAX_QUESTION_STEM_BYTES = 768;
constexpr uint16_t CONTENT_MAX_QUESTION_OPTION_BYTES = 192;

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
  CONTENT_PACK_QUIZ = 4,
  CONTENT_PACK_ITEM = 5,
  CONTENT_PACK_BATTLE = 6,
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
  uint32_t contentVersion;
};

struct MegaFormEntry {
  SpeciesId species = SPECIES_NONE;
  MegaFormKind form = MEGA_FORM_STANDARD;
  AbilityKey ability = ABILITY_NONE;
  uint8_t type1 = T_NORMAL, type2 = T_NONE;
  uint8_t bAtk = 1, bDef = 1, bSpA = 1, bSpD = 1, bSpe = 1;
  SpeciesId learnsetSpecies = SPECIES_NONE;
  uint8_t spriteScale = 0;
  uint8_t spritePack = 0xFF;
  uint32_t spriteAt = 0, spriteSize = 0;
  uint32_t shinySpriteAt = 0, shinySpriteSize = 0;
};

using GmaxMoveId = uint8_t;
constexpr GmaxMoveId GMAX_MOVE_NONE = 0;

enum GmaxEffect : uint8_t {
  GMAX_EFFECT_NONE = 0,
  GMAX_EFFECT_VINE_LASH,
  GMAX_EFFECT_WILDFIRE,
  GMAX_EFFECT_CANNONADE,
  GMAX_EFFECT_BEFUDDLE,
  GMAX_EFFECT_VOLT_CRASH,
  GMAX_EFFECT_GOLD_RUSH,
  GMAX_EFFECT_CHI_STRIKE,
  GMAX_EFFECT_TERROR,
  GMAX_EFFECT_FOAM_BURST,
  GMAX_EFFECT_RESONANCE,
  GMAX_EFFECT_CUDDLE,
  GMAX_EFFECT_REPLENISH,
  GMAX_EFFECT_MALODOR,
  GMAX_EFFECT_MELTDOWN,
  GMAX_EFFECT_DRUM_SOLO,
  GMAX_EFFECT_FIREBALL,
  GMAX_EFFECT_HYDROSNIPE,
  GMAX_EFFECT_WIND_RAGE,
  GMAX_EFFECT_GRAVITAS,
  GMAX_EFFECT_STONESURGE,
  GMAX_EFFECT_VOLCALITH,
  GMAX_EFFECT_TARTNESS,
  GMAX_EFFECT_SWEETNESS,
  GMAX_EFFECT_SANDBLAST,
  GMAX_EFFECT_STUN_SHOCK,
  GMAX_EFFECT_CENTIFERNO,
  GMAX_EFFECT_SMITE,
  GMAX_EFFECT_SNOOZE,
  GMAX_EFFECT_FINALE,
  GMAX_EFFECT_STEELSURGE,
  GMAX_EFFECT_DEPLETION,
  GMAX_EFFECT_ONE_BLOW,
  GMAX_EFFECT_RAPID_FLOW,
  GMAX_EFFECT_COUNT,
};

struct GmaxMoveEntry {
  GmaxMoveId id = GMAX_MOVE_NONE;
  uint8_t sourceType = T_NORMAL;
  GmaxEffect effect = GMAX_EFFECT_NONE;
  uint8_t power = 0;
  const char *name = nullptr;
};

struct ItemIconView {
  uint8_t width = 0, height = 0, paletteCount = 0;
  // RGB565 palette entries are little-endian; 0xFF pixels are transparent.
  const uint8_t *palette565 = nullptr;
  const uint8_t *pixels = nullptr;
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

struct ContentChoiceQuestion {
  uint32_t idHash = 0;
  char id[CONTENT_MAX_QUESTION_ID_BYTES + 1] = {};
  char stem[CONTENT_MAX_QUESTION_STEM_BYTES + 1] = {};
  char options[CONTENT_MAX_QUIZ_OPTIONS][CONTENT_MAX_QUESTION_OPTION_BYTES + 1] = {};
  uint8_t optionCount = 0;
  uint8_t correctIndex = 0;
};

enum UiFontFormat : uint8_t {
  UI_FONT_BITMAP = 1,
  UI_FONT_OPENTYPE = 2,
};

// Mount and validate every compatible pack. On desktop builds the first
// catalogue access lazily calls this against CONTENT_DIR.
bool contentBegin();
ContentPackValidation contentValidatePackFile(const char *path);
bool contentReadPackInfo(const char *path, ContentPackInfo &out);
bool contentReady();
bool contentHasUi();
bool contentHasPets();
bool contentHasMoves();
bool contentHasBreeding();
uint32_t contentMechanicsHash();

// Question packs keep locale spans and fixed-width record indexes on the SD.
// Counting is metadata-only; reading one random question touches one index row
// and one variable-size record instead of scanning or loading the whole bank.
uint32_t contentChoiceQuestionCount(const char *locale);
bool contentChoiceQuestionAt(const char *locale, uint32_t index,
                             ContentChoiceQuestion &out);

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
const char *itemDescription(ItemKey key, const char *locale);
const char *speciesName(SpeciesId species);
const char *moveName(MoveId move);
const char *itemName(ItemKey key);

// Breeding metadata belongs to each species' region pack. Egg moves reference
// the stable MoveId catalogue without duplicating move definitions.
uint16_t speciesEggGroups(SpeciesId species);
uint8_t speciesOffspringCount(SpeciesId species);
SpeciesId speciesOffspring(SpeciesId species, uint8_t index);
bool speciesHasEggMove(SpeciesId species, MoveId move);

uint16_t itemCount();
const ItemEntry *itemAt(uint16_t index);
const ItemEntry *itemByKey(ItemKey key);
bool contentItemIcon(ItemKey key, ItemIconView &out);
const MegaFormEntry *megaFormFor(SpeciesId species,
                                 MegaFormKind form = MEGA_FORM_NONE);
bool contentGigantamaxEligible(SpeciesId species);
const GmaxMoveEntry *gmaxMoveFor(SpeciesId species, uint8_t sourceType);
const char *gmaxMoveName(GmaxMoveId move);
const char *maxMoveName(uint8_t sourceType, bool status = false);

uint8_t typeEffectTenth(uint8_t attack, uint8_t defense);
const char *packedTypeName(uint8_t type);
uint16_t packedTypeColor(uint8_t type);
bool packedTypeColorIsLight(uint8_t type);

// The caller owns the returned PSRAM/malloc buffer and frees it with free().
bool contentLoadSprite(SpeciesId species, bool shiny, uint8_t gender, bool mega,
                       MegaFormKind megaForm, bool gigantamax, uint8_t **out,
                       uint32_t *size, uint8_t *displayScale);
bool contentLoadThumbs(uint8_t **out, uint32_t *size);

// Serial/Web diagnostics. The text is one line per installed pack and ends in
// an empty string when index is out of range.
uint8_t contentPackCount();
const char *contentPackSummary(uint8_t index);

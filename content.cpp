#include "content.h"
#include "badges.h"
#include "gender.h"
#include "trainers.h"

#include <Arduino.h>
#include <algorithm>
#include <stdlib.h>
#include <string.h>

#if defined(ESP32)
#include <FS.h>
#include <SD_MMC.h>
#else
#include <cstdio>
#include <filesystem>
#include <string>
#ifndef CONTENT_DIR
#define CONTENT_DIR "web/packs"
#endif
#endif

namespace {

constexpr uint8_t MAX_PACKS = 32;
constexpr uint8_t MAX_SECTIONS = 24;
constexpr uint16_t COMMON_SIZE = 48;
constexpr uint16_t SECTION_SIZE = 16;
constexpr uint8_t MAX_PET_PACKS = CONTENT_MAX_REGIONS;
constexpr uint8_t MAX_QUIZ_LOCALES = 16;
constexpr uint8_t MAX_STARTERS = 16;
constexpr uint32_t UI_FONT_HEADER_SIZE = 8;
constexpr uint32_t UI_FONT_GLYPH_SIZE = 137;
constexpr uint32_t UI_VECTOR_FONT_HEADER_SIZE = 20;

struct SectionRef {
  char tag[5];
  uint32_t offset, size, count;
};

struct PackRef {
  char path[128];
  char id[21];
  uint8_t kind;
  bool loaded;
  uint32_t revision, mechanicsHash;
  uint8_t sectionCount;
  SectionRef sections[MAX_SECTIONS];
  char summary[96];
};

struct SpriteRef {
  uint8_t pack = 0xFF;
  uint8_t displayScale = 0;
  uint16_t localIndex = 0;
  uint32_t normalAt = 0, normalSize = 0;
  uint32_t shinyAt = 0, shinySize = 0;
  uint32_t femaleAt = 0, femaleSize = 0;
  uint32_t femaleShinyAt = 0, femaleShinySize = 0;
};

struct BreedingRuntime {
  uint16_t groups = 0;
  SpeciesId offspring[2] = { SPECIES_NONE, SPECIES_NONE };
  uint32_t eggMoveOffset = 0;
  uint16_t eggMoveCount = 0;
};

struct GigantamaxRuntime {
  SpeciesId species = SPECIES_NONE;
  GmaxMoveId moves[2] = { GMAX_MOVE_NONE, GMAX_MOVE_NONE };
};

struct RegionPackRuntime {
  uint8_t packRef = 0xFF;
  uint8_t *locales = nullptr;
  uint32_t localesSize = 0;
  uint8_t *localizedNames = nullptr;
  uint32_t localizedNamesSize = 0;
  uint16_t firstSpecies = 0;
  uint16_t speciesCount = 0;
  uint32_t *learnOffsets = nullptr;
  uint32_t learnOffsetCount = 0;
  LearnEntry *learnEntries = nullptr;
  uint32_t learnEntryCount = 0;
  MoveId *eggMoves = nullptr;
  uint32_t eggMoveCount = 0;
};

struct UiRuntime {
  UiLocaleInfo info{};
  uint8_t packRef = 0xFF;
};

struct QuizLocaleSpan {
  char locale[16];
  uint32_t first, count;
};

struct QuizPackRuntime {
  uint8_t packRef;
  uint8_t localeCount;
  uint32_t questionCount;
  QuizLocaleSpan locales[MAX_QUIZ_LOCALES];
};

struct AbilityRuntime {
  AbilityKey key = ABILITY_NONE;
  const char *name = nullptr;
};

static bool gAttempted = false;
static bool gRegionsReady = false, gMoves = false, gItemsReady = false, gBattle = false;
static PackRef *gPacks = nullptr;
static uint8_t gPackCount = 0;
static RegionPackRuntime gRegionPacks[MAX_PET_PACKS];
static uint8_t gRegionPackCount = 0;

static DexEntry *gDex = nullptr;
static SpriteRef *gSprites = nullptr;
static BreedingRuntime *gBreeding = nullptr;
static uint16_t gDexCapacity = 0;
static uint16_t gDexCount = 0;
static char *gSpeciesNames[MAX_PET_PACKS] = {};

static RegionInfo gRegions[CONTENT_MAX_REGIONS + 1] = {};
static char gRegionNames[CONTENT_MAX_REGIONS + 1][17] = {};
static SpeciesId gRegionStarters[CONTENT_MAX_REGIONS + 1][MAX_STARTERS * 2] = {};
static uint8_t gRealRegionCount = 0;
static uint16_t gRegionMask = 0;
static RegionBattleInfo gRegionBattles[CONTENT_MAX_REGIONS] = {};
static Trainer *gTrainers[CONTENT_MAX_REGIONS] = {};
static char *gTrainerStrings[CONTENT_MAX_REGIONS] = {};
static uint8_t *gRegionalNames[CONTENT_MAX_REGIONS] = {};
static uint32_t gRegionalNamesSize[CONTENT_MAX_REGIONS] = {};
static BadgeArt gBadges[CONTENT_MAX_REGIONS][CONTENT_MAX_BADGES_PER_REGION] = {};
static uint8_t *gBadgeBlobs[CONTENT_MAX_REGIONS] = {};
static uint8_t gBadgeCounts[CONTENT_MAX_REGIONS] = {};

static MoveEntry *gMovesTable = nullptr;
static uint16_t gMoveCount = 0;
static char *gMoveNames = nullptr;
static AbilityRuntime *gAbilities = nullptr;
static uint16_t gAbilityCount = 0;
static char *gAbilityNames = nullptr;
static uint8_t *gAbilityLocalizedNames = nullptr;
static uint32_t gAbilityLocalizedNamesSize = 0;
static uint8_t *gAbilityLocales = nullptr;
static uint32_t gAbilityLocalesSize = 0;
static uint8_t *gMoveLocales = nullptr;
static uint32_t gMoveLocalesSize = 0;
static uint8_t *gMoveLocalizedNames = nullptr;
static uint32_t gMoveLocalizedNamesSize = 0;
static ItemEntry *gItems = nullptr;
static uint16_t gItemCount = 0;
static char *gItemNames = nullptr;
static uint8_t *gItemLocalizedNames = nullptr;
static uint32_t gItemLocalizedNamesSize = 0;
static uint8_t *gItemLocales = nullptr;
static uint32_t gItemLocalesSize = 0;
static uint8_t *gItemIcons = nullptr;
static uint32_t gItemIconsSize = 0;
static MegaFormEntry *gMegaForms = nullptr;
static uint16_t gMegaFormCount = 0;
static uint16_t gMegaFormCapacity = 0;
static GigantamaxRuntime *gGigantamaxSpecies = nullptr;
static uint16_t gGigantamaxCount = 0;
static uint16_t gGigantamaxCapacity = 0;
static GmaxMoveEntry *gGmaxMoves = nullptr;
static uint8_t gGmaxMoveCount = 0;
static char *gGmaxMoveNames = nullptr;
static uint8_t *gGmaxMoveLocalizedNames = nullptr;
static uint32_t gGmaxMoveLocalizedNamesSize = 0;
static char *gMaxMoveNames = nullptr;
static uint32_t gMaxMoveNameOffsets[TYPE_COUNT + 1] = {};
static uint8_t *gMaxMoveLocalizedNames = nullptr;
static uint32_t gMaxMoveLocalizedNamesSize = 0;
static uint8_t *gTypeLocalizedNames = nullptr;
static uint32_t gTypeLocalizedNamesSize = 0;
static uint8_t gTypeChart[TYPE_COUNT * TYPE_COUNT] = {};
static char *gTypeNames = nullptr;
static uint32_t gTypeNameOffsets[TYPE_COUNT] = {};
static uint16_t gTypeColors[TYPE_COUNT] = {};
static uint8_t gTypeLight[TYPE_COUNT] = {};

static UiRuntime gUi[CONTENT_MAX_UI_LOCALES];
static uint8_t gUiCount = 0, gUiActive = 0xFF;
static uint8_t *gUiStrings = nullptr, *gUiFont = nullptr, *gUiLayout = nullptr;
static uint32_t gUiStringsSize = 0, gUiFontSize = 0, gUiLayoutSize = 0;
static uint32_t gMechanicsHash = 2166136261u;
static QuizPackRuntime *gQuizPacks = nullptr;
static uint8_t gQuizPackCapacity = 0;
static uint8_t gQuizPackCount = 0;

static const DexEntry MISSING_SPECIES = {
  "?", 0, 0, 0x2946, 50, 50, 50, 50, 50, 50, 0, T_NORMAL, T_NONE,
  GENDER_RATE_NONE, ENCOUNTER_BOTH, {}, {}, 0,
};
static const MoveEntry MISSING_MOVE = {
  "-", T_NORMAL, MC_STATUS, 0, 0, EF_NONE, 0, 0, 0, TG_SELF, AIL_NONE, 0,
  MF_NONE, MT_NONE,
};
static const RegionInfo MISSING_REGION = { "?", 0, 0, nullptr, 0 };
static const UiLocaleInfo MISSING_LOCALE = { "", "--", "Recovery", true, false };

static uint16_t rd16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int8_t rdI8(const uint8_t *p) { return (int8_t)*p; }

static uint32_t crcStep(uint32_t crc, const uint8_t *data, size_t size) {
  crc = ~crc;
  while (size--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
  }
  return ~crc;
}

struct Reader {
#if defined(ESP32)
  File file;
#else
  FILE *file = nullptr;
#endif
  uint32_t size = 0;

  bool open(const char *path) {
#if defined(ESP32)
    file = SD_MMC.open(path, FILE_READ);
    if (!file) return false;
    size = (uint32_t)file.size();
#else
    file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length < 0 || (unsigned long)length > UINT32_MAX) { close(); return false; }
    size = (uint32_t)length;
#endif
    return true;
  }

  bool seek(uint32_t offset) {
    if (!file || offset > size) return false;
#if defined(ESP32)
    return file.seek(offset);
#else
    return fseek(file, (long)offset, SEEK_SET) == 0;
#endif
  }

  bool read(void *out, size_t length) {
    if (!file) return false;
    uint8_t *cursor = (uint8_t *)out;
    while (length > 0) {
#if defined(ESP32)
      size_t count = file.read(cursor, length);
#else
      size_t count = fread(cursor, 1, length, file);
#endif
      if (count == 0) return false;
      cursor += count;
      length -= count;
    }
    return true;
  }

  bool readAt(uint32_t offset, void *out, size_t length) {
    return offset <= size && length <= size - offset && seek(offset) && read(out, length);
  }

  void close() {
#if defined(ESP32)
    if (file) file.close();
#else
    if (file) fclose(file);
    file = nullptr;
#endif
    size = 0;
  }

  ~Reader() { close(); }
};

static void *contentAlloc(size_t size) {
  if (!size) return nullptr;
#if defined(ESP32)
  void *ptr = ps_malloc(size);
#else
  void *ptr = malloc(size);
#endif
  if (ptr) memset(ptr, 0, size);
  return ptr;
}

static bool allocateSpeciesCatalog(uint16_t maxSpecies) {
  uint16_t required = maxSpecies + 1;
  if (gDex || gSprites || gDexCapacity) return false;
  DexEntry *dex = (DexEntry *)contentAlloc(sizeof(DexEntry) * required);
  SpriteRef *sprites = (SpriteRef *)contentAlloc(sizeof(SpriteRef) * required);
  BreedingRuntime *breeding = (BreedingRuntime *)contentAlloc(
      sizeof(BreedingRuntime) * required);
  if (!dex || !sprites || !breeding) {
    free(dex); free(sprites); free(breeding); return false;
  }
  for (uint16_t i = 0; i < required; i++) sprites[i].pack = 0xFF;
  gDex = dex;
  gSprites = sprites;
  gBreeding = breeding;
  gDexCapacity = required;
  return true;
}

static bool readRange(const PackRef &pack, uint32_t offset, void *out, uint32_t size) {
  Reader reader;
  return reader.open(pack.path) && reader.readAt(offset, out, size);
}

static uint8_t *readSection(const PackRef &pack, const char *tag, uint32_t *size = nullptr,
                            uint32_t *count = nullptr) {
  for (uint8_t i = 0; i < pack.sectionCount; i++) {
    const SectionRef &section = pack.sections[i];
    if (strcmp(section.tag, tag) != 0) continue;
    uint8_t *data = (uint8_t *)contentAlloc(section.size ? section.size : 1u);
    if (!data || (section.size && !readRange(pack, section.offset, data, section.size))) {
      free(data);
      return nullptr;
    }
    if (size) *size = section.size;
    if (count) *count = section.count;
    return data;
  }
  return nullptr;
}

static const SectionRef *findSection(const PackRef &pack, const char *tag) {
  for (uint8_t i = 0; i < pack.sectionCount; i++)
    if (strcmp(pack.sections[i].tag, tag) == 0) return &pack.sections[i];
  return nullptr;
}

static ContentPackValidation payloadCrc(Reader &reader, uint32_t offset, uint32_t expected) {
  uint8_t buffer[4096];
  if (!reader.seek(offset)) return CONTENT_PACK_READ_FAILED;
  uint32_t crc = 0, position = offset;
  while (position < reader.size) {
    uint32_t take = reader.size - position;
    if (take > sizeof(buffer)) take = sizeof(buffer);
    if (!reader.read(buffer, take)) return CONTENT_PACK_READ_FAILED;
    crc = crcStep(crc, buffer, take);
    position += take;
  }
  return crc == expected ? CONTENT_PACK_VALID : CONTENT_PACK_CHECKSUM_MISMATCH;
}

static ContentPackValidation validatePackHeader(const char *path, Reader &reader,
                                                uint8_t common[COMMON_SIZE]) {
  if (!path || !reader.open(path)) return CONTENT_PACK_OPEN_FAILED;
  if (reader.size < COMMON_SIZE) return CONTENT_PACK_SIZE_MISMATCH;
  if (!reader.readAt(0, common, COMMON_SIZE)) return CONTENT_PACK_READ_FAILED;
  uint8_t kind = common[6];
  uint16_t headerSize = rd16(common + 24), sectionCount = rd16(common + 26);
  if (memcmp(common, "TPPK", 4) != 0 || kind < CONTENT_PACK_UI || kind > CONTENT_PACK_BATTLE ||
      sectionCount == 0 || sectionCount > MAX_SECTIONS ||
      headerSize != COMMON_SIZE + sectionCount * SECTION_SIZE || headerSize > reader.size)
    return CONTENT_PACK_HEADER_INVALID;
  uint16_t abi = rd16(common + 4);
  if (abi != CONTENT_PACK_ABI) return CONTENT_PACK_ABI_MISMATCH;
  if (rd32(common + 8) != reader.size) return CONTENT_PACK_SIZE_MISMATCH;
  return CONTENT_PACK_VALID;
}

static bool readPackHeader(const char *path, Reader &reader, uint8_t common[COMMON_SIZE]) {
  return validatePackHeader(path, reader, common) == CONTENT_PACK_VALID;
}

static bool validationFailed(ContentPackValidation result, ContentPackValidation *validation) {
  if (validation) *validation = result;
  return false;
}

enum class PackParseMode : uint8_t {
  METADATA_ONLY,
  VERIFY_PAYLOAD,
};

static bool parsePack(const char *path, PackRef &out,
                      PackParseMode mode,
                      ContentPackValidation *validation = nullptr) {
  Reader reader;
  uint8_t common[COMMON_SIZE];
  ContentPackValidation result = validatePackHeader(path, reader, common);
  if (result != CONTENT_PACK_VALID) return validationFailed(result, validation);
  uint8_t kind = common[6];
  uint32_t fileSize = rd32(common + 8), expectedCrc = rd32(common + 12);
  uint16_t headerSize = rd16(common + 24), sectionCount = rd16(common + 26);
  if (mode == PackParseMode::VERIFY_PAYLOAD) {
    result = payloadCrc(reader, headerSize, expectedCrc);
    if (result != CONTENT_PACK_VALID) return validationFailed(result, validation);
  }

  memset(&out, 0, sizeof(out));
  snprintf(out.path, sizeof(out.path), "%s", path);
  out.kind = kind;
  out.revision = rd32(common + 16);
  out.mechanicsHash = rd32(common + 20);
  memcpy(out.id, common + 28, 20);
  out.id[20] = 0;
  out.sectionCount = (uint8_t)sectionCount;
  uint8_t raw[MAX_SECTIONS * SECTION_SIZE];
  if (!reader.readAt(COMMON_SIZE, raw, sectionCount * SECTION_SIZE))
    return validationFailed(CONTENT_PACK_READ_FAILED, validation);
  uint32_t lastEnd = headerSize;
  for (uint8_t i = 0; i < sectionCount; i++) {
    const uint8_t *row = raw + i * SECTION_SIZE;
    SectionRef &section = out.sections[i];
    memcpy(section.tag, row, 4);
    section.tag[4] = 0;
    section.offset = rd32(row + 4);
    section.size = rd32(row + 8);
    section.count = rd32(row + 12);
    if (section.offset < headerSize || section.offset > fileSize ||
        section.size > fileSize - section.offset ||
        section.offset < lastEnd)
      return validationFailed(CONTENT_PACK_DIRECTORY_INVALID, validation);
    lastEnd = section.offset + section.size;
  }
  snprintf(out.summary, sizeof(out.summary), "%s kind=%u rev=%lu hash=%08lx",
           out.id, out.kind, (unsigned long)out.revision, (unsigned long)out.mechanicsHash);
  if (validation) *validation = CONTENT_PACK_VALID;
  return true;
}

static bool hasPackExtension(const char *path) {
  if (!path) return false;
  size_t length = strlen(path);
  const char *extensions[] = {
    ".tui", ".tmove", ".tregion", ".tquiz", ".titem", ".tbattle"
  };
  for (const char *extension : extensions) {
    size_t extensionLength = strlen(extension);
    if (length >= extensionLength &&
        strcmp(path + length - extensionLength, extension) == 0) return true;
  }
  return false;
}

static void scanPacks() {
#if defined(ESP32)
  File dir = SD_MMC.open("/packs");
  if (!dir || !dir.isDirectory()) return;
  File entry;
  while (gPackCount < MAX_PACKS && (entry = dir.openNextFile())) {
    if (!entry.isDirectory()) {
      char path[128];
      snprintf(path, sizeof(path), "%s", entry.path());
      entry.close();
      if (!hasPackExtension(path)) continue;
      PackRef parsed;
      if (parsePack(path, parsed, PackParseMode::METADATA_ONLY)) gPacks[gPackCount++] = parsed;
      else Serial.printf("pack invalido: %s\n", path);
    } else entry.close();
  }
  dir.close();
#else
  std::error_code error;
  for (const auto &entry : std::filesystem::directory_iterator(CONTENT_DIR, error)) {
    if (error || gPackCount >= MAX_PACKS) break;
    if (!entry.is_regular_file()) continue;
    std::string path = entry.path().string();
    if (!hasPackExtension(path.c_str())) continue;
    PackRef parsed;
    if (parsePack(path.c_str(), parsed, PackParseMode::METADATA_ONLY))
      gPacks[gPackCount++] = parsed;
  }
#endif
  std::sort(gPacks, gPacks + gPackCount, [](const PackRef &a, const PackRef &b) {
    return strcmp(a.id, b.id) < 0;
  });
}

static uint16_t installedSpeciesMaximum() {
  uint16_t maximum = 0;
  for (uint8_t i = 0; i < gPackCount; i++) {
    if (gPacks[i].kind != CONTENT_PACK_REGION) continue;
    const SectionRef *region = findSection(gPacks[i], "REGN");
    uint8_t row[54];
    if (!region || region->count != 1 || region->size != sizeof(row) ||
        !readRange(gPacks[i], region->offset, row, sizeof(row))) continue;
    uint16_t lo = rd16(row + 17), hi = rd16(row + 19);
    if (row[0] >= CONTENT_MAX_REGIONS || !lo || lo > hi ||
        hi > CONTENT_MAX_SPECIES) continue;
    if (hi > maximum) maximum = hi;
  }
  return maximum;
}

static void installedFormCounts(uint16_t &mega, uint16_t &gigantamax) {
  mega = gigantamax = 0;
  for (uint8_t i = 0; i < gPackCount; i++) {
    if (gPacks[i].kind != CONTENT_PACK_REGION) continue;
    const SectionRef *megaSection = findSection(gPacks[i], "MEGA");
    const SectionRef *gmaxSection = findSection(gPacks[i], "GMAX");
    if (megaSection && megaSection->count <= UINT16_MAX - mega)
      mega = (uint16_t)(mega + megaSection->count);
    if (gmaxSection && gmaxSection->count <= UINT16_MAX - gigantamax)
      gigantamax = (uint16_t)(gigantamax + gmaxSection->count);
  }
}

static bool validString(const uint8_t *blob, uint32_t size, uint32_t offset) {
  if (offset >= size) return false;
  return memchr(blob + offset, 0, size - offset) != nullptr;
}

static bool validThumbs(const uint8_t *data, uint32_t size) {
  if (!data || size < 6 || memcmp(data, "TPTH", 4) != 0) return false;
  uint16_t count = rd16(data + 4);
  uint32_t tableEnd = 6u + (uint32_t)count * 4u;
  if (!count || count > CONTENT_MAX_SPECIES || tableEnd > size) return false;
  uint32_t previousEnd = tableEnd;
  for (uint16_t i = 0; i < count; i++) {
    uint32_t offset = rd32(data + 6 + (uint32_t)i * 4u);
    if (!offset) continue;  // The species exists, but community art does not.
    if (offset < previousEnd || offset > size || size - offset < 3) return false;
    uint8_t width = data[offset], height = data[offset + 1], paletteCount = data[offset + 2];
    uint32_t recordSize = 3u + (uint32_t)paletteCount * 2u + (uint32_t)width * height;
    if (!width || !height || !paletteCount || recordSize > size - offset) return false;
    const uint8_t *pixels = data + offset + 3u + (uint32_t)paletteCount * 2u;
    for (uint32_t pixel = 0; pixel < (uint32_t)width * height; pixel++)
      if (pixels[pixel] != 0xFF && pixels[pixel] >= paletteCount) return false;
    previousEnd = offset + recordSize;
  }
  return true;
}

static bool validItemIcons(const uint8_t *data, uint32_t size, uint32_t expectedCount) {
  constexpr uint32_t RECORD_SIZE = 8;
  if (!data || size < 4 || rd16(data) != expectedCount ||
      rd16(data + 2) != RECORD_SIZE) return false;
  uint32_t tableEnd = 4u + expectedCount * RECORD_SIZE;
  if (tableEnd > size) return false;
  uint32_t previousEnd = tableEnd;
  for (uint32_t i = 0; i < expectedCount; i++) {
    const uint8_t *row = data + 4u + i * RECORD_SIZE;
    uint32_t offset = rd32(row), length = rd32(row + 4);
    if (!offset && !length) continue;
    if (offset < previousEnd || !length || offset > size || length > size - offset ||
        length < 8 || memcmp(data + offset, "TIC1", 4) != 0) return false;
    const uint8_t *icon = data + offset;
    uint8_t width = icon[4], height = icon[5], paletteCount = icon[6];
    uint32_t expectedSize = 8u + (uint32_t)paletteCount * 2u +
                            (uint32_t)width * height;
    if (!width || !height || width > 32 || height > 32 || !paletteCount ||
        icon[7] || expectedSize != length) return false;
    const uint8_t *pixels = icon + 8u + (uint32_t)paletteCount * 2u;
    for (uint32_t pixel = 0; pixel < (uint32_t)width * height; pixel++)
      if (pixels[pixel] != 0xFF && pixels[pixel] >= paletteCount) return false;
    previousEnd = offset + length;
  }
  return true;
}

static const char *localizedAt(const uint8_t *data, uint32_t size, const char *locale,
                               uint32_t item) {
  if (!data || size < 4 || !locale) return nullptr;
  uint16_t localeCount = rd16(data), itemCount = rd16(data + 2);
  if (item >= itemCount || 4u + (uint32_t)localeCount * 28u > size) return nullptr;
  for (uint16_t i = 0; i < localeCount; i++) {
    const uint8_t *row = data + 4 + i * 28;
    char code[17];
    memcpy(code, row, 16); code[16] = 0;
    if (strncmp(code, locale, 16) != 0) continue;
    uint32_t indexAt = rd32(row + 16), blobAt = rd32(row + 20), blobSize = rd32(row + 24);
    if (indexAt + (uint32_t)(itemCount + 1) * 4u > size ||
        blobAt > size || blobSize > size - blobAt) return nullptr;
    uint32_t offset = rd32(data + indexAt + item * 4);
    uint32_t end = rd32(data + indexAt + (item + 1) * 4);
    if (offset > end || end > blobSize || !validString(data + blobAt, blobSize, offset))
      return nullptr;
    return (const char *)(data + blobAt + offset);
  }
  return nullptr;
}

static bool validateUiPayload(const PackRef &pack) {
  uint32_t stringsSize = 0, stringsCount = 0, fontSize = 0, fontCount = 0;
  uint32_t layoutSize = 0, layoutCount = 0;
  uint8_t *strings = readSection(pack, "STRS", &stringsSize, &stringsCount);
  uint8_t *font = readSection(pack, "FONT", &fontSize, &fontCount);
  uint8_t *layout = readSection(pack, "LAYT", &layoutSize, &layoutCount);
  bool valid = strings && stringsSize >= 6 && rd16(strings) == stringsCount &&
               font && fontSize >= 8 && layout && layoutSize == layoutCount * 4u;
  if (valid) {
    uint32_t tableSize = 2u + (stringsCount + 1u) * 4u;
    valid = tableSize <= stringsSize;
    uint32_t previous = 0, blobSize = valid ? stringsSize - tableSize : 0;
    for (uint32_t i = 0; valid && i <= stringsCount; i++) {
      uint32_t offset = rd32(strings + 2 + i * 4);
      if (offset < previous || offset > blobSize) valid = false;
      previous = offset;
    }
    for (uint32_t i = 0; valid && i < stringsCount; i++) {
      uint32_t offset = rd32(strings + 2 + i * 4);
      if (!validString(strings + tableSize, blobSize, offset)) valid = false;
    }
  }
  if (valid && memcmp(font, "FNT4", 4) == 0) {
    uint16_t glyphs = rd16(font + 6);
    valid = font[4] && font[5] && glyphs == fontCount &&
            UI_FONT_HEADER_SIZE + (uint32_t)glyphs * UI_FONT_GLYPH_SIZE == fontSize;
    uint32_t previous = 0;
    for (uint16_t i = 0; valid && i < glyphs; i++) {
      const uint8_t *row = font + UI_FONT_HEADER_SIZE + (uint32_t)i * UI_FONT_GLYPH_SIZE;
      uint32_t codepoint = rd32(row);
      if ((i && codepoint <= previous) || !row[4] || row[4] > 16 ||
          !row[5] || row[5] > 16 || !row[6]) valid = false;
      previous = codepoint;
    }
  } else if (valid && memcmp(font, "FNT5", 4) == 0) {
    uint8_t sizeCount = font[6];
    uint32_t dataSize = fontSize >= UI_VECTOR_FONT_HEADER_SIZE ? rd32(font + 16) : 0;
    valid = fontSize >= UI_VECTOR_FONT_HEADER_SIZE && font[4] == 1 && sizeCount &&
            sizeCount <= 8 && fontCount == 1 &&
            dataSize == fontSize - UI_VECTOR_FONT_HEADER_SIZE;
    for (uint8_t i = 0; valid && i < sizeCount; i++)
      if (!font[8 + i] || (i && font[8 + i] <= font[7 + i])) valid = false;
    if (valid) {
      const uint8_t *signature = font + UI_VECTOR_FONT_HEADER_SIZE;
      valid = !memcmp(signature, "OTTO", 4) || !memcmp(signature, "ttcf", 4) ||
              (!signature[0] && signature[1] == 1 && !signature[2] && !signature[3]);
    }
  } else if (valid) {
    valid = false;
  }
  free(strings); free(font); free(layout);
  return valid;
}

static bool loadMovePack(uint8_t packIndex) {
  const PackRef &pack = gPacks[packIndex];
  uint32_t moveSize = 0, moveRecords = 0, nameSize = 0;
  uint8_t *rawMoves = readSection(pack, "MOVE", &moveSize, &moveRecords);
  uint8_t *names = readSection(pack, "NAME", &nameSize);
  if (!rawMoves || !names || moveRecords == 0 || moveRecords > CONTENT_MAX_MOVES ||
      moveSize != moveRecords * 17u) {
    free(rawMoves); free(names); return false;
  }
  MoveEntry *table = (MoveEntry *)contentAlloc(sizeof(MoveEntry) * moveRecords);
  if (!table) { free(rawMoves); free(names); return false; }
  for (uint32_t i = 0; i < moveRecords; i++) {
    const uint8_t *row = rawMoves + i * 17;
    uint16_t id = rd16(row);
    uint32_t nameOffset = rd32(row + 13);
    if (id >= moveRecords || row[2] >= TYPE_COUNT || row[3] > MC_STATUS ||
        row[6] > EF_PIVOT || row[10] > TG_FOE || row[11] > AIL_CONFUSE ||
        row[12] > 100 || !validString(names, nameSize, nameOffset)) {
      free(rawMoves); free(names); free(table); return false;
    }
    MoveEntry &move = table[id];
    move.name = (const char *)(names + nameOffset);
    move.type = row[2]; move.cat = row[3]; move.power = row[4]; move.acc = row[5];
    move.effect = row[6]; move.param = rdI8(row + 7); move.statMask = row[8];
    move.stages = rdI8(row + 9); move.target = row[10]; move.ailment = row[11];
    move.ailChance = row[12];
    move.fieldFlags = MF_NONE;
    move.tags = MT_NONE;
  }
  for (uint32_t i = 0; i < moveRecords; i++) {
    if (!table[i].name) { free(rawMoves); free(names); free(table); return false; }
  }
  free(rawMoves);

  uint32_t flagSize = 0, flagCount = 0, tagSize = 0, tagCount = 0;
  uint8_t *flags = readSection(pack, "MFLG", &flagSize, &flagCount);
  uint8_t *tags = readSection(pack, "MTAG", &tagSize, &tagCount);
  if (!flags || flagSize != moveRecords || flagCount != moveRecords ||
      !tags || tagSize != moveRecords * 2u || tagCount != moveRecords) {
    free(flags); free(tags); free(names); free(table); return false;
  }
  for (uint32_t i = 0; i < moveRecords; i++) {
    uint16_t moveTags = rd16(tags + i * 2u);
    if (flags[i] & ~(MF_RAIN_ACCURATE | MF_SNOW_ACCURATE |
                     MF_SOLAR_CHARGE | MF_GRASSY_WEAKENED |
                     MF_STANCE_SHIELD | MF_AURA_WHEEL | MF_GULP_MISSILE) ||
        moveTags & ~MT_ALL) {
      free(flags); free(tags); free(names); free(table); return false;
    }
    table[i].fieldFlags = flags[i];
    table[i].tags = moveTags;
  }
  free(flags);
  free(tags);

  uint32_t localesSize = 0;
  uint8_t *locales = readSection(pack, "LOCL", &localesSize);
  uint32_t localizedNamesSize = 0;
  uint8_t *localizedNames = readSection(pack, "LNAM", &localizedNamesSize);
  if (!locales || !localizedNames) {
    free(locales); free(localizedNames); free(names); free(table); return false;
  }

  uint32_t gmaxMoveSize = 0, gmaxMoveCount = 0, gmaxNameSize = 0, gmaxNameCount = 0;
  uint32_t gmaxLocalizedSize = 0, gmaxLocalizedCount = 0;
  uint8_t *rawGmaxMoves = readSection(pack, "GMOV", &gmaxMoveSize, &gmaxMoveCount);
  uint8_t *gmaxNames = readSection(pack, "GMNM", &gmaxNameSize, &gmaxNameCount);
  uint8_t *gmaxLocalizedNames = readSection(
      pack, "GMLN", &gmaxLocalizedSize, &gmaxLocalizedCount);
  GmaxMoveEntry *gmaxMoves = gmaxMoveCount
      ? (GmaxMoveEntry *)contentAlloc(sizeof(GmaxMoveEntry) * gmaxMoveCount) : nullptr;
  bool validGmax = rawGmaxMoves && gmaxNames && gmaxLocalizedNames && gmaxMoves &&
      gmaxMoveCount <= UINT8_MAX && gmaxMoveSize == gmaxMoveCount * 8u &&
      gmaxNameCount == gmaxMoveCount && gmaxLocalizedCount == gmaxMoveCount;
  for (uint32_t i = 0; validGmax && i < gmaxMoveCount; i++) {
    const uint8_t *row = rawGmaxMoves + i * 8u;
    uint32_t nameOffset = rd32(row + 4);
    GmaxMoveEntry &move = gmaxMoves[i];
    move.id = row[0]; move.sourceType = row[1];
    move.effect = (GmaxEffect)row[2]; move.power = row[3];
    move.name = validString(gmaxNames, gmaxNameSize, nameOffset)
        ? (const char *)(gmaxNames + nameOffset) : nullptr;
    validGmax = move.id == i + 1u && move.sourceType < TYPE_COUNT &&
        move.effect > GMAX_EFFECT_NONE && move.effect < GMAX_EFFECT_COUNT && move.name;
  }
  free(rawGmaxMoves);

  uint32_t maxNameSize = 0, maxNameCount = 0;
  uint32_t maxLocalizedSize = 0, maxLocalizedCount = 0;
  uint8_t *maxNames = readSection(pack, "MXNM", &maxNameSize, &maxNameCount);
  uint8_t *maxLocalizedNames = readSection(
      pack, "MXLN", &maxLocalizedSize, &maxLocalizedCount);
  uint32_t maxNameOffsets[TYPE_COUNT + 1] = {};
  bool validMax = maxNames && maxLocalizedNames && maxNameCount == TYPE_COUNT + 1u &&
      maxLocalizedCount == maxNameCount;
  uint32_t maxNameAt = 0;
  for (uint8_t i = 0; validMax && i < TYPE_COUNT + 1u; i++) {
    if (!validString(maxNames, maxNameSize, maxNameAt)) {
      validMax = false;
      break;
    }
    maxNameOffsets[i] = maxNameAt;
    maxNameAt += strlen((const char *)(maxNames + maxNameAt)) + 1u;
  }
  if (maxNameAt != maxNameSize) validMax = false;
  if (!validGmax || !validMax) {
    free(maxNames); free(maxLocalizedNames); free(gmaxMoves); free(gmaxNames);
    free(gmaxLocalizedNames); free(locales); free(localizedNames); free(names); free(table);
    return false;
  }

  gMovesTable = table;
  gMoveCount = (uint16_t)moveRecords;
  gMoveNames = (char *)names;
  gMoveLocales = locales;
  gMoveLocalesSize = localesSize;
  gMoveLocalizedNames = localizedNames;
  gMoveLocalizedNamesSize = localizedNamesSize;
  gGmaxMoves = gmaxMoves;
  gGmaxMoveCount = (uint8_t)gmaxMoveCount;
  gGmaxMoveNames = (char *)gmaxNames;
  gGmaxMoveLocalizedNames = gmaxLocalizedNames;
  gGmaxMoveLocalizedNamesSize = gmaxLocalizedSize;
  gMaxMoveNames = (char *)maxNames;
  memcpy(gMaxMoveNameOffsets, maxNameOffsets, sizeof(maxNameOffsets));
  gMaxMoveLocalizedNames = maxLocalizedNames;
  gMaxMoveLocalizedNamesSize = maxLocalizedSize;
  gMoves = true;
  gPacks[packIndex].loaded = true;
  return true;
}

static bool loadItemPack(uint8_t packIndex) {
  const PackRef &pack = gPacks[packIndex];
  uint32_t itemSize = 0, itemRecords = 0, itemNamesSize = 0;
  uint32_t localizedNamesSize = 0, localizedNameCount = 0;
  uint32_t localesSize = 0, localeCount = 0;
  uint8_t *rawItems = readSection(pack, "ITEM", &itemSize, &itemRecords);
  uint8_t *names = readSection(pack, "INAM", &itemNamesSize);
  uint8_t *localizedNames = readSection(
      pack, "ILNM", &localizedNamesSize, &localizedNameCount);
  uint8_t *locales = readSection(pack, "ILOC", &localesSize, &localeCount);
  if (!rawItems || !names || !localizedNames || !locales || !itemRecords ||
      itemRecords > CONTENT_MAX_ITEMS || itemSize != itemRecords * 16u ||
      localizedNameCount != itemRecords || localeCount != itemRecords) {
    free(rawItems); free(names); free(localizedNames); free(locales); return false;
  }
  ItemEntry *items = (ItemEntry *)contentAlloc(sizeof(ItemEntry) * itemRecords);
  if (!items) {
    free(rawItems); free(names); free(localizedNames); free(locales); return false;
  }
  bool valid = true;
  for (uint32_t i = 0; valid && i < itemRecords; i++) {
    const uint8_t *row = rawItems + i * 16u;
    ItemKey key = rd16(row);
    int16_t param = (int16_t)rd16(row + 6);
    uint32_t nameOffset = rd32(row + 12);
    bool training = row[3] == ITEM_EFFECT_TRAINING_FLOOR;
    bool boost = row[3] == ITEM_EFFECT_BATTLE_STAGE;
    bool mechanic = row[3] == ITEM_EFFECT_BATTLE_MECHANIC;
    bool catchItem = row[3] == ITEM_EFFECT_CATCH;
    bool moveStone = row[3] == ITEM_EFFECT_TEACH_MOVE;
    valid = key && row[2] >= ITEM_CATEGORY_BALL && row[2] <= ITEM_CATEGORY_MOVE_STONE &&
        row[3] >= ITEM_EFFECT_CATCH && row[3] <= ITEM_EFFECT_TEACH_MOVE &&
        row[4] && row[4] <= 4 && row[10] <= ITEM_STACK_LIMIT &&
        (!catchItem || (row[2] == ITEM_CATEGORY_BALL &&
                        (param > 0 || param == ITEM_CATCH_GUARANTEED))) &&
        (!training || (row[2] == ITEM_CATEGORY_TRAINING &&
                       (row[5] == ITEM_STAT_ATK || row[5] == ITEM_STAT_DEF ||
                        row[5] == ITEM_STAT_SPE) && param > 0)) &&
        (!boost || (row[2] == ITEM_CATEGORY_BATTLE_BOOST &&
                    (row[5] == ITEM_STAT_ATK || row[5] == ITEM_STAT_DEF ||
                     row[5] == ITEM_STAT_SPA || row[5] == ITEM_STAT_SPD ||
                     row[5] == ITEM_STAT_SPE) && param > 0 && param <= 6)) &&
        (!mechanic || (row[2] == ITEM_CATEGORY_MECHANIC &&
                       row[5] >= ITEM_MECHANIC_Z_MOVE && row[5] <= ITEM_MECHANIC_MEGA &&
                       (row[5] == ITEM_MECHANIC_MEGA || param == 0) &&
                       (row[5] != ITEM_MECHANIC_MEGA ||
                        (param >= MEGA_FORM_STANDARD && param <= MEGA_FORM_Z)) &&
                       !row[8] && !row[9] && !row[10])) &&
        (row[3] != ITEM_EFFECT_GIGANTAMAX_FACTOR ||
         (row[2] == ITEM_CATEGORY_EVOLUTION && !row[5] && !param && !row[10])) &&
        (!moveStone || (row[2] == ITEM_CATEGORY_MOVE_STONE && !row[5] && !param && !row[10])) &&
        validString(names, itemNamesSize, nameOffset);
    for (uint32_t previous = 0; valid && previous < i; previous++)
      if (items[previous].key == key) valid = false;
    if (!valid) break;
    ItemEntry &item = items[i];
    item.key = key; item.name = (const char *)(names + nameOffset);
    item.category = row[2]; item.effect = row[3]; item.rarity = row[4];
    item.flags = row[5]; item.param = param;
    item.dropWeight = rd16(row + 8); item.dailyMin = row[10];
  }
  free(rawItems);
  uint32_t iconSize = 0, iconCount = 0;
  uint8_t *icons = nullptr;
  if (valid && findSection(pack, "IICO")) {
    icons = readSection(pack, "IICO", &iconSize, &iconCount);
    valid = icons && iconCount == itemRecords && validItemIcons(icons, iconSize, itemRecords);
  }
  if (!valid) {
    free(icons); free(items); free(names); free(localizedNames); free(locales); return false;
  }
  gItems = items; gItemCount = (uint16_t)itemRecords; gItemNames = (char *)names;
  gItemLocalizedNames = localizedNames; gItemLocalizedNamesSize = localizedNamesSize;
  gItemLocales = locales; gItemLocalesSize = localesSize;
  gItemIcons = icons; gItemIconsSize = iconSize;
  gItemsReady = true; gPacks[packIndex].loaded = true;
  return true;
}

static bool loadBattlePack(uint8_t packIndex) {
  const PackRef &pack = gPacks[packIndex];
  uint32_t chartSize = 0, typeSize = 0, typeRecords = 0, typeNamesSize = 0;
  uint8_t *chart = readSection(pack, "CHRT", &chartSize);
  uint8_t *types = readSection(pack, "TYPS", &typeSize, &typeRecords);
  uint8_t *typeNames = readSection(pack, "TSTR", &typeNamesSize);
  uint32_t localizedTypeNamesSize = 0, localizedTypeCount = 0;
  uint8_t *localizedTypeNames = readSection(
      pack, "TLNM", &localizedTypeNamesSize, &localizedTypeCount);
  bool valid = chart && chartSize == sizeof(gTypeChart) && types &&
      typeRecords == TYPE_COUNT && typeSize == TYPE_COUNT * 8u && typeNames &&
      localizedTypeNames && localizedTypeCount == TYPE_COUNT;
  for (uint8_t i = 0; valid && i < TYPE_COUNT; i++) {
    uint32_t nameOffset = rd32(types + i * 8u);
    if (!validString(typeNames, typeNamesSize, nameOffset) || types[i * 8u + 7]) {
      valid = false; break;
    }
    gTypeNameOffsets[i] = nameOffset;
    gTypeColors[i] = rd16(types + i * 8u + 4u);
    gTypeLight[i] = types[i * 8u + 6u];
  }

  uint32_t abilitySize = 0, abilityRecords = 0, abilityNamesSize = 0;
  uint32_t localizedAbilityNamesSize = 0, localizedAbilityNameCount = 0;
  uint32_t abilityLocalesSize = 0, abilityLocaleCount = 0;
  uint8_t *rawAbilities = readSection(pack, "ABIL", &abilitySize, &abilityRecords);
  uint8_t *abilityNames = readSection(pack, "ANAM", &abilityNamesSize);
  uint8_t *localizedAbilityNames = readSection(
      pack, "ALNM", &localizedAbilityNamesSize, &localizedAbilityNameCount);
  uint8_t *abilityLocales = readSection(
      pack, "ALOC", &abilityLocalesSize, &abilityLocaleCount);
  AbilityRuntime *abilities = (AbilityRuntime *)contentAlloc(
      sizeof(AbilityRuntime) * abilityRecords);
  valid = valid && rawAbilities && abilityNames && localizedAbilityNames &&
      abilityLocales && abilities && abilityRecords &&
      abilityRecords <= CONTENT_MAX_ABILITIES && abilitySize == abilityRecords * 6u &&
      localizedAbilityNameCount == abilityRecords && abilityLocaleCount == abilityRecords;
  for (uint32_t i = 0; valid && i < abilityRecords; i++) {
    const uint8_t *row = rawAbilities + i * 6u;
    AbilityKey key = rd16(row);
    uint32_t nameOffset = rd32(row + 2u);
    if (!key || (i && key <= abilities[i - 1].key) ||
        !validString(abilityNames, abilityNamesSize, nameOffset)) {
      valid = false; break;
    }
    abilities[i].key = key;
    abilities[i].name = (const char *)(abilityNames + nameOffset);
  }
  if (valid) memcpy(gTypeChart, chart, sizeof(gTypeChart));
  free(chart); free(types); free(rawAbilities);
  if (!valid) {
    free(typeNames); free(localizedTypeNames); free(abilities); free(abilityNames);
    free(localizedAbilityNames); free(abilityLocales); return false;
  }
  gTypeNames = (char *)typeNames;
  gTypeLocalizedNames = localizedTypeNames;
  gTypeLocalizedNamesSize = localizedTypeNamesSize;
  gAbilities = abilities; gAbilityCount = (uint16_t)abilityRecords;
  gAbilityNames = (char *)abilityNames;
  gAbilityLocalizedNames = localizedAbilityNames;
  gAbilityLocalizedNamesSize = localizedAbilityNamesSize;
  gAbilityLocales = abilityLocales; gAbilityLocalesSize = abilityLocalesSize;
  gBattle = true; gPacks[packIndex].loaded = true;
  return true;
}

static bool loadRegionMetadata(const uint8_t *data, uint32_t size, uint32_t count) {
  constexpr uint32_t RECORD = 54;
  if (!data || count != 1 || size != count * RECORD) return false;
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t *row = data + i * RECORD;
    uint8_t id = row[0], starterCount = row[21];
    uint16_t lo = rd16(row + 17), hi = rd16(row + 19);
    if (id >= CONTENT_MAX_REGIONS || gRegions[id].name || starterCount > MAX_STARTERS ||
        !memchr(row + 1, 0, 16) || !lo || lo > hi || hi > CONTENT_MAX_SPECIES) return false;
    for (uint8_t j = 0; j < starterCount; j++) {
      SpeciesId starter = rd16(row + 22 + j * 2);
      if (starter < lo || starter > hi) return false;
    }
    memcpy(gRegionNames[id], row + 1, 16); gRegionNames[id][16] = 0;
    RegionInfo &region = gRegions[id];
    region.name = gRegionNames[id]; region.lo = lo; region.hi = hi;
    region.starterCount = starterCount; region.starters = gRegionStarters[id];
    for (uint8_t j = 0; j < starterCount; j++) gRegionStarters[id][j] = rd16(row + 22 + j * 2);
    if (id + 1 > gRealRegionCount) gRealRegionCount = id + 1;
    if (region.hi > gDexCount) gDexCount = region.hi;
  }
  return true;
}

static bool loadRegionBattleData(const PackRef &pack, uint8_t expectedRegion) {
  uint32_t metaSize = 0, metaCount = 0, trainerSize = 0, trainerCount = 0;
  uint32_t stringsSize = 0, badgeSize = 0, badgeCount = 0, badgeBlobSize = 0;
  uint8_t *meta = readSection(pack, "BTTL", &metaSize, &metaCount);
  uint8_t *rawTrainers = readSection(pack, "TRNR", &trainerSize, &trainerCount);
  uint8_t *strings = readSection(pack, "GSTR", &stringsSize);
  uint8_t *rawBadges = readSection(pack, "BADG", &badgeSize, &badgeCount);
  uint8_t *badgeBlob = readSection(pack, "BBLB", &badgeBlobSize);
  constexpr uint32_t TRAINER_RECORD = 30;
  constexpr uint32_t BADGE_RECORD = 12;
  if (!meta || metaSize != 8 || metaCount != 1 || meta[0] != expectedRegion ||
      trainerCount != meta[1] || !trainerCount ||
      trainerCount > CONTENT_MAX_TRAINERS_PER_REGION ||
      !meta[2] || meta[2] > trainerCount || meta[3] > trainerCount - meta[2] ||
      meta[4] > 31 || meta[5] > 31 ||
      trainerSize != trainerCount * TRAINER_RECORD || !rawTrainers || !strings ||
      badgeCount != meta[2] || badgeCount > CONTENT_MAX_BADGES_PER_REGION ||
      badgeSize != badgeCount * BADGE_RECORD || !rawBadges || !badgeBlob) {
    free(meta); free(rawTrainers); free(strings); free(rawBadges); free(badgeBlob);
    return false;
  }
  Trainer *trainers = (Trainer *)contentAlloc(sizeof(Trainer) * trainerCount);
  if (!trainers) {
    free(meta); free(rawTrainers); free(strings); free(rawBadges); free(badgeBlob);
    return false;
  }
  for (uint32_t i = 0; i < trainerCount; i++) {
    const uint8_t *row = rawTrainers + i * TRAINER_RECORD;
    uint32_t nameAt = rd32(row + 4), placeAt = rd32(row + 8);
    if (row[0] != i || row[1] >= TYPE_COUNT || !row[2] || row[2] > TRAINER_TEAM_MAX ||
        !validString(strings, stringsSize, nameAt) || !validString(strings, stringsSize, placeAt)) {
      free(meta); free(rawTrainers); free(strings); free(rawBadges); free(badgeBlob); free(trainers);
      return false;
    }
    Trainer &trainer = trainers[i];
    trainer.name = (const char *)strings + nameAt;
    trainer.place = (const char *)strings + placeAt;
    trainer.type = row[1]; trainer.count = row[2];
    for (uint8_t member = 0; member < TRAINER_TEAM_MAX; member++) {
      const uint8_t *mon = row + 12 + member * 3;
      trainer.team[member].dex = rd16(mon);
      trainer.team[member].level = mon[2];
      if (member < trainer.count &&
          (!trainer.team[member].dex || trainer.team[member].dex > CONTENT_MAX_SPECIES ||
           !trainer.team[member].level || trainer.team[member].level > MAX_TRAINER_LEVEL)) {
        free(meta); free(rawTrainers); free(strings); free(rawBadges); free(badgeBlob); free(trainers);
        return false;
      }
    }
  }
  free(rawTrainers);

  BadgeArt parsedBadges[CONTENT_MAX_BADGES_PER_REGION] = {};
  for (uint32_t i = 0; i < badgeCount; i++) {
    const uint8_t *row = rawBadges + i * BADGE_RECORD;
    uint8_t width = row[1], height = row[2], paletteCount = row[3];
    uint32_t paletteAt = rd32(row + 4), pixelsAt = rd32(row + 8);
    uint32_t pixelCount = (uint32_t)width * height;
    if (row[0] != i || !width || !height || !paletteCount ||
        paletteAt > badgeBlobSize || paletteCount * 2u > badgeBlobSize - paletteAt ||
        pixelsAt > badgeBlobSize || pixelCount > badgeBlobSize - pixelsAt) {
      free(meta); free(strings); free(rawBadges); free(badgeBlob); free(trainers);
      return false;
    }
    for (uint32_t pixel = 0; pixel < pixelCount; pixel++)
      if (badgeBlob[pixelsAt + pixel] != 0xFF && badgeBlob[pixelsAt + pixel] >= paletteCount) {
        free(meta); free(strings); free(rawBadges); free(badgeBlob); free(trainers);
        return false;
      }
    parsedBadges[i].pal = (const uint16_t *)(badgeBlob + paletteAt);
    parsedBadges[i].idx = badgeBlob + pixelsAt;
    parsedBadges[i].width = width; parsedBadges[i].height = height;
    parsedBadges[i].paletteCount = paletteCount;
  }
  free(rawBadges);

  gRegionBattles[expectedRegion] = {
    meta[1], meta[2], meta[3], meta[4], meta[5],
  };
  free(meta);
  gTrainers[expectedRegion] = trainers;
  gTrainerStrings[expectedRegion] = (char *)strings;
  gBadgeBlobs[expectedRegion] = badgeBlob;
  gBadgeCounts[expectedRegion] = (uint8_t)badgeCount;
  for (uint8_t i = 0; i < badgeCount; i++) gBadges[expectedRegion][i] = parsedBadges[i];
  return true;
}

static bool loadRegionPack(uint8_t packIndex) {
  if (gRegionPackCount >= MAX_PET_PACKS) return false;
  const PackRef &pack = gPacks[packIndex];
  uint32_t specSize = 0, specCount = 0, namesSize = 0;
  uint8_t *specs = readSection(pack, "SPEC", &specSize, &specCount);
  uint8_t *names = readSection(pack, "NAME", &namesSize);
  uint32_t regionSize = 0, regionRecords = 0;
  uint8_t *regions = readSection(pack, "REGN", &regionSize, &regionRecords);
  const SectionRef *evolutionSection = findSection(pack, "EVOS");
  const SectionRef *learnOffsetSection = findSection(pack, "LOFS");
  const SectionRef *learnSection = findSection(pack, "LERN");
  const SectionRef *breedingSection = findSection(pack, "BRSP");
  const SectionRef *eggMoveSection = findSection(pack, "BEMV");
  const SectionRef *megaSection = findSection(pack, "MEGA");
  const SectionRef *gmaxSection = findSection(pack, "GMAX");
  if (!specs || !names || !regions || specCount == 0 || specCount > CONTENT_MAX_SPECIES ||
      specSize != specCount * 22u || !evolutionSection ||
      evolutionSection->size != evolutionSection->count * 4u ||
      !learnOffsetSection || learnOffsetSection->count != specCount + 1u ||
      learnOffsetSection->size != learnOffsetSection->count * 4u ||
      !learnSection || learnSection->size != learnSection->count * 4u ||
      !breedingSection || breedingSection->count != specCount ||
      breedingSection->size != breedingSection->count * 14u ||
      !eggMoveSection || eggMoveSection->size != eggMoveSection->count * 2u ||
      !megaSection || megaSection->size != megaSection->count * 14u ||
      !gmaxSection || gmaxSection->size != gmaxSection->count * 4u) {
    free(specs); free(names); free(regions); return false;
  }
  uint16_t maxSpecies = 0;
  for (uint32_t i = 0; i < specCount; i++) {
    SpeciesId id = rd16(specs + i * 22u);
    if (id > maxSpecies) maxSpecies = id;
  }
  if (!maxSpecies || maxSpecies > CONTENT_MAX_SPECIES ||
      maxSpecies >= gDexCapacity) {
    free(specs); free(names); free(regions); return false;
  }
  SpeciesId *touched = (SpeciesId *)contentAlloc(sizeof(SpeciesId) * specCount);
  if (!touched) { free(specs); free(names); free(regions); return false; }
  uint32_t touchedCount = 0;
  RegionInfo oldRegions[CONTENT_MAX_REGIONS + 1];
  char oldRegionNames[CONTENT_MAX_REGIONS + 1][17];
  SpeciesId oldRegionStarters[CONTENT_MAX_REGIONS + 1][MAX_STARTERS * 2];
  memcpy(oldRegions, gRegions, sizeof(oldRegions));
  memcpy(oldRegionNames, gRegionNames, sizeof(oldRegionNames));
  memcpy(oldRegionStarters, gRegionStarters, sizeof(oldRegionStarters));
  uint8_t oldRealRegionCount = gRealRegionCount;
  uint16_t oldDexCount = gDexCount, oldRegionMask = gRegionMask;
  uint16_t oldMegaFormCount = gMegaFormCount, oldGigantamaxCount = gGigantamaxCount;
  uint32_t *learnOffsets = nullptr;
  LearnEntry *learnEntries = nullptr;
  MoveId *eggMoves = nullptr;
  uint32_t learnOffsetCount = 0, learnEntryCount = 0;
  auto rollback = [&]() {
    for (uint32_t i = 0; i < touchedCount; i++) {
      gDex[touched[i]] = DexEntry{};
      gSprites[touched[i]] = SpriteRef{};
      gBreeding[touched[i]] = BreedingRuntime{};
    }
    memcpy(gRegions, oldRegions, sizeof(oldRegions));
    memcpy(gRegionNames, oldRegionNames, sizeof(oldRegionNames));
    memcpy(gRegionStarters, oldRegionStarters, sizeof(oldRegionStarters));
    gRealRegionCount = oldRealRegionCount;
    gDexCount = oldDexCount;
    gRegionMask = oldRegionMask;
    gMegaFormCount = oldMegaFormCount;
    gGigantamaxCount = oldGigantamaxCount;
    free(learnOffsets);
    free(learnEntries);
    free(eggMoves);
  };
  uint8_t metadataRegion = regions[0];
  if (!loadRegionMetadata(regions, regionSize, regionRecords)) {
    rollback(); free(touched); free(specs); free(names); free(regions); return false;
  }
  free(regions);

  uint8_t runtimePack = gRegionPackCount;
  uint16_t firstSpecies = 0;
  uint8_t packRegion = 0xFF;
  for (uint32_t i = 0; i < specCount; i++) {
    const uint8_t *row = specs + i * 22;
    SpeciesId id = rd16(row);
    uint32_t nameOffset = rd32(row + 17);
    if (!id || id > CONTENT_MAX_SPECIES || gDex[id].name ||
        !validString(names, namesSize, nameOffset)) {
      rollback(); free(touched); free(specs); free(names); return false;
    }
    touched[touchedCount++] = id;
    if (!firstSpecies) firstSpecies = id;
    DexEntry &species = gDex[id];
    species.name = (const char *)(names + nameOffset);
    species.evolveLevel = row[2]; species.rarity = row[3]; species.accent = rd16(row + 4);
    species.bHp = row[6]; species.bAtk = row[7]; species.bDef = row[8]; species.bSpe = row[9];
    species.bSpA = row[10]; species.bSpD = row[11]; species.biome = row[12];
    species.type1 = row[13]; species.type2 = row[14];
    species.femaleRate = row[21];
    species.encounterPeriods = row[16];
    uint8_t region = row[15];
    if (region >= CONTENT_MAX_REGIONS || species.type1 >= TYPE_COUNT ||
        (species.type2 != T_NONE && species.type2 >= TYPE_COUNT) ||
        !species.bHp || !species.bAtk || !species.bDef || !species.bSpe ||
        !species.bSpA || !species.bSpD ||
        (species.femaleRate > 8 && species.femaleRate != GENDER_RATE_NONE) ||
        !species.encounterPeriods || (species.encounterPeriods & ~ENCOUNTER_BOTH)) {
      rollback(); free(touched); free(specs); free(names); return false;
    }
    gRegionMask |= (uint16_t)(1u << region);
    if (packRegion == 0xFF) packRegion = region;
    else if (packRegion != region) {
      rollback(); free(touched); free(specs); free(names); return false;
    }
    if (id > gDexCount) gDexCount = id;
    gSprites[id].pack = runtimePack; gSprites[id].localIndex = (uint16_t)i;
  }
  free(specs);

  uint32_t abilitySlotSize = 0, abilitySlotCount = 0;
  uint8_t *abilitySlots = readSection(pack, "ASLT", &abilitySlotSize, &abilitySlotCount);
  if (!abilitySlots || abilitySlotCount != specCount || abilitySlotSize != specCount * 8u) {
    rollback(); free(touched); free(abilitySlots); free(names); return false;
  }
  for (uint32_t i = 0; i < abilitySlotCount; i++) {
    const uint8_t *row = abilitySlots + i * 8u;
    SpeciesId species = rd16(row);
    AbilityKey slotOne = rd16(row + 2), slotTwo = rd16(row + 4);
    AbilityKey hidden = rd16(row + 6);
    if (species != touched[i] || !slotOne || !abilityValid(slotOne) ||
        (slotTwo && !abilityValid(slotTwo)) || (hidden && !abilityValid(hidden))) {
      rollback(); free(touched); free(abilitySlots); free(names); return false;
    }
    gDex[species].abilities[0] = slotOne;
    gDex[species].abilities[1] = slotTwo;
    gDex[species].abilities[2] = hidden;
  }
  free(abilitySlots);

  uint32_t breedingSize = 0, breedingCount = 0;
  uint32_t eggMoveSize = 0, eggMoveCount = 0;
  uint8_t *breedingRows = readSection(
      pack, "BRSP", &breedingSize, &breedingCount);
  uint8_t *rawEggMoves = readSection(
      pack, "BEMV", &eggMoveSize, &eggMoveCount);
  eggMoves = eggMoveCount
      ? (MoveId *)contentAlloc(sizeof(MoveId) * eggMoveCount) : nullptr;
  bool breedingValid = breedingRows && rawEggMoves &&
      breedingCount == specCount && breedingSize == specCount * 14u &&
      eggMoveSize == eggMoveCount * 2u && (!eggMoveCount || eggMoves);
  for (uint32_t i = 0; breedingValid && i < breedingCount; i++) {
    const uint8_t *row = breedingRows + i * 14u;
    SpeciesId species = rd16(row);
    if (species != touched[i] || species >= gDexCapacity) {
      breedingValid = false;
      break;
    }
    BreedingRuntime &entry = gBreeding[species];
    entry.groups = rd16(row + 2);
    entry.offspring[0] = rd16(row + 4);
    entry.offspring[1] = rd16(row + 6);
    entry.eggMoveOffset = rd32(row + 8);
    entry.eggMoveCount = rd16(row + 12);
    breedingValid = entry.groups && !(entry.groups & 0x8000u) && entry.offspring[0] &&
        entry.offspring[0] <= CONTENT_MAX_SPECIES && entry.offspring[1] &&
        entry.offspring[1] <= CONTENT_MAX_SPECIES &&
        entry.eggMoveOffset <= eggMoveCount &&
        entry.eggMoveCount <= eggMoveCount - entry.eggMoveOffset;
    MoveId previous = MOVE_NONE;
    for (uint16_t j = 0; breedingValid && j < entry.eggMoveCount; j++) {
      MoveId move = rd16(rawEggMoves + (entry.eggMoveOffset + j) * 2u);
      eggMoves[entry.eggMoveOffset + j] = move;
      breedingValid = moveValid(move) && move > previous;
      previous = move;
    }
  }
  free(breedingRows);
  free(rawEggMoves);
  if (!breedingValid) {
    rollback(); free(touched); free(names); return false;
  }

  uint8_t *evolutions = nullptr;
  if (evolutionSection->count) {
    uint32_t evolutionSize = 0, evolutionCountValue = 0;
    evolutions = readSection(pack, "EVOS", &evolutionSize, &evolutionCountValue);
    if (!evolutions || evolutionSize != evolutionSection->size ||
        evolutionCountValue != evolutionSection->count) {
      rollback(); free(touched); free(evolutions); free(names); return false;
    }
    for (uint32_t i = 0; i < evolutionCountValue; i++) {
      SpeciesId source = rd16(evolutions + i * 4), target = rd16(evolutions + i * 4 + 2);
      if (!source || source > CONTENT_MAX_SPECIES || !target ||
          target > CONTENT_MAX_SPECIES || source == target ||
          gSprites[source].pack != runtimePack ||
          gDex[source].evolutionCount >= CONTENT_MAX_EVOLUTIONS) {
        rollback(); free(touched); free(evolutions); free(names); return false;
      }
      DexEntry &species = gDex[source];
      for (uint8_t j = 0; j < species.evolutionCount; j++)
        if (species.evolutions[j] == target) {
          rollback(); free(touched); free(evolutions); free(names); return false;
        }
      species.evolutions[species.evolutionCount++] = target;
    }
    free(evolutions);
  }

  uint32_t learnOffsetSize = 0, learnSize = 0;
  uint8_t *rawLearnOffsets = readSection(
      pack, "LOFS", &learnOffsetSize, &learnOffsetCount);
  uint8_t *rawLearnEntries = readSection(
      pack, "LERN", &learnSize, &learnEntryCount);
  learnOffsets = (uint32_t *)contentAlloc(sizeof(uint32_t) * learnOffsetCount);
  learnEntries = learnEntryCount
      ? (LearnEntry *)contentAlloc(sizeof(LearnEntry) * learnEntryCount) : nullptr;
  if (!rawLearnOffsets || !rawLearnEntries || !learnOffsets ||
      (learnEntryCount && !learnEntries)) {
    rollback(); free(touched); free(rawLearnOffsets); free(rawLearnEntries); free(names);
    return false;
  }
  for (uint32_t i = 0; i < learnOffsetCount; i++) {
    learnOffsets[i] = rd32(rawLearnOffsets + i * 4u);
    if ((i && learnOffsets[i] < learnOffsets[i - 1]) ||
        learnOffsets[i] > learnEntryCount) {
      rollback(); free(touched); free(rawLearnOffsets); free(rawLearnEntries); free(names);
      return false;
    }
  }
  if (learnOffsets[0] || learnOffsets[learnOffsetCount - 1] != learnEntryCount) {
    rollback(); free(touched); free(rawLearnOffsets); free(rawLearnEntries); free(names);
    return false;
  }
  for (uint32_t i = 0; i < learnEntryCount; i++) {
    LearnEntry &entry = learnEntries[i];
    entry.move = rd16(rawLearnEntries + i * 4u);
    entry.level = rawLearnEntries[i * 4u + 2u];
    entry.method = rawLearnEntries[i * 4u + 3u];
    if (!moveValid(entry.move) || entry.method > LM_EGG) {
      rollback(); free(touched); free(rawLearnOffsets); free(rawLearnEntries); free(names);
      return false;
    }
  }
  free(rawLearnOffsets); free(rawLearnEntries);

  uint32_t megaSize = 0, megaCount = 0;
  uint8_t *rawMega = readSection(pack, "MEGA", &megaSize, &megaCount);
  if (!rawMega || megaCount > gMegaFormCapacity - gMegaFormCount) {
    rollback(); free(touched); free(rawMega); free(names); return false;
  }
  SpeciesId previousMegaSpecies = SPECIES_NONE;
  MegaFormKind previousMegaForm = MEGA_FORM_NONE;
  for (uint32_t i = 0; i < megaCount; i++) {
    const uint8_t *row = rawMega + i * 14u;
    MegaFormEntry &form = gMegaForms[gMegaFormCount++];
    form.species = rd16(row);
    form.form = (MegaFormKind)row[2];
    form.type1 = row[3]; form.type2 = row[4];
    form.bAtk = row[5]; form.bDef = row[6]; form.bSpA = row[7];
    form.bSpD = row[8]; form.bSpe = row[9];
    form.ability = rd16(row + 10);
    form.learnsetSpecies = rd16(row + 12);
    if (!form.species || form.species >= gDexCapacity || form.form > MEGA_FORM_Z ||
        gSprites[form.species].pack != runtimePack ||
        (i && (form.species < previousMegaSpecies ||
               (form.species == previousMegaSpecies && form.form <= previousMegaForm))) ||
        form.type1 >= TYPE_COUNT || (form.type2 != T_NONE && form.type2 >= TYPE_COUNT) ||
        !form.bAtk || !form.bDef || !form.bSpA || !form.bSpD || !form.bSpe ||
        (form.ability && !abilityValid(form.ability)) || !dexValid(form.learnsetSpecies)) {
      rollback(); free(touched); free(rawMega); free(names); return false;
    }
    previousMegaSpecies = form.species;
    previousMegaForm = form.form;
  }
  free(rawMega);

  uint32_t gmaxSize = 0, gmaxCount = 0;
  uint8_t *rawGmax = readSection(pack, "GMAX", &gmaxSize, &gmaxCount);
  if (!rawGmax || gmaxCount > gGigantamaxCapacity - gGigantamaxCount) {
    rollback(); free(touched); free(rawGmax); free(names); return false;
  }
  SpeciesId previousGmax = SPECIES_NONE;
  for (uint32_t i = 0; i < gmaxCount; i++) {
    const uint8_t *row = rawGmax + i * 4u;
    SpeciesId species = rd16(row);
    GmaxMoveId firstMove = row[2], secondMove = row[3];
    if (!species || species >= gDexCapacity || gSprites[species].pack != runtimePack ||
        (i && species <= previousGmax) || !firstMove || firstMove > gGmaxMoveCount ||
        secondMove > gGmaxMoveCount ||
        (secondMove && (secondMove == firstMove ||
                        gGmaxMoves[secondMove - 1u].sourceType ==
                            gGmaxMoves[firstMove - 1u].sourceType))) {
      rollback(); free(touched); free(rawGmax); free(names); return false;
    }
    GigantamaxRuntime &entry = gGigantamaxSpecies[gGigantamaxCount++];
    entry.species = species;
    entry.moves[0] = firstMove;
    entry.moves[1] = secondMove;
    previousGmax = species;
  }
  free(rawGmax);

  const SectionRef *spriteBlob = findSection(pack, "SBLB");
  uint32_t spriteSize = 0, spriteCount = 0;
  uint8_t *spriteIndex = readSection(pack, "SPRI", &spriteSize, &spriteCount);
  if (!spriteBlob || !spriteIndex || spriteCount != specCount ||
      spriteSize != spriteCount * 35u) {
    rollback(); free(touched); free(spriteIndex); free(names); return false;
  }
  for (uint32_t i = 0; i < spriteCount; i++) {
    const uint8_t *row = spriteIndex + i * 35;
    SpeciesId id = rd16(row);
    if (!dexValid(id)) {
      rollback(); free(touched); free(spriteIndex); free(names); return false;
    }
    SpriteRef &sprite = gSprites[id];
    sprite.normalAt = spriteBlob->offset + rd32(row + 2); sprite.normalSize = rd32(row + 6);
    sprite.shinyAt = spriteBlob->offset + rd32(row + 10); sprite.shinySize = rd32(row + 14);
    sprite.femaleAt = spriteBlob->offset + rd32(row + 18); sprite.femaleSize = rd32(row + 22);
    sprite.femaleShinyAt = spriteBlob->offset + rd32(row + 26);
    sprite.femaleShinySize = rd32(row + 30);
    sprite.displayScale = row[34];
    uint32_t normalOffset = rd32(row + 2), shinyOffset = rd32(row + 10);
    uint32_t femaleOffset = rd32(row + 18), femaleShinyOffset = rd32(row + 26);
    if ((sprite.normalSize && (sprite.displayScale < 2 || sprite.displayScale > 6)) ||
        (!sprite.normalSize && sprite.displayScale) ||
        normalOffset > spriteBlob->size || sprite.normalSize > spriteBlob->size - normalOffset ||
        shinyOffset > spriteBlob->size || sprite.shinySize > spriteBlob->size - shinyOffset ||
        femaleOffset > spriteBlob->size || sprite.femaleSize > spriteBlob->size - femaleOffset ||
        femaleShinyOffset > spriteBlob->size ||
        sprite.femaleShinySize > spriteBlob->size - femaleShinyOffset) {
      rollback(); free(touched); free(spriteIndex); free(names); return false;
    }
  }
  free(spriteIndex);

  const SectionRef *megaSpriteBlob = findSection(pack, "MFBL");
  const SectionRef *megaSpriteSection = findSection(pack, "MFSP");
  uint8_t *megaIndex = nullptr;
  uint32_t megaIndexCount = 0;
  if ((megaSpriteBlob == nullptr) != (megaSpriteSection == nullptr)) {
    rollback(); free(touched); free(names); return false;
  }
  if (megaSpriteSection) {
    uint32_t megaIndexSize = 0;
    megaIndex = readSection(pack, "MFSP", &megaIndexSize, &megaIndexCount);
    if (!megaIndex || megaIndexSize != megaIndexCount * 20u) {
      rollback(); free(touched); free(megaIndex); free(names); return false;
    }
    for (uint32_t i = 0; i < megaIndexCount; i++) {
      const uint8_t *row = megaIndex + i * 20u;
      SpeciesId species = rd16(row);
      MegaFormKind kind = (MegaFormKind)row[2];
      uint8_t displayScale = row[3];
      uint32_t normalAt = rd32(row + 4), normalSize = rd32(row + 8);
      uint32_t shinyAt = rd32(row + 12), shinySize = rd32(row + 16);
      MegaFormEntry *form = nullptr;
      for (uint16_t j = 0; j < gMegaFormCount; j++)
        if (gMegaForms[j].species == species && gMegaForms[j].form == kind) {
          form = &gMegaForms[j]; break;
        }
      if (!form || displayScale < 2 || displayScale > 6 || !normalSize ||
          normalAt > megaSpriteBlob->size || normalSize > megaSpriteBlob->size - normalAt ||
          shinyAt > megaSpriteBlob->size || shinySize > megaSpriteBlob->size - shinyAt) {
        rollback(); free(touched); free(megaIndex); free(names); return false;
      }
    }
  }

  if (packRegion == 0xFF || packRegion != metadataRegion ||
      !loadRegionBattleData(pack, packRegion)) {
    rollback(); free(touched); free(megaIndex); free(names);
    return false;
  }
  for (uint32_t i = 0; i < megaIndexCount; i++) {
    const uint8_t *row = megaIndex + i * 20u;
    for (uint16_t j = 0; j < gMegaFormCount; j++) {
      MegaFormEntry &form = gMegaForms[j];
      if (form.species != rd16(row) || form.form != (MegaFormKind)row[2]) continue;
      form.spritePack = packIndex;
      form.spriteScale = row[3];
      form.spriteAt = megaSpriteBlob->offset + rd32(row + 4);
      form.spriteSize = rd32(row + 8);
      form.shinySpriteAt = megaSpriteBlob->offset + rd32(row + 12);
      form.shinySpriteSize = rd32(row + 16);
      break;
    }
  }
  free(megaIndex);
  RegionPackRuntime &runtime = gRegionPacks[gRegionPackCount++];
  runtime.packRef = packIndex; runtime.firstSpecies = firstSpecies;
  runtime.speciesCount = (uint16_t)specCount;
  runtime.learnOffsets = learnOffsets;
  runtime.learnOffsetCount = learnOffsetCount;
  runtime.learnEntries = learnEntries;
  runtime.learnEntryCount = learnEntryCount;
  runtime.eggMoves = eggMoves;
  runtime.eggMoveCount = eggMoveCount;
  runtime.locales = readSection(pack, "LOCL", &runtime.localesSize);
  runtime.localizedNames = readSection(pack, "LNAM", &runtime.localizedNamesSize);
  uint32_t regionalNameCount = 0;
  gRegionalNames[packRegion] = readSection(
      pack, "RLNM", &gRegionalNamesSize[packRegion], &regionalNameCount);
  if (!gRegionalNames[packRegion] ||
      regionalNameCount != 1u + (uint32_t)gRegionBattles[packRegion].trainerCount * 2u) {
    free(gRegionalNames[packRegion]);
    gRegionalNames[packRegion] = nullptr;
    gRegionalNamesSize[packRegion] = 0;
  }
  gSpeciesNames[runtimePack] = (char *)names;
  gRegionsReady = true;
  gPacks[packIndex].loaded = true;
  free(touched);
  return true;
}

static bool loadUiMeta(uint8_t packIndex) {
  if (gUiCount >= CONTENT_MAX_UI_LOCALES) return false;
  if (!validateUiPayload(gPacks[packIndex])) return false;
  uint32_t size = 0, count = 0;
  uint8_t *meta = readSection(gPacks[packIndex], "META", &size, &count);
  if (!meta || size != 60 || count != 1 || !meta[0] || !memchr(meta, 0, 16) ||
      !meta[16] || !memchr(meta + 16, 0, 8) || !meta[24] || !memchr(meta + 24, 0, 32)) {
    free(meta); return false;
  }
  for (uint8_t i = 0; i < gUiCount; i++)
    if (strncmp(gUi[i].info.locale, (const char *)meta, 16) == 0) {
      free(meta); return false;
    }
  UiRuntime &ui = gUi[gUiCount++];
  memcpy(ui.info.locale, meta, 16); ui.info.locale[15] = 0;
  memcpy(ui.info.shortLabel, meta + 16, 8); ui.info.shortLabel[7] = 0;
  memcpy(ui.info.displayName, meta + 24, 32); ui.info.displayName[31] = 0;
  ui.info.isDefault = meta[56] != 0; ui.info.isCjk = meta[57] != 0;
  ui.packRef = packIndex;
  gPacks[packIndex].loaded = true;
  free(meta);
  return true;
}

static bool parseQuizMeta(const PackRef &pack, QuizPackRuntime &runtime) {
  const SectionRef *locales = findSection(pack, "QLOC");
  const SectionRef *index = findSection(pack, "QIDX");
  const SectionRef *data = findSection(pack, "QDAT");
  if (!locales || !index || !data || !locales->count ||
      locales->count > MAX_QUIZ_LOCALES ||
      locales->size != locales->count * 24u ||
      !index->count || index->size != index->count * 12u ||
      data->count != index->count || !data->size) return false;
  uint8_t *raw = readSection(pack, "QLOC");
  if (!raw) return false;
  memset(&runtime, 0, sizeof(runtime));
  runtime.packRef = 0xFF;
  runtime.localeCount = (uint8_t)locales->count;
  runtime.questionCount = index->count;
  uint32_t covered = 0;
  bool valid = true;
  for (uint8_t i = 0; i < runtime.localeCount; i++) {
    const uint8_t *row = raw + (uint32_t)i * 24u;
    QuizLocaleSpan &span = runtime.locales[i];
    memcpy(span.locale, row, 16);
    span.locale[15] = 0;
    span.first = rd32(row + 16);
    span.count = rd32(row + 20);
    if (!span.locale[0] || !memchr(row, 0, 16) || !span.count ||
        span.first != covered || span.count > runtime.questionCount - covered) {
      valid = false;
      break;
    }
    for (uint8_t previous = 0; previous < i; previous++)
      if (strncmp(runtime.locales[previous].locale, span.locale, 16) == 0) valid = false;
    if (!valid) break;
    covered += span.count;
  }
  free(raw);
  return valid && covered == runtime.questionCount;
}

static bool loadQuizMeta(uint8_t packIndex) {
  if (gQuizPackCount >= gQuizPackCapacity) return false;
  QuizPackRuntime runtime;
  if (!parseQuizMeta(gPacks[packIndex], runtime)) return false;
  runtime.packRef = packIndex;
  gQuizPacks[gQuizPackCount++] = runtime;
  gPacks[packIndex].loaded = true;
  return true;
}

static void buildAllRegion() {
  if (!gRealRegionCount || gRealRegionCount >= CONTENT_MAX_REGIONS) return;
  uint8_t all = gRealRegionCount;
  snprintf(gRegionNames[all], sizeof(gRegionNames[all]), "ALL");
  RegionInfo &region = gRegions[all];
  region.name = gRegionNames[all]; region.lo = UINT16_MAX; region.hi = 0;
  region.starters = gRegionStarters[all]; region.starterCount = 0;
  for (uint8_t i = 0; i < gRealRegionCount; i++) {
    const RegionInfo &part = gRegions[i];
    if (!part.name || !part.lo) continue;
    if (part.lo < region.lo) region.lo = part.lo;
    if (part.hi > region.hi) region.hi = part.hi;
    for (uint8_t j = 0; j < part.starterCount && region.starterCount < MAX_STARTERS * 2; j++)
      gRegionStarters[all][region.starterCount++] = part.starters[j];
  }
  if (region.lo == UINT16_MAX) region.lo = 0;
}

static void ensureContent() {
  if (!gAttempted) contentBegin();
}

}  // namespace

bool contentBegin() {
  if (gAttempted) return contentReady();
  gAttempted = true;
  gPacks = (PackRef *)contentAlloc(sizeof(PackRef) * MAX_PACKS);
  if (!gPacks) return false;
  scanPacks();
  uint16_t installedMaximum = installedSpeciesMaximum();
  if (installedMaximum && !allocateSpeciesCatalog(installedMaximum)) return false;
  installedFormCounts(gMegaFormCapacity, gGigantamaxCapacity);
  if (gMegaFormCapacity) {
    gMegaForms = (MegaFormEntry *)contentAlloc(
        sizeof(MegaFormEntry) * gMegaFormCapacity);
    if (!gMegaForms) return false;
  }
  if (gGigantamaxCapacity) {
    gGigantamaxSpecies = (GigantamaxRuntime *)contentAlloc(
        sizeof(GigantamaxRuntime) * gGigantamaxCapacity);
    if (!gGigantamaxSpecies) return false;
  }
  for (uint8_t i = 0; i < gPackCount; i++)
    if (gPacks[i].kind == CONTENT_PACK_QUIZ) gQuizPackCapacity++;
  if (gQuizPackCapacity) {
    gQuizPacks = (QuizPackRuntime *)contentAlloc(
        sizeof(QuizPackRuntime) * gQuizPackCapacity);
    if (!gQuizPacks) return false;
  }
  for (uint8_t i = 0; i < gPackCount; i++)
    if (gPacks[i].kind == CONTENT_PACK_BATTLE && !gBattle) loadBattlePack(i);
  for (uint8_t i = 0; i < gPackCount; i++)
    if (gPacks[i].kind == CONTENT_PACK_MOVE && !gMoves) loadMovePack(i);
  for (uint8_t i = 0; i < gPackCount; i++)
    if (gPacks[i].kind == CONTENT_PACK_ITEM && !gItemsReady) loadItemPack(i);
  for (uint8_t i = 0; i < gPackCount; i++)
    if (gPacks[i].kind == CONTENT_PACK_REGION) loadRegionPack(i);
  for (uint8_t i = 0; i < gPackCount; i++)
    if (gPacks[i].kind == CONTENT_PACK_UI) loadUiMeta(i);
  for (uint8_t i = 0; i < gPackCount; i++)
    if (gPacks[i].kind == CONTENT_PACK_QUIZ) loadQuizMeta(i);
  buildAllRegion();

  for (uint8_t i = 0; i < gPackCount; i++) {
    if (gPacks[i].kind == CONTENT_PACK_UI || gPacks[i].kind == CONTENT_PACK_QUIZ ||
        !gPacks[i].loaded) continue;
    gMechanicsHash ^= gPacks[i].mechanicsHash;
    gMechanicsHash *= 16777619u;
  }
  int8_t initial = -1;
  for (uint8_t i = 0; i < gUiCount; i++) if (gUi[i].info.isDefault) initial = i;
  if (initial < 0 && gUiCount) initial = 0;
  if (initial >= 0 && !uiActivateLocale((uint8_t)initial)) initial = -1;
  if (initial < 0)
    for (uint8_t i = 0; i < gUiCount && gUiActive >= gUiCount; i++) uiActivateLocale(i);
  Serial.printf("packs: %u ui=%u region=%u battle=%u moves=%u items=%u dex=%u\n",
                gPackCount, gUiCount, gRegionPackCount, gBattle ? 1 : 0,
                gMoves ? 1 : 0, gItemsReady ? 1 : 0, gDexCount);
  return contentReady();
}

ContentPackValidation contentValidatePackFile(const char *path) {
  PackRef parsed;
  ContentPackValidation validation = CONTENT_PACK_OPEN_FAILED;
  if (parsePack(path, parsed, PackParseMode::VERIFY_PAYLOAD, &validation) &&
      parsed.kind == CONTENT_PACK_QUIZ) {
    QuizPackRuntime runtime;
    if (!parseQuizMeta(parsed, runtime)) validation = CONTENT_PACK_DIRECTORY_INVALID;
  }
  return validation;
}

bool contentReadPackInfo(const char *path, ContentPackInfo &out) {
  Reader reader;
  uint8_t common[COMMON_SIZE];
  if (!readPackHeader(path, reader, common)) return false;
  uint16_t headerSize = rd16(common + 24);
  uint16_t directorySize = headerSize - COMMON_SIZE;
  uint8_t directory[MAX_SECTIONS * SECTION_SIZE];
  if (!reader.readAt(COMMON_SIZE, directory, directorySize)) return false;
  memcpy(out.id, common + 28, 20);
  out.id[20] = 0;
  out.revision = rd32(common + 16);
  // GLUE: Mirror pack_content_version's wire-format identity until a future pack
  // ABI stores it directly. The common header already carries the payload CRC.
  out.contentVersion = crcStep(0, common, 16);
  out.contentVersion = crcStep(out.contentVersion, common + 20, COMMON_SIZE - 20);
  out.contentVersion = crcStep(out.contentVersion, directory, directorySize);
  return out.id[0] != 0;
}

bool contentReady() {
  return contentHasUi() && contentHasPets() && contentHasMoves() &&
         contentHasBreeding() && gItemsReady && gBattle;
}
bool contentHasUi() { ensureContent(); return gUiActive < gUiCount; }
bool contentHasPets() { ensureContent(); return gRegionsReady; }
bool contentHasMoves() { ensureContent(); return gMoves; }
bool contentHasBreeding() { ensureContent(); return gRegionsReady && gBreeding; }
uint32_t contentMechanicsHash() { ensureContent(); return gMechanicsHash; }

uint32_t contentChoiceQuestionCount(const char *locale) {
  ensureContent();
  if (!locale || !*locale) return 0;
  uint32_t total = 0;
  for (uint8_t packIndex = 0; packIndex < gQuizPackCount; packIndex++) {
    const QuizPackRuntime &pack = gQuizPacks[packIndex];
    for (uint8_t localeIndex = 0; localeIndex < pack.localeCount; localeIndex++)
      if (strncmp(pack.locales[localeIndex].locale, locale, 16) == 0) {
        total += pack.locales[localeIndex].count;
        break;
      }
  }
  return total;
}

static bool readChoiceQuestion(const QuizPackRuntime &runtime, uint32_t index,
                               ContentChoiceQuestion &out) {
  if (runtime.packRef >= gPackCount || index >= runtime.questionCount) return false;
  const PackRef &pack = gPacks[runtime.packRef];
  const SectionRef *indexSection = findSection(pack, "QIDX");
  const SectionRef *dataSection = findSection(pack, "QDAT");
  if (!indexSection || !dataSection) return false;
  uint8_t indexRow[12];
  if (!readRange(pack, indexSection->offset + index * sizeof(indexRow),
                 indexRow, sizeof(indexRow))) return false;
  uint32_t idHash = rd32(indexRow), offset = rd32(indexRow + 4), size = rd32(indexRow + 8);
  constexpr uint32_t RECORD_HEADER = 14;
  constexpr uint32_t RECORD_MAX = RECORD_HEADER + CONTENT_MAX_QUESTION_ID_BYTES +
      CONTENT_MAX_QUESTION_STEM_BYTES +
      CONTENT_MAX_QUIZ_OPTIONS * CONTENT_MAX_QUESTION_OPTION_BYTES;
  if (size < RECORD_HEADER || size > RECORD_MAX || offset > dataSection->size ||
      size > dataSection->size - offset) return false;
  uint8_t *record = (uint8_t *)contentAlloc(size);
  if (!record || !readRange(pack, dataSection->offset + offset, record, size)) {
    free(record); return false;
  }
  uint8_t optionCount = record[0], correct = record[1];
  uint16_t idSize = rd16(record + 2), stemSize = rd16(record + 4);
  uint16_t optionSize[CONTENT_MAX_QUIZ_OPTIONS];
  uint32_t total = RECORD_HEADER + idSize + stemSize;
  for (uint8_t i = 0; i < CONTENT_MAX_QUIZ_OPTIONS; i++) {
    optionSize[i] = rd16(record + 6 + i * 2);
    total += optionSize[i];
  }
  bool valid = optionCount >= 2 && optionCount <= CONTENT_MAX_QUIZ_OPTIONS &&
               correct < optionCount && idSize && idSize <= CONTENT_MAX_QUESTION_ID_BYTES &&
               stemSize && stemSize <= CONTENT_MAX_QUESTION_STEM_BYTES && total == size;
  for (uint8_t i = 0; i < CONTENT_MAX_QUIZ_OPTIONS; i++)
    if ((i < optionCount && (!optionSize[i] || optionSize[i] > CONTENT_MAX_QUESTION_OPTION_BYTES)) ||
        (i >= optionCount && optionSize[i])) valid = false;
  uint32_t cursor = RECORD_HEADER;
  if (valid && (memchr(record + cursor, 0, idSize) ||
                memchr(record + cursor + idSize, 0, stemSize))) valid = false;
  if (valid) {
    memset(&out, 0, sizeof(out));
    out.idHash = idHash;
    memcpy(out.id, record + cursor, idSize); cursor += idSize;
    memcpy(out.stem, record + cursor, stemSize); cursor += stemSize;
    out.optionCount = optionCount;
    out.correctIndex = correct;
    for (uint8_t i = 0; i < optionCount; i++) {
      if (memchr(record + cursor, 0, optionSize[i])) { valid = false; break; }
      memcpy(out.options[i], record + cursor, optionSize[i]);
      cursor += optionSize[i];
    }
  }
  free(record);
  return valid;
}

bool contentChoiceQuestionAt(const char *locale, uint32_t index,
                             ContentChoiceQuestion &out) {
  ensureContent();
  if (!locale || !*locale) return false;
  for (uint8_t packIndex = 0; packIndex < gQuizPackCount; packIndex++) {
    const QuizPackRuntime &pack = gQuizPacks[packIndex];
    for (uint8_t localeIndex = 0; localeIndex < pack.localeCount; localeIndex++) {
      const QuizLocaleSpan &span = pack.locales[localeIndex];
      if (strncmp(span.locale, locale, 16) != 0) continue;
      if (index < span.count) return readChoiceQuestion(pack, span.first + index, out);
      index -= span.count;
      break;
    }
  }
  return false;
}

uint16_t dexCount() { ensureContent(); return gDexCount; }
bool dexValid(SpeciesId id) {
  ensureContent();
  return id && id < gDexCapacity && gDex && gDex[id].name;
}
const DexEntry &dexEntry(SpeciesId id) { return dexValid(id) ? gDex[id] : MISSING_SPECIES; }
uint8_t evolutionCount(SpeciesId id) { return dexValid(id) ? gDex[id].evolutionCount : 0; }
SpeciesId evolutionTarget(SpeciesId id, uint8_t index) {
  return index < evolutionCount(id) ? gDex[id].evolutions[index] : SPECIES_NONE;
}
bool evolutionAvailable(SpeciesId id) {
  for (uint8_t i = 0; i < evolutionCount(id); i++)
    if (dexValid(evolutionTarget(id, i))) return true;
  return false;
}

uint16_t speciesEggGroups(SpeciesId species) {
  ensureContent();
  return dexValid(species) && gBreeding ? gBreeding[species].groups : 0;
}
uint8_t speciesOffspringCount(SpeciesId species) {
  ensureContent();
  if (!dexValid(species) || !gBreeding || !gBreeding[species].offspring[0]) return 0;
  return gBreeding[species].offspring[0] == gBreeding[species].offspring[1] ? 1 : 2;
}
SpeciesId speciesOffspring(SpeciesId species, uint8_t index) {
  uint8_t count = speciesOffspringCount(species);
  return index < count ? gBreeding[species].offspring[index] : SPECIES_NONE;
}
bool speciesHasEggMove(SpeciesId species, MoveId move) {
  ensureContent();
  if (!moveValid(move) || !dexValid(species) || !gBreeding) return false;
  const SpriteRef &sprite = gSprites[species];
  if (sprite.pack >= gRegionPackCount) return false;
  const RegionPackRuntime &runtime = gRegionPacks[sprite.pack];
  const BreedingRuntime &entry = gBreeding[species];
  uint32_t lo = entry.eggMoveOffset;
  uint32_t hi = lo + entry.eggMoveCount;
  if (hi > runtime.eggMoveCount || (!runtime.eggMoves && lo != hi)) return false;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2u;
    if (runtime.eggMoves[mid] < move) lo = mid + 1u; else hi = mid;
  }
  return lo < entry.eggMoveOffset + entry.eggMoveCount &&
         runtime.eggMoves[lo] == move;
}

uint8_t regionCount() { ensureContent(); return gRealRegionCount ? gRealRegionCount + 1 : 0; }
uint8_t regionAll() { ensureContent(); return gRealRegionCount; }
const RegionInfo &regionInfo(uint8_t index) {
  ensureContent(); return index < regionCount() ? gRegions[index] : MISSING_REGION;
}
const char *regionName(uint8_t index) {
  ensureContent();
  const char *fallback = regionInfo(index).name;
  if (index >= gRealRegionCount || !gRegionalNames[index]) return fallback;
  const char *localized = localizedAt(gRegionalNames[index], gRegionalNamesSize[index],
                                      uiActiveLocaleCode(), 0);
  return localized && *localized ? localized : fallback;
}
bool regionPackAvailable(uint8_t index) {
  ensureContent();
  if (index == regionAll()) return gRegionMask != 0;
  return index < 16 && (gRegionMask & (uint16_t)(1u << index));
}

uint16_t moveCount() { ensureContent(); return gMoveCount; }
bool moveValid(MoveId id) {
  ensureContent();
  return id != MOVE_NONE && id < gMoveCount && gMovesTable && gMovesTable[id].name;
}
const MoveEntry &moveEntry(MoveId id) { return moveValid(id) ? gMovesTable[id] : MISSING_MOVE; }
bool abilitySlotValid(AbilitySlot slot) {
  return slot >= ABILITY_SLOT_ONE && slot <= ABILITY_SLOT_HIDDEN;
}
uint16_t abilityCount() { ensureContent(); return gAbilityCount; }
static int32_t abilityIndex(AbilityKey key) {
  uint16_t lo = 0, hi = gAbilityCount;
  while (lo < hi) {
    uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
    if (gAbilities[mid].key < key) lo = (uint16_t)(mid + 1u); else hi = mid;
  }
  return lo < gAbilityCount && gAbilities[lo].key == key ? (int32_t)lo : -1;
}
bool abilityValid(AbilityKey key) {
  ensureContent();
  return key && gAbilities && abilityIndex(key) >= 0;
}
const char *abilityName(AbilityKey key) {
  ensureContent();
  int32_t index = gAbilities ? abilityIndex(key) : -1;
  if (index < 0) return "?";
  const char *localized = gUiActive < gUiCount
      ? localizedAt(gAbilityLocalizedNames, gAbilityLocalizedNamesSize,
                    uiActiveLocaleCode(), (uint32_t)index)
      : nullptr;
  return localized && *localized ? localized : gAbilities[index].name;
}
const char *abilityDescription(AbilityKey key, const char *locale) {
  ensureContent();
  int32_t index = gAbilities ? abilityIndex(key) : -1;
  if (index < 0) return nullptr;
  const char *localized = localizedAt(
      gAbilityLocales, gAbilityLocalesSize, locale, (uint32_t)index);
  return localized ? localized : localizedAt(
      gAbilityLocales, gAbilityLocalesSize, "en-US", (uint32_t)index);
}
AbilityKey speciesAbility(SpeciesId species, AbilitySlot slot) {
  if (!dexValid(species) || !abilitySlotValid(slot)) return ABILITY_NONE;
  return gDex[species].abilities[(uint8_t)slot - 1u];
}
AbilitySlot speciesNormalAbilitySlot(SpeciesId species, uint32_t stableRoll) {
  if (!speciesAbility(species, ABILITY_SLOT_ONE)) return ABILITY_SLOT_UNKNOWN;
  return speciesAbility(species, ABILITY_SLOT_TWO) && (stableRoll & 1u)
      ? ABILITY_SLOT_TWO : ABILITY_SLOT_ONE;
}
AbilitySlot abilitySlotForLegacy(SpeciesId species, uint8_t ivAtk, uint8_t ivDef,
                                 uint8_t ivSpe, uint8_t ivHp) {
  uint32_t hash = 2166136261u;
  const uint8_t bytes[] = {
    (uint8_t)species, (uint8_t)(species >> 8), ivAtk, ivDef, ivSpe, ivHp,
  };
  for (uint8_t value : bytes) {
    hash ^= value;
    hash *= 16777619u;
  }
  return speciesNormalAbilitySlot(species, hash);
}
uint16_t itemCount() { ensureContent(); return gItemCount; }
const ItemEntry *itemAt(uint16_t index) {
  ensureContent(); return index < gItemCount ? &gItems[index] : nullptr;
}
const ItemEntry *itemByKey(ItemKey key) {
  ensureContent();
  for (uint16_t i = 0; i < gItemCount; i++) if (gItems[i].key == key) return &gItems[i];
  return nullptr;
}
bool contentItemIcon(ItemKey key, ItemIconView &out) {
  out = ItemIconView{};
  const ItemEntry *item = itemByKey(key);
  if (!item || !gItemIcons || gItemIconsSize < 4) return false;
  uint32_t index = (uint32_t)(item - gItems);
  const uint8_t *row = gItemIcons + 4u + index * 8u;
  uint32_t offset = rd32(row), length = rd32(row + 4);
  if (!offset || !length || offset > gItemIconsSize || length > gItemIconsSize - offset)
    return false;
  const uint8_t *icon = gItemIcons + offset;
  out.width = icon[4]; out.height = icon[5]; out.paletteCount = icon[6];
  out.palette565 = icon + 8;
  out.pixels = out.palette565 + (uint32_t)out.paletteCount * 2u;
  return true;
}

const MegaFormEntry *megaFormFor(SpeciesId species, MegaFormKind form) {
  ensureContent();
  if (form == MEGA_FORM_NONE) {
    const MegaFormEntry *fallback = nullptr;
    for (uint16_t i = 0; i < gMegaFormCount; i++) {
      if (gMegaForms[i].species != species) continue;
      if (gMegaForms[i].form == MEGA_FORM_STANDARD) return &gMegaForms[i];
      if (!fallback) fallback = &gMegaForms[i];
    }
    return fallback;
  }
  for (uint16_t i = 0; i < gMegaFormCount; i++)
    if (gMegaForms[i].species == species && gMegaForms[i].form == form)
      return &gMegaForms[i];
  return nullptr;
}

bool contentGigantamaxEligible(SpeciesId species) {
  ensureContent();
  for (uint16_t i = 0; i < gGigantamaxCount; i++)
    if (gGigantamaxSpecies[i].species == species) return true;
  return false;
}

const GmaxMoveEntry *gmaxMoveFor(SpeciesId species, uint8_t sourceType) {
  ensureContent();
  for (uint16_t i = 0; i < gGigantamaxCount; i++) {
    const GigantamaxRuntime &entry = gGigantamaxSpecies[i];
    if (entry.species != species) continue;
    for (GmaxMoveId move : entry.moves)
      if (move && move <= gGmaxMoveCount &&
          gGmaxMoves[move - 1u].sourceType == sourceType)
        return &gGmaxMoves[move - 1u];
    return nullptr;
  }
  return nullptr;
}

const char *gmaxMoveName(GmaxMoveId move) {
  ensureContent();
  if (!move || move > gGmaxMoveCount) return "?";
  const GmaxMoveEntry &entry = gGmaxMoves[move - 1u];
  const char *localized = gUiActive < gUiCount
      ? localizedAt(gGmaxMoveLocalizedNames, gGmaxMoveLocalizedNamesSize,
                    uiActiveLocaleCode(), move - 1u)
      : nullptr;
  return localized && *localized ? localized : entry.name;
}

const char *maxMoveName(uint8_t sourceType, bool status) {
  ensureContent();
  uint8_t index = status ? TYPE_COUNT : sourceType;
  if (index > TYPE_COUNT || !gMaxMoveNames) return nullptr;
  const char *localized = gUiActive < gUiCount
      ? localizedAt(gMaxMoveLocalizedNames, gMaxMoveLocalizedNamesSize,
                    uiActiveLocaleCode(), index)
      : nullptr;
  return localized && *localized
      ? localized : gMaxMoveNames + gMaxMoveNameOffsets[index];
}
uint16_t learnCount(SpeciesId species) {
  ensureContent();
  if (!dexValid(species)) return 0;
  const SpriteRef &sprite = gSprites[species];
  if (sprite.pack >= gRegionPackCount) return 0;
  const RegionPackRuntime &runtime = gRegionPacks[sprite.pack];
  uint32_t local = sprite.localIndex;
  if (local + 1u >= runtime.learnOffsetCount) return 0;
  uint32_t first = runtime.learnOffsets[local], last = runtime.learnOffsets[local + 1u];
  return last >= first && last <= runtime.learnEntryCount ? (uint16_t)(last - first) : 0;
}
MoveId learnMove(SpeciesId species, uint16_t index) {
  if (index >= learnCount(species)) return MOVE_NONE;
  const SpriteRef &sprite = gSprites[species];
  const RegionPackRuntime &runtime = gRegionPacks[sprite.pack];
  return runtime.learnEntries[runtime.learnOffsets[sprite.localIndex] + index].move;
}
uint8_t learnLevel(SpeciesId species, uint16_t index) {
  if (index >= learnCount(species)) return 0;
  const SpriteRef &sprite = gSprites[species];
  const RegionPackRuntime &runtime = gRegionPacks[sprite.pack];
  return runtime.learnEntries[runtime.learnOffsets[sprite.localIndex] + index].level;
}
LearnMethod learnMethod(SpeciesId species, uint16_t index) {
  if (index >= learnCount(species)) return LM_LEVEL_UP;
  const SpriteRef &sprite = gSprites[species];
  const RegionPackRuntime &runtime = gRegionPacks[sprite.pack];
  return (LearnMethod)runtime.learnEntries[
      runtime.learnOffsets[sprite.localIndex] + index].method;
}

bool speciesCanLearnMove(SpeciesId species, MoveId move) {
  if (!moveValid(move)) return false;
  uint16_t count = learnCount(species);
  for (uint16_t i = 0; i < count; i++)
    if (learnMove(species, i) == move) return true;
  return false;
}

uint8_t uiLocaleCount() { ensureContent(); return gUiCount; }
const UiLocaleInfo &uiLocaleInfo(uint8_t index) {
  ensureContent(); return index < gUiCount ? gUi[index].info : MISSING_LOCALE;
}
int8_t uiFindLocale(const char *locale) {
  ensureContent();
  if (!locale) return -1;
  for (uint8_t i = 0; i < gUiCount; i++)
    if (strncmp(gUi[i].info.locale, locale, sizeof(gUi[i].info.locale)) == 0) return (int8_t)i;
  return -1;
}
bool uiActivateLocale(uint8_t index) {
  ensureContent();
  if (index >= gUiCount) return false;
  const PackRef &pack = gPacks[gUi[index].packRef];
  uint32_t stringsSize = 0, stringsCount = 0, fontSize = 0, layoutSize = 0;
  uint8_t *strings = readSection(pack, "STRS", &stringsSize, &stringsCount);
  const SectionRef *fontSection = findSection(pack, "FONT");
  uint8_t prefix[4] = {};
  uint8_t *font = nullptr;
  if (fontSection && fontSection->size >= 8 &&
      readRange(pack, fontSection->offset, prefix, sizeof(prefix))) {
    if (!memcmp(prefix, "FNT5", 4) && fontSection->size >= UI_VECTOR_FONT_HEADER_SIZE) {
      fontSize = UI_VECTOR_FONT_HEADER_SIZE;
      font = (uint8_t *)contentAlloc(fontSize);
      if (font && !readRange(pack, fontSection->offset, font, fontSize)) {
        free(font); font = nullptr;
      }
    } else {
      font = readSection(pack, "FONT", &fontSize);
    }
  }
  uint8_t *layout = readSection(pack, "LAYT", &layoutSize);
  if (!strings || stringsSize < 6 || rd16(strings) != stringsCount || !font || fontSize < 8) {
    free(strings); free(font); free(layout); return false;
  }
  free(gUiStrings); free(gUiFont); free(gUiLayout);
  gUiStrings = strings; gUiStringsSize = stringsSize;
  gUiFont = font; gUiFontSize = fontSize;
  gUiLayout = layout; gUiLayoutSize = layoutSize;
  gUiActive = index;
  return true;
}
uint8_t uiActiveLocale() { ensureContent(); return gUiActive; }
const char *uiActiveLocaleCode() {
  ensureContent(); return gUiActive < gUiCount ? gUi[gUiActive].info.locale : "";
}
const char *uiString(uint16_t id) {
  ensureContent();
  if (!gUiStrings || gUiStringsSize < 6) return "?";
  uint16_t count = rd16(gUiStrings);
  uint32_t tableSize = 2u + (uint32_t)(count + 1) * 4u;
  if (id >= count || tableSize > gUiStringsSize) return "?";
  uint32_t offset = rd32(gUiStrings + 2 + id * 4);
  uint32_t end = rd32(gUiStrings + 2 + (id + 1) * 4);
  uint32_t blobSize = gUiStringsSize - tableSize;
  if (offset > end || end > blobSize || !validString(gUiStrings + tableSize, blobSize, offset)) return "?";
  return (const char *)(gUiStrings + tableSize + offset);
}
int16_t uiLayoutMetric(uint16_t id, int16_t fallback) {
  ensureContent();
  for (uint32_t at = 0; gUiLayout && at + 4 <= gUiLayoutSize; at += 4)
    if (rd16(gUiLayout + at) == id) return (int16_t)rd16(gUiLayout + at + 2);
  return fallback;
}
uint8_t uiFontLineHeight() {
  ensureContent();
  if (!gUiFont || gUiFontSize < 8) return 8;
  return memcmp(gUiFont, "FNT5", 4) == 0 && gUiFontSize >= UI_VECTOR_FONT_HEADER_SIZE
             ? gUiFont[8] : gUiFont[4];
}
uint8_t uiFontDesignHeight() {
  ensureContent();
  return gUiFont && gUiFontSize >= 8 && memcmp(gUiFont, "FNT4", 4) == 0 ? gUiFont[5] : 7;
}
const UiFontGlyph *uiFontGlyph(uint32_t codepoint) {
  ensureContent();
  if (!gUiFont || gUiFontSize < UI_FONT_HEADER_SIZE ||
      memcmp(gUiFont, "FNT4", 4) != 0) return nullptr;
  uint16_t count = rd16(gUiFont + 6);
  if (UI_FONT_HEADER_SIZE + (uint32_t)count * UI_FONT_GLYPH_SIZE > gUiFontSize) return nullptr;
  uint16_t lo = 0, hi = count;
  while (lo < hi) {
    uint16_t mid = lo + (hi - lo) / 2;
    uint32_t value = rd32(gUiFont + UI_FONT_HEADER_SIZE +
                          (uint32_t)mid * UI_FONT_GLYPH_SIZE);
    if (value < codepoint) lo = mid + 1; else hi = mid;
  }
  if (lo >= count) return nullptr;
  const uint8_t *row = gUiFont + UI_FONT_HEADER_SIZE +
                       (uint32_t)lo * UI_FONT_GLYPH_SIZE;
  if (rd32(row) != codepoint) return nullptr;
  static UiFontGlyph glyph;
  glyph.codepoint = codepoint; glyph.width = row[4]; glyph.height = row[5];
  glyph.advance = row[6]; glyph.xOffset = rdI8(row + 7); glyph.yOffset = rdI8(row + 8);
  glyph.alpha4 = row + 9;
  return &glyph;
}

UiFontFormat uiFontFormat() {
  ensureContent();
  return gUiFont && gUiFontSize >= UI_VECTOR_FONT_HEADER_SIZE &&
         memcmp(gUiFont, "FNT5", 4) == 0 ? UI_FONT_OPENTYPE : UI_FONT_BITMAP;
}

uint8_t uiFontPixelSize(uint8_t logicalSize) {
  ensureContent();
  if (!logicalSize) logicalSize = 1;
  if (uiFontFormat() != UI_FONT_OPENTYPE) return uiFontLineHeight();
  uint8_t count = gUiFont[6];
  uint8_t index = logicalSize > count ? count - 1 : logicalSize - 1;
  return gUiFont[8 + index];
}

uint8_t uiFontFaceIndex() {
  ensureContent();
  return uiFontFormat() == UI_FONT_OPENTYPE ? gUiFont[5] : 0;
}

uint32_t uiFontDataSize() {
  ensureContent();
  return uiFontFormat() == UI_FONT_OPENTYPE ? rd32(gUiFont + 16) : 0;
}

bool uiFontLoadData(uint8_t **out, uint32_t *size) {
  ensureContent();
  if (!out || !size || uiFontFormat() != UI_FONT_OPENTYPE || gUiActive >= gUiCount) return false;
  const PackRef &pack = gPacks[gUi[gUiActive].packRef];
  const SectionRef *section = findSection(pack, "FONT");
  uint32_t dataSize = uiFontDataSize();
  if (!section || section->size != UI_VECTOR_FONT_HEADER_SIZE + dataSize) return false;
  uint8_t *data = (uint8_t *)contentAlloc(dataSize);
  if (!data || !readRange(pack, section->offset + UI_VECTOR_FONT_HEADER_SIZE, data, dataSize)) {
    free(data); return false;
  }
  *out = data;
  *size = dataSize;
  return true;
}

const char *speciesDescription(SpeciesId species, const char *locale) {
  if (!dexValid(species) || gSprites[species].pack == 0xFF) return nullptr;
  const SpriteRef &sprite = gSprites[species];
  const RegionPackRuntime &pack = gRegionPacks[sprite.pack];
  return localizedAt(pack.locales, pack.localesSize, locale, sprite.localIndex);
}
const char *moveDescription(MoveId move, const char *locale) {
  if (!moveValid(move)) return nullptr;
  return localizedAt(gMoveLocales, gMoveLocalesSize, locale, move);
}
const char *itemDescription(ItemKey key, const char *locale) {
  const ItemEntry *item = itemByKey(key);
  if (!item) return nullptr;
  uint32_t index = (uint32_t)(item - gItems);
  const char *localized = localizedAt(gItemLocales, gItemLocalesSize, locale, index);
  return localized ? localized
                   : localizedAt(gItemLocales, gItemLocalesSize, "en-US", index);
}

const char *speciesName(SpeciesId species) {
  if (!dexValid(species)) return MISSING_SPECIES.name;
  const char *fallback = gDex[species].name;
  if (gUiActive >= gUiCount || gSprites[species].pack == 0xFF) return fallback;
  const SpriteRef &sprite = gSprites[species];
  const RegionPackRuntime &pack = gRegionPacks[sprite.pack];
  const char *localized = localizedAt(pack.localizedNames, pack.localizedNamesSize,
                                      uiActiveLocaleCode(), sprite.localIndex);
  return localized && *localized ? localized : fallback;
}

const char *moveName(MoveId move) {
  if (!moveValid(move)) return MISSING_MOVE.name;
  const char *localized = gUiActive < gUiCount
      ? localizedAt(gMoveLocalizedNames, gMoveLocalizedNamesSize,
                    uiActiveLocaleCode(), move)
      : nullptr;
  return localized && *localized ? localized : gMovesTable[move].name;
}
const char *itemName(ItemKey key) {
  const ItemEntry *item = itemByKey(key);
  if (!item) return "?";
  const char *localized = gUiActive < gUiCount
      ? localizedAt(gItemLocalizedNames, gItemLocalizedNamesSize,
                    uiActiveLocaleCode(), (uint32_t)(item - gItems))
      : nullptr;
  return localized && *localized ? localized : item->name;
}

uint8_t typeEffectTenth(uint8_t attack, uint8_t defense) {
  ensureContent();
  return attack < TYPE_COUNT && defense < TYPE_COUNT ? gTypeChart[attack * TYPE_COUNT + defense] : 10;
}
const char *packedTypeName(uint8_t type) {
  ensureContent();
  if (type >= TYPE_COUNT || !gTypeNames) return "?";
  const char *localized = gUiActive < gUiCount
      ? localizedAt(gTypeLocalizedNames, gTypeLocalizedNamesSize,
                    uiActiveLocaleCode(), type)
      : nullptr;
  return localized && *localized ? localized : gTypeNames + gTypeNameOffsets[type];
}
uint16_t packedTypeColor(uint8_t type) { ensureContent(); return type < TYPE_COUNT ? gTypeColors[type] : 0x8410; }
bool packedTypeColorIsLight(uint8_t type) { ensureContent(); return type < TYPE_COUNT && gTypeLight[type]; }

bool spriteAvailable(SpeciesId species) {
  return dexValid(species) && gSprites[species].pack != 0xFF &&
         gSprites[species].normalSize != 0;
}
bool contentLoadSprite(SpeciesId species, bool shiny, uint8_t gender, bool mega,
                       MegaFormKind megaForm, uint8_t **out, uint32_t *size,
                       uint8_t *displayScale) {
  if (!out || !size || !displayScale) return false;
  *out = nullptr; *size = 0; *displayScale = 0;
  if (!spriteAvailable(species)) return false;
  if (mega) {
    const MegaFormEntry *form = megaFormFor(species, megaForm);
    uint32_t offset = shiny && form ? form->shinySpriteAt
                                    : form ? form->spriteAt : 0;
    uint32_t length = shiny && form ? form->shinySpriteSize
                                    : form ? form->spriteSize : 0;
    if (form && length && form->spriteScale && form->spritePack < gPackCount) {
      uint8_t *data = (uint8_t *)contentAlloc(length);
      if (!data || !readRange(gPacks[form->spritePack], offset, data, length)) {
        free(data); return false;
      }
      *out = data; *size = length; *displayScale = form->spriteScale;
      return true;
    }
  }
  const SpriteRef &sprite = gSprites[species];
  uint32_t offset = sprite.normalAt, length = sprite.normalSize;
  if (gender == GENDER_FEMALE && shiny && sprite.femaleShinySize) {
    offset = sprite.femaleShinyAt; length = sprite.femaleShinySize;
  } else if (shiny && sprite.shinySize) {
    offset = sprite.shinyAt; length = sprite.shinySize;
  } else if (gender == GENDER_FEMALE && sprite.femaleSize) {
    offset = sprite.femaleAt; length = sprite.femaleSize;
  }
  if (!length) return false;
  uint8_t *data = (uint8_t *)contentAlloc(length);
  const PackRef &pack = gPacks[gRegionPacks[sprite.pack].packRef];
  if (!data || !readRange(pack, offset, data, length)) { free(data); return false; }
  *out = data; *size = length; *displayScale = sprite.displayScale;
  return true;
}
bool contentLoadThumbs(uint8_t **out, uint32_t *size) {
  ensureContent();
  if (!out || !size) return false;
  for (uint8_t i = 0; i < gRegionPackCount; i++) {
    const PackRef &pack = gPacks[gRegionPacks[i].packRef];
    const SectionRef *thumbs = findSection(pack, "THMB");
    if (!thumbs || !thumbs->size) continue;
    uint8_t *data = (uint8_t *)contentAlloc(thumbs->size);
    if (!data || !readRange(pack, thumbs->offset, data, thumbs->size)) { free(data); continue; }
    if (!validThumbs(data, thumbs->size)) { free(data); continue; }
    *out = data; *size = thumbs->size; return true;
  }
  return false;
}

uint8_t contentPackCount() { ensureContent(); return gPackCount; }
const char *contentPackSummary(uint8_t index) {
  ensureContent(); return index < gPackCount ? gPacks[index].summary : "";
}

bool regionBattleAvailable(uint8_t region) {
  ensureContent();
  if (region >= CONTENT_MAX_REGIONS || !gTrainers[region] ||
      !gRegionBattles[region].trainerCount) return false;
  for (uint8_t i = 0; i < gRegionBattles[region].trainerCount; i++)
    for (uint8_t member = 0; member < gTrainers[region][i].count; member++)
      if (!dexValid(gTrainers[region][i].team[member].dex)) return false;
  return true;
}

const RegionBattleInfo &regionBattleInfo(uint8_t region) {
  ensureContent();
  static const RegionBattleInfo missing = {};
  return region < CONTENT_MAX_REGIONS && gTrainers[region] ? gRegionBattles[region] : missing;
}

const Trainer &trainerInfo(uint8_t region, uint8_t index) {
  ensureContent();
  static const Trainer missing = { "?", "?", T_NORMAL, 0, {} };
  return region < CONTENT_MAX_REGIONS && gTrainers[region] &&
                 index < gRegionBattles[region].trainerCount
             ? gTrainers[region][index] : missing;
}

const char *trainerName(uint8_t region, uint8_t index) {
  const char *fallback = trainerInfo(region, index).name;
  if (region >= gRealRegionCount || index >= gRegionBattles[region].trainerCount ||
      !gRegionalNames[region]) return fallback;
  const char *localized = localizedAt(gRegionalNames[region], gRegionalNamesSize[region],
                                      uiActiveLocaleCode(), 1u + (uint32_t)index * 2u);
  return localized && *localized ? localized : fallback;
}

const char *trainerPlace(uint8_t region, uint8_t index) {
  const char *fallback = trainerInfo(region, index).place;
  if (region >= gRealRegionCount || index >= gRegionBattles[region].trainerCount ||
      !gRegionalNames[region]) return fallback;
  const char *localized = localizedAt(gRegionalNames[region], gRegionalNamesSize[region],
                                      uiActiveLocaleCode(), 2u + (uint32_t)index * 2u);
  return localized && *localized ? localized : fallback;
}

const BadgeArt &badgeArt(uint8_t region, uint8_t index) {
  ensureContent();
  static const BadgeArt missing = {};
  return region < CONTENT_MAX_REGIONS && index < gBadgeCounts[region]
             ? gBadges[region][index] : missing;
}

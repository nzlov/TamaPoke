#include "content.h"
#include "badges.h"
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
constexpr uint8_t MAX_SECTIONS = 16;
constexpr uint16_t COMMON_SIZE = 48;
constexpr uint16_t SECTION_SIZE = 16;
constexpr uint8_t MAX_PET_PACKS = CONTENT_MAX_REGIONS;
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
  uint16_t localIndex = 0;
  uint32_t normalAt = 0, normalSize = 0;
  uint32_t shinyAt = 0, shinySize = 0;
};

struct RegionPackRuntime {
  uint8_t packRef = 0xFF;
  uint8_t *locales = nullptr;
  uint32_t localesSize = 0;
  uint8_t *localizedNames = nullptr;
  uint32_t localizedNamesSize = 0;
  uint16_t firstSpecies = 0;
  uint16_t speciesCount = 0;
};

struct UiRuntime {
  UiLocaleInfo info{};
  uint8_t packRef = 0xFF;
};

static bool gAttempted = false;
static bool gRegionsReady = false, gMoves = false;
static PackRef gPacks[MAX_PACKS];
static uint8_t gPackCount = 0;
static RegionPackRuntime gRegionPacks[MAX_PET_PACKS];
static uint8_t gRegionPackCount = 0;

static DexEntry *gDex = nullptr;
static SpriteRef *gSprites = nullptr;
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
static uint32_t *gLearnOffsets = nullptr;
static uint32_t gLearnOffsetCount = 0;
static LearnEntry *gLearnEntries = nullptr;
static uint32_t gLearnEntryCount = 0;
static uint8_t *gMoveLocales = nullptr;
static uint32_t gMoveLocalesSize = 0;
static uint8_t *gMoveLocalizedNames = nullptr;
static uint32_t gMoveLocalizedNamesSize = 0;
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

static const DexEntry MISSING_SPECIES = {
  "?", 0, 0, 0x2946, 50, 50, 50, 50, 50, 50, 0, T_NORMAL, T_NONE, {}, 0,
};
static const MoveEntry MISSING_MOVE = {
  "-", T_NORMAL, MC_STATUS, 0, 0, EF_NONE, 0, 0, 0, TG_SELF, AIL_NONE, 0,
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

static bool readRange(const PackRef &pack, uint32_t offset, void *out, uint32_t size) {
  Reader reader;
  return reader.open(pack.path) && reader.readAt(offset, out, size);
}

static uint8_t *readSection(const PackRef &pack, const char *tag, uint32_t *size = nullptr,
                            uint32_t *count = nullptr) {
  for (uint8_t i = 0; i < pack.sectionCount; i++) {
    const SectionRef &section = pack.sections[i];
    if (strcmp(section.tag, tag) != 0) continue;
    uint8_t *data = (uint8_t *)contentAlloc(section.size);
    if (!data || !readRange(pack, section.offset, data, section.size)) {
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

static bool payloadCrc(Reader &reader, uint32_t offset, uint32_t expected) {
  uint8_t buffer[4096];
  if (!reader.seek(offset)) return false;
  uint32_t crc = 0, position = offset;
  while (position < reader.size) {
    uint32_t take = reader.size - position;
    if (take > sizeof(buffer)) take = sizeof(buffer);
    if (!reader.read(buffer, take)) return false;
    crc = crcStep(crc, buffer, take);
    position += take;
  }
  return crc == expected;
}

static bool readPackHeader(const char *path, Reader &reader, uint8_t common[COMMON_SIZE]) {
  if (!path || !reader.open(path) || !reader.readAt(0, common, COMMON_SIZE)) return false;
  uint8_t kind = common[6];
  uint16_t headerSize = rd16(common + 24), sectionCount = rd16(common + 26);
  return memcmp(common, "TPPK", 4) == 0 && rd16(common + 4) == CONTENT_PACK_ABI &&
         kind >= CONTENT_PACK_UI && kind <= CONTENT_PACK_MOVE &&
         rd32(common + 8) == reader.size && sectionCount > 0 && sectionCount <= MAX_SECTIONS &&
         headerSize == COMMON_SIZE + sectionCount * SECTION_SIZE;
}

static bool parsePack(const char *path, PackRef &out) {
  Reader reader;
  uint8_t common[COMMON_SIZE];
  if (!readPackHeader(path, reader, common)) return false;
  uint8_t kind = common[6];
  uint32_t fileSize = rd32(common + 8), expectedCrc = rd32(common + 12);
  uint16_t headerSize = rd16(common + 24), sectionCount = rd16(common + 26);
  if (!payloadCrc(reader, headerSize, expectedCrc)) return false;

  memset(&out, 0, sizeof(out));
  snprintf(out.path, sizeof(out.path), "%s", path);
  out.kind = kind;
  out.revision = rd32(common + 16);
  out.mechanicsHash = rd32(common + 20);
  memcpy(out.id, common + 28, 20);
  out.id[20] = 0;
  out.sectionCount = (uint8_t)sectionCount;
  uint8_t raw[MAX_SECTIONS * SECTION_SIZE];
  if (!reader.readAt(COMMON_SIZE, raw, sectionCount * SECTION_SIZE)) return false;
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
        section.offset < lastEnd) return false;
    lastEnd = section.offset + section.size;
  }
  snprintf(out.summary, sizeof(out.summary), "%s kind=%u rev=%lu hash=%08lx",
           out.id, out.kind, (unsigned long)out.revision, (unsigned long)out.mechanicsHash);
  return true;
}

static bool hasPackExtension(const char *path) {
  if (!path) return false;
  size_t length = strlen(path);
  const char *extensions[] = { ".tui", ".tmove", ".tregion" };
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
      if (parsePack(path, parsed)) gPacks[gPackCount++] = parsed;
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
    if (parsePack(path.c_str(), parsed)) gPacks[gPackCount++] = parsed;
  }
#endif
  std::sort(gPacks, gPacks + gPackCount, [](const PackRef &a, const PackRef &b) {
    return strcmp(a.id, b.id) < 0;
  });
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
        row[6] > EF_CHARGE || row[10] > TG_FOE || row[11] > AIL_CONFUSE ||
        row[12] > 100 || !validString(names, nameSize, nameOffset)) {
      free(rawMoves); free(names); free(table); return false;
    }
    MoveEntry &move = table[id];
    move.name = (const char *)(names + nameOffset);
    move.type = row[2]; move.cat = row[3]; move.power = row[4]; move.acc = row[5];
    move.effect = row[6]; move.param = rdI8(row + 7); move.statMask = row[8];
    move.stages = rdI8(row + 9); move.target = row[10]; move.ailment = row[11];
    move.ailChance = row[12];
  }
  for (uint32_t i = 0; i < moveRecords; i++) {
    if (!table[i].name) { free(rawMoves); free(names); free(table); return false; }
  }
  free(rawMoves);

  uint32_t offsetSize = 0, offsetCount = 0, learnSize = 0, learnCountValue = 0;
  uint8_t *offsetRaw = readSection(pack, "LOFS", &offsetSize, &offsetCount);
  uint8_t *learnRaw = readSection(pack, "LERN", &learnSize, &learnCountValue);
  if (!offsetRaw || !learnRaw || offsetSize != offsetCount * 4u ||
      learnSize != learnCountValue * 4u || offsetCount < 2) {
    free(offsetRaw); free(learnRaw); free(names); free(table); return false;
  }
  uint32_t *offsets = (uint32_t *)contentAlloc(offsetSize);
  LearnEntry *entries = (LearnEntry *)contentAlloc(sizeof(LearnEntry) * learnCountValue);
  if (!offsets || !entries) {
    free(offsetRaw); free(learnRaw); free(offsets); free(entries); free(names); free(table);
    return false;
  }
  for (uint32_t i = 0; i < offsetCount; i++) {
    offsets[i] = rd32(offsetRaw + i * 4);
    if ((i && offsets[i] < offsets[i - 1]) || offsets[i] > learnCountValue) {
      free(offsetRaw); free(learnRaw); free(offsets); free(entries); free(names); free(table);
      return false;
    }
  }
  if (offsets[offsetCount - 1] != learnCountValue) {
    free(offsetRaw); free(learnRaw); free(offsets); free(entries); free(names); free(table);
    return false;
  }
  for (uint32_t i = 0; i < learnCountValue; i++) {
    entries[i].move = rd16(learnRaw + i * 4);
    entries[i].level = learnRaw[i * 4 + 2];
    entries[i].method = learnRaw[i * 4 + 3];
    if (entries[i].move >= moveRecords || entries[i].method > LM_EGG) {
      free(offsetRaw); free(learnRaw); free(offsets); free(entries); free(names); free(table);
      return false;
    }
  }
  free(offsetRaw); free(learnRaw);

  uint32_t chartSize = 0, typeSize = 0, typeCountValue = 0, typeNamesSize = 0;
  uint8_t *chart = readSection(pack, "CHRT", &chartSize);
  uint8_t *types = readSection(pack, "TYPS", &typeSize, &typeCountValue);
  uint8_t *typeNames = readSection(pack, "TSTR", &typeNamesSize);
  if (!chart || chartSize != sizeof(gTypeChart) || !types || typeCountValue != TYPE_COUNT ||
      typeSize != TYPE_COUNT * 8u || !typeNames) {
    free(chart); free(types); free(typeNames); free(offsets); free(entries); free(names); free(table);
    return false;
  }
  memcpy(gTypeChart, chart, sizeof(gTypeChart));
  for (uint8_t i = 0; i < TYPE_COUNT; i++) {
    uint32_t nameOffset = rd32(types + i * 8);
    if (!validString(typeNames, typeNamesSize, nameOffset)) {
      free(chart); free(types); free(typeNames); free(offsets); free(entries); free(names); free(table);
      return false;
    }
    gTypeNameOffsets[i] = nameOffset;
    gTypeColors[i] = rd16(types + i * 8 + 4);
    gTypeLight[i] = types[i * 8 + 6];
  }
  free(chart); free(types);

  uint32_t localesSize = 0;
  uint8_t *locales = readSection(pack, "LOCL", &localesSize);
  uint32_t localizedNamesSize = 0;
  uint8_t *localizedNames = readSection(pack, "LNAM", &localizedNamesSize);
  uint32_t localizedTypeNamesSize = 0;
  uint8_t *localizedTypeNames = readSection(pack, "TLNM", &localizedTypeNamesSize);
  gMovesTable = table; gMoveCount = (uint16_t)moveRecords; gMoveNames = (char *)names;
  gLearnOffsets = offsets; gLearnOffsetCount = offsetCount;
  gLearnEntries = entries; gLearnEntryCount = learnCountValue;
  gMoveLocales = locales; gMoveLocalesSize = localesSize;
  gMoveLocalizedNames = localizedNames; gMoveLocalizedNamesSize = localizedNamesSize;
  gTypeLocalizedNames = localizedTypeNames;
  gTypeLocalizedNamesSize = localizedTypeNamesSize;
  gTypeNames = (char *)typeNames;
  gMoves = true;
  gPacks[packIndex].loaded = true;
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
  if (!specs || !names || !regions || specCount == 0 || specCount > CONTENT_MAX_SPECIES ||
      specSize != specCount * 20u || !evolutionSection ||
      evolutionSection->size != evolutionSection->count * 4u) {
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
  auto rollback = [&]() {
    for (uint32_t i = 0; i < touchedCount; i++) {
      gDex[touched[i]] = DexEntry{};
      gSprites[touched[i]] = SpriteRef{};
    }
    memcpy(gRegions, oldRegions, sizeof(oldRegions));
    memcpy(gRegionNames, oldRegionNames, sizeof(oldRegionNames));
    memcpy(gRegionStarters, oldRegionStarters, sizeof(oldRegionStarters));
    gRealRegionCount = oldRealRegionCount;
    gDexCount = oldDexCount;
    gRegionMask = oldRegionMask;
  };
  uint8_t metadataRegion = regions[0];
  if (!loadRegionMetadata(regions, regionSize, regionRecords)) {
    rollback(); free(touched); free(specs); free(names); free(regions); return false;
  }
  free(regions);

  if (!gDex) {
    gDex = (DexEntry *)contentAlloc(sizeof(DexEntry) * (CONTENT_MAX_SPECIES + 1));
    gSprites = (SpriteRef *)contentAlloc(sizeof(SpriteRef) * (CONTENT_MAX_SPECIES + 1));
    if (!gDex || !gSprites) {
      free(gDex); free(gSprites); gDex = nullptr; gSprites = nullptr;
      rollback(); free(touched); free(specs); free(names); return false;
    }
    for (uint32_t i = 0; i <= CONTENT_MAX_SPECIES; i++) gSprites[i].pack = 0xFF;
  }
  uint8_t runtimePack = gRegionPackCount;
  uint16_t firstSpecies = 0;
  uint8_t packRegion = 0xFF;
  for (uint32_t i = 0; i < specCount; i++) {
    const uint8_t *row = specs + i * 20;
    SpeciesId id = rd16(row);
    uint32_t nameOffset = rd32(row + 16);
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
    uint8_t region = row[15];
    if (region >= CONTENT_MAX_REGIONS || species.type1 >= TYPE_COUNT ||
        (species.type2 != T_NONE && species.type2 >= TYPE_COUNT) ||
        !species.bHp || !species.bAtk || !species.bDef || !species.bSpe ||
        !species.bSpA || !species.bSpD) {
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

  const SectionRef *spriteBlob = findSection(pack, "SBLB");
  uint32_t spriteSize = 0, spriteCount = 0;
  uint8_t *spriteIndex = readSection(pack, "SPRI", &spriteSize, &spriteCount);
  if (!spriteBlob || !spriteIndex || spriteCount != specCount || spriteSize != spriteCount * 18u) {
    rollback(); free(touched); free(spriteIndex); free(names); return false;
  }
  for (uint32_t i = 0; i < spriteCount; i++) {
    const uint8_t *row = spriteIndex + i * 18;
    SpeciesId id = rd16(row);
    if (!dexValid(id)) {
      rollback(); free(touched); free(spriteIndex); free(names); return false;
    }
    SpriteRef &sprite = gSprites[id];
    sprite.normalAt = spriteBlob->offset + rd32(row + 2); sprite.normalSize = rd32(row + 6);
    sprite.shinyAt = spriteBlob->offset + rd32(row + 10); sprite.shinySize = rd32(row + 14);
    uint32_t normalOffset = rd32(row + 2), shinyOffset = rd32(row + 10);
    if (normalOffset > spriteBlob->size || sprite.normalSize > spriteBlob->size - normalOffset ||
        shinyOffset > spriteBlob->size || sprite.shinySize > spriteBlob->size - shinyOffset) {
      rollback(); free(touched); free(spriteIndex); free(names); return false;
    }
  }
  free(spriteIndex);

  if (packRegion == 0xFF || packRegion != metadataRegion ||
      !loadRegionBattleData(pack, packRegion)) {
    rollback(); free(touched); free(names);
    return false;
  }
  RegionPackRuntime &runtime = gRegionPacks[gRegionPackCount++];
  runtime.packRef = packIndex; runtime.firstSpecies = firstSpecies;
  runtime.speciesCount = (uint16_t)specCount;
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
  scanPacks();
  for (uint8_t i = 0; i < gPackCount; i++)
    if (gPacks[i].kind == CONTENT_PACK_MOVE && !gMoves) loadMovePack(i);
  for (uint8_t i = 0; i < gPackCount; i++)
    if (gPacks[i].kind == CONTENT_PACK_REGION) loadRegionPack(i);
  for (uint8_t i = 0; i < gPackCount; i++)
    if (gPacks[i].kind == CONTENT_PACK_UI) loadUiMeta(i);
  buildAllRegion();

  for (uint8_t i = 0; i < gPackCount; i++) {
    if (gPacks[i].kind == CONTENT_PACK_UI || !gPacks[i].loaded) continue;
    gMechanicsHash ^= gPacks[i].mechanicsHash;
    gMechanicsHash *= 16777619u;
  }
  int8_t initial = -1;
  for (uint8_t i = 0; i < gUiCount; i++) if (gUi[i].info.isDefault) initial = i;
  if (initial < 0 && gUiCount) initial = 0;
  if (initial >= 0 && !uiActivateLocale((uint8_t)initial)) initial = -1;
  if (initial < 0)
    for (uint8_t i = 0; i < gUiCount && gUiActive >= gUiCount; i++) uiActivateLocale(i);
  Serial.printf("packs: %u ui=%u region=%u moves=%u dex=%u\n", gPackCount, gUiCount,
                gRegionPackCount, gMoves ? 1 : 0, gDexCount);
  return contentReady();
}

bool contentValidatePackFile(const char *path) {
  PackRef parsed;
  return path && parsePack(path, parsed);
}

bool contentReadPackInfo(const char *path, ContentPackInfo &out) {
  Reader reader;
  uint8_t common[COMMON_SIZE];
  if (!readPackHeader(path, reader, common)) return false;
  memcpy(out.id, common + 28, 20);
  out.id[20] = 0;
  out.revision = rd32(common + 16);
  return out.id[0] != 0;
}

bool contentReady() {
  return contentHasUi() && contentHasPets() && contentHasMoves() &&
         (uint32_t)gDexCount + 2u <= gLearnOffsetCount;
}
bool contentHasUi() { ensureContent(); return gUiActive < gUiCount; }
bool contentHasPets() { ensureContent(); return gRegionsReady; }
bool contentHasMoves() { ensureContent(); return gMoves; }
uint32_t contentMechanicsHash() { ensureContent(); return gMechanicsHash; }

uint16_t dexCount() { ensureContent(); return gDexCount; }
bool dexValid(SpeciesId id) {
  ensureContent();
  return id && id <= CONTENT_MAX_SPECIES && gDex && gDex[id].name;
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
bool moveValid(MoveId id) { ensureContent(); return id < gMoveCount && gMovesTable && gMovesTable[id].name; }
const MoveEntry &moveEntry(MoveId id) { return moveValid(id) ? gMovesTable[id] : MISSING_MOVE; }
uint16_t learnCount(SpeciesId species) {
  ensureContent();
  if (!species || (uint32_t)species + 1u >= gLearnOffsetCount) return 0;
  uint32_t first = gLearnOffsets[species], last = gLearnOffsets[species + 1];
  return last >= first && last <= gLearnEntryCount ? (uint16_t)(last - first) : 0;
}
MoveId learnMove(SpeciesId species, uint16_t index) {
  if (index >= learnCount(species)) return MOVE_NONE;
  return gLearnEntries[gLearnOffsets[species] + index].move;
}
uint8_t learnLevel(SpeciesId species, uint16_t index) {
  if (index >= learnCount(species)) return 0;
  return gLearnEntries[gLearnOffsets[species] + index].level;
}
LearnMethod learnMethod(SpeciesId species, uint16_t index) {
  if (index >= learnCount(species)) return LM_LEVEL_UP;
  return (LearnMethod)gLearnEntries[gLearnOffsets[species] + index].method;
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

bool contentLoadSprite(SpeciesId species, bool shiny, uint8_t **out, uint32_t *size) {
  if (!out || !size || !dexValid(species) || gSprites[species].pack == 0xFF) return false;
  const SpriteRef &sprite = gSprites[species];
  uint32_t offset = shiny && sprite.shinySize ? sprite.shinyAt : sprite.normalAt;
  uint32_t length = shiny && sprite.shinySize ? sprite.shinySize : sprite.normalSize;
  if (!length) return false;
  uint8_t *data = (uint8_t *)contentAlloc(length);
  const PackRef &pack = gPacks[gRegionPacks[sprite.pack].packRef];
  if (!data || !readRange(pack, offset, data, length)) { free(data); return false; }
  *out = data; *size = length;
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

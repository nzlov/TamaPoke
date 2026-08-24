#include "font_engine.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace {

constexpr uint16_t FONT_CACHE_SLOTS = 96;
constexpr uint32_t FONT_CACHE_LIMIT = 192u * 1024u;

struct CacheEntry {
  bool used = false;
  uint32_t codepoint = 0;
  uint32_t stamp = 0;
  uint32_t bytes = 0;
  uint8_t pixelSize = 0;
  RuntimeFontGlyph glyph{};
};

static FT_Library gLibrary = nullptr;
static FT_Face gFace = nullptr;
static uint8_t *gFontData = nullptr;
static CacheEntry gCache[FONT_CACHE_SLOTS];
static RuntimeFontStats gStats{};
static uint32_t gStamp = 0;
static uint8_t gPixelSize = 0;

static void *fontAlloc(size_t size) {
#if defined(ESP32)
  return ps_malloc(size);
#else
  return malloc(size);
#endif
}

static void release(CacheEntry &entry) {
  if (!entry.used) return;
  free((void *)entry.glyph.alpha);
  if (gStats.cacheBytes >= entry.bytes) gStats.cacheBytes -= entry.bytes;
  if (gStats.cachedGlyphs) gStats.cachedGlyphs--;
  entry = CacheEntry{};
}

static void clearCache() {
  for (CacheEntry &entry : gCache) release(entry);
  gStats = RuntimeFontStats{};
  gStamp = 0;
  gPixelSize = 0;
}

static CacheEntry *cacheSlot() {
  CacheEntry *oldest = nullptr;
  for (CacheEntry &entry : gCache) {
    if (!entry.used) return &entry;
    if (!oldest || entry.stamp < oldest->stamp) oldest = &entry;
  }
  return oldest;
}

static CacheEntry *oldestUsed(const CacheEntry *skip) {
  CacheEntry *oldest = nullptr;
  for (CacheEntry &entry : gCache)
    if (entry.used && &entry != skip && (!oldest || entry.stamp < oldest->stamp)) oldest = &entry;
  return oldest;
}

static bool copyBitmap(CacheEntry &entry, const FT_Bitmap &bitmap) {
  entry.bytes = (uint32_t)bitmap.width * bitmap.rows;
  if (!entry.bytes) return true;
  while (gStats.cacheBytes + entry.bytes > FONT_CACHE_LIMIT) {
    CacheEntry *oldest = oldestUsed(&entry);
    if (!oldest) break;
    release(*oldest);
  }
  uint8_t *alpha = (uint8_t *)fontAlloc(entry.bytes);
  if (!alpha) return false;
  for (uint16_t row = 0; row < bitmap.rows; row++) {
    const uint8_t *source = bitmap.buffer +
        (bitmap.pitch >= 0 ? row * bitmap.pitch : (bitmap.rows - 1 - row) * -bitmap.pitch);
    uint8_t *destination = alpha + (uint32_t)row * bitmap.width;
    if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
      memcpy(destination, source, bitmap.width);
    } else if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
      for (uint16_t col = 0; col < bitmap.width; col++)
        destination[col] = source[col >> 3] & (0x80 >> (col & 7)) ? 255 : 0;
    } else {
      free(alpha);
      return false;
    }
  }
  entry.glyph.alpha = alpha;
  gStats.cacheBytes += entry.bytes;
  return true;
}

}  // namespace

bool runtimeFontBegin(uint8_t *data, size_t size, uint8_t faceIndex) {
  if (!data || !size || size > 0x7FFFFFFFu) { free(data); return false; }
  if (!gLibrary && FT_Init_FreeType(&gLibrary)) { free(data); return false; }
  FT_Face face = nullptr;
  if (FT_New_Memory_Face(gLibrary, data, (FT_Long)size, faceIndex, &face) ||
      FT_Select_Charmap(face, FT_ENCODING_UNICODE)) {
    if (face) FT_Done_Face(face);
    free(data);
    return false;
  }

  clearCache();
  if (gFace) FT_Done_Face(gFace);
  free(gFontData);
  gFace = face;
  gFontData = data;
  return true;
}

void runtimeFontEnd() {
  clearCache();
  if (gFace) FT_Done_Face(gFace);
  gFace = nullptr;
  free(gFontData);
  gFontData = nullptr;
}

bool runtimeFontActive() { return gFace != nullptr; }

const RuntimeFontGlyph *runtimeFontGlyph(uint32_t codepoint, uint8_t pixelSize) {
  if (!gFace || !pixelSize) return nullptr;
  for (CacheEntry &entry : gCache) {
    if (entry.used && entry.codepoint == codepoint && entry.pixelSize == pixelSize) {
      entry.stamp = ++gStamp;
      gStats.cacheHits++;
      return &entry.glyph;
    }
  }

  FT_UInt glyphIndex = FT_Get_Char_Index(gFace, codepoint);
  if (!glyphIndex && codepoint) return nullptr;
  if (gPixelSize != pixelSize) {
    if (FT_Set_Pixel_Sizes(gFace, 0, pixelSize)) return nullptr;
    gPixelSize = pixelSize;
  }
  if (FT_Load_Glyph(gFace, glyphIndex, FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL) ||
      FT_Render_Glyph(gFace->glyph, FT_RENDER_MODE_NORMAL)) return nullptr;

  CacheEntry *entry = cacheSlot();
  if (!entry) return nullptr;
  if (entry->used) release(*entry);
  entry->used = true;
  entry->codepoint = codepoint;
  entry->pixelSize = pixelSize;
  entry->stamp = ++gStamp;
  entry->glyph.left = gFace->glyph->bitmap_left;
  entry->glyph.top = gFace->glyph->bitmap_top;
  entry->glyph.advance = (int16_t)((gFace->glyph->advance.x + 32) >> 6);
  entry->glyph.width = (uint16_t)gFace->glyph->bitmap.width;
  entry->glyph.height = (uint16_t)gFace->glyph->bitmap.rows;
  gStats.cachedGlyphs++;
  if (!copyBitmap(*entry, gFace->glyph->bitmap)) {
    release(*entry);
    return nullptr;
  }
  gStats.cacheMisses++;
  return &entry->glyph;
}

RuntimeFontStats runtimeFontStats() { return gStats; }

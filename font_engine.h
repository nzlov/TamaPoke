#pragma once

#include <stddef.h>
#include <stdint.h>

struct RuntimeFontGlyph {
  int16_t left;
  int16_t top;
  int16_t advance;
  uint16_t width;
  uint16_t height;
  const uint8_t *alpha;
};

struct RuntimeFontStats {
  uint32_t cacheHits;
  uint32_t cacheMisses;
  uint32_t cacheBytes;
  uint16_t cachedGlyphs;
};

// Takes ownership of data on both success and failure.
bool runtimeFontBegin(uint8_t *data, size_t size, uint8_t faceIndex);
void runtimeFontEnd();
bool runtimeFontActive();
const RuntimeFontGlyph *runtimeFontGlyph(uint32_t codepoint, uint8_t pixelSize);
RuntimeFontStats runtimeFontStats();

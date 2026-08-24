#pragma once
#include <cstddef>
#include <cstdint>

constexpr int EMU_CJK_MAX_ROWS = 16;

struct EmuCjkGlyph {
  uint16_t codepoint;
  uint8_t width;
  uint8_t height;
  int8_t xOffset;
  int8_t yOffset;
  uint8_t advance;
  uint16_t rows[EMU_CJK_MAX_ROWS];
};

const EmuCjkGlyph *emuCjkGlyph(uint32_t codepoint);
size_t emuCjkGlyphCount();
uint32_t emuNextUtf8(const char *&text);
int emuCjkTextWidth(const char *text, uint8_t scale);

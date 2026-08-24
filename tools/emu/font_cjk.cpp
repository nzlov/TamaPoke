#include "font_cjk.h"
#include <climits>

#include "font_cjk_data.inc"

const EmuCjkGlyph *emuCjkGlyph(uint32_t codepoint) {
  size_t lo = 0, hi = sizeof(EMU_CJK_GLYPHS) / sizeof(EMU_CJK_GLYPHS[0]);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (EMU_CJK_GLYPHS[mid].codepoint < codepoint) lo = mid + 1;
    else hi = mid;
  }
  if (lo >= emuCjkGlyphCount() || EMU_CJK_GLYPHS[lo].codepoint != codepoint)
    return nullptr;
  return &EMU_CJK_GLYPHS[lo];
}

size_t emuCjkGlyphCount() {
  return sizeof(EMU_CJK_GLYPHS) / sizeof(EMU_CJK_GLYPHS[0]);
}

uint32_t emuNextUtf8(const char *&text) {
  const unsigned char *p = (const unsigned char *)text;
  uint32_t codepoint = *p++;
  if (codepoint < 0x80) { text = (const char *)p; return codepoint; }

  int continuation = 0;
  if ((codepoint & 0xE0) == 0xC0) { codepoint &= 0x1F; continuation = 1; }
  else if ((codepoint & 0xF0) == 0xE0) { codepoint &= 0x0F; continuation = 2; }
  else if ((codepoint & 0xF8) == 0xF0) { codepoint &= 0x07; continuation = 3; }
  else { text = (const char *)p; return '?'; }

  while (continuation--) {
    if ((*p & 0xC0) != 0x80) { text = (const char *)p; return '?'; }
    codepoint = (codepoint << 6) | (*p++ & 0x3F);
  }
  text = (const char *)p;
  return codepoint;
}

int emuCjkTextWidth(const char *text, uint8_t scale) {
  int cursor = 0, minX = INT_MAX, maxX = INT_MIN;
  while (text && *text) {
    uint32_t codepoint = emuNextUtf8(text);
    const EmuCjkGlyph *glyph = emuCjkGlyph(codepoint);
    if (!glyph) glyph = emuCjkGlyph('?');
    if (!glyph) continue;
    if (glyph->width) {
      int x1 = cursor + glyph->xOffset;
      int x2 = x1 + glyph->width - 1;
      if (x1 < minX) minX = x1;
      if (x2 > maxX) maxX = x2;
    }
    cursor += glyph->advance;
  }
  return minX == INT_MAX ? 0 : (maxX - minX + 1) * scale;
}

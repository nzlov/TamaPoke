#include "Arduino_GFX_Library.h"
#include <cstdio>

static int litPixels(const Arduino_Canvas &canvas) {
  int count = 0;
  for (uint16_t pixel : canvas.fb)
    if (pixel) count++;
  return count;
}

int main() {
  Arduino_CO5300 panel(nullptr, 0, 0, 0, 0, 0, 0, 0, 0);
  Arduino_Canvas canvas(64, 32, &panel);
  canvas.setTextColor(RGB565_WHITE);
  canvas.setTextSize(1);
  canvas.setCjkFont(true);
  canvas.setCursor(0, 16);
  canvas.print("中文");

  int bad = 0;
  if (canvas.cx != 32) { printf("FAIL: Chinese advance is %d, want 32\n", canvas.cx); bad++; }
  if (emuCjkTextWidth("中文", 1) != 32) { printf("FAIL: Chinese bounds width is not 32\n"); bad++; }
  if (litPixels(canvas) < 100) { printf("FAIL: Chinese medium weight was not rendered\n"); bad++; }

  const EmuCjkGlyph *zhong = emuCjkGlyph(0x4E2D);
  const EmuCjkGlyph *wen = emuCjkGlyph(0x6587);
  if (!zhong || !wen || zhong->rows[4] == wen->rows[4]) {
    printf("FAIL: generated Chinese glyphs are missing or indistinguishable\n");
    bad++;
  }
  printf("%s: Unifont renders Chinese in the emulator (%zu glyph subset)\n",
         bad ? "FAIL" : "PASS", emuCjkGlyphCount());
  return bad ? 1 : 0;
}

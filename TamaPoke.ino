// TamaPoke - tamagotchi pixel art inspirado en la gen 1
// para Waveshare ESP32-S3-Touch-AMOLED-1.75
//
// Librerias (Library Manager o repo de Waveshare):
//   - "GFX Library for Arduino" 1.6.4+ (moononournation), CO5300 + CJK
//   - "U8g2" (olikraus), habilita la fuente UTF-8 incluida en Arduino_GFX
//   - "SensorLib" (Lewis He), drivers CST9217 y QMI8658
//
// Placa: ESP32S3 Dev Module | Flash 16MB | PSRAM: OPI PSRAM | USB CDC On Boot: Enabled
//
// Los sprites y la tabla de especies se generan con tools/sprites.py (emit).

#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "pin_config.h"
#include "ui_art.h"
#include "boot_splash.h"
#include "dex.h"
#include "types.h"
#include "moves.h"
#include "font_engine.h"
#include "battle.h"
#include "trainers.h"
#include "link.h"
#include "linknow.h"
#include "backs.h"
#include "art_codec.h"
#include "badges.h"
#include "avatars.h"
#include <stdarg.h>
#include "party.h"
#include "inventory.h"
#include "wild.h"
#include "ui_scroll.h"
#include "save.h"
#include "pet.h"
#include "quiz.h"
#include "sdmon.h"
#include "rtcbat.h"
#include "i18n.h"
#include "audio.h"
#include "motion.h"
#include "perf.h"
#include <Preferences.h>

#if defined(ESP32)
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
// FreeType's CFF glyph interpreter needs more than Arduino's default 8 KiB
// loop-task stack while rendering the Chinese OpenType font.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);
#endif

// Release builds inject the GitHub Release tag. Supported local build scripts
// inject the current short commit plus the UTC build time.
#ifndef FW_VERSION
#define FW_VERSION "local-unversioned"
#endif

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RESET, 0 /*rotation*/, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

class TamaCanvas : public Arduino_Canvas {
public:
  using Arduino_Canvas::Arduino_Canvas;
  using Arduino_Canvas::print;

  static uint8_t packScale(uint8_t requested) {
    uint8_t height = uiFontLineHeight();
    uint8_t design = uiFontDesignHeight();
    uint8_t scale = (requested * design + height / 2) / height;
    return scale ? scale : 1;
  }

  void setTextSize(uint8_t size) {
    textScale = size ? size : 1;
    Arduino_Canvas::setTextSize(fontActive && !fontVector ? packScale(textScale) : textScale);
  }

  void setCursor(int16_t x, int16_t y) {
    Arduino_Canvas::setCursor(x, y);
  }

  void setTextColor(uint16_t color) {
    fontColor = color;
    Arduino_Canvas::setTextColor(color);
  }

  uint8_t textLineHeight() const {
    return fontVector ? uiFontPixelSize(textScale) + 2
                      : uiFontLineHeight() * packScale(textScale);
  }

  void setPackFont(bool enabled) {
    uint8_t *fontData = nullptr;
    uint32_t fontSize = 0;
    fontVector = enabled && uiFontFormat() == UI_FONT_OPENTYPE &&
                 uiFontLoadData(&fontData, &fontSize) &&
                 runtimeFontBegin(fontData, fontSize, uiFontFaceIndex());
    if (!fontVector) runtimeFontEnd();
    fontActive = enabled && (uiFontFormat() == UI_FONT_BITMAP || fontVector);
    Arduino_Canvas::setTextSize(fontActive && !fontVector ? packScale(textScale) : textScale);
  }

  size_t print(const char *text) {
    if (!fontActive || !text) {
      Arduino_Canvas::print(text);
      return text ? strlen(text) : 0;
    }
    const char *at = text;
    while (*at) {
      uint32_t codepoint = nextUtf8(at);
      if (codepoint == '\n') {
        int16_t lineHeight = fontVector ? uiFontPixelSize(textScale) + 2
                                        : uiFontLineHeight() * packScale(textScale);
        Arduino_Canvas::setCursor(0, getCursorY() + lineHeight);
      } else {
        if (fontVector) drawVectorGlyph(codepoint); else drawPackGlyph(codepoint);
      }
    }
    return (size_t)(at - text);
  }

  int16_t textWidth(const char *text) {
    if (!text) return 0;
    if (fontActive) {
      const char *at = text;
      if (fontVector) {
        uint8_t pixelSize = uiFontPixelSize(textScale);
        int16_t cursor = 0, minX = 0, maxX = 0;
        while (*at) {
          uint32_t codepoint = nextUtf8(at);
          const RuntimeFontGlyph *glyph = runtimeFontGlyph(codepoint, pixelSize);
          if (!glyph) glyph = runtimeFontGlyph('?', pixelSize);
          if (!glyph) continue;
          int16_t left = cursor + glyph->left;
          int16_t right = left + glyph->width;
          if (left < minX) minX = left;
          if (right > maxX) maxX = right;
          cursor += glyph->advance;
        }
        if (cursor > maxX) maxX = cursor;
        return maxX - minX;
      }
      uint8_t scale = packScale(textScale);
      int16_t width = 0;
      while (*at) {
        const UiFontGlyph *glyph = uiFontGlyph(nextUtf8(at));
        if (!glyph) glyph = uiFontGlyph('?');
        if (glyph) width += glyph->advance * scale;
      }
      return width;
    }
    size_t glyphs = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
      if ((*p & 0xC0) != 0x80) glyphs++;
    return (int16_t)(glyphs * 6 * textScale);
  }

  bool textInkBounds(const char *text, int16_t *left, int16_t *top,
                     int16_t *right, int16_t *bottom) {
    if (!text || !*text) return false;
    int16_t minX = 0, minY = 0, maxX = 0, maxY = 0, cursor = 0;
    bool haveInk = false;
    const char *at = text;
    while (*at) {
      uint32_t codepoint = nextUtf8(at);
      if (codepoint == '\n') break;
      int16_t glyphLeft = cursor, glyphTop = 0, glyphRight = cursor, glyphBottom = 0;
      if (fontActive && fontVector) {
        uint8_t pixelSize = uiFontPixelSize(textScale);
        const RuntimeFontGlyph *glyph = runtimeFontGlyph(codepoint, pixelSize);
        if (!glyph) glyph = runtimeFontGlyph('?', pixelSize);
        if (!glyph) continue;
        glyphLeft = cursor + glyph->left;
        glyphTop = pixelSize - glyph->top;
        glyphRight = glyphLeft + glyph->width;
        glyphBottom = glyphTop + glyph->height;
        cursor += glyph->advance;
      } else if (fontActive) {
        uint8_t scale = packScale(textScale);
        const UiFontGlyph *glyph = uiFontGlyph(codepoint);
        if (!glyph) glyph = uiFontGlyph('?');
        if (!glyph) continue;
        glyphLeft = cursor + glyph->xOffset * scale;
        glyphRight = glyphLeft + glyph->width * scale;
        glyphBottom = glyph->height * scale;
        cursor += glyph->advance * scale;
      } else {
        glyphLeft = cursor;
        glyphRight = cursor + 5 * textScale;
        glyphBottom = 8 * textScale;
        cursor += 6 * textScale;
      }
      if (glyphRight <= glyphLeft || glyphBottom <= glyphTop) continue;
      if (!haveInk) {
        minX = glyphLeft; minY = glyphTop; maxX = glyphRight; maxY = glyphBottom;
        haveInk = true;
      } else {
        if (glyphLeft < minX) minX = glyphLeft;
        if (glyphTop < minY) minY = glyphTop;
        if (glyphRight > maxX) maxX = glyphRight;
        if (glyphBottom > maxY) maxY = glyphBottom;
      }
    }
    if (!haveInk) return false;
    if (left) *left = minX;
    if (top) *top = minY;
    if (right) *right = maxX;
    if (bottom) *bottom = maxY;
    return true;
  }

  void flush() {
    uint32_t started = perfNowUs();
    Arduino_Canvas::flush();
    perfRecord(PERF_FLUSH, perfNowUs() - started);
  }

private:
  static uint16_t blend565(uint16_t background, uint16_t foreground, uint8_t alpha) {
    if (!alpha) return background;
    if (alpha >= 15) return foreground;
    uint16_t red = ((((background >> 11) & 0x1F) * (15 - alpha) +
                     ((foreground >> 11) & 0x1F) * alpha + 7) / 15) << 11;
    uint16_t green = ((((background >> 5) & 0x3F) * (15 - alpha) +
                       ((foreground >> 5) & 0x3F) * alpha + 7) / 15) << 5;
    uint16_t blue = (((background & 0x1F) * (15 - alpha) +
                      (foreground & 0x1F) * alpha + 7) / 15);
    return red | green | blue;
  }

  static uint16_t blend565x8(uint16_t background, uint16_t foreground, uint8_t alpha) {
    if (!alpha) return background;
    if (alpha == 255) return foreground;
    uint16_t red = ((((background >> 11) & 0x1F) * (255 - alpha) +
                     ((foreground >> 11) & 0x1F) * alpha + 127) / 255) << 11;
    uint16_t green = ((((background >> 5) & 0x3F) * (255 - alpha) +
                       ((foreground >> 5) & 0x3F) * alpha + 127) / 255) << 5;
    uint16_t blue = (((background & 0x1F) * (255 - alpha) +
                      (foreground & 0x1F) * alpha + 127) / 255);
    return red | green | blue;
  }

  static uint32_t nextUtf8(const char *&at) {
    uint8_t first = (uint8_t)*at++;
    if (first < 0x80) return first;
    uint32_t value;
    uint8_t remaining;
    if ((first & 0xE0) == 0xC0) { value = first & 0x1F; remaining = 1; }
    else if ((first & 0xF0) == 0xE0) { value = first & 0x0F; remaining = 2; }
    else if ((first & 0xF8) == 0xF0) { value = first & 0x07; remaining = 3; }
    else return '?';
    while (remaining--) {
      uint8_t next = (uint8_t)*at;
      if ((next & 0xC0) != 0x80) return '?';
      at++;
      value = (value << 6) | (next & 0x3F);
    }
    return value;
  }

  void drawPackGlyph(uint32_t codepoint) {
    const UiFontGlyph *glyph = uiFontGlyph(codepoint);
    if (!glyph) glyph = uiFontGlyph('?');
    if (!glyph) return;
    uint8_t scale = packScale(textScale);
    int16_t x = getCursorX() + glyph->xOffset * scale;
    int16_t y = getCursorY();
    uint16_t *framebuffer = getFramebuffer();
    int16_t canvasWidth = width(), canvasHeight = height();
    for (uint8_t row = 0; row < glyph->height && row < 16; row++) {
      for (uint8_t col = 0; col < glyph->width && col < 16; col++) {
        uint16_t pixel = (uint16_t)row * 16u + col;
        uint8_t packed = glyph->alpha4[pixel >> 1];
        uint8_t alpha = (pixel & 1) ? packed >> 4 : packed & 0x0F;
        if (!alpha) continue;
        for (uint8_t dy = 0; dy < scale; dy++) {
          int16_t py = y + row * scale + dy;
          if (py < 0 || py >= canvasHeight) continue;
          for (uint8_t dx = 0; dx < scale; dx++) {
            int16_t px = x + col * scale + dx;
            if (px < 0 || px >= canvasWidth) continue;
            uint16_t &destination = framebuffer[(size_t)py * canvasWidth + px];
            destination = blend565(destination, fontColor, alpha);
          }
        }
      }
    }
    Arduino_Canvas::setCursor(getCursorX() + glyph->advance * scale, getCursorY());
  }

  void drawVectorGlyph(uint32_t codepoint) {
    uint8_t pixelSize = uiFontPixelSize(textScale);
    const RuntimeFontGlyph *glyph = runtimeFontGlyph(codepoint, pixelSize);
    if (!glyph) glyph = runtimeFontGlyph('?', pixelSize);
    if (!glyph) return;
    int16_t x = getCursorX() + glyph->left;
    int16_t y = getCursorY() + pixelSize - glyph->top;
    uint16_t *framebuffer = getFramebuffer();
    int16_t canvasWidth = width(), canvasHeight = height();
    for (uint16_t row = 0; row < glyph->height; row++) {
      int16_t py = y + row;
      if (py < 0 || py >= canvasHeight) continue;
      for (uint16_t col = 0; col < glyph->width; col++) {
        int16_t px = x + col;
        if (px < 0 || px >= canvasWidth) continue;
        uint8_t alpha = glyph->alpha[(uint32_t)row * glyph->width + col];
        if (!alpha) continue;
        uint16_t &destination = framebuffer[(size_t)py * canvasWidth + px];
        destination = blend565x8(destination, fontColor, alpha);
      }
    }
    Arduino_Canvas::setCursor(getCursorX() + glyph->advance, getCursorY());
  }

  uint8_t textScale = 1;
  uint16_t fontColor = RGB565_WHITE;
  bool fontActive = false;
  bool fontVector = false;
};

// Framebuffer completo en PSRAM: dibujamos todo y hacemos flush() (sin parpadeo)
TamaCanvas *gfx = new TamaCanvas(LCD_WIDTH, LCD_HEIGHT, panel);

void refreshUiFont() { gfx->setPackFont(contentHasUi()); }

static int16_t uiCenterX(const char *text, int16_t center = LCD_WIDTH / 2) {
  return center - gfx->textWidth(text) / 2;
}

static int16_t uiCenterIn(const char *text, int16_t x, int16_t width) {
  return x + (width - gfx->textWidth(text)) / 2;
}

static int16_t uiRightX(const char *text, int16_t right) {
  return right - gfx->textWidth(text);
}

static void uiDrawCenteredIn(const char *text, int16_t x, int16_t y,
                             int16_t width, int16_t height) {
  int16_t left, top, right, bottom;
  if (!gfx->textInkBounds(text, &left, &top, &right, &bottom)) {
    gfx->setCursor(uiCenterIn(text, x, width), y);
  } else {
    gfx->setCursor(x + (width - (right - left)) / 2 - left,
                   y + (height - (bottom - top)) / 2 - top);
  }
  gfx->print(text);
}

// Concrete item identities and optional artwork remain pack-owned. Procedural
// effect icons keep packs without artwork usable without firmware-side key maps.
static void drawItemIcon(const ItemEntry &item, int16_t cx, int16_t cy,
                         uint8_t scale = 1, bool muted = false) {
  const int16_t s = scale ? scale : 1;
  const uint16_t ink = muted ? 0x8410 : UI_INK;
  const uint16_t white = muted ? 0xBDF7 : UI_WHITE;
  const uint16_t red = muted ? 0x8410 : 0xF986;
  const uint16_t blue = muted ? 0x8410 : 0x3D7F;
  const uint16_t yellow = muted ? 0xA514 : 0xFFE0;
  const uint16_t purple = muted ? 0x8410 : 0xA99F;
  const uint16_t green = muted ? 0x8410 : UI_BAR_OK;
  auto x = [cx, s](int16_t n) { return (int16_t)(cx + n * s); };
  auto y = [cy, s](int16_t n) { return (int16_t)(cy + n * s); };

  ItemIconView icon;
  if (contentItemIcon(item.key, icon)) {
    int16_t left = cx - (int16_t)icon.width * s / 2;
    int16_t top = cy - (int16_t)icon.height * s / 2;
    for (uint8_t row = 0; row < icon.height; row++) {
      uint8_t col = 0;
      while (col < icon.width) {
        uint8_t paletteIndex = icon.pixels[(uint16_t)row * icon.width + col];
        if (paletteIndex == 0xFF) { col++; continue; }
        uint8_t end = col + 1;
        while (end < icon.width &&
               icon.pixels[(uint16_t)row * icon.width + end] == paletteIndex) end++;
        const uint8_t *packed = icon.palette565 + (uint16_t)paletteIndex * 2u;
        uint16_t color = muted ? 0x8410
                               : (uint16_t)packed[0] | ((uint16_t)packed[1] << 8);
        gfx->fillRect(left + col * s, top + row * s, (end - col) * s, s, color);
        col = end;
      }
    }
    return;
  }

  if (item.effect == ITEM_EFFECT_CATCH) {
    uint16_t top = red;
    if (item.param == ITEM_CATCH_GUARANTEED) top = purple;
    else if (item.param >= 200) top = ink;
    else if (item.param >= 150) top = blue;
    const int16_t radius = 10 * s;
    auto discHalfWidth = [radius](int16_t dy) {
      int16_t half = 0;
      while ((int32_t)(half + 1) * (half + 1) + (int32_t)dy * dy <=
             (int32_t)radius * radius) half++;
      return half;
    };
    gfx->fillCircle(cx, cy, radius, white);
    for (int16_t dy = -radius; dy < 0; dy++) {
      int16_t half = discHalfWidth(dy);
      gfx->fillRect(cx - half, cy + dy, half * 2 + 1, 1, top);
    }
    if (item.param == ITEM_CATCH_GUARANTEED) {
      gfx->fillCircle(x(-5), y(-4), 2 * s, white);
      gfx->fillCircle(x(5), y(-4), 2 * s, white);
      gfx->drawLine(x(-4), y(-7), x(-2), y(-2), white);
      gfx->drawLine(x(-2), y(-2), cx, y(-6), white);
      gfx->drawLine(cx, y(-6), x(2), y(-2), white);
      gfx->drawLine(x(2), y(-2), x(4), y(-7), white);
    } else if (item.param >= 200) {
      gfx->fillTriangle(x(-7), y(-7), x(-2), y(-7), x(-5), y(-2), yellow);
      gfx->fillTriangle(x(2), y(-7), x(7), y(-7), x(5), y(-2), yellow);
    } else if (item.param >= 150) {
      gfx->fillRect(x(-7), y(-6), 4 * s, 3 * s, red);
      gfx->fillRect(x(4), y(-6), 4 * s, 3 * s, red);
    }
    for (int16_t dy = -2 * s; dy < 2 * s; dy++) {
      int16_t half = discHalfWidth(dy);
      gfx->fillRect(cx - half, cy + dy, half * 2 + 1, 1, ink);
    }
    gfx->fillCircle(cx, cy, 4 * s, ink);
    gfx->fillCircle(cx, cy, 2 * s, white);
    gfx->drawCircle(cx, cy, radius, ink);
    return;
  }

  if (item.effect == ITEM_EFFECT_HEAL_HP) {
    uint16_t fill = item.param > 20 ? yellow : blue;
    gfx->fillRoundRect(x(-7), y(-7), 14 * s, 17 * s, 3 * s, white);
    gfx->fillRect(x(-5), y(-10), 10 * s, 4 * s, fill);
    gfx->drawRect(x(-5), y(-10), 10 * s, 4 * s, ink);
    gfx->drawRoundRect(x(-7), y(-7), 14 * s, 17 * s, 3 * s, ink);
    gfx->fillRect(x(-5), y(2), 10 * s, 6 * s, fill);
    gfx->fillRect(x(-1), y(-4), 3 * s, 9 * s, red);
    gfx->fillRect(x(-4), y(-1), 9 * s, 3 * s, red);
    if (item.param > 20) gfx->drawLine(x(-5), y(5), x(5), y(5), red);
    return;
  }

  if (item.effect == ITEM_EFFECT_CURE_STATUS) {
    gfx->fillRoundRect(x(-7), y(-10), 14 * s, 20 * s, 6 * s, white);
    gfx->fillRoundRect(x(-7), y(-10), 14 * s, 10 * s, 6 * s, purple);
    gfx->drawRoundRect(x(-7), y(-10), 14 * s, 20 * s, 6 * s, ink);
    gfx->fillRect(x(-1), y(-5), 3 * s, 10 * s, green);
    gfx->fillRect(x(-5), y(-1), 11 * s, 3 * s, green);
    return;
  }

  if (item.effect == ITEM_EFFECT_REVIVE) {
    gfx->fillTriangle(cx, y(-11), x(-9), cy, cx, y(11), yellow);
    gfx->fillTriangle(cx, y(-11), x(9), cy, cx, y(11), white);
    gfx->drawLine(cx, y(-11), x(-9), cy, ink);
    gfx->drawLine(x(-9), cy, cx, y(11), ink);
    gfx->drawLine(cx, y(11), x(9), cy, ink);
    gfx->drawLine(x(9), cy, cx, y(-11), ink);
    gfx->drawLine(x(-9), cy, x(9), cy, ink);
    return;
  }

  if (item.effect == ITEM_EFFECT_TRAINING_FLOOR) {
    uint16_t fill = item.flags == ITEM_STAT_ATK ? red
                    : item.flags == ITEM_STAT_DEF ? blue : yellow;
    gfx->fillRoundRect(x(-7), y(-7), 14 * s, 17 * s, 3 * s, fill);
    gfx->fillRect(x(-5), y(-10), 10 * s, 4 * s, white);
    gfx->drawRect(x(-5), y(-10), 10 * s, 4 * s, ink);
    gfx->drawRoundRect(x(-7), y(-7), 14 * s, 17 * s, 3 * s, ink);
    if (item.flags == ITEM_STAT_ATK) {
      gfx->fillCircle(cx, y(1), 4 * s, white);
      gfx->fillRect(x(-7), y(-1), 14 * s, 3 * s, white);
    } else if (item.flags == ITEM_STAT_DEF) {
      gfx->fillTriangle(cx, y(-4), x(-5), y(-1), cx, y(7), white);
      gfx->fillTriangle(cx, y(-4), x(5), y(-1), cx, y(7), white);
    } else {
      gfx->fillTriangle(x(2), y(-5), x(-4), y(2), x(1), y(2), white);
      gfx->fillTriangle(x(-1), y(6), x(5), y(-1), cx, y(-1), white);
    }
    return;
  }

  if (item.effect == ITEM_EFFECT_BATTLE_STAGE) {
    uint16_t fill = (item.flags == ITEM_STAT_ATK || item.flags == ITEM_STAT_SPA) ? red
                    : (item.flags == ITEM_STAT_DEF || item.flags == ITEM_STAT_SPD) ? blue
                    : yellow;
    gfx->fillCircle(cx, cy, 10 * s, fill);
    gfx->drawCircle(cx, cy, 10 * s, ink);
    gfx->fillTriangle(cx, y(-7), x(-5), y(-1), x(5), y(-1), white);
    gfx->fillRect(x(-2), y(-1), 5 * s, 8 * s, white);
    if (item.flags == ITEM_STAT_SPA || item.flags == ITEM_STAT_SPD) {
      gfx->fillCircle(x(-6), y(5), s, white);
      gfx->fillCircle(x(6), y(5), s, white);
    } else if (item.flags == ITEM_STAT_SPE) {
      gfx->drawLine(x(-7), y(6), x(7), y(6), ink);
    }
    return;
  }

  if (item.effect == ITEM_EFFECT_TEACH_MOVE) {
    gfx->fillTriangle(cx, y(-11), x(-10), y(1), cx, y(11), purple);
    gfx->fillTriangle(cx, y(-11), x(10), y(1), cx, y(11), blue);
    gfx->drawLine(cx, y(-11), x(-10), y(1), ink);
    gfx->drawLine(x(-10), y(1), cx, y(11), ink);
    gfx->drawLine(cx, y(11), x(10), y(1), ink);
    gfx->drawLine(x(10), y(1), cx, y(-11), ink);
    gfx->fillCircle(cx, cy, 3 * s, white);
    return;
  }

  if (item.effect == ITEM_EFFECT_BATTLE_MECHANIC) {
    if (item.flags == ITEM_MECHANIC_Z_MOVE) {
      gfx->fillTriangle(cx, y(-11), x(-9), cy, cx, y(11), yellow);
      gfx->fillTriangle(cx, y(-11), x(9), cy, cx, y(11), purple);
      gfx->drawLine(x(-4), y(-5), x(4), y(-5), ink);
      gfx->drawLine(x(4), y(-5), x(-4), y(5), ink);
      gfx->drawLine(x(-4), y(5), x(4), y(5), ink);
    } else if (item.flags == ITEM_MECHANIC_DYNAMAX) {
      gfx->fillCircle(cx, cy, 9 * s, red);
      gfx->drawCircle(cx, cy, 10 * s, ink);
      gfx->fillTriangle(cx, y(-8), x(-3), y(-1), x(3), y(-1), white);
      gfx->fillTriangle(cx, y(8), x(-3), y(1), x(3), y(1), white);
      gfx->fillCircle(cx, cy, 2 * s, white);
    } else {
      gfx->fillCircle(cx, cy, 10 * s, blue);
      gfx->drawCircle(cx, cy, 10 * s, ink);
      gfx->drawCircle(cx, cy, 6 * s, white);
      gfx->drawLine(x(-7), y(5), x(7), y(-5), yellow);
      gfx->fillCircle(x(-5), y(4), 2 * s, yellow);
      gfx->fillCircle(x(5), y(-4), 2 * s, yellow);
    }
    return;
  }

  gfx->fillCircle(cx, cy, 9 * s, green);
  gfx->drawCircle(cx, cy, 9 * s, ink);
  gfx->fillCircle(cx, cy, 3 * s, white);
}

TouchDrvCST92xx touch;
Pet pet;
QuizRuntime quiz;

// Sprite data is resolved only through the installed region pack.
PmdMon pmd;         // sprite PMD multi-accion (pantalla principal)
PmdMon evoPmd;      // forma anterior, solo durante el parpadeo de evolucion
int16_t monFor = -2;
bool monShinyFor = false;
PetGender monGenderFor = GENDER_UNKNOWN;

// comportamiento del bicho en pantalla
struct {
  uint8_t mode = 0;     // 0 idle, 1 paseo, 2 gesto one-shot
  uint8_t act = PMD_IDLE;
  uint32_t t0 = 0;      // inicio de la animacion en curso
  uint32_t until = 0;   // fin del estado actual
  float x = 233, targetX = 233;
} beh;
#define PET_GROUND 304  // linea de suelo de la mascota
PmdMon galleryPmd;  // sprite grande de la vista detalle de la galeria (PMD/TPK2, legal)

// galeria pokedex
bool galleryOpen = false;
bool galleryDirty = false;
// 16 to a page, and the Pokedex is browsed ONE REGION AT A TIME. Three
// generations flat is 25 pages of swiping to reach Hoenn, which is not a
// Pokedex, it is a scroll. A vertical swipe changes region and a horizontal one
// pages within it, so nothing is ever more than ten pages from the front.
// ALL is deliberately not offered here -- it is the thing being replaced.
#define GAL_PER_PAGE 16
#define GAL_REGIONS (regionCount() - 1)          // the real regions, not ALL
#define GAL_LO (regionInfo(galleryRegion % GAL_REGIONS).lo)
#define GAL_HI (regionInfo(galleryRegion % GAL_REGIONS).hi)
#define GAL_SPAN (GAL_HI - GAL_LO + 1)
#define GAL_PAGES ((GAL_SPAN + GAL_PER_PAGE - 1) / GAL_PER_PAGE)
uint8_t galleryRegion = 0;
// Both the Pokedex and the gym ladder now open on a REGION CHOOSER rather than
// dropping you into whichever region was last viewed. The vertical swipe that
// changes region still works, but it is invisible, so on its own it meant the
// Johto and Hoenn content looked absent.
bool galleryPick = false;
bool gymPick = false;
uint8_t rpickPage = 0;      // the region chooser is paged; shared by all 3 modes
int galleryPage = 0;        // GAL_PAGES paginas de GAL_PER_PAGE
int16_t galleryDetail = 0;  // dex en vista detalle, 0 = rejilla

bool screenOff = false;       // pulsacion corta del boton PWR
bool cardOpen = false;        // ficha del bicho (deslizar vertical)
bool natureInfoOpen = false;
bool kbOpen = false;
enum : uint8_t { KB_PET = 0, KB_TRAINER };
uint8_t kbTarget = KB_PET;          // teclado para renombrar al bicho
char nameBuf[12] = "";
uint8_t nameLen = 0;
#define CARD_PAGES 4   // profile, stats, moves, progress -- medals moved to
                       // the player card, where the totals already live
uint8_t cardPage = 0;         // 0 perfil, 1 stats+medallas
// Menu overlay: opened by tapping the pet's name on the main screen. The
// horizontal swipe is already taken by the Pokedex (it pages through 10 pages
// internally), so a hub on that axis would be ambiguous; the header was inert
// and is the only free surface left.
bool menuOpen = false;
#define MENU_X 73
// Party and gym rows no longer live here; storage is opened from the main slot
// indicators and battles from the swipe-down hub. Sized to the bezel -- the panel is 320 wide, so 160 from
// the centre, and sqrt(233^2 - 160^2) = 169 means it can only span y 64..402.
#define MENU_Y 75
#define MENU_W 320
#define MENU_H 316
#define MENU_ROW_H 52
#define MENU_ROW_GAP 6
// 5 rows: LEAD / POKEDEX / SETTINGS / RELEASE-or-FAREWELL / POWER OFF. At MENU_Y
// 75 the panel spans 75..391 and the round display gives a half-width of 171 there against
// the 160 a row needs, so the corners stay on glass.
#define MENU_ROWS 5
#define MENU_ROW_Y(i) (MENU_Y + 16 + (i) * (MENU_ROW_H + MENU_ROW_GAP))

bool menuRowDisabled(uint8_t row) {
  return row == 0 && party.activeIndex() == party.leadIndex();
}

// Swipe-down navigation is deliberately separate from the name-band menu.
// The latter remains the only route to Pokedex, settings and creature exit; this
// full page is the requested fast route to the three player-wide destinations.
bool navMenuOpen = false;
#define NAVMENU_BTN_X 85
#define NAVMENU_BTN_W 296
#define NAVMENU_BTN_H 72
#define NAVMENU_BTN_GAP 18
#define NAVMENU_BTN_Y(i) (100 + (i) * (NAVMENU_BTN_H + NAVMENU_BTN_GAP))
#define NAVMENU_ROWS 3

// Box is the only cultivation-management screen. Selecting any Box cell opens
// its embedded cultivation picker: an empty cell deposits the chosen creature,
// while an occupied cell can be withdrawn into a free cultivation slot or
// exchanged with a chosen member. `partyPick` reuses that picker when a captured
// creature needs a replacement slot.
bool partyPick = false;
PartyMon partyPending;
#define BURY_TARGET_NONE  -2
#define BURY_TARGET_PET   -1
int8_t buryTarget = BURY_TARGET_NONE;
bool boxOpen = false;
uint8_t boxPage = 0;
uint8_t boxSel = 0;        // selected Box slot + 1; may refer to an empty cell
#define BOX_PER_PAGE 6
uint32_t partyBannerUntil = 0;   // "<name> joined the party!"
char partyBannerName[32] = "";
#define PARTY_CELL_W 150
#define PARTY_CELL_H 70
#define PARTY_GRID_X 78
#define PARTY_GRID_Y 88
#define PARTY_THUMB_X_OFF -14
#define PARTY_THUMB_Y_OFF 3
#define PARTY_THUMB_SCALE 2
static constexpr int THUMB_CELL = 80;
#define PARTY_TEXT_X_OFF 52

// Bag inventory. Item identities and labels come from the move pack.
bool bagOpen = false;
ItemKey bagDetailKey = ITEM_KEY_NONE;
enum BagView : uint8_t {
  BAG_VIEW_LIST,
  BAG_VIEW_ACTIONS,
  BAG_VIEW_DETAIL,
  BAG_VIEW_TARGET,
  BAG_VIEW_QUANTITY,
  BAG_VIEW_CONFIRM,
};
uint8_t bagView = BAG_VIEW_LIST;
ItemKey bagSelectedKey = ITEM_KEY_NONE;
MoveId bagSelectedMove = MOVE_NONE;
uint8_t bagDiscardAmount = 1;
UiScrollView bagScroll;
#define BAG_LIST_Y 82
#define BAG_LIST_H 274
#define BAG_ROW_STEP 54
#define BAG_ROW_H 46
#define BAG_ACTION_X 93
#define BAG_ACTION_W 280
#define BAG_ACTION_Y(i) (148 + (i) * 58)
#define BAG_ACTION_H 48
MoveId bagDetailMove = MOVE_NONE;
enum BagStoneDialog : uint8_t {
  BAG_STONE_DIALOG_NONE = 0,
  BAG_STONE_DIALOG_CONFIRM,
  BAG_STONE_DIALOG_INCOMPATIBLE,
  BAG_STONE_DIALOG_KNOWN,
};
BagStoneDialog bagStoneDialog = BAG_STONE_DIALOG_NONE;
uint8_t bagStoneTarget = PARTY_SLOTS;

#define DEAD_BTN_Y 346
#define DEAD_BTN_W 150
#define DEAD_BTN_H 48
#define DEAD_REVIVE_X 74
#define DEAD_BURY_X 242

bool clockOpen = false;       // pantalla de ajuste de hora (deslizar abajo)
int clockH = 12, clockM = 0;  // hora en edicion
uint8_t userBrightness = 7;   // brillo normal 1..10; el reposo aun puede atenuarlo
#define BRIGHT_TRACK_X 156
#define BRIGHT_TRACK_Y 283
#define BRIGHT_TRACK_W 154
#define BRIGHT_HIT_X 132
#define BRIGHT_HIT_Y 250
#define BRIGHT_HIT_W 204
#define BRIGHT_HIT_H 44

void loadUserBrightness() {
  Preferences prefs;
  prefs.begin("tamapoke", true);
  userBrightness = prefs.getUChar("bright", 7);
  prefs.end();
  if (userBrightness < 1 || userBrightness > 10) userBrightness = 7;
}

void setUserBrightness(uint8_t level, bool persist) {
  userBrightness = level < 1 ? 1 : level > 10 ? 10 : level;
  if (!persist) return;
  Preferences prefs;
  prefs.begin("tamapoke", false);
  prefs.putUChar("bright", userBrightness);
  prefs.end();
}

uint8_t brightnessLevelAt(int16_t x) {
  if (x < BRIGHT_TRACK_X) x = BRIGHT_TRACK_X;
  if (x > BRIGHT_TRACK_X + BRIGHT_TRACK_W) x = BRIGHT_TRACK_X + BRIGHT_TRACK_W;
  return 1 + ((x - BRIGHT_TRACK_X) * 9 + BRIGHT_TRACK_W / 2) / BRIGHT_TRACK_W;
}

// escena de bano: espuma sobre el bicho y limpieza al reventar
uint32_t bathUntil = 0;
struct { int16_t x, y; uint8_t r, ph; } bubbles[14];
uint32_t feedMenuUntil = 0;   // selector de comida abierto hasta este millis
enum QuizPurpose : uint8_t { QUIZ_PURPOSE_NONE = 0, QUIZ_PURPOSE_CARE, QUIZ_PURPOSE_BATTLE };
QuizPurpose quizPurpose = QUIZ_PURPOSE_NONE;
CareAction quizCareAction;
uint8_t quizBattleMoveSlot = 0;
bool quizShowsGameResult = false;

// minijuego "toques": mantener la pokeball en el aire
bool gameOpen = false;
uint32_t gameOverUntil = 0;
// The ball game used to run until you missed three times or walked away, so a
// session had no length and the reward no shape. The bag is 10 s and the
// reaction test 15 s; this sits between them, and whichever comes first --
// the clock or three misses -- ends it.
#define GAME_MS 20000UL
uint32_t gameUntil = 0;
float ballX, ballY, ballVX, ballVY, gamePetX;
uint8_t gameScore, gameMisses;
float hitX, hitY;             // ultimo golpe (anillo de impacto)
uint32_t hitTime = 0;
bool gameNewHi = false;

// saco de entrenamiento (entrena la fuerza)
bool sackOpen = false;
uint32_t sackUntil = 0, sackOverUntil = 0;
uint16_t sackHits = 0;
float sackShake = 0;
uint8_t sackGain = 0;
bool sackNewHi = false;

// training submenu (the 5th icon): routes to the trainer for each stat.
bool trainOpen = false;

// Move picker: battle slots can only trade with this creature's four active and
// four reserve learned moves.
bool movePickOpen = false;
bool moveInfoOpen = false;
uint8_t movePickSlot = 0;   // which of the 4 battle slots is being changed
uint8_t movePickParty = 0;  // 0 = the live pet, else the party slot + 1
uint8_t movePickPage = 0;
void renderMoveInfo();
int drawWrappedText(const char *text, int x, int y, int width, uint8_t maxLines);
int drawWrappedTextWindow(const char *text, int x, int y, int width,
                          uint8_t skipLines, uint8_t maxLines, uint8_t *totalLines);
#define MOVE_ROW_Y(i) (96 + (i) * 58)
#define MOVE_ROW_H 50
#define MOVE_NAME_TOP 6
#define MOVE_CHIP_TOP 27
#define MOVE_CHIP_H 18
#define MOVE_PICK_PER_PAGE 5
#define MOVE_PICK_Y(i) (76 + (i) * 58)

// ---------- battle ----------
// The move menu is a 2x2 grid rather than four stacked rows: the round panel
// has to fit both creatures, both HP bars and the menu, and four full-width
// rows do not leave room for the sprites.
bool battleOpen = false;
#define BTL_TAP_DEBOUNCE_MS 300
uint32_t btlLastAcceptedTap = 0;
bool btlTapDebounceArmed = false;

static void btlResetTapDebounce() {
  btlTapDebounceArmed = false;
}

enum : uint8_t {
  SCR_QUIZ = 0, SCR_LANGUAGE, SCR_STARTER, SCR_REGION, SCR_GALLERY, SCR_DEXPICK, SCR_MOVEPICK, SCR_BOX,
  SCR_KEYBOARD, SCR_CARD, SCR_PLAYER, SCR_CLOCK, SCR_GYM, SCR_GYMPICK,
  SCR_LAN, SCR_PICK, SCR_BATTLE, SCR_WIN, SCR_TRAIN, SCR_MENU,
  SCR_GAME, SCR_MAIN, SCR_BAG, SCR_COUNT
};
extern const char *const SCREEN_NAME[SCR_COUNT];   // const is internal linkage in C++

// Declared HERE, above every use. tools/emu/build.sh generates a proto.h with
// every prototype at the top, so the emulator compiled these happily while
// arduino-cli -- which relies on the IDE's auto-prototyping and does not always
// produce one -- did not. A green emulator build is not proof the firmware
// builds; only arduino-cli is.
void bootReport();
uint8_t uiCurrentScreen();
bool beginCareQuiz(const CareAction &action, bool showGameResult);
bool beginBattleQuiz(uint8_t moveSlot);
void updateQuiz(uint32_t now);
bool quizBlocking();
void commitBattleMove(uint8_t moveSlot, uint8_t percent);
void settleCareInteraction(const CareAction &action, uint8_t percent, bool correct,
                           bool showGameResult, uint32_t now);
void startBathAnimation(uint32_t now);
void renderQuiz();
void renderFirstBootLanguage();
void quizTap(int16_t x, int16_t y);
void renderBag();
void bagTap(int16_t x, int16_t y);
static void btlDismissWin();
static void btlApplyEntry(uint8_t side);
static bool btlReplaceActive(uint8_t side, uint8_t next, bool announce);
void btlCompleteCapture();
void btlUpdateThrow(uint32_t now);
void btlFinish(bool won);
void startWildBattle(uint8_t region, bool hard);
void drawSparkleParticles(int cx, int groundY, uint32_t now, uint8_t scale = 1);
uint32_t pmdActTotalMs(const PmdAct &action);
const char *const SCREEN_NAME[SCR_COUNT] = {
  "quiz", "language", "starter", "region", "gallery", "dexpick", "movepick", "box",
  "keyboard", "card", "player", "clock", "gym", "gympick",
  "lan", "pick", "battle", "win", "train", "menu",
  "minigame", "main", "bag"
};

Combatant btlYou, btlFoe;
BattleField btlField;
bool btlOver = false;
bool btlWon = false;
bool btlWild = false;
bool btlFoeDetailOpen = false;
uint8_t btlFoeDetailPage = 0;
#define BTL_FOE_DETAIL_PAGES 3
PartyMon btlWildMon;
PartyMon capturedMon;

const char *rareMark(bool rare) {
  return rare ? "*" : "";
}
bool btlNewBadge = false;
uint32_t btlWinUntil = 0;   // the win screen is up
uint16_t btlRewardTraining[3] = { 0, 0, 0 };
static constexpr uint8_t BTL_REWARD_ITEM_MAX = 4;
ItemRef btlRewardItems[BTL_REWARD_ITEM_MAX] = {};
uint8_t btlRewardItemCount = 0;
UiScrollView btlRewardScroll;

static void btlResetRewardSummary() {
  btlRewardTraining[0] = btlRewardTraining[1] = btlRewardTraining[2] = 0;
  for (ItemRef &item : btlRewardItems) item = ItemRef();
  btlRewardItemCount = 0;
  btlRewardScroll.reset();
}

static void btlRememberRewardItem(ItemRef item) {
  if (item && btlRewardItemCount < BTL_REWARD_ITEM_MAX)
    btlRewardItems[btlRewardItemCount++] = item;
}
// A trainer fight is a run of 1v1s: both sides queue their squad and the next
// one steps up when the current one faints. This is the whole difficulty curve
// -- no gating, just attrition, so one strong creature sweeps Brock and dies
// four deep into Lance.
// The ladder is now sequential: a leader opens once the previous one is beaten,
// tracked per difficulty so hard mode is its own run. This replaces the earlier
// "no gating, attrition is the gate" rule -- with both ladders level-capped,
// nothing stopped you opening with Lance and simply losing, which read as a
// dead end rather than a challenge.
// reaction test (trains SPEED)
bool spdOpen = false;
uint32_t spdUntil = 0, spdOverUntil = 0, spdBorn = 0;
int16_t spdX = 0, spdY = 0;
uint16_t spdHits = 0, spdMisses = 0;
uint8_t spdGain = 0;
bool spdNewHi = false;

bool gymOpen = false;
bool gymHard = false;   // which ladder the list is showing

// LAN battle. `lanOpen` is the pairing screen; once both squads are known the
// normal battle screen takes over with btlLink set.
bool lanOpen = false;
Link lan;
// What the shared region chooser is being used FOR. It only changes the
// subtitle and whether there is a way back: at first boot every count would
// read zero, which tells the player nothing, and there is nowhere to go back to.
#define RPICK_FOR_GYMS  0
#define RPICK_FOR_DEX   1
#define RPICK_FOR_START 2
// Three rows is what the round panel fits above the BACK label. The dex now
// lists four regions and will list more, so the chooser PAGES rather than
// growing -- and every paged screen in this sketch has to be driven by
// swipe_test, which is why that test exists at all.
#define RPICK_PER_PAGE  3
extern uint8_t rpickPage;
static void renderRegionPick(uint8_t mode);   // the region chooser, defined below
// Not static: swipe_test drives these so it asks the FIRMWARE for the page
// count instead of recomputing it from its own copy of RPICK_PER_PAGE, which
// would prove the transcription rather than the screen.
uint8_t rpickRegions(uint8_t mode);           // rows this mode lists (gyms: 3)
uint8_t rpickPageCount(uint8_t mode);
uint8_t rpickModeNow();                       // which chooser is up, or 0xFF
static bool rpickSwipe(int dir);              // true if it handled the gesture
static int regionPickTap(int16_t x, int16_t y, uint8_t mode);
static void drawEggRegion();          // defined with the egg screen helpers
static int eggRegionTap(int16_t x, int16_t y);
void drawGenderIcon(PetGender gender, int x, int y, int scale);
static void drawBtlBack();
static void btlLinkPoll();   // defined with the battle code, called from render()
static void btlSwitchTo(uint8_t i);
static void btlResolve(MoveId yourMove, uint8_t yourPercent = 100,
                       BattleMechanic yourMechanic = BMECH_NONE);
static void btlSetPersistentDead(uint8_t index, bool dead);
static void btlMarkEntered(uint8_t index);
// The opposing team stays live so trainer and linked creatures can switch out
// and back in without losing HP or transient member state. Host side only for
// linked battles; the guest takes absolute state off the wire.
Combatant btlFoeSquad[TRAINER_TEAM_MAX];
uint8_t btlFoeSquadN = 0;
uint8_t btlMyAct = 0;        // host: our own action, latched until theirs lands
uint8_t btlMyPercent = 0;    // answer effect attached to that latched move
BattleMechanic btlMyMechanic = BMECH_NONE;
// Which ladder the gym screen and the current fight belong to. The battle keeps
// its own copy so that leaving the gym list mid-fight cannot retarget the badge.
// The smallest a button may be. Three separate "hard to hit" reports -- the
// battle grid and navigation controls among them -- were all the same mistake:
// a control sized to fit its label rather than a finger.
// 44 px is the usual guidance and roughly a fingertip on this 466 px panel.
#define UI_TAP_MIN 44

// The LAN battle button on the gym region chooser.
#define LANBTN_W 190
#define LANBTN_H UI_TAP_MIN
#define LANBTN_X (233 - LANBTN_W / 2)
#define LANBTN_Y 322

#define BOXPICK_BACK_Y 376
#define BOXPICK_BACK_H UI_TAP_MIN
#define BOXPICK_BACK_X 133
#define BOXPICK_BACK_W 200
#define BOXPICK_ACTION_X (PARTY_GRID_X + PARTY_CELL_W + 10)
#define BOXPICK_ACTION_W PARTY_CELL_W

uint8_t gymRegion = 0;
uint8_t btlRegion = 0;
bool gShowAllAvatars = false;  // emulator screenshot aid, never set on hardware
bool btlPetIn = false;       // was the currently displayed team slot selected?
GymIvReward btlIvReward = GYM_IV_NONE;
uint8_t btlIvWhich = 0;
bool btlLink = false;      // this fight is against another device
bool btlLinkHost = false;
static bool gymUnlocked(uint8_t idx, bool hard) {
  return idx == 0 || player.hasBadge(gymRegion, idx - 1, hard);
}

// Team select. Candidate bits map directly to the six cultivation slots.
bool pickOpen = false;
// The team picker serves the gym ladder and the LAN screen both. PICK_LAN is
// not a trainer index: squadCap() already returns an uncapped six for anything
// past the roster, which is what a LAN battle wants -- two players who know
// each other can bring what they like.
#define PICK_LAN 0xFF
static void lanOffer(bool host);
uint8_t pickTrainer = 0;
bool pickHard = false;
bool lanWantHost = true;   // which button opened the picker
uint16_t squadMask = 0xFFFF;   // everything, until the player says otherwise
uint8_t pickPage = 0;
#define PICK_PER_PAGE 6
#define PICK_CELL_W 150
#define PICK_CELL_H 74
#define PICK_X(i) (78 + ((i) % 2) * (PICK_CELL_W + 10))
#define PICK_Y(i) (86 + ((i) / 2) * (PICK_CELL_H + 6))
#define PICK_GO_Y 350
// BACK beside FIGHT on the team-select screen. There was no way out of it but a
// swipe, which is invisible -- the same complaint as everywhere else.
#define PICK_BTN_W 155
#define PICK_BTN_H UI_TAP_MIN
#define PICK_BACK_X (233 - PICK_BTN_W - 7)
#define PICK_GO_X (233 + 7)
bool playerOpen = false;
// One badge page per gym region, then the medals. Three ladders will not fit on
// one page, and the page you are on IS the region -- no extra control needed,
// and horizontal paging already works everywhere else.
uint8_t playerPage = 0;
#define PLAYER_PAGES (regionAll() + 1)
#define playerBadgeRegion (regionAll() ? playerPage % regionAll() : 0)
uint8_t gymPage = 0;
#define GYM_ROWS 5
#define GYM_ROW_Y(i) (110 + (i) * 50)
int8_t btlTrainer = -1;      // index into TRAINERS, -1 = a one-off fight
bool btlHard = false;
Combatant btlSquad[TRAINER_TEAM_MAX + 1];
uint8_t btlSquadN = 0, btlSquadAt = 0;
// Squad-index bits: merely being selected is not participation; entering is.
uint8_t btlEnteredMask = 0;
uint8_t btlFoeAt = 0;
// Each battle copy points back to the persistent creature it came from. Damage
// remains temporary; death and revival cross this boundary explicitly.
#define BTL_SOURCE_NONE -1
int8_t btlSquadSource[TRAINER_TEAM_MAX + 1];
int8_t lanMineSource[TRAINER_TEAM_MAX];
uint8_t lanMineSourceN = 0;
uint8_t btlSwapPending = 0;   // bit0 player, bit1 opponent

// Animation. Deliberately built on the thumbnails the screen already draws
// rather than on PmdMon: three PmdMon blobs are live already, and the battle
// has to stay graceful on a board with no SD at all (S_NO_SPRITES). Index 0 is
// you, 1 is the foe.
uint32_t btlLungeUntil[2] = { 0, 0 };   // acted: leans toward the opponent
uint32_t btlHitUntil[2] = { 0, 0 };     // was hit: jitters and flashes
uint16_t btlHpShown[2] = { 0, 0 };      // bars ease toward the real value
// Two streamed sprites, so the creatures can actually swing and flinch. They
// cost ~135 KB of PSRAM each on average and are freed when the fight ends. The
// player's side is NOT the global `pmd`: the active creature may be a banked
// party member rather than the live pet.
PmdMon btlPmd[2];
int32_t btlPmdKey[2] = { 0, 0 };
// A faint used to swap the next creature in instantly, inside the same call
// that resolved the turn -- which is why it felt like a jump cut. The swap is
// now deferred: the fainted one drops out of frame, and the replacement slides
// in only once the player dismisses that message.
uint32_t btlFaintUntil[2] = { 0, 0 };
uint32_t btlEnterUntil[2] = { 0, 0 };
int8_t btlSwapWho = -1;        // 0 = your side, 1 = the foe's, -1 = nothing due
#define BTL_FAINT_MS 700
#define BTL_ENTER_MS 420
// Capture is a battle animation, not a separate screen: the item and random
// roll settle once, then rendering advances through these non-blocking beats.
enum : uint8_t {
  BTL_CAPTURE_NONE = 0,
  BTL_CAPTURE_CENTER,
  BTL_CAPTURE_THROW,
  BTL_CAPTURE_ABSORB,
  BTL_CAPTURE_SHAKE,
  BTL_CAPTURE_SUCCESS,
  BTL_CAPTURE_FAILURE,
  BTL_CAPTURE_RETURN,
};
bool btlCaptureAnimating = false;
bool btlCaptureSuccess = false;
bool btlCaptureCuePlayed = false;
uint32_t btlCaptureStartedAt = 0;
ItemKey btlCaptureItem = ITEM_KEY_NONE;
ThrowGestureDetector btlThrowDetector;
bool btlThrowArmed = false;
uint32_t btlThrowStartedAt = 0;
ItemKey btlThrowItem = ITEM_KEY_NONE;
#define BTL_THROW_TIMEOUT_MS 3000UL
#define BTL_CAPTURE_CENTER_MS 600UL
#define BTL_CAPTURE_THROW_MS 650UL
#define BTL_CAPTURE_ABSORB_MS 350UL
#define BTL_CAPTURE_SHAKE_MS 1350UL
#define BTL_CAPTURE_RESULT_MS 700UL
#define BTL_CAPTURE_RETURN_MS 600UL

void btlResetThrow() {
  btlThrowArmed = false;
  btlThrowStartedAt = 0;
  btlThrowItem = ITEM_KEY_NONE;
  motionStop();
}

// battle menu: root, moves, switch, direct warehouse view, revive target
uint8_t btlMenu = 0;
uint8_t btlItemPage = 0;
uint8_t btlTargetPage = 0;
ItemKey btlPendingItem = ITEM_KEY_NONE;
BattleMechanic btlPendingMechanic = BMECH_NONE;
BattleMechanic btlWildMechanic = BMECH_NONE;
MegaFormKind btlPendingMegaForm = MEGA_FORM_NONE;
MegaFormKind btlWildMegaForm = MEGA_FORM_NONE;
BattleSideMechanics btlYourMechanics, btlFoeMechanics;
#define BTL_LUNGE_MS 260
#define BTL_HIT_MS 420
char btlMsg[6][96];

static const char *displaySpeciesName(SpeciesId dex, const char *nickname) {
  return nickname && nickname[0] ? nickname : speciesName(dex);
}

static const char *displayCombatantName(const Combatant &combatant) {
  const char *fallback = dexEntry(combatant.dex).name;
  return combatant.name[0] && strcmp(combatant.name, fallback) != 0
      ? combatant.name
      : speciesName(combatant.dex);
}
uint8_t btlMsgCount = 0;   // queued lines; a tap shows the next
#define BTL_CELL_W 160
#define BTL_CELL_H 44
#define BTL_GRID_X 69
#define BTL_GRID_Y 274
#define BTL_CELL_X(i) (BTL_GRID_X + ((i) % 2) * (BTL_CELL_W + 8))
#define BTL_CELL_Y(i) (BTL_GRID_Y + ((i) / 2) * (BTL_CELL_H + 8))

// A cell's HIT area is bigger than the cell that is drawn. On the board the two
// bottom buttons were much harder to hit than the top two: the drawn cells are
// only 44 px tall, there was an 8 px dead gap between the rows, and everything
// below the bottom row was dead too -- so a finger landing low, or a touch panel
// reading a few pixels high, missed entirely. Nothing else is tappable in this
// area while the grid is open, so the slop costs nothing.
//
// The gap between the two rows and the two columns is split down the middle, and
// the bottom row additionally claims the empty space beneath it.
#define BTL_HIT_PAD 4
// The bottom row keeps extra room downward, but not so much that it reaches the
// BACK bar below it -- the mistake made once already with BOX and CLOSE.
#define BTL_HIT_BOTTOM 6
// BACK, under the grid: the move and switch screens had no way out except
// choosing something.
// The gym list's EASY/HARD pill. It was 24 px tall, which is half a fingertip.
// Sits between the title and the first leader row. At 72 with a 44 px height it
// ran to 116 and overlapped the first row, which begins at 110 -- introduced
// when the pill was enlarged to a real tap target.
#define GYMDIF_Y 60
#define GYMDIF_H UI_TAP_MIN
#define BTL_BACK_W 190
#define BTL_BACK_H UI_TAP_MIN
#define BTL_BACK_X (233 - BTL_BACK_W / 2)
#define BTL_BACK_Y 384
#define BTL_HIT_X0(i) (BTL_CELL_X(i) - BTL_HIT_PAD)
// The far edges stop one pixel short so the four boxes TILE: the gap between
// two cells is split down the middle with no pixel left over and none shared.
#define BTL_HIT_X1(i) (BTL_CELL_X(i) + BTL_CELL_W + BTL_HIT_PAD - 1)
#define BTL_HIT_Y0(i) (BTL_CELL_Y(i) - BTL_HIT_PAD)
#define BTL_HIT_Y1(i) (BTL_CELL_Y(i) + BTL_CELL_H - 1 + \
                       ((i) / 2 ? BTL_HIT_BOTTOM : BTL_HIT_PAD))

// Which cell a point falls in, or -1. Exposed (not static) so a test can sweep
// the panel and prove there are no dead pixels between the cells -- the bug that
// made the bottom row hard to press was a gap, not a wrong rectangle.
int btlCellIndexAt(int16_t x, int16_t y);

// Used by BOTH the move grid and the switch grid. They had a copy each of the
// same rectangle test, which is exactly how two halves of one control drift.
static inline bool btlCellHit(int i, int16_t x, int16_t y) {
  return x >= BTL_HIT_X0(i) && x <= BTL_HIT_X1(i) &&
         y >= BTL_HIT_Y0(i) && y <= BTL_HIT_Y1(i);
}
#define TRAIN_X 73
#define TRAIN_Y 96
#define TRAIN_W 320
#define TRAIN_H 274
#define TRAIN_ROW_H 56
#define TRAIN_ROW_GAP 8
#define TRAIN_ROW_Y(i) (TRAIN_Y + 54 + (i) * (TRAIN_ROW_H + TRAIN_ROW_GAP))

#define CX 233  // centro de la pantalla redonda
#define CY 233
#define PET_CY 202  // centro vertical del sprite

// Main-screen roster navigation. Only occupied cultivation slots get dots;
// horizontal swipes switch creatures and tapping the dot strip opens the Box.
#define PETNAV_HIT_Y 24
#define PETNAV_HIT_H 44
#define PETNAV_DOT_Y 46

static const uint16_t INK_K = 0x18C4;  // spriteColor('k')

// botones de icono siguiendo el arco inferior de la pantalla redonda
// (los exteriores van mas altos para no salirse del circulo)
struct Btn {
  int16_t cx, cy;
  const char *const *icon;
};
// Five across the arc: spacing tightened 62 -> 54 so the outer pair stays far
// enough in to keep its old y. Lifting them instead would have run the row into
// the ENE/HYG bars, which end at y=361 -- the buttons are 52 tall, so any centre
// above 387 overlaps them.
// Four, not five. The ball had its own icon here until it became DEFENCE's
// trainer and moved into the training menu -- at which point tapping it just
// opened the same menu the dumbbell does, two icons for one destination.
// They sit on the panel's curve: y = 406 - dx^2/729.
#define BTN_COUNT 4
// Referred to by NAME, never by literal index. Removing the ball icon shifted
// every index by one and drawButtons() still had `i != 2` meaning LIGHT -- which
// silently made the BATH button the one that wakes the pet.
#define BTN_FOOD  0
#define BTN_LIGHT 1
#define BTN_BATH  2
#define BTN_TRAIN 3
Btn buttons[BTN_COUNT] = {
  { 134, 393, SPR_ICON_FOOD },   // comer
  { 200, 405, SPR_ICON_LIGHT },  // luz
  { 266, 405, SPR_ICON_CLEAN },  // bano
  { 332, 393, SPR_ICON_TRAIN },  // entrenar
};
#define BTN_HALF 30  // boton de 60x60 -- mas grande y mas separado que antes
#define BTN_HIT 40   // radio tactil (un poco mas generoso)

// grietas del huevo (pixeles 'k' sobre el sprite)
static const uint8_t CRACK1[][2] = { {15,8},{16,9},{15,10} };
static const uint8_t CRACK2[][2] = { {11,13},{12,14},{11,15},{20,12},{19,13},{20,14} };
// estrellas del modo noche
static const uint16_t STARS[][2] = { {120,140},{330,120},{370,210},{95,230},{280,90},{160,95} };

bool wasPressed = false;
// eleccion de inicial (primera partida): Bulbasaur / Charmander / Squirtle, 3 filas
// The first-boot starter list is the FRONT of each region's starter array in
// dex.h -- not a copy of it. That array is also the pool a region's first egg
// is drawn from (pet.cpp rollInRegion), where Kanto's five deliberately include
// Pikachu and Eevee; the choice screen shows the canonical three and leaves the
// rest to the egg. starter_test pins the first three of every region, so
// reordering that array cannot silently change the first screen anyone sees.
#define STARTER_SHOWN 3
int16_t starterOf(uint8_t region, uint8_t i) {
  const RegionInfo &rg = regionInfo(region % regionCount());
  if (i >= rg.starterCount) i = 0;
  return rg.starters[i];
}
uint8_t starterCountShown(uint8_t region) {
  uint8_t n = regionInfo(region % regionCount()).starterCount;
  return n < STARTER_SHOWN ? n : STARTER_SHOWN;
}

// First boot runs language -> region -> starter. Language is persisted as soon
// as it is chosen. Region progress is intentionally not persisted: a reset
// between the last two steps safely lands back on the region.
static bool starterLanguageDone = false;
static bool starterRegionDone = false;
#define LANGUAGE_COL_X(i) (74 + ((i) % 2) * 168)
#define LANGUAGE_ROW_Y(i) (104 + ((i) / 2) * 70)
#define LANGUAGE_CELL_W 150
#define LANGUAGE_CELL_H 54
#define STARTER_ROW_Y 110
#define STARTER_ROW_H 70
#define STARTER_ROW_GAP 8
// boton-CTA de evolucion (centrado, mitad de pantalla)
#define EVO_BTN_W 256
#define EVO_BTN_H 64
#define EVO_BTN_X (CX - EVO_BTN_W / 2)
#define EVO_BTN_Y 172
// boton-CTA de despedida (mas ancho: lleva el nombre + frase)
#define FAR_BTN_W 408
#define FAR_BTN_H 58
#define FAR_BTN_X (CX - FAR_BTN_W / 2)
#define FAR_BTN_Y 176
// el CST9217 avisa por el pin INT cuando hay datos tactiles; lo usamos para no
// leer el bus I2C mientras el chip esta dormido (esa lectura se colgaba ~1s)
volatile bool gTouchIrq = false;
void IRAM_ATTR touchIsr() { gTouchIrq = true; }
uint32_t lastRender = 0;
bool uiRenderDirty = true;
uint8_t uiLastRenderedScreen = 0xFF;
// proteccion del AMOLED: atenuado por inactividad
uint32_t lastInteract = 0;
uint8_t dimStage = 0;        // 0 despierto, 1 atenuado (90s), 2 casi apagado (5min)
bool swallowGesture = false; // el toque que despierta no acciona nada
uint8_t choiceKind = 0;     // decision dialog: 0 none, 1 evolution, 3 farewell/release, 4 power off
uint32_t choiceUntil = 0;   // se cierra solo a este millis
#define CHOICE_BTN_X 93
#define CHOICE_BTN_W 280
#define CHOICE_BTN_H 52
#define CHOICE_BTN1_Y 216
#define CHOICE_BTN2_Y 278
int16_t tX0, tY0, tXl, tYl; // gesto en curso (inicio y ultima posicion)
uint32_t tStart = 0;
bool recoveryMode = false;

void renderBootSplash() {
  // The splash is shown before SD content is mounted and must not trigger the
  // one-shot content scan.
  gfx->setPackFont(false);
  drawBootSplashFrame(*gfx);
}

void renderRecovery() {
  gfx->setPackFont(false);
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, 0xF77C);
  gfx->setTextColor(0x2946);
  gfx->setTextSize(3);
  gfx->setCursor(CX - 117, 72);
  gfx->print("DATA PACKS");
  gfx->setCursor(CX - 72, 98);
  gfx->print("REQUIRED");
  gfx->setTextSize(2);
  gfx->setCursor(CX - 96, 160);
  gfx->print(sdReady ? "USB INSTALL READY" : "INSERT MICROSD");
  gfx->setCursor(CX - 102, 204);
  gfx->print("Use Web Installer");
  gfx->setCursor(CX - 108, 232);
  gfx->print("to deploy packs.");
  gfx->setTextSize(1);
  char status[40];
  snprintf(status, sizeof(status), "VALID PACKS: %u", contentPackCount());
  gfx->setCursor(CX - 54, 292);
  gfx->print(status);
  gfx->setCursor(CX - 84, 326);
  gfx->print("Restart after install");
  gfx->flush();
}

void setup() {
  Serial.setRxBufferSize(8192);  // la transferencia a SD llega en bloques de 2 KB
  Serial.begin(115200);
  // CRITICO: sin esto, Serial.print BLOQUEA el juego cuando no hay un
  // monitor serie abierto en el host (el bufer TX del USB CDC se llena
  // y nadie lo vacia) -> con timeout 0 los mensajes se descartan
  Serial.setTxTimeoutMs(0);
  Serial.printf("TamaPoke fw %s\n", FW_VERSION);
  bootReport();   // why the last run ended, and what it was doing
  Wire.begin(IIC_SDA, IIC_SCL);
  // CST9217 (tactil), QMI8658 (IMU), AXP2101 (PMU) y PCF85063 (RTC)
  // comparten este bus I2C.
  // Red de seguridad para PMU/RTC (SensorLib NO respeta este timeout en el
  // tactil; el cuelgue del tactil dormido se resuelve gateando por INT, ver
  // handleTouch).
  Wire.setTimeOut(50);

  // CRITICO: encender la alimentacion del panel (BLDO1=OLED VDD 3.3V) ANTES de
  // inicializar el display. Si el PMU se reseteo (drenaje total), este rail
  // queda OFF y la pantalla se ve negra aunque el resto de la placa funcione.
  pmuEnablePanel();

  // QSPI a 80MHz (por defecto 40): el flush del framebuffer es el cuello de
  // botella del fps (~56ms a 40MHz). Si el panel mostrara basura, bajar a 40M.
  if (!gfx->begin(80000000)) Serial.println("gfx->begin() fallo");
  panel->setBrightness(180);
  renderBootSplash();

  sdBegin();
  quiz.loadConfig();

  touch.setPins(TP_RESET, TP_INT);
  bool touchOk = false;
  for (int i = 0; i < 3 && !touchOk; i++) {  // a veces falla al primer intento
    touchOk = touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    if (!touchOk) delay(150);
  }
  if (!touchOk) Serial.println("CST9217 no detectado");
  // begin() deja el chip en modo comando (lee la identidad y no sale);
  // hace falta un reset por hardware para que vuelva a reportar toques
  touch.reset();
  touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
  touch.setMirrorXY(true, true);  // el panel esta montado girado 180 grados
  // INT activo-bajo: salta cuando hay datos. Gatea las lecturas I2C (ver loop)
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), touchIsr, FALLING);

  if (!contentReady()) {
    recoveryMode = true;
    lastInteract = millis();
    renderRecovery();
    return;
  }

  bool motionOk = motionBegin();
#if defined(ARDUINO)
  if (!motionOk) Serial.println("QMI8658 no detectado; captura tactil activa");
#else
  (void)motionOk;
#endif

  pet.begin();
  party.begin();
  if ((pet.speciesId > 0 && !dexValid(pet.speciesId)) ||
      party.hasUnavailableSpecies()) {
    Serial.println("saved roster requires a missing regional pack");
    recoveryMode = true;
    lastInteract = millis();
    renderRecovery();
    return;
  }
  party.attach(pet);
  inventory.begin();
  loadUserBrightness();
  starterLanguageDone = loadLang();
  gfx->setPackFont(contentHasUi());
  thumbs.load();

  // reloj real: aplica el tiempo que estuvo apagado
  rtcBegin();
  batBegin();
  pwrSetup();
  uint32_t e = rtcEpoch();
  if (e == 0) {
    rtcSetEpoch(1767225600UL);  // RTC virgen: semilla (la hora absoluta da igual,
    e = rtcEpoch();             // solo importan las diferencias)
    Serial.println("RTC sin hora: sembrado, sin progresion offline esta vez");
  }
  party.syncClock(pet, e);
  inventory.ensureDailySupply(e / 86400UL);

  audioBegin();  // ES8311 + I2S + amplificador (suena un jingle de arranque)

  lastInteract = millis();
}

// carga/descarga el sprite de SD cuando cambia la especie
void ensureMon() {
  if (pet.speciesId == monFor && monShinyFor == pet.shiny &&
      monGenderFor == pet.gender && !sdDirty) return;
  sdDirty = false;
  monFor = pet.speciesId;
  monShinyFor = pet.shiny;
  monGenderFor = pet.gender;
  pmd.unload();
  beh.x = beh.targetX = 233;
  beh.mode = 0;
  beh.until = 0;
  if (pet.speciesId >= 1 && pet.speciesId <= dexCount()) {
    pmd.load(pet.speciesId, pet.shiny, pet.gender);
  }
}

void loop() {
  if (recoveryMode) {
    handleSerial();
    static uint32_t lastRecoveryRender = 0;
    if (millis() - lastRecoveryRender >= 500) {
      lastRecoveryRender = millis();
      renderRecovery();
    }
    return;
  }
  uint32_t now = millis();
  pet.update(now);
  party.update(pet, now);
  inventory.ensureDailySupply(pet.lastSeenEpoch / 86400UL);
  updateQuiz(now);

  // The link is pumped here rather than from the LAN screen, because it has to
  // keep running through the battle too: linkNowPoll() drains what the radio
  // parked on the WiFi task, and tick() is what resends a lost packet and gives
  // up on a peer that has gone quiet.
  if (lan.live()) {
    linkNowPoll();
    lan.tick(now);
  }

  // avisa con un sonido cuando el bicho pasa a estar listo para evolucionar
  // (incluye el caso de cumplir al despertar). canEvolveNow es false durmiendo.
  static bool wasEvoReady = false;
  bool evoReady = pet.wantEvolveButton();
  if (evoReady && !wasEvoReady) sfxPlay(SFX_MEDAL);
  wasEvoReady = evoReady;
  // aviso sombrio cuando el bicho esta a punto de escaparse por abandono
  static bool wasRunReady = false;
  bool runReady = pet.canRunawayNow();
  if (runReady && !wasRunReady) sfxPlay(SFX_DENY);
  wasRunReady = runReady;

  handleTouch();
  if (battleOpen) btlUpdateThrow(millis());
  handleSerial();
  ensureMon();

  // pulsacion corta del PWR: pantalla on/off
  static uint32_t lastPwr = 0;
  if (now - lastPwr > 250) {
    lastPwr = now;
    if (pwrShortPressed()) {
      screenOff = !screenOff;
      pet.setScreenOff(screenOff);   // asleep only if it is also night
      if (!screenOff) lastInteract = now;
      uiRenderDirty = true;
    }
  }

  updateBrightness(now);

  // vuelca el autoguardado periodico SOLO con la pantalla atenuada/apagada o
  // durmiendo: la escritura a NVS congela ~1s ambos cores (caché de flash off),
  // y aqui no hay animacion que se corte ni dedo esperando respuesta. Con 90s
  // de inactividad la pantalla ya atenua, asi que se vuelca enseguida; el uso
  // activo persiste igual por los guardados de cada accion (comer/jugar/...).
  if ((pet.savePending() || party.savePending()) &&
      (screenOff || dimStage >= 1 || pet.sleeping)) {
    party.flushSave(pet);
  }

  // anota la hora real cada 30 s (se persiste en cada save del juego)
  static uint32_t lastClock = 0;
  if (now - lastClock > 30000) {
    lastClock = now;
    uint32_t e = rtcEpoch();
    if (e) pet.lastSeenEpoch = e;
    uint8_t screen = uiCurrentScreen();
    if (screen == SCR_CLOCK || screen == SCR_CARD) uiRenderDirty = true;
  }

  // latido de salud cada 5 min (para el soak test; se descarta si no hay monitor)
  static uint32_t lastHealth = 0;
  if (now - lastHealth > 300000) {
    lastHealth = now;
    printHealthReport();
  }

  // 85 ms en juego/saco: margen seguro para que el redibujado no pise el envio
  // DMA del frame anterior (a 40-65 ms solapaba y causaba flashes negros; con
  // sprites grandes el dibujo tarda mas, asi que se deja colchon)
  uint8_t screen = uiCurrentScreen();
  bool renderDue = uiRenderDirty || screen != uiLastRenderedScreen ||
                   uiScreenContinuous(screen);
  if (renderDue &&
      now - lastRender >= (uint32_t)((gameOpen || sackOpen || spdOpen) ? 85 : 100)) {
    lastRender = now;
    uint32_t started = perfNowUs();
    render();
    perfRecord(PERF_FRAME, perfNowUs() - started);
    uiRenderDirty = false;
    uiLastRenderedScreen = uiCurrentScreen();
  }
}

// brillo segun sueno + inactividad (proteccion del AMOLED)
void updateBrightness(uint32_t now) {
  // los eventos visibles despiertan la pantalla solos
  if (pet.evolving() || pet.ceremony || pet.eating() || pet.showHeart() || quiz.active) {
    lastInteract = now;
  }
  uint32_t idle = now - lastInteract;
  dimStage = (idle > 300000) ? 2 : (idle > 90000) ? 1 : 0;
  // El nivel 7 conserva los valores historicos: 180 con USB, 145 con bateria.
  uint8_t target = 15 + ((usbPresent() ? 235 : 185) * userBrightness + 5) / 10;
  bool sleepingOnMain = pet.sleeping && uiCurrentScreen() == SCR_MAIN;
  if (sleepingOnMain && target > 25) target = 25;
  if (dimStage == 1) {
    uint8_t dimTarget = sleepingOnMain ? 10 : 60;
    if (target > dimTarget) target = dimTarget;
  }
  else if (dimStage == 2) target = 8;
  if (screenOff) target = 0;
  static uint8_t current = 255;
  if (target != current) {
    current = target;
    panel->setBrightness(target);
  }
}

// ---------- consola serie (provision de SD + depuracion) ----------

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  uiRenderDirty = true;
  if (line == "INFO") {
    Serial.printf("FW\t%s\n", FW_VERSION);
    sdSerialPackInfo();
    return;
  }
  if (sdSerialCommand(line)) return;

  if (line == "QUIZCFG") {
    quiz.config = quizConfigLoad();
    char response[128];
    if (quizConfigFormat(quiz.config, response, sizeof(response))) Serial.println(response);
    else Serial.println("ERR QUIZ_CONFIG_INVALID");
    Serial.println("DONE");
    return;
  }
  if (line.startsWith("QUIZSET ")) {
    QuizConfig configured;
    if (!quizConfigParse(line.c_str() + 8, configured) || !quizConfigSave(configured)) {
      Serial.println("ERR QUIZ_CONFIG_INVALID");
      return;
    }
    quiz.config = configured;
    char response[128];
    quizConfigFormat(quiz.config, response, sizeof(response));
    Serial.println(response);
    Serial.println("DONE");
    return;
  }

  if (line == "HATCH") {
    pet.eggTap(); pet.eggTap(); pet.eggTap();
    Serial.println("DONE");
  } else if (line.startsWith("SPEC ")) {
    int n = line.substring(5).toInt();
    if (n >= 1 && n <= dexCount()) {
      pet.prevSpeciesId = pet.speciesId;
      pet.speciesId = n;
      Serial.printf("especie #%d %s\n", n, speciesName(n));
    }
    Serial.println("DONE");
  } else if (line.startsWith("LVL ")) {
    // level() is 1 + age/rate, so the age for level N is (N-1) rates -- this
    // used to set N and hand back N+1, which is a poor thing for a command
    // named LVL to do when it is what every balance check is anchored on.
    long want = line.substring(4).toInt();
    if (want < 1) want = 1;
    if (want > MAX_LEVEL) want = MAX_LEVEL;
    pet.ageMinutes = (uint32_t)(want - 1) * MINUTES_PER_LEVEL;
    pet.saveNow();
    Serial.printf("lvl=%u\n", pet.level());
  } else if (line.startsWith("MISS ")) {
    // MISS <n>: sets the care mistakes -- "descuidos", the desc= on STATS.
    // Each one pushes every evolution threshold up a level, so a creature that
    // was neglected early evolves late; MISS 0 forgives that. Its sibling
    // commands are IV, TR and LVL.
    long m = line.substring(5).toInt();
    if (m < 0) m = 0;
    if (m > 255) m = 255;
    pet.careMistakes = (uint8_t)m;
    pet.saveNow();
    Serial.printf("desc=%u\n", pet.careMistakes);
  } else if (line.startsWith("TR ")) {
    // TR <atk> <def> <spe>: sets the TRAINING (this game's EVs), for testing a
    // fully-raised creature without playing the minigames for an hour. Each is
    // clamped to trMaxFor(iv), the same IV-bound ceiling the games enforce, so
    // this cannot produce a creature the player could not have raised.
    int v[3] = { 0, 0, 0 };
    int n = sscanf(line.c_str() + 3, "%d %d %d", &v[0], &v[1], &v[2]);
    if (n >= 1) {
      int a = v[0], d = (n >= 2) ? v[1] : v[0], e = (n >= 3) ? v[2] : v[0];
      pet.trAtk = (uint8_t)(a < 0 ? 0 : (a > pet.trMaxAtk() ? pet.trMaxAtk() : a));
      pet.trDef = (uint8_t)(d < 0 ? 0 : (d > pet.trMaxDef() ? pet.trMaxDef() : d));
      pet.trSpe = (uint8_t)(e < 0 ? 0 : (e > pet.trMaxSpe() ? pet.trMaxSpe() : e));
      if (pet.trAtk < pet.trMinAtk) pet.trAtk = pet.trMinAtk;
      if (pet.trDef < pet.trMinDef) pet.trDef = pet.trMinDef;
      if (pet.trSpe < pet.trMinSpe) pet.trSpe = pet.trMinSpe;
      pet.saveNow();
    }
    Serial.printf("tr=%u/%u/%u topes=%u/%u/%u\n", pet.trAtk, pet.trDef, pet.trSpe,
                  pet.trMaxAtk(), pet.trMaxDef(), pet.trMaxSpe());
  } else if (line.startsWith("IV ")) {
    // IV <fue> <def> <vel> <vit>: fija los valores individuales (pruebas).
    // Con "IV 31 31 31 31" se ve el techo; con "IV 8 8 8 8" el suelo.
    int v[4] = { 16, 16, 16, 16 };
    int n = sscanf(line.c_str() + 3, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]);
    if (n >= 1) {
      for (int i = 0; i < 4; i++) v[i] = v[i] < 0 ? 0 : (v[i] > 31 ? 31 : v[i]);
      pet.ivAtk = v[0];
      pet.ivDef = (n >= 2) ? v[1] : v[0];
      pet.ivSpe = (n >= 3) ? v[2] : v[0];
      pet.ivHp = (n >= 4) ? v[3] : v[0];
      if (pet.trMinAtk > pet.trMaxAtk()) pet.trMinAtk = pet.trMaxAtk();
      if (pet.trMinDef > pet.trMaxDef()) pet.trMinDef = pet.trMaxDef();
      if (pet.trMinSpe > pet.trMaxSpe()) pet.trMinSpe = pet.trMaxSpe();
      if (pet.trAtk > pet.trMaxAtk()) pet.trAtk = pet.trMaxAtk();
      if (pet.trDef > pet.trMaxDef()) pet.trDef = pet.trMaxDef();
      if (pet.trSpe > pet.trMaxSpe()) pet.trSpe = pet.trMaxSpe();
      if (pet.trAtk < pet.trMinAtk) pet.trAtk = pet.trMinAtk;
      if (pet.trDef < pet.trMinDef) pet.trDef = pet.trMinDef;
      if (pet.trSpe < pet.trMinSpe) pet.trSpe = pet.trMinSpe;
    }
    pet.saveNow();   // IV used to change RAM only and never write
    Serial.printf("iv=%u/%u/%u/%u topes=%u/%u/%u\n", pet.ivAtk, pet.ivDef,
                  pet.ivSpe, pet.ivHp, pet.trMaxAtk(), pet.trMaxDef(), pet.trMaxSpe());
    Serial.println("DONE");
  } else if (line.startsWith("TIME ")) {
    uint32_t e = (uint32_t)line.substring(5).toInt();
    rtcSetEpoch(e);
    pet.setClock(e);
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line.startsWith("RTCSET ")) {  // solo RTC (simular apagados en pruebas)
    rtcSetEpoch((uint32_t)line.substring(7).toInt());
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "TIME") {
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "GAL") {
    galleryOpen = !galleryOpen;
    galleryDetail = 0;
    galleryDirty = true;
    if (!galleryOpen) galleryPmd.unload();
    Serial.println("DONE");
  } else if (line == "EGGS") {
    // simula 20 tiradas de huevo (no cambia el estado del juego)
    for (int i = 0; i < 20; i++) {
      int16_t d = pet.pickEggSpecies();
      Serial.printf("%d:%s(r%u) ", d, speciesName(d), dexEntry(d).rarity);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line.startsWith("EGG ")) {
    // EGG <dex> [color]: hatch a chosen species immediately for hardware tests.
    int dex = 0, sh = 0;
    int n = sscanf(line.c_str() + 4, "%d %d", &dex, &sh);
    if (n >= 1 && dex >= 1 && dex <= dexCount()) {
      pet.dbgHatchAs(dex, sh != 0);
      Serial.printf("%s%s iv=%u/%u/%u/%u\n", speciesName(dex),
                    pet.shiny ? " *COLOR*" : "", pet.ivAtk, pet.ivDef,
                    pet.ivSpe, pet.ivHp);
    }
    Serial.println("DONE");
  } else if (line.startsWith("BATTLE")) {
    // BATTLE <dex> [level] -- the only way in until the trainer roster exists
    int dex = 0, lvl = 0;
    int n = sscanf(line.c_str() + 6, "%d %d", &dex, &lvl);
    if (n >= 1 && dex >= 1 && dex <= dexCount()) {
      startBattle(dex, lvl > 0 ? (uint8_t)lvl : pet.level());
      Serial.printf("battle vs %s Lv.%u\n", speciesName(dex), btlFoe.level);
    } else {
      Serial.println("uso: BATTLE <dex> [nivel]");
    }
    Serial.println("DONE");
  } else if (line == "SHINY") {  // alterna shiny del actual (pruebas)
    pet.shiny = !pet.shiny;
    Serial.printf("shiny=%d\n", pet.shiny);
    Serial.println("DONE");
  } else if (line == "SPARKLE") {
    pet.shiny = !pet.shiny;  // legacy console alias for the combined state
    Serial.printf("shiny=%d\n", pet.shiny);
    Serial.println("DONE");
  } else if (line.startsWith("NICK ")) {
    pet.rename(line.substring(5).c_str());
    Serial.printf("nick=%s\n", pet.nick);
    Serial.println("DONE");
  } else if (line == "CAREDAY") {  // simula un dia nuevo cuidado (pruebas)
    pet.setClock(pet.lastSeenEpoch + 86400);
    pet.caress();
    Serial.printf("streak=%u bond=%u medals=0x%X\n", player.streak, pet.bond, pet.medals);
    Serial.println("DONE");
  } else if (line == "BYE") {
    pet.startFarewell();
    Serial.println("DONE");
  } else if (line == "RUN") {
    pet.startRunaway();
    Serial.println("DONE");
  } else if (line == "BEEP") {
    sfxPlay(SFX_HATCH);  // prueba de audio
    Serial.println("DONE");
  } else if (line == "ABANDON") {
    pet.dbgRunawayReady();  // fuerza el estado "lista para escaparse" (test del boton)
    Serial.println("DONE");
  } else if (line == "EXPORT") {
    // Prints the whole save as a block of IMPORT commands. Pasting that block
    // back is the restore -- there is no separate format to get wrong, and no
    // single 2000-character line for a terminal to mangle.
    uint8_t *buf = static_cast<uint8_t *>(ps_malloc(SAVE_BLOB_MAX));
    if (!buf) { Serial.println("EXPORT FAIL"); return; }
    size_t n = saveExport(buf, SAVE_BLOB_MAX);
    if (!n) { free(buf); Serial.println("EXPORT FAIL"); return; }
    Serial.printf("# TamaPoke save, %u bytes. Paste this whole block back.\n",
                  (unsigned)n);
    for (size_t i = 0; i < n; i += 48) {
      Serial.print("IMPORT ");
      for (size_t j = i; j < i + 48 && j < n; j++) Serial.printf("%02X", buf[j]);
      Serial.println();
    }
    Serial.println("IMPORT");        // the empty one commits
    free(buf);
  } else if (line.startsWith("IMPORT")) {
    // IMPORT <hex>   append a chunk
    // IMPORT         commit what has been appended
    static uint8_t *in = nullptr;
    static size_t inN = 0;
    auto clearImport = [&]() {
      free(in);
      in = nullptr;
      inN = 0;
    };
    String hex = line.substring(6);
    hex.trim();
    if (hex.length()) {
      if (hex.length() & 1) { Serial.println("IMPORT ODD"); clearImport(); return; }
      if (!in) {
        in = static_cast<uint8_t *>(ps_malloc(SAVE_BLOB_MAX));
        if (!in) { Serial.println("IMPORT NOMEM"); return; }
      }
      for (size_t i = 0; i + 1 < (size_t)hex.length(); i += 2) {
        if (inN >= SAVE_BLOB_MAX) {
          Serial.println("IMPORT FULL"); clearImport(); return;
        }
        auto nyb = [](char c) -> int {
          if (c >= '0' && c <= '9') return c - '0';
          if (c >= 'A' && c <= 'F') return c - 'A' + 10;
          if (c >= 'a' && c <= 'f') return c - 'a' + 10;
          return -1;
        };
        const char *hs = hex.c_str();
        int hi = nyb(hs[i]), lo = nyb(hs[i + 1]);
        if (hi < 0 || lo < 0) {
          Serial.println("IMPORT BAD"); clearImport(); return;
        }
        in[inN++] = (uint8_t)((hi << 4) | lo);
      }
      return;                        // silent while collecting
    }
    if (!inN) { Serial.println("IMPORT EMPTY"); return; }
    bool ok = saveImport(in, inN);
    clearImport();
    Serial.println(ok ? "IMPORT OK" : "IMPORT REJECTED");
    if (ok) { Serial.println("DONE"); delay(100); ESP.restart(); }
  } else if (line == "WIPE") {
    pet.factoryReset();     // borra NVS y reinicia -> partida nueva (eleccion de inicial)
    Serial.println("DONE");
    delay(100);
    ESP.restart();
  } else if (line.startsWith("PARTY")) {
    // PARTY          list the party
    // PARTY <dex>    add a level-50 cultivation specimen (for testing)
    // PARTY CLEAR    empty it
    String arg = line.substring(5);
    arg.trim();
    if (arg == "CLEAR") {
      for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
    } else if (arg.length()) {
      int d = arg.toInt();
      if (d >= 1 && d <= dexCount()) {
        PartyMon m;
        m.dex = d;
        m.level = 50;
        m.ageMinutes = 49UL * MINUTES_PER_LEVEL;
        m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
        m.trAtk = m.trDef = m.trSpe = 50;
        Serial.println(party.add(m) ? "added" : "party full");
      }
    }
    Serial.printf("party %u/%u:", party.count(), PARTY_SLOTS);
    for (int i = 0; i < PARTY_SLOTS; i++) {
      const PartyMon &m = party.slots[i];
      if (m.empty()) Serial.print(" -");
      else if (m.isEgg()) Serial.print(" EGG");
      else Serial.printf(" %s%s(nv%u)", speciesName(m.dex),
                         rareMark(m.shiny || m.sparkle), m.level);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line == "REG") {
    Serial.printf("pokedex %u/%u:", player.registeredCount(), dexCount());
    for (int i = 1; i <= dexCount(); i++)
      if (player.isRegistered(i)) Serial.printf(" %d", i);
    Serial.println();
    Serial.println("DONE");
  } else if (line == "HEALTH") {
    printHealthReport();
    Serial.println("DONE");
  } else if (line == "STATS") {
    Serial.printf("spec=%d nv=%u com=%u fel=%u ene=%u lim=%u desc=%u sd=%d mon=%d bat=%d usb=%d rtc=%u\n",
                  pet.speciesId, pet.level(), pet.fullness, pet.joy, pet.energy,
                  pet.hygiene, pet.careMistakes, sdReady, pmd.loaded,
                  batPercent(), usbPresent(), rtcEpoch());
    Serial.printf("peso=%u fue=%u def=%u vel=%u vit=%u baya=%d\n",
                  pet.weight, pet.atkStat(), pet.defStat(), pet.speStat(),
                  pet.vitStat(), pet.berryKnown);
    Serial.printf("iv=%u/%u/%u/%u tr=%u/%u/%u min=%u/%u/%u topes=%u/%u/%u\n",
                  pet.ivAtk, pet.ivDef, pet.ivSpe, pet.ivHp,
                  pet.trAtk, pet.trDef, pet.trSpe,
                  pet.trMinAtk, pet.trMinDef, pet.trMinSpe,
                  pet.trMaxAtk(), pet.trMaxDef(), pet.trMaxSpe());
    Serial.printf("shiny=%d wildBonus=%u streak=%u/%u bond=%u medals=0x%X(%u) nick=%s\n",
                  pet.shiny, player.wildRareBonus, player.streak,
                  player.bestStreak, pet.bond, pet.medals, player.totalMedals, pet.nick);
    Serial.println("DONE");
  }
}

static void printPerfSample(const char *name, PerfMetric metric) {
  const PerfSample &sample = perfSample(metric);
  uint32_t average = sample.count ? (uint32_t)(sample.totalUs / sample.count) : 0;
  Serial.printf("perf %s n=%lu avg_us=%lu max_us=%lu nvs=%lu\n", name,
                (unsigned long)sample.count, (unsigned long)average,
                (unsigned long)sample.maxUs, (unsigned long)sample.nvsWrites);
}

void printHealthReport() {
  Serial.printf("up=%lus heap=%u min=%u sd=%d mon=%d\n",
                (unsigned long)(millis() / 1000), ESP.getFreeHeap(),
                ESP.getMinFreeHeap(), sdReady, pmd.loaded);
  // PSRAM is where the sprites and the framebuffer live, so a memory problem
  // shows up here long before it shows up in the heap figure above.
  Serial.printf("psram=%u screen=%s btlspr=%d/%d\n", (unsigned)ESP.getFreePsram(),
                SCREEN_NAME[uiCurrentScreen() % SCR_COUNT],
                btlPmd[0].loaded ? 1 : 0, btlPmd[1].loaded ? 1 : 0);
#if defined(ESP32)
  Serial.printf("heapblk=%u psblk=%u psmin=%u stack_words=%u\n",
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)uxTaskGetStackHighWaterMark(nullptr));
#else
  Serial.println("heapblk=0 psblk=0 psmin=0 stack_words=0");
#endif
  printPerfSample("frame", PERF_FRAME);
  printPerfSample("flush", PERF_FLUSH);
  printPerfSample("pet", PERF_PET_SAVE);
  printPerfSample("player", PERF_PLAYER_SAVE);
  printPerfSample("team", PERF_TEAM_SAVE);
  printPerfSample("items", PERF_INVENTORY_SAVE);
}

// ---------- entrada tactil ----------

bool inPetZone(int16_t x, int16_t y) {
  return x > 110 && x < 356 && y > 95 && y < 310;
}

static int partySlotAtOrdinal(uint8_t ordinal) {
  for (uint8_t i = 0; i < PARTY_SLOTS; i++) {
    if (party.slots[i].empty()) continue;
    if (!ordinal--) return i;
  }
  return -1;
}

static int petNavDotX(uint8_t ordinal, uint8_t count) {
  return CX - ((int)count - 1) * 12 + ordinal * 24;
}

bool inPetNavZone(int16_t x, int16_t y) {
  uint8_t count = party.count();
  if (!count) return false;
  return x >= petNavDotX(0, count) - 12 &&
         x <= petNavDotX(count - 1, count) + 12 &&
         y >= PETNAV_HIT_Y && y <= PETNAV_HIT_Y + PETNAV_HIT_H;
}

// el toque se resuelve al LEVANTAR el dedo para distinguir tap de deslizar
void handleTouch() {
  static uint32_t lastPoll = 0;
  static bool brightnessDrag = false;
  if (millis() - lastPoll < 20) return;  // 50 Hz le sobra a un dedo
  lastPoll = millis();
  // solo tocamos el bus si el chip aviso por INT o si el dedo sigue abajo (hay
  // que detectar el levantamiento). Leer el CST9217 dormido se colgaba ~1s y
  // congelaba el loop entero; SensorLib no respeta el timeout de Wire.
  if (!gTouchIrq && !wasPressed) return;
  gTouchIrq = false;
  int16_t x, y;
  bool pressed = touch.getPoint(&x, &y, 1) > 0;

  // saco de entrenamiento: cada toque cuenta al instante (aporrear rapido)
  if (sackOpen && !quizBlocking()) {
    if (pressed && !wasPressed) {
      lastInteract = millis();
      uiRenderDirty = true;
      if (y < 72) leaveSack();       // tocar arriba = salir, conservando lo ganado
      else sackTap();
    }
    wasPressed = pressed;
    return;
  }

  if (pressed && !wasPressed) {  // empieza el gesto
    tX0 = tXl = x;
    tY0 = tYl = y;
    tStart = millis();
    swallowGesture = (dimStage > 0) || screenOff;  // si estaba a oscuras, solo despierta
    if (screenOff) pet.setScreenOff(false);        // waking the screen wakes it
    screenOff = false;
    lastInteract = millis();
    brightnessDrag = clockOpen && !swallowGesture &&
                     x >= BRIGHT_HIT_X && x <= BRIGHT_HIT_X + BRIGHT_HIT_W &&
                     y >= BRIGHT_HIT_Y && y <= BRIGHT_HIT_Y + BRIGHT_HIT_H;
    if (brightnessDrag) setUserBrightness(brightnessLevelAt(x), false);
  } else if (pressed) {  // sigue apoyado
    tXl = x;
    tYl = y;
    if (brightnessDrag) {
      setUserBrightness(brightnessLevelAt(x), false);
      uiRenderDirty = true;
    }
  } else if (wasPressed) {  // levanta el dedo: resolver gesto
    lastInteract = millis();
    uiRenderDirty = true;
    int dx = tXl - tX0, dy = tYl - tY0;
    uint32_t dt = millis() - tStart;
    if (brightnessDrag) {
      setUserBrightness(brightnessLevelAt(tXl), true);
      brightnessDrag = false;
    } else if (!swallowGesture) {
      if (abs(dx) > 80 && abs(dy) < 70 && dt < 800) onSwipe(dx > 0 ? 1 : -1);
      else if (abs(dy) > 80 && abs(dx) < 70 && dt < 800) onSwipeV(dy > 0 ? 1 : -1);
      else if (dt < 1500 && abs(dx) < 40 && abs(dy) < 40) onTap(tX0, tY0);
    }
  }
  wasPressed = pressed;
}

// deslizar vertical: abre/cierra la ficha del bicho

void openClock();  // prototipo

void onSwipeV(int dir) {
  if (quiz.active) {
    if (!quiz.answered) {
      if (dir < 0 && quiz.scrollLine < quiz.maxScrollLine) quiz.scrollLine++;
      else if (dir > 0 && quiz.scrollLine) quiz.scrollLine--;
    }
    return;
  }
  if (moveInfoOpen) { moveInfoOpen = false; return; }
  if (btlWinUntil) {
    if (btlTrainer < 0 && btlRewardScroll.scroll(dir)) sfxPlay(SFX_TAP);
    return;
  }
  if (pet.awaitingStarter()) return;  // bloqueado durante la eleccion de inicial
  if (uiCurrentScreen() == SCR_DEXPICK || uiCurrentScreen() == SCR_GYMPICK)
    return;                 // on the chooser, vertical does nothing: pick a row
  if (navMenuOpen) { navMenuOpen = false; return; }
  if (menuOpen) { menuOpen = false; return; }   // any swipe closes the menu
  if (battleOpen) return;   // no swiping out of a fight
  if (pickOpen) { pickOpen = false; return; }
  if (lanOpen) { lanLeave(); lanOpen = false; return; }
  if (bagOpen) {
    if (bagView == BAG_VIEW_LIST) {
      if (bagScroll.scroll(dir)) sfxPlay(SFX_TAP);
      return;
    }
    bagDetailKey = ITEM_KEY_NONE;
    bagDetailMove = MOVE_NONE;
    bagStoneDialog = BAG_STONE_DIALOG_NONE;
    bagStoneTarget = PARTY_SLOTS;
    bagView = bagView == BAG_VIEW_DETAIL || bagView == BAG_VIEW_TARGET
                ? BAG_VIEW_ACTIONS : BAG_VIEW_LIST;
    return;
  }
  if (boxOpen) {
    if (boxSel) { boxSel = 0; return; }
    if (partyPick) {
      partyPick = false;
      partyPending = PartyMon();
    }
    boxOpen = false;
    return;
  }
  if (gymOpen) {
    // Same gesture as the Pokedex: vertical changes region, horizontal pages.
    uint8_t regions = regionAll();
    if (regions) gymRegion = (uint8_t)((gymRegion + (dir > 0 ? 1 : regions - 1)) % regions);
    gymPage = 0;
    sfxPlay(SFX_TAP);
    return;
  }
  if (playerOpen) { playerOpen = false; return; }
  if (trainOpen) { trainOpen = false; return; }
  if (movePickOpen) { movePickOpen = false; return; }
  // Either minigame exits on a swipe. A swipe cannot be confused with a ball
  // hit -- the gesture resolver separates them -- which the header tap no
  // longer can now that the ball is hittable up there.
  if (gameOpen) { leaveGame(); return; }
  if (sackOpen) { leaveSack(); return; }
  if (spdOpen) { leaveSpeed(); return; }
  if (galleryOpen) {
    if (galleryDetail) { galleryDetail = 0; galleryPmd.unload(); galleryDirty = true; return; }
    galleryRegion = (uint8_t)((galleryRegion + (dir > 0 ? 1 : GAL_REGIONS - 1)) % GAL_REGIONS);
    galleryPage = 0;
    galleryDirty = true;
    sfxPlay(SFX_TAP);
    return;
  }
  if (kbOpen || pet.ceremony) return;
  if (clockOpen) { clockOpen = false; return; }
  if (cardOpen) {
    if (natureInfoOpen) { natureInfoOpen = false; return; }
    if (dir < 0) cardOpen = false;  // arriba cierra la ficha
    return;
  }
  // Swipe down opens the player-wide navigation page; swipe up remains the
  // current creature's card.
  if (dir > 0) {
    if (!feedMenuUntil) navMenuOpen = true;
  } else if (!pet.isEgg() && !feedMenuUntil) {
    cardOpen = true;                // deslizar arriba: ficha
    cardPage = 0;
  }
}

// This is the only item store. Battle actions query and consume the same
// Inventory directly; there is no carried subset or second battle bag.
static void itemRefName(ItemRef ref, char *out, size_t size) {
  const ItemEntry *item = itemByKey(ref.key);
  if (!out || !size) return;
  if (item && item->effect == ITEM_EFFECT_TEACH_MOVE && moveValid(ref.move))
    snprintf(out, size, "%s: %s", itemName(ref.key), moveName(ref.move));
  else
    snprintf(out, size, "%s", item ? itemName(ref.key) : "?");
}

static void drawBagStoneDialog() {
  if (bagStoneDialog == BAG_STONE_DIALOG_NONE ||
      !moveValid(bagSelectedMove) || bagStoneTarget >= PARTY_SLOTS) return;
  const PartyMon &target = party.slots[bagStoneTarget];
  int16_t dex = bagStoneTarget == party.activeIndex() ? pet.speciesId : target.dex;
  const char *nick = bagStoneTarget == party.activeIndex() ? pet.nick : target.nick;
  const char *petName = dex > 0 ? displaySpeciesName(dex, nick) : T(S_EGG_HDR);
  char message[96];
  if (bagStoneDialog == BAG_STONE_DIALOG_CONFIRM)
    snprintf(message, sizeof(message), T(S_STONE_CONFIRM_FMT),
             moveName(bagSelectedMove), petName);
  else if (bagStoneDialog == BAG_STONE_DIALOG_KNOWN)
    snprintf(message, sizeof(message), T(S_STONE_KNOWN_FMT),
             petName, moveName(bagSelectedMove));
  else
    snprintf(message, sizeof(message), T(S_STONE_INCOMPATIBLE_FMT),
             petName, moveName(bagSelectedMove));
  gfx->fillRoundRect(73, 156, 320, 188, 16, UI_WHITE);
  gfx->drawRoundRect(73, 156, 320, 188, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  drawWrappedText(message, 91, 176, 284, 2);
  gfx->fillRoundRect(CHOICE_BTN_X, CHOICE_BTN1_Y, CHOICE_BTN_W, CHOICE_BTN_H,
                     12, UI_BAR_OK);
  gfx->setTextColor(UI_WHITE);
  uiDrawCenteredIn(bagStoneDialog == BAG_STONE_DIALOG_CONFIRM ? T(S_YES) : T(S_OK),
                   CHOICE_BTN_X, CHOICE_BTN1_Y, CHOICE_BTN_W, CHOICE_BTN_H);
  if (bagStoneDialog == BAG_STONE_DIALOG_CONFIRM) {
    gfx->fillRoundRect(CHOICE_BTN_X, CHOICE_BTN2_Y, CHOICE_BTN_W, CHOICE_BTN_H,
                       12, UI_TRACK);
    gfx->setTextColor(UI_INK);
    uiDrawCenteredIn(T(S_NO), CHOICE_BTN_X, CHOICE_BTN2_Y, CHOICE_BTN_W, CHOICE_BTN_H);
  }
}

static bool bagTargetCanApply(const ItemEntry &item, uint8_t slot) {
  if (slot >= PARTY_SLOTS) return false;
  MoveId move = item.effect == ITEM_EFFECT_TEACH_MOVE
                  ? bagSelectedMove : MOVE_NONE;
  if (slot == party.activeIndex()) return itemCanApplyToPet(item, pet, move);
  return !party.slots[slot].empty() &&
         itemCanApplyToPartyMon(item, party.slots[slot], move);
}

static bool bagTargetKnowsMove(uint8_t slot) {
  if (slot >= PARTY_SLOTS || party.slots[slot].empty()) return false;
  if (slot == party.activeIndex()) return pet.knowsMove(bagSelectedMove);
  const PartyMon &target = party.slots[slot];
  return Pet::knowsLearnedMove(target.moves, target.reserveMoves,
                               bagSelectedMove);
}

static bool bagHasUsableTarget(const ItemEntry &item) {
  if (!itemUsableOutsideBattle(item)) return false;
  for (uint8_t slot = 0; slot < PARTY_SLOTS; slot++)
    if (bagTargetCanApply(item, slot)) return true;
  return false;
}

static void drawBagList() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(T(S_BAG)), 42);
  gfx->print(T(S_BAG));
  uint16_t stackCount = inventory.stackCount();
  int contentHeight = stackCount ? stackCount * BAG_ROW_STEP - 8 : 0;
  bagScroll.configure(BAG_LIST_Y, BAG_LIST_H, contentHeight, BAG_ROW_STEP);
  if (!stackCount) {
    gfx->setTextColor(UI_MUTED);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(T(S_ITEM_EMPTY)), 220);
    gfx->print(T(S_ITEM_EMPTY));
  }
  for (uint16_t index = 0; index < stackCount; index++) {
    const InventoryStack *stack = inventory.stackAt(index);
    if (!stack) continue;
    const ItemEntry *item = itemByKey(stack->key);
    if (!item) continue;
    int y = bagScroll.contentY(index * BAG_ROW_STEP);
    if (!bagScroll.fullyVisible(y, BAG_ROW_H)) continue;
    gfx->fillRoundRect(70, y, 326, BAG_ROW_H, 10, UI_BG_DAY);
    gfx->drawRoundRect(70, y, 326, BAG_ROW_H, 10, UI_INK);
    drawItemIcon(*item, 94, y + BAG_ROW_H / 2);
    gfx->setTextColor(UI_INK);
    bool attributedStone = item->effect == ITEM_EFFECT_TEACH_MOVE && moveValid(stack->move);
    gfx->setTextSize(attributedStone ? 1 : 2);
    gfx->setCursor(114, y + (attributedStone ? 17 : 13));
    char label[64];
    itemRefName(stack->ref(), label, sizeof(label));
    gfx->print(label);
    char count[8];
    snprintf(count, sizeof(count), "x%u", stack->count);
    gfx->setTextSize(2);
    gfx->setCursor(uiRightX(count, 382), y + 13);
    gfx->print(count);
  }
  if (bagScroll.scrollable()) {
    gfx->fillRoundRect(405, BAG_LIST_Y, 5, BAG_LIST_H, 2, UI_TRACK);
    int thumbH = bagScroll.thumbHeight(BAG_LIST_H, 18);
    int thumbY = bagScroll.thumbTop(BAG_LIST_Y, BAG_LIST_H, 18);
    gfx->fillRoundRect(405, thumbY, 5, thumbH, 2, UI_INK);
  }
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_CLOSE)), 402);
  gfx->print(T(S_CLOSE));
}

static void drawBagDetail(const ItemEntry &item) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  drawItemIcon(item, CX, 92, 2);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(itemName(item.key)), 130);
  gfx->print(itemName(item.key));
  if (item.effect == ITEM_EFFECT_TEACH_MOVE && moveValid(bagSelectedMove)) {
    gfx->setTextColor(dexEntry(pet.speciesId).accent);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(moveName(bagSelectedMove)), 158);
    gfx->print(moveName(bagSelectedMove));
  }
  char meta[32];
  char rarity[5] = "****";
  rarity[item.rarity < 4 ? item.rarity : 4] = 0;
  snprintf(meta, sizeof(meta), "x%u  %s",
           inventory.count(item.key, bagSelectedMove), rarity);
  gfx->setTextColor(UI_BAR_WARN);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(meta), item.effect == ITEM_EFFECT_TEACH_MOVE ? 184 : 168);
  gfx->print(meta);
  const char *description = itemDescription(item.key, uiActiveLocaleCode());
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  drawWrappedText(description ? description : "?", 76,
                  item.effect == ITEM_EFFECT_TEACH_MOVE ? 218 : 208, 314, 7);
  gfx->setTextColor(UI_MUTED);
  gfx->setCursor(uiCenterX(T(S_BACK)), 408);
  gfx->print(T(S_BACK));
}

static void drawBagActions(const ItemEntry &item) {
  drawBagList();
  gfx->fillRoundRect(73, 124, 320, 224, 16, UI_WHITE);
  gfx->drawRoundRect(73, 124, 320, 224, 16, UI_INK);
  const char *labels[3] = { T(S_ITEM_VIEW), T(S_USE), T(S_ITEM_DISCARD) };
  bool canUse = item.effect == ITEM_EFFECT_TEACH_MOVE || bagHasUsableTarget(item);
  for (uint8_t row = 0; row < 3; row++) {
    uint16_t fill = row == 1 && !canUse ? UI_TRACK : UI_BG_DAY;
    gfx->fillRoundRect(BAG_ACTION_X, BAG_ACTION_Y(row), BAG_ACTION_W,
                       BAG_ACTION_H, 12, fill);
    gfx->drawRoundRect(BAG_ACTION_X, BAG_ACTION_Y(row), BAG_ACTION_W,
                       BAG_ACTION_H, 12, UI_INK);
    gfx->setTextColor(row == 1 && !canUse ? UI_MUTED : UI_INK);
    gfx->setTextSize(2);
    uiDrawCenteredIn(labels[row], BAG_ACTION_X, BAG_ACTION_Y(row),
                     BAG_ACTION_W, BAG_ACTION_H);
  }
}

static void drawBagTargets(const ItemEntry &item) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_ITEM_CHOOSE_TARGET)), 42);
  gfx->print(T(S_ITEM_CHOOSE_TARGET));
  for (uint8_t slot = 0; slot < PARTY_SLOTS; slot++) {
    int x = 78 + (slot % 2) * 160;
    int y = 82 + (slot / 2) * 84;
    const PartyMon &m = party.slots[slot];
    bool usable = bagTargetCanApply(item, slot);
    bool selectable = item.effect == ITEM_EFFECT_TEACH_MOVE
                        ? !m.empty() : usable;
    gfx->fillRoundRect(x, y, 150, 70, 10, selectable ? UI_WHITE : UI_TRACK);
    gfx->drawRoundRect(x, y, 150, 70, 10, selectable ? UI_INK : UI_MUTED);
    if (m.empty()) {
      gfx->setTextColor(UI_MUTED);
      gfx->setTextSize(2);
      uiDrawCenteredIn(T(S_PARTY_EMPTY), x, y, 150, 70);
      continue;
    }
    int16_t dex = slot == party.activeIndex() ? pet.speciesId : m.dex;
    const char *nick = slot == party.activeIndex() ? pet.nick : m.nick;
    const uint8_t *th = dex > 0 ? thumbs.get(dex) : nullptr;
    if (th) drawThumb(th, x - 12, y - 5, 2, !selectable);
    gfx->setTextColor(selectable ? UI_INK : UI_MUTED);
    gfx->setTextSize(1);
    gfx->setCursor(x + 54, y + 20);
    gfx->print(dex > 0 ? displaySpeciesName(dex, nick) : T(S_EGG_HDR));
    gfx->setTextSize(1);
    gfx->setCursor(x + 54, y + 42);
    gfx->print(usable ? T(S_USE) : T(S_ITEM_CANT_USE));
  }
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_BACK)), 392);
  gfx->print(T(S_BACK));
}

static void drawBagQuantity(const ItemEntry &item) {
  drawBagList();
  gfx->fillRoundRect(73, 144, 320, 206, 16, UI_WHITE);
  gfx->drawRoundRect(73, 144, 320, 206, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_ITEM_QUANTITY)), 166);
  gfx->print(T(S_ITEM_QUANTITY));
  gfx->setTextSize(1);
  gfx->setCursor(uiCenterX(itemName(item.key)), 194);
  gfx->print(itemName(item.key));
  gfx->fillRoundRect(96, 218, 72, 52, 12, UI_TRACK);
  gfx->fillRoundRect(298, 218, 72, 52, 12, UI_TRACK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(4);
  uiDrawCenteredIn("-", 96, 218, 72, 52);
  uiDrawCenteredIn("+", 298, 218, 72, 52);
  char amount[12];
  snprintf(amount, sizeof(amount), "%u/%u", bagDiscardAmount,
           inventory.count(item.key, bagSelectedMove));
  gfx->setTextSize(2);
  uiDrawCenteredIn(amount, 168, 218, 130, 52);
  gfx->fillRoundRect(133, 282, 200, 52, 12, UI_BAR_BAD);
  gfx->setTextColor(UI_WHITE);
  uiDrawCenteredIn(T(S_CONFIRM), 133, 282, 200, 52);
}

static void drawBagConfirm(const ItemEntry &item) {
  drawBagList();
  char question[96];
  char selectedName[64];
  itemRefName({ item.key, bagSelectedMove }, selectedName, sizeof(selectedName));
  snprintf(question, sizeof(question), T(S_ITEM_DISCARD_Q_FMT),
           bagDiscardAmount, selectedName);
  gfx->fillRoundRect(73, 156, 320, 188, 16, UI_WHITE);
  gfx->drawRoundRect(73, 156, 320, 188, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  uiDrawCenteredIn(question, 83, 166, 300, 42);
  gfx->fillRoundRect(CHOICE_BTN_X, CHOICE_BTN1_Y, CHOICE_BTN_W,
                     CHOICE_BTN_H, 12, UI_BAR_BAD);
  gfx->setTextColor(UI_WHITE);
  uiDrawCenteredIn(T(S_YES), CHOICE_BTN_X, CHOICE_BTN1_Y,
                   CHOICE_BTN_W, CHOICE_BTN_H);
  gfx->fillRoundRect(CHOICE_BTN_X, CHOICE_BTN2_Y, CHOICE_BTN_W,
                     CHOICE_BTN_H, 12, UI_TRACK);
  gfx->setTextColor(UI_INK);
  uiDrawCenteredIn(T(S_NO), CHOICE_BTN_X, CHOICE_BTN2_Y,
                   CHOICE_BTN_W, CHOICE_BTN_H);
}

// This is the only item store. Battle actions query and consume the same
// Inventory directly; there is no carried subset or second battle bag.
void renderBag() {
  const ItemEntry *item = itemByKey(bagSelectedKey);
  if (bagView != BAG_VIEW_LIST &&
      (!item || !inventory.count(bagSelectedKey, bagSelectedMove))) {
    bagView = BAG_VIEW_LIST;
    bagSelectedKey = bagDetailKey = ITEM_KEY_NONE;
    bagSelectedMove = bagDetailMove = MOVE_NONE;
    bagStoneDialog = BAG_STONE_DIALOG_NONE;
    bagStoneTarget = PARTY_SLOTS;
  }
  if (bagView == BAG_VIEW_DETAIL && item) drawBagDetail(*item);
  else if (bagView == BAG_VIEW_ACTIONS && item) drawBagActions(*item);
  else if (bagView == BAG_VIEW_TARGET && item) drawBagTargets(*item);
  else if (bagView == BAG_VIEW_QUANTITY && item) drawBagQuantity(*item);
  else if (bagView == BAG_VIEW_CONFIRM && item) drawBagConfirm(*item);
  else drawBagList();
  drawBagStoneDialog();
  gfx->flush();
}

static void bagReturnToList() {
  bagView = BAG_VIEW_LIST;
  bagSelectedKey = bagDetailKey = ITEM_KEY_NONE;
  bagSelectedMove = bagDetailMove = MOVE_NONE;
  bagStoneDialog = BAG_STONE_DIALOG_NONE;
  bagStoneTarget = PARTY_SLOTS;
  bagDiscardAmount = 1;
}

void bagTap(int16_t x, int16_t y) {
  const ItemEntry *item = itemByKey(bagSelectedKey);
  bool button1 = x >= CHOICE_BTN_X && x <= CHOICE_BTN_X + CHOICE_BTN_W &&
                 y >= CHOICE_BTN1_Y && y <= CHOICE_BTN1_Y + CHOICE_BTN_H;
  bool button2 = x >= CHOICE_BTN_X && x <= CHOICE_BTN_X + CHOICE_BTN_W &&
                 y >= CHOICE_BTN2_Y && y <= CHOICE_BTN2_Y + CHOICE_BTN_H;
  if (bagStoneDialog != BAG_STONE_DIALOG_NONE) {
    bool applied = false;
    if (bagStoneDialog == BAG_STONE_DIALOG_CONFIRM && button1 && item &&
        bagStoneTarget < PARTY_SLOTS) {
      applied = bagStoneTarget == party.activeIndex()
          ? itemApplyToPet(*item, pet, bagSelectedMove)
          : itemApplyToPartyMon(*item, party.slots[bagStoneTarget],
                                bagSelectedMove);
    }
    if (applied && inventory.consume(item->key, 1, bagSelectedMove)) {
      if (bagStoneTarget == party.activeIndex()) party.captureActive(pet, true);
      else party.save();
      bagStoneDialog = BAG_STONE_DIALOG_NONE;
      if (!inventory.count(item->key, bagSelectedMove)) bagReturnToList();
      sfxPlay(SFX_TAP);
      return;
    }
    if (button1 || button2) {
      bagStoneDialog = BAG_STONE_DIALOG_NONE;
      sfxPlay(SFX_TAP);
    }
    return;
  }
  if (bagView == BAG_VIEW_DETAIL) {
    bagDetailKey = ITEM_KEY_NONE;
    bagDetailMove = MOVE_NONE;
    bagView = BAG_VIEW_ACTIONS;
    sfxPlay(SFX_TAP);
    return;
  }
  if (bagView == BAG_VIEW_ACTIONS && item) {
    for (uint8_t row = 0; row < 3; row++) {
      int top = BAG_ACTION_Y(row);
      if (x < BAG_ACTION_X || x > BAG_ACTION_X + BAG_ACTION_W ||
          y < top || y > top + BAG_ACTION_H) continue;
      if (row == 0) {
        bagDetailKey = item->key;
        bagDetailMove = bagSelectedMove;
        bagView = BAG_VIEW_DETAIL;
      } else if (row == 1) {
        if (item->effect != ITEM_EFFECT_TEACH_MOVE &&
            !bagHasUsableTarget(*item)) { sfxPlay(SFX_DENY); return; }
        party.captureActive(pet, false);
        bagStoneTarget = PARTY_SLOTS;
        bagView = BAG_VIEW_TARGET;
      } else {
        bagDiscardAmount = 1;
        bagView = inventory.count(item->key, bagSelectedMove) > 1
                    ? BAG_VIEW_QUANTITY : BAG_VIEW_CONFIRM;
      }
      sfxPlay(SFX_TAP);
      return;
    }
    bagReturnToList();
    sfxPlay(SFX_TAP);
    return;
  }
  if (bagView == BAG_VIEW_TARGET && item) {
    for (uint8_t slot = 0; slot < PARTY_SLOTS; slot++) {
      int left = 78 + (slot % 2) * 160;
      int top = 82 + (slot / 2) * 84;
      if (x < left || x > left + 150 || y < top || y > top + 70) continue;
      if (item->effect == ITEM_EFFECT_TEACH_MOVE) {
        if (party.slots[slot].empty()) { sfxPlay(SFX_DENY); return; }
        bagStoneTarget = slot;
        bagStoneDialog = bagTargetKnowsMove(slot) ? BAG_STONE_DIALOG_KNOWN
            : bagTargetCanApply(*item, slot) ? BAG_STONE_DIALOG_CONFIRM
                                             : BAG_STONE_DIALOG_INCOMPATIBLE;
        sfxPlay(SFX_TAP);
        return;
      }
      if (!bagTargetCanApply(*item, slot)) { sfxPlay(SFX_DENY); return; }
      bool applied = slot == party.activeIndex()
          ? itemApplyToPet(*item, pet)
          : itemApplyToPartyMon(*item, party.slots[slot]);
      if (!applied || !inventory.consume(item->key, 1, bagSelectedMove)) {
        sfxPlay(SFX_DENY);
        return;
      }
      if (slot == party.activeIndex()) party.captureActive(pet, true);
      else party.save();
      bagReturnToList();
      sfxPlay(SFX_TAP);
      return;
    }
    if (y >= 370) bagView = BAG_VIEW_ACTIONS;
    return;
  }
  if (bagView == BAG_VIEW_QUANTITY && item) {
    uint8_t count = inventory.count(item->key, bagSelectedMove);
    if (x >= 96 && x <= 168 && y >= 218 && y <= 270) {
      if (bagDiscardAmount > 1) bagDiscardAmount--;
    } else if (x >= 298 && x <= 370 && y >= 218 && y <= 270) {
      if (bagDiscardAmount < count) bagDiscardAmount++;
    } else if (x >= 133 && x <= 333 && y >= 282 && y <= 334) {
      bagView = BAG_VIEW_CONFIRM;
    }
    sfxPlay(SFX_TAP);
    return;
  }
  if (bagView == BAG_VIEW_CONFIRM && item) {
    if (button1 && inventory.consume(item->key, bagDiscardAmount, bagSelectedMove)) {
      bagReturnToList();
      sfxPlay(SFX_TAP);
    } else if (button2) {
      bagView = inventory.count(item->key, bagSelectedMove) > 1
                  ? BAG_VIEW_QUANTITY : BAG_VIEW_ACTIONS;
      sfxPlay(SFX_TAP);
    }
    return;
  }
  uint16_t stackCount = inventory.stackCount();
  for (uint16_t index = 0; index < stackCount; index++) {
    const InventoryStack *stack = inventory.stackAt(index);
    if (!stack) continue;
    int top = bagScroll.contentY(index * BAG_ROW_STEP);
    if (!bagScroll.fullyVisible(top, BAG_ROW_H)) continue;
    if (x >= 70 && x <= 396 && y >= top && y <= top + BAG_ROW_H) {
      bagSelectedKey = stack->key;
      bagSelectedMove = stack->move;
      bagDetailKey = ITEM_KEY_NONE;
      bagDetailMove = MOVE_NONE;
      bagStoneDialog = BAG_STONE_DIALOG_NONE;
      bagStoneTarget = PARTY_SLOTS;
      bagView = BAG_VIEW_ACTIONS;
      sfxPlay(SFX_TAP);
      return;
    }
  }
  if (y >= 380) {
    bagOpen = false;
    sfxPlay(SFX_TAP);
  }
}

static const ItemEntry *availableReviveItem() {
  for (uint16_t i = 0; i < inventory.stackCount(); i++) {
    const InventoryStack *stack = inventory.stackAt(i);
    const ItemEntry *item = stack ? itemByKey(stack->key) : nullptr;
    if (item && item->effect == ITEM_EFFECT_REVIVE && stack->count) return item;
  }
  return nullptr;
}

static bool useReviveOnPet() {
  const ItemEntry *item = availableReviveItem();
  if (!item || !pet.isDead() || !inventory.consume(item->key)) return false;
  pet.setDead(false);
  return true;
}

static void drawDeadButtons() {
  bool canRevive = availableReviveItem() != nullptr;
  gfx->fillRoundRect(DEAD_REVIVE_X, DEAD_BTN_Y, DEAD_BTN_W, DEAD_BTN_H, 12,
                     canRevive ? UI_BAR_OK : UI_TRACK);
  gfx->drawRoundRect(DEAD_REVIVE_X, DEAD_BTN_Y, DEAD_BTN_W, DEAD_BTN_H, 12, UI_INK);
  gfx->setTextColor(canRevive ? UI_WHITE : 0x8410);
  gfx->setTextSize(2);
  uiDrawCenteredIn(T(S_REVIVE), DEAD_REVIVE_X, DEAD_BTN_Y, DEAD_BTN_W, DEAD_BTN_H);
  gfx->fillRoundRect(DEAD_BURY_X, DEAD_BTN_Y, DEAD_BTN_W, DEAD_BTN_H, 12, UI_BAR_BAD);
  gfx->drawRoundRect(DEAD_BURY_X, DEAD_BTN_Y, DEAD_BTN_W, DEAD_BTN_H, 12, UI_INK);
  gfx->setTextColor(UI_WHITE);
  uiDrawCenteredIn(T(S_BURY), DEAD_BURY_X, DEAD_BTN_Y, DEAD_BTN_W, DEAD_BTN_H);
}

static void drawBuryDialog(const char *name) {
  gfx->fillRoundRect(73, 156, 320, 188, 16, UI_WHITE);
  gfx->drawRoundRect(73, 156, 320, 188, 16, UI_INK);
  char question[64];
  snprintf(question, sizeof(question), T(S_BURY_Q), name);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(question), 180);
  gfx->print(question);
  gfx->fillRoundRect(CHOICE_BTN_X, CHOICE_BTN1_Y, CHOICE_BTN_W, CHOICE_BTN_H,
                     12, UI_BAR_BAD);
  gfx->setTextColor(UI_WHITE);
  uiDrawCenteredIn(T(S_BURY), CHOICE_BTN_X, CHOICE_BTN1_Y, CHOICE_BTN_W, CHOICE_BTN_H);
  gfx->fillRoundRect(CHOICE_BTN_X, CHOICE_BTN2_Y, CHOICE_BTN_W, CHOICE_BTN_H,
                     12, UI_TRACK);
  gfx->setTextColor(UI_INK);
  uiDrawCenteredIn(T(S_NO), CHOICE_BTN_X, CHOICE_BTN2_Y, CHOICE_BTN_W, CHOICE_BTN_H);
}

static void buryActivePet() {
  uint8_t buried = party.activeIndex();
  party.captureActive(pet, false);
  party.releaseAt(buried);
  if (party.count()) return;
  for (uint8_t i = 0; i < BOX_SLOTS; i++) {
    if (party.box[i].empty()) continue;
    party.swapPartyBox(buried, i);
    return;
  }
  pet.newEgg();
  party.captureActive(pet);
}

static void buryTap(int16_t x, int16_t y) {
  bool yes = x >= CHOICE_BTN_X && x <= CHOICE_BTN_X + CHOICE_BTN_W &&
             y >= CHOICE_BTN1_Y && y <= CHOICE_BTN1_Y + CHOICE_BTN_H;
  bool no = x >= CHOICE_BTN_X && x <= CHOICE_BTN_X + CHOICE_BTN_W &&
            y >= CHOICE_BTN2_Y && y <= CHOICE_BTN2_Y + CHOICE_BTN_H;
  if (!yes && !no) return;
  int8_t target = buryTarget;
  buryTarget = BURY_TARGET_NONE;
  if (!yes) { sfxPlay(SFX_TAP); return; }
  if (target == BURY_TARGET_PET && pet.isDead()) buryActivePet();
  sfxPlay(SFX_BYE);
}

// Every primary button's height, so a test can hold them all to UI_TAP_MIN
// instead of waiting for somebody to report the next one by hand.
void uiButtonHeights(int *out, int max, int *n) {
  const int h[] = { BOXPICK_BACK_H, LANBTN_H, CHOICE_BTN_H,
                    BTL_CELL_H + BTL_HIT_PAD * 2, PETNAV_HIT_H,
                    NAVMENU_BTN_H };
  int c = (int)(sizeof(h) / sizeof(h[0]));
  if (c > max) c = max;
  for (int i = 0; i < c; i++) out[i] = h[i];
  if (n) *n = c;
}

// The gym list's difficulty pill against its first leader row. Enlarging the
// pill to a real tap target once pushed it straight over that row -- the third
// overlap of this kind, after BOX/CLOSE and the battle grid against BACK.
// Which home icon is the only one live while the pet sleeps, and where it is.
// Exposed so a test can prove it is the LIGHT: removing an icon once shifted
// every index and quietly made the BATH button the wake-up button.
// Asleep, the LIGHT is the only live icon -- it is what wakes the pet. Both the
// draw path and the tap path ask THIS, so a greyed button can never still be
// tappable and the greying can never point at the wrong icon.
bool uiButtonDisabled(int i) { return pet.sleeping && i != BTN_LIGHT; }

int uiSleepButton(int *cx, int *cy) {
  if (cx) *cx = buttons[BTN_LIGHT].cx;
  if (cy) *cy = buttons[BTN_LIGHT].cy;
  return BTN_LIGHT;
}

void uiButtonAt(int i, int *cx, int *cy, int *half) {
  if (i < 0 || i >= BTN_COUNT) return;
  if (cx) *cx = buttons[i].cx;
  if (cy) *cy = buttons[i].cy;
  if (half) *half = BTN_HALF;
}

void gymHeaderRects(int *pillTop, int *pillBot, int *rowTop) {
  if (pillTop) *pillTop = GYMDIF_Y;
  if (pillBot) *pillBot = GYMDIF_Y + GYMDIF_H;
  if (rowTop) *rowTop = GYM_ROW_Y(0);
}

// deslizar: dir +1 = hacia la derecha
void onSwipe(int dir) {
  if (quizBlocking()) return;
  if (moveInfoOpen) { moveInfoOpen = false; return; }
  if (btlWinUntil) return;
  // The region chooser pages, and it is checked before everything else because
  // it sits on TOP of the starter/gallery/gym screens -- each of which has its
  // own horizontal handler that would otherwise swallow the gesture. Paging a
  // screen by closing it is the bug this project shipped four times.
  if (rpickSwipe(dir)) return;
  if (pet.awaitingStarter()) return;  // bloqueado durante la eleccion de inicial
  if (navMenuOpen) { navMenuOpen = false; return; }
  if (menuOpen) { menuOpen = false; return; }   // any swipe closes the menu
  if (battleOpen) {
    if (btlFoeDetailOpen) {
      int p = (int)btlFoeDetailPage + (dir > 0 ? -1 : 1);
      if (p < 0) p = 0;
      if (p >= BTL_FOE_DETAIL_PAGES) p = BTL_FOE_DETAIL_PAGES - 1;
      btlFoeDetailPage = (uint8_t)p;
      return;
    }
    if (btlMenu == 3) {
      int p = (int)btlItemPage + (dir > 0 ? -1 : 1);
      if (p >= 0) btlItemPage = (uint8_t)p;
    } else if (btlMenu == 2 || btlMenu == 4) {
      uint8_t pages = (uint8_t)((btlSquadN + 3) / 4);
      int p = (int)btlTargetPage + (dir > 0 ? -1 : 1);
      if (p >= 0 && p < pages) btlTargetPage = (uint8_t)p;
    }
    return;
  }
  if (pickOpen) {   // horizontal pages the candidates, as everywhere else
    uint8_t pages = (pickCandidates() + PICK_PER_PAGE - 1) / PICK_PER_PAGE;
    if (!pages) pages = 1;
    int p = (int)pickPage + (dir > 0 ? -1 : 1);
    if (p < 0 || p >= pages) pickOpen = false;
    else pickPage = (uint8_t)p;
    return;
  }
  if (bagOpen) {
    if (bagView == BAG_VIEW_LIST) bagOpen = false;
    else if (bagView == BAG_VIEW_DETAIL || bagView == BAG_VIEW_TARGET) {
      bagDetailKey = ITEM_KEY_NONE;
      bagDetailMove = MOVE_NONE;
      bagView = BAG_VIEW_ACTIONS;
    } else {
      bagReturnToList();
    }
    return;
  }
  if (boxOpen) {
    if (partyPick) return;          // a pending creature needs an explicit choice
    if (boxSel) { boxSel = 0; return; }
    uint8_t pages = BOX_SLOTS / BOX_PER_PAGE;
    int p = (int)boxPage + (dir > 0 ? -1 : 1);
    if (p < 0 || p >= pages) boxOpen = false;
    else boxPage = (uint8_t)p;
    return;
  }
  if (gymOpen) {   // horizontal pages the ladder; vertical backs out
    uint8_t pages = (regionBattleInfo(gymRegion).trainerCount + 1 + GYM_ROWS - 1) / GYM_ROWS;
    int p = (int)gymPage + (dir > 0 ? -1 : 1);
    if (p < 0 || p >= pages) { gymPick = true; rpickPage = 0; }  // back to the chooser
    else gymPage = (uint8_t)p;
    return;
  }
  if (playerOpen) {   // horizontal pages it, like the card and the gallery
    int p = (int)playerPage + (dir > 0 ? -1 : 1);
    if (p < 0 || p >= PLAYER_PAGES) playerOpen = false;
    else playerPage = (uint8_t)p;
    return;
  }
  if (trainOpen) { trainOpen = false; return; }
  if (movePickOpen) {   // the picker is paged; without this its later pages
    MoveId all[64];    // were simply unreachable
    uint8_t n = learnableList(all, sizeof(all) / sizeof(all[0]));
    uint8_t pages = n ? (n + MOVE_PICK_PER_PAGE - 1) / MOVE_PICK_PER_PAGE : 1;
    int p = (int)movePickPage + (dir > 0 ? -1 : 1);
    if (p < 0 || p >= pages) movePickOpen = false;
    else movePickPage = (uint8_t)p;
    return;
  }
  if (gameOpen) { leaveGame(); return; }   // swipe out, keeping what you earned
  if (spdOpen) { leaveSpeed(); return; }
  if (kbOpen || clockOpen) return;
  if (cardOpen) {  // dentro de la ficha: cambiar entre las 4 paginas
    if (natureInfoOpen) { natureInfoOpen = false; return; }
    int p = (int)cardPage + (dir > 0 ? -1 : 1);  // izquierda avanza
    cardPage = p < 0 ? 0 : (p > CARD_PAGES - 1 ? CARD_PAGES - 1 : p);
    return;
  }
  if (!galleryOpen) {
    // The main horizontal gesture moves through occupied cultivation slots.
    if (!pet.ceremony) {
      if (party.activateNext(dir < 0 ? 1 : -1, pet)) sfxPlay(SFX_TAP);
    }
    return;
  }
  if (galleryDetail) {  // en detalle: volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  int np = galleryPage - dir;  // deslizar a la izquierda avanza pagina
  if (np < 0) {                // back past the first page = the region chooser
    galleryPick = true;
    rpickPage = 0;
    galleryPmd.unload();
    return;
  }
  if (np > GAL_PAGES - 1) np = GAL_PAGES - 1;
  if (np != galleryPage) {
    galleryPage = np;
    galleryDirty = true;
  }
}

bool petNavTap(int16_t x, int16_t y) {
  if (!inPetNavZone(x, y)) return false;
  party.captureActive(pet, false);
  boxOpen = true;
  boxPage = 0;
  boxSel = 0;
  sfxPlay(SFX_TAP);
  return true;
}

void petNavPoints(int *slot0X, int *slotGap, int *y) {
  uint8_t count = party.count();
  if (slot0X) *slot0X = petNavDotX(0, count ? count : 1);
  if (slotGap) *slotGap = 24;
  if (y) *y = PETNAV_DOT_Y;
}

uint8_t petNavCount() { return party.count(); }

void navMenuButtonPoint(uint8_t index, int *x, int *y) {
  if (x) *x = NAVMENU_BTN_X + NAVMENU_BTN_W / 2;
  if (y) *y = NAVMENU_BTN_Y(index) + NAVMENU_BTN_H / 2;
}

bool navMenuTap(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < NAVMENU_ROWS; i++) {
    int top = NAVMENU_BTN_Y(i);
    if (x < NAVMENU_BTN_X || x > NAVMENU_BTN_X + NAVMENU_BTN_W ||
        y < top || y > top + NAVMENU_BTN_H) continue;
    navMenuOpen = false;
    sfxPlay(SFX_TAP);
    if (i == 0) {
      bagOpen = true;
      bagView = BAG_VIEW_LIST;
      bagSelectedKey = bagDetailKey = ITEM_KEY_NONE;
      bagSelectedMove = bagDetailMove = MOVE_NONE;
      bagStoneDialog = BAG_STONE_DIALOG_NONE;
      bagStoneTarget = PARTY_SLOTS;
      bagDiscardAmount = 1;
      bagScroll.reset();
    } else if (i == 1) {
      gymOpen = true;
      gymPick = true;
      gymPage = 0;
      rpickPage = 0;
    } else {
      playerOpen = true;
      playerPage = 0;
    }
    return true;
  }
  navMenuOpen = false;
  return false;
}

void onTap(int16_t x, int16_t y) {
  if (quiz.active) { quizTap(x, y); return; }
  if (buryTarget != BURY_TARGET_NONE) { buryTap(x, y); return; }
  if (pet.awaitingStarter()) {  // primera partida: idioma, region e inicial
    if (!starterLanguageDone) {
      for (uint8_t i = 0; i < langCount(); i++) {
        int lx = LANGUAGE_COL_X(i);
        int ly = LANGUAGE_ROW_Y(i);
        if (x >= lx && x <= lx + LANGUAGE_CELL_W &&
            y >= ly && y <= ly + LANGUAGE_CELL_H) {
          if (setLang((Lang)i)) {
            starterLanguageDone = true;
            refreshUiFont();
            sfxPlay(SFX_TAP);
          }
          break;
        }
      }
      return;
    }
    if (!starterRegionDone) {
      int r = regionPickTap(x, y, RPICK_FOR_START);
      if (r >= 0) {
        pet.setRegion((uint8_t)r);   // the region picked here is where eggs come from too
        starterRegionDone = true;
        sfxPlay(SFX_TAP);
      }
      return;
    }
    for (int i = 0; i < starterCountShown(player.region); i++) {
      int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
      if (x >= 70 && x <= 396 && y >= ry && y <= ry + STARTER_ROW_H) {
        pet.chooseStarter(starterOf(player.region, (uint8_t)i));
        sfxPlay(SFX_TAP);
        break;
      }
    }
    return;
  }
  if (moveInfoOpen) {
    int changeY = uiLayoutMetric(UI_LAYOUT_MOVE_CHANGE_Y, 320);
    if (x >= 93 && x <= 373 && y >= changeY && y <= changeY + 52) {
      moveInfoOpen = false;
      movePickPage = 0;
      movePickOpen = true;
      sfxPlay(SFX_TAP);
    } else {
      moveInfoOpen = false;
    }
    return;
  }
  if (btlWinUntil) {
    btlDismissWin();
    return;
  }
  if (bagOpen) {
    bagTap(x, y);
    return;
  }
  if (battleOpen) {
    battleTap(x, y);
    return;
  }
  if (playerOpen) {
    if (playerPage == 0 && y >= 32 && y < 68) {   // the name: rename yourself
      openKeyboardFor(KB_TRAINER);
      sfxPlay(SFX_TAP);
      return;
    }
    // the avatar is the only other live target; everything else backs out
    if (playerPage == 0 && x > CX - 40 && x < CX + 40 && y > 70 && y < 146) {
      player.avatar = (uint8_t)((player.avatar + 1) % AVATAR_COUNT);
      player.save();
      sfxPlay(SFX_TAP);
      return;
    }
    playerOpen = false;
    return;
  }
  if (pickOpen) {
    pickTap(x, y);
    return;
  }
  if (lanOpen) {
    lanTap(x, y);
    return;
  }
  if (boxOpen) {
    boxTap(x, y);
    return;
  }
  if (gymOpen && gymPick) {
    int r = regionPickTap(x, y, RPICK_FOR_GYMS);
    if (r >= 0) {
      gymRegion = (uint8_t)r;
      gymPage = 0;
      gymPick = false;
      sfxPlay(SFX_TAP);
      return;
    }
    if (y >= LANBTN_Y && y <= LANBTN_Y + LANBTN_H &&
        x >= LANBTN_X && x <= LANBTN_X + LANBTN_W) {   // LAN battle
      gymOpen = false; gymPick = false;
      lan.state = LINK_OFF;
      lanOpen = true;
      sfxPlay(SFX_TAP);
      return;
    }
    if (y > 380) { gymOpen = false; gymPick = false; }
    return;
  }
  if (gymOpen) {
    if (y >= GYMDIF_Y && y <= GYMDIF_Y + GYMDIF_H) {   // the difficulty pill
      gymHard = !gymHard;
      sfxPlay(SFX_TAP);
      return;
    }
    if (y >= 380 && y <= 412 && x >= 148 && x <= 318) {   // LAN battle
      gymOpen = false;
      lan.state = LINK_OFF;
      lanOpen = true;
      sfxPlay(SFX_TAP);
      return;
    }
    for (int i = 0; i < GYM_ROWS; i++) {
      uint8_t entry = gymPage * GYM_ROWS + i;
      if (entry > regionBattleInfo(gymRegion).trainerCount) break;
      int ry = GYM_ROW_Y(i);
      if (x < 70 || x > 396 || y < ry || y > ry + 44) continue;
      if (entry == 0) {
        sfxPlay(SFX_TAP);
        startWildBattle(gymRegion, gymHard);
        return;
      }
      uint8_t idx = entry - 1;
      if (!gymUnlocked(idx, gymHard)) { sfxPlay(SFX_DENY); return; }
      sfxPlay(SFX_TAP);
      gymOpen = false;
      pickTrainer = idx;
      pickHard = gymHard;
      pickPage = 0;
      pickDefault(squadCap(idx, gymHard));
      pickOpen = true;
      return;
    }
    gymOpen = false;
    return;
  }
  if (trainOpen) {
    bool inPanel = (x >= TRAIN_X && x <= TRAIN_X + TRAIN_W &&
                    y >= TRAIN_Y && y <= TRAIN_Y + TRAIN_H);
    if (!inPanel) { trainOpen = false; return; }   // tap outside = back to the pet
    for (int i = 0; i < 3; i++) {   // all three train something now
      int ry = TRAIN_ROW_Y(i);
      if (x < TRAIN_X + 18 || x > TRAIN_X + TRAIN_W - 18) continue;
      if (y < ry || y > ry + TRAIN_ROW_H) continue;
      sfxPlay(SFX_TAP);
      trainOpen = false;
      if (i == 0) startSack();
      else if (i == 1) startSpeedGame();
      else startGame();          // the ball game trains DEF
      return;
    }
    return;
  }
  if (navMenuOpen) { navMenuTap(x, y); return; }
  // The menu is modal. A tap outside the panel or any swipe closes it; its last
  // row deliberately opens a confirmation instead of being a third close path.
  // Deliberately no timeout: a menu that vanishes while you read it is worse
  // than one that lingers.
  if (menuOpen) {
    bool inPanel = (x >= MENU_X && x <= MENU_X + MENU_W &&
                    y >= MENU_Y && y <= MENU_Y + MENU_H);
    if (!inPanel) { menuOpen = false; return; }   // tap outside = back to the pet
    for (int i = 0; i < MENU_ROWS; i++) {
      int ry = MENU_ROW_Y(i);
      if (x < MENU_X + 18 || x > MENU_X + MENU_W - 18) continue;
      if (y < ry || y > ry + MENU_ROW_H) continue;
      if (menuRowDisabled((uint8_t)i)) { sfxPlay(SFX_DENY); return; }
      sfxPlay(SFX_TAP);
      menuOpen = false;
      if (i == 0) { party.setLead(party.activeIndex()); }
      else if (i == 1) { galleryOpen = true; galleryPick = true; galleryPage = 0; rpickPage = 0; galleryDetail = 0; galleryDirty = true; }
      else if (i == 2) { openClock(); }
      else if (i == 3) {
        if (!pet.canExitNow()) { sfxPlay(SFX_DENY); return; }
        choiceKind = 3; choiceUntil = millis() + 12000;
      }
      else if (i == 4) { choiceKind = 4; choiceUntil = millis() + 12000; }
      return;
    }
    return;
  }
  if (movePickOpen) {
    MoveId all[64];
    uint8_t n = learnableList(all, sizeof(all) / sizeof(all[0]));
    for (uint8_t i = 0; i < MOVE_PICK_PER_PAGE; i++) {
      uint8_t idx = movePickPage * MOVE_PICK_PER_PAGE + i;
      if (idx >= n) break;
      int ry = MOVE_PICK_Y(i);
      if (x < 70 || x > 396 || y < ry || y > ry + MOVE_ROW_H) continue;
      sfxPlay(SFX_TAP);
      // Swapping for a move already in another slot would silently duplicate
      // it, so trade its active or reserve slot with the selected battle slot.
      MoveId *active = pickTargetMoves();
      MoveId *reserve = pickTargetReserveMoves();
      MoveId chosen = all[idx];
      for (int s = 0; s < MOVE_SLOTS; s++)
        if (active[s] == chosen && s != movePickSlot) {
          active[s] = active[movePickSlot];
          active[movePickSlot] = chosen;
          chosen = MOVE_NONE;
          break;
        }
      for (int s = 0; chosen && s < RESERVE_MOVE_SLOTS; s++)
        if (reserve[s] == chosen) {
          reserve[s] = active[movePickSlot];
          active[movePickSlot] = chosen;
          break;
        }
      if (movePickParty) party.save(); else pet.saveNow();
      movePickOpen = false;
      return;
    }
    movePickOpen = false;   // tap anywhere else = back to the moves page
    return;
  }
  if (galleryOpen) {
    if (galleryPick) {
      int r = regionPickTap(x, y, RPICK_FOR_DEX);
      if (r >= 0) {
        galleryRegion = (uint8_t)r;
        galleryPage = 0;
        galleryDetail = 0;
        galleryDirty = true;
        galleryPick = false;
        sfxPlay(SFX_TAP);
      } else if (y > 380) {
        galleryOpen = false;
      }
      return;
    }
    galleryTap(x, y);
    return;
  }
  if (kbOpen) {
    keyboardTap(x, y);
    return;
  }
  if (clockOpen) {
    clockTap(x, y);
    return;
  }
  if (pet.ceremony) return;  // durante la despedida no hay botones
  if (cardOpen) {
    if (natureInfoOpen) { natureInfoOpen = false; return; }
    if (cardPage == 0 && x >= 90 && x <= 376 && y >= 306 && y <= 352) {
      natureInfoOpen = true;
      sfxPlay(SFX_TAP);
    }
    else if (cardPage == 2) {
      for (int i = 0; i < MOVE_SLOTS; i++) {   // tap a slot to change it
        int ry = MOVE_ROW_Y(i);
        if (x < 70 || x > 396 || y < ry || y > ry + MOVE_ROW_H) continue;
        sfxPlay(SFX_TAP);
        movePickParty = 0;      // the live pet
        movePickSlot = i;
        movePickPage = 0;
        moveInfoOpen = pet.moves[i] != MOVE_NONE;
        movePickOpen = !moveInfoOpen;
        return;
      }
      cardOpen = false;            // anywhere else on the page still exits
    } else {
      cardOpen = false;
    }
    return;
  }
  if (spdOpen) {
    spdTap(x, y);
    return;
  }
  if (gameOpen) {
    gameTap(x, y);
    return;
  }
  if (choiceKind) {          // dialogo de decision: boton accion (arriba) / mantener (abajo)
    bool b1 = (x >= CHOICE_BTN_X && x <= CHOICE_BTN_X + CHOICE_BTN_W &&
               y >= CHOICE_BTN1_Y && y <= CHOICE_BTN1_Y + CHOICE_BTN_H);
    bool b2 = (x >= CHOICE_BTN_X && x <= CHOICE_BTN_X + CHOICE_BTN_W &&
               y >= CHOICE_BTN2_Y && y <= CHOICE_BTN2_Y + CHOICE_BTN_H);
    if (choiceKind == 1) {                 // evolucion
      if (b1) {
        int16_t old = pet.speciesId;
        pet.evolve();
        evoPmd.load(old, pet.shiny, pet.gender);
      }
      else if (b2) pet.declineEvolve();
    } else if (choiceKind == 3) {          // contextual farewell / release
      if (b1) {
        if (pet.canFarewellNow()) pet.startFarewell();
        else pet.release();
      }
    } else if (choiceKind == 4) {          // confirmed firmware power-off
      if (b1) {
        uint32_t e = rtcEpoch();
        party.saveSnapshot(pet, e ? e : pet.lastSeenEpoch);
        pwrShutdown();
      }
    }
    choiceKind = 0;
    return;
  }
  if (feedMenuUntil) {       // selector de comida
    if (millis() < feedMenuUntil && y >= 288 && y <= 352 && x >= 101 && x <= 365) {
      int item = (x - 101) / 66;
      if (item == 3) beginCareQuiz({ CARE_ACTION_FEED_CANDY, 0 }, false);
      else beginCareQuiz({ CARE_ACTION_FEED_BERRY, (uint16_t)item }, false);
    }
    feedMenuUntil = 0;
    return;
  }
  if (petNavTap(x, y)) return;
  if (pet.isEgg()) {
    // the region pill first, or choosing a region would also crack the egg --
    // and a near miss is swallowed rather than counted, since three taps hatch
    if (eggRegionTap(x, y)) return;
    pet.eggTap();
    sfxPlay(SFX_TAP);
    return;
  }
  if (pet.isDead()) {
    if (y >= DEAD_BTN_Y && y <= DEAD_BTN_Y + DEAD_BTN_H &&
        x >= DEAD_REVIVE_X && x <= DEAD_REVIVE_X + DEAD_BTN_W) {
      sfxPlay(useReviveOnPet() ? SFX_HATCH : SFX_DENY);
      return;
    }
    if (y >= DEAD_BTN_Y && y <= DEAD_BTN_Y + DEAD_BTN_H &&
        x >= DEAD_BURY_X && x <= DEAD_BURY_X + DEAD_BTN_W) {
      buryTarget = BURY_TARGET_PET;
      sfxPlay(SFX_TAP);
    }
    return;
  }
  // boton de evolucion: abre el dialogo evolucionar/mantener
  if (pet.wantEvolveButton() && x >= EVO_BTN_X && x <= EVO_BTN_X + EVO_BTN_W &&
      y >= EVO_BTN_Y && y <= EVO_BTN_Y + EVO_BTN_H) {
    choiceKind = 1; choiceUntil = millis() + 12000;
    return;
  }
  // The neglect CTA triggers runaway directly; farewell/release live in menu.
  //
  // The runaway does NOT ask, deliberately. A pet you have to authorise to
  // leave is not really at stake, and neglect having teeth is the whole premise.
  // What was actually wrong is that a night's sleep could reach this state at
  // all -- fixed where it belongs, in the drain, by having the creature put
  // itself to bed (Pet::tick, autoSleep).
  if (x >= FAR_BTN_X && x <= FAR_BTN_X + FAR_BTN_W &&
      y >= FAR_BTN_Y && y <= FAR_BTN_Y + FAR_BTN_H) {
    if (pet.canRunawayNow()) { pet.startRunaway(); return; }
  }
  for (int i = 0; i < BTN_COUNT; i++) {
    int dx = x - buttons[i].cx, dy = y - buttons[i].cy;
    if (dx * dx + dy * dy <= BTN_HIT * BTN_HIT) {
      if (uiButtonDisabled(i)) { sfxPlay(SFX_DENY); return; }
      sfxPlay(SFX_TAP);
      if (i == BTN_FOOD) feedMenuUntil = millis() + 6000;
      else if (i == BTN_LIGHT) pet.toggleLight();
      else if (i == BTN_BATH) startBath();
      else trainOpen = true;
      return;
    }
  }
  // Tapping the name band opens the creature menu. The slot dots above
  // it are handled first by petNavTap(), so both controls keep distinct hit zones.
  if (y >= 68 && y < 120) {
    menuOpen = true;
    sfxPlay(SFX_TAP);
    return;
  }
  // tocar al bicho = caricia
  if (inPetZone(x, y)) {
    if (!pet.sleeping && beginCareQuiz({ CARE_ACTION_CARESS, 0 }, false) && quiz.active)
      sfxPlay(SFX_TAP);
  }
}

// ---------- render ----------

bool gNight = false;  // noche real (por hora) o durmiendo: lo fija render()
uint16_t inkColor() { return gNight ? UI_INK_NIGHT : UI_INK; }

// ---------- escena de fondo: bioma del tipo + hora real del RTC ----------

#define C565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define HORIZON 232  // linea donde el cielo se encuentra con el suelo

uint16_t lerp565(uint16_t a, uint16_t b, int i, int n) {
  if (n <= 0) return a;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)((((ar + (br - ar) * i / n) << 11)) |
                    (((ag + (bg - ag) * i / n) << 5)) | (ab + (bb - ab) * i / n));
}

// hora del dia 0-23 (de la hora real cacheada cada 30s; 13 si no hay reloj)
int sceneHour() {
  uint32_t e = pet.lastSeenEpoch;
  return e ? (int)((e / 3600) % 24) : 13;
}

// suelo de cada bioma de dia (de noche se mezcla hacia el azul nocturno)
static const uint16_t BIOME_SOIL[6] = {
  C565(0x7e, 0xc0, 0x7f),  // 0 pradera
  C565(0xdc, 0xca, 0x94),  // 1 playa (arena)
  C565(0x4f, 0x8a, 0x55),  // 2 bosque
  C565(0x8a, 0x55, 0x44),  // 3 volcan
  C565(0xa8, 0x90, 0x6a),  // 4 montana
  C565(0xe6, 0xee, 0xf5),  // 5 nieve
};

void drawClouds(uint32_t now, uint16_t col) {
  for (int k = 0; k < 2; k++) {
    int cx = (int)((now / 50 + k * 250) % 560) - 40;
    int cy = 70 + k * 34;
    gfx->fillCircle(cx, cy, 16, col);
    gfx->fillCircle(cx + 18, cy + 3, 13, col);
    gfx->fillCircle(cx - 15, cy + 4, 12, col);
  }
}

void drawScene(uint8_t biome, uint32_t now, bool night) {
  int h = sceneHour();
  uint16_t top, bot;
  if (night)            { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (h < 8)       { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }  // amanecer
  else if (h < 18)      { top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }  // dia
  else                  { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }  // atardecer

  // cielo en bandas
  for (int y = 0; y < HORIZON; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, HORIZON));

  // sol o luna
  if (night) {
    gfx->fillCircle(360, 78, 24, C565(0xe8, 0xee, 0xf5));
    gfx->fillCircle(370, 72, 22, lerp565(top, bot, 78, HORIZON));  // creciente
    for (auto &st : STARS) gfx->fillRect(st[0], st[1], 4, 4, UI_WHITE);
  } else if (h < 18) {
    gfx->fillCircle(360, 84, 26, h < 8 ? C565(0xff, 0xd9, 0x8a) : C565(0xff, 0xe7, 0x9f));
    drawClouds(now, C565(0xff, 0xff, 0xff));
  } else {
    gfx->fillCircle(233, HORIZON - 6, 34, C565(0xff, 0xf1, 0xc8));  // sol poniente
  }

  // mar de la playa: una franja de agua sobre la arena
  uint16_t soil = BIOME_SOIL[biome < 6 ? biome : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  if (biome == 1) {
    uint16_t sea = night ? C565(0x1c, 0x34, 0x52) : C565(0x4f, 0x96, 0xc4);
    gfx->fillRect(0, HORIZON - 26, 466, 26, sea);
    for (int i = 0; i < 3; i++) {
      int wy = HORIZON - 22 + i * 7;
      uint16_t fc = night ? C565(0x3a, 0x58, 0x78) : C565(0xbf, 0xe6, 0xf5);
      gfx->fillRect(60 + ((now / 60 + i * 30) % 60), wy, 26, 2, fc);
      gfx->fillRect(300 - ((now / 60 + i * 20) % 60), wy, 26, 2, fc);
    }
  }

  // suelo
  gfx->fillRect(0, HORIZON, 466, 466 - HORIZON, soil);
  uint16_t hill = lerp565(soil, night ? C565(0x0c, 0x12, 0x24) : C565(0xff, 0xff, 0xff), 3, 16);
  gfx->fillRoundRect(-60, HORIZON - 14, 586, 60, 30, hill);

  // detalles del bioma
  uint16_t dk = lerp565(soil, C565(0x10, 0x18, 0x20), night ? 11 : 7, 16);
  if (biome == 2) {  // bosque: coniferas en silueta
    for (int tx : { 60, 150, 360, 416 }) {
      gfx->fillTriangle(tx, HORIZON - 46, tx - 16, HORIZON, tx + 16, HORIZON, dk);
      gfx->fillTriangle(tx, HORIZON - 60, tx - 12, HORIZON - 28, tx + 12, HORIZON - 28, dk);
    }
  } else if (biome == 3) {  // volcan: rocas y brasas
    gfx->fillTriangle(70, HORIZON, 40, HORIZON + 30, 100, HORIZON + 30, dk);
    gfx->fillTriangle(400, HORIZON + 4, 372, HORIZON + 30, 430, HORIZON + 30, dk);
    if (!night)
      for (int e = 0; e < 4; e++)
        gfx->fillRect(120 + e * 70, HORIZON + 8 + (e % 2) * 6, 4, 4, C565(0xff, 0x9b, 0x3a));
  } else if (biome == 4) {  // montana: cumbres al fondo
    gfx->fillTriangle(140, HORIZON - 50, 60, HORIZON, 220, HORIZON, dk);
    gfx->fillTriangle(330, HORIZON - 38, 250, HORIZON, 410, HORIZON, dk);
  } else if (biome == 5 && !night) {  // nieve: copos cayendo
    for (int f = 0; f < 10; f++) {
      int fx = (f * 53 + now / 40) % 466;
      int fy = (f * 90 + now / 18) % HORIZON;
      gfx->fillRect(fx, fy, 3, 3, UI_WHITE);
    }
  } else if (biome == 0) {  // pradera: matas de hierba
    for (int gx : { 80, 175, 300, 395 })
      for (int b = -1; b <= 1; b++)
        gfx->fillRect(gx + b * 5, HORIZON + 6, 2, 8 + (b == 0 ? 4 : 0), dk);
  }
}

// primera partida: elige el idioma antes de la region y el inicial
void renderFirstBootLanguage() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  const char *title = T(S_LANG_LABEL);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(title), 64);
  gfx->print(title);
  for (uint8_t i = 0; i < langCount(); i++) {
    int lx = LANGUAGE_COL_X(i);
    int ly = LANGUAGE_ROW_Y(i);
    gfx->fillRoundRect(lx, ly, LANGUAGE_CELL_W, LANGUAGE_CELL_H, 12, UI_WHITE);
    gfx->drawRoundRect(lx, ly, LANGUAGE_CELL_W, LANGUAGE_CELL_H, 12, UI_TRACK);
    const char *label = langDisplayName((Lang)i);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterIn(label, lx, LANGUAGE_CELL_W), ly + 18);
    gfx->print(label);
  }
  gfx->flush();
}

// primera partida: elige inicial entre Bulbasaur / Charmander / Squirtle
void renderStarterSelect() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  const char *t = T(S_CHOOSE_STARTER);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(t), 68);
  gfx->print(t);
  for (int i = 0; i < starterCountShown(player.region); i++) {
    int16_t d = starterOf(player.region, i);
    const DexEntry &de = dexEntry(d);
    int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
    gfx->fillRoundRect(70, ry, 326, STARTER_ROW_H, 14, lerp565(de.accent, UI_WHITE, 6, 8));
    gfx->drawRoundRect(70, ry, 326, STARTER_ROW_H, 14, de.accent);
    const uint8_t *th = thumbs.get(d);     // miniatura del inicial (si la SD esta lista)
    if (th) drawThumb(th, 76, ry - 5, 3, false);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(3);
    gfx->setCursor(178, ry + 24);
    gfx->print(speciesName(d));
  }
  gfx->flush();
}

// ---------- crash breadcrumbs ----------
//
// "It has a mini crash" is not a bug report you can act on, because the board
// never says WHY it restarted. These three live in RTC memory, which survives
// a panic, a watchdog and a software reset -- everything except pulling the
// power -- so the next boot can say what the firmware was doing when it died.
//
// Written every frame. That is one word to RTC RAM per render, which costs
// nothing next to a full-screen redraw, and it means the crumb always names
// the screen that was actually on the panel rather than the last one opened.
#define CRUMB_MAGIC 0x7AABE10C
RTC_NOINIT_ATTR uint32_t gCrumbMagic;
RTC_NOINIT_ATTR uint32_t gCrumbScreen;
RTC_NOINIT_ATTR uint32_t gCrumbHeap;


// Which screen is on the panel RIGHT NOW, in the same order render() tests.
uint8_t uiCurrentScreen() {
  if (quiz.active) return SCR_QUIZ;
  if (pet.awaitingStarter()) {
    if (!starterLanguageDone) return SCR_LANGUAGE;
    return starterRegionDone ? SCR_STARTER : SCR_REGION;
  }
  if (moveInfoOpen) return SCR_MOVEPICK;
  if (galleryOpen) return galleryPick ? SCR_DEXPICK : SCR_GALLERY;
  if (movePickOpen) return SCR_MOVEPICK;
  if (btlWinUntil) return SCR_WIN;
  if (bagOpen) return SCR_BAG;
  if (boxOpen) return SCR_BOX;
  if (kbOpen) return SCR_KEYBOARD;
  if (cardOpen) return SCR_CARD;
  if (playerOpen) return SCR_PLAYER;
  if (clockOpen) return SCR_CLOCK;
  if (navMenuOpen) return SCR_MENU;
  if (battleOpen) return SCR_BATTLE;
  if (pickOpen) return SCR_PICK;
  if (lanOpen) return SCR_LAN;
  if (gymOpen) return gymPick ? SCR_GYMPICK : SCR_GYM;
  if (gameOpen || sackOpen || spdOpen) return SCR_GAME;
  if (trainOpen) return SCR_TRAIN;
  if (menuOpen) return SCR_MENU;
  return SCR_MAIN;
}

bool uiScreenContinuous(uint8_t screen) {
  return (screen == SCR_MAIN && !screenOff) || screen == SCR_QUIZ ||
         (screen == SCR_GALLERY && galleryDetail) || screen == SCR_BATTLE || screen == SCR_GAME ||
         screen == SCR_LAN || (screen == SCR_CARD && cardPage == 0 && !natureInfoOpen);
}

static void crumbDrop() {
  gCrumbMagic = CRUMB_MAGIC;
  gCrumbScreen = uiCurrentScreen();
  gCrumbHeap = ESP.getFreeHeap();
}

static const char *resetReasonName(int r) {
  switch (r) {
    case ESP_RST_POWERON:  return "power on";
    case ESP_RST_EXT:      return "reset pin";
    case ESP_RST_SW:       return "software (our own restart)";
    case ESP_RST_PANIC:    return "PANIC -- a crash";
    case ESP_RST_INT_WDT:  return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK WATCHDOG -- something blocked too long";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT -- the supply sagged";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "unknown";
  }
}

// Printed once at boot. On a clean start it is one line; after a crash it says
// which screen was up and how much heap was left, which is the whole point.
void bootReport() {
  int r = (int)esp_reset_reason();
  bool bad = (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
              r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT);
  Serial.printf("boot: reset=%s heap=%u psram=%u\n", resetReasonName(r),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  if (bad && gCrumbMagic == CRUMB_MAGIC) {
    Serial.printf("CRASH: it died on the '%s' screen with heap=%u\n",
                  gCrumbScreen < SCR_COUNT ? SCREEN_NAME[gCrumbScreen] : "?",
                  (unsigned)gCrumbHeap);
  } else if (bad) {
    Serial.println("CRASH: no breadcrumb (RTC memory was lost too)");
  }
  gCrumbMagic = 0;   // one report per crash, not on every later boot
}

void render() {
  crumbDrop();   // so a crash can name the screen it happened on
  if (quiz.active) {
    quiz.markRendered(millis());
    renderQuiz();
    return;
  }
  if (pet.awaitingStarter()) {  // primera partida: idioma, region e inicial
    if (!starterLanguageDone) renderFirstBootLanguage();
    else if (!starterRegionDone) renderRegionPick(RPICK_FOR_START);
    else renderStarterSelect();
    return;
  }
  if (moveInfoOpen) {
    renderMoveInfo();
    return;
  }
  if (galleryOpen) {
    if (galleryPick) { renderRegionPick(RPICK_FOR_DEX); return; }
    renderGallery();
    return;
  }
  if (movePickOpen) {
    renderMovePick();
    return;
  }
  if (btlWinUntil) {
    renderBattle();
    return;
  }
  if (bagOpen) {
    renderBag();
    return;
  }
  if (boxOpen) {
    renderBox();
    return;
  }
  if (gameOpen) {
    renderGame();
    return;
  }
  if (sackOpen) {
    renderSack();
    return;
  }
  if (spdOpen) {
    renderSpeed();
    return;
  }
  if (trainOpen) {
    renderTrain();
    return;
  }
  if (kbOpen) {
    renderKeyboard();
    return;
  }
  if (clockOpen) {
    renderClock();
    return;
  }
  if (battleOpen) {
    btlLinkPoll();
    renderBattle();
    return;
  }
  if (pickOpen) {
    renderPick();
    return;
  }
  if (lanOpen) {
    renderLan();
    return;
  }
  if (gymOpen) {
    if (gymPick) renderRegionPick(RPICK_FOR_GYMS);
    else renderGyms();
    return;
  }
  if (playerOpen) {
    renderPlayer();
    return;
  }
  if (navMenuOpen) {
    renderNavMenu();
    return;
  }
  if (cardOpen) {
    renderCard();
    return;
  }
  int h = sceneHour();
  gNight = pet.sleeping || h < 6 || h >= 20;
  // drawScene cubre los 466x466 completos: sin fillScreen(NEGRO) previo para
  // que un flush DMA solapado nunca capture negro a medias (anti-parpadeo)
  drawScene(pet.isEgg() ? 0 : dexEntry(pet.speciesId).biome, millis(), gNight);

  if (pet.ceremony) {
    const DexEntry &d = dexEntry(pet.speciesId);
    const char *msg = (pet.ceremony == CER_FAREWELL) ? T(S_FAREWELL)
                      : (pet.ceremony == CER_RUNAWAY) ? T(S_RUNAWAY)
                                                      : T(S_GOODBYE);
    drawHeader(speciesName(pet.speciesId), d.accent, msg);
    drawCeremony();
    gfx->flush();
    return;
  }

  if (pet.isEgg()) {
    drawHeader(T(S_EGG_HDR), inkColor(), eggMsg());
    int s = 5, x = CX - 16 * s, y = PET_CY - 16 * s;
    drawMap(SPR_EGG, SPRITE_H, x, y, s, false);
    if (pet.eggCracks() >= 1)
      for (auto &c : CRACK1) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggCracks() >= 2)
      for (auto &c : CRACK2) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggRarity() >= R_RARO) {
      const char *rar = (pet.eggRarity() == R_LEGENDARIO) ? T(S_EGG_LEGEND) : T(S_EGG_RARE);
      gfx->setTextColor(pet.eggRarity() == R_LEGENDARIO ? UI_BAR_WARN : 0x4C98);
      gfx->setTextSize(2);
      gfx->setCursor(uiCenterX(rar), 316);
      gfx->print(rar);
    }
    char reg[24];
    snprintf(reg, sizeof(reg), T(S_POKEDEX_FMT), player.registeredCount(), dexCount());
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    gfx->setTextColor(inkColor());
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(reg), 344);
    gfx->print(reg);

    // Which generation this egg comes from. It lives HERE rather than in the
    // settings screen because this is the only moment it does anything: the
    // species is decided when the egg appears, so choosing the region is
    // something you do to the egg in front of you.
    drawEggRegion();
  } else if (pet.isDead()) {
    const DexEntry &d = dexEntry(pet.speciesId);
    const char *name = displaySpeciesName(pet.speciesId, pet.nick);
    drawHeader(name, d.accent, T(S_DEAD));
    if (pmd.loaded) {
      uint8_t act = pmd.has(PMD_HURT) ? PMD_HURT : PMD_IDLE;
      drawPmdAct(act, CX, PET_GROUND, 0, false, true, 5);
    } else {
      const uint8_t *thumb = thumbs.get(pet.speciesId);
      if (thumb) drawThumb(thumb, CX - 72, PET_GROUND - 144, 3, true);
    }
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    drawDeadButtons();
    if (buryTarget == BURY_TARGET_PET) drawBuryDialog(name);
  } else {
    const DexEntry &d = dexEntry(pet.speciesId);
    char name[64];
    const char *base = displaySpeciesName(pet.speciesId, pet.nick);
    snprintf(name, sizeof(name), T(S_NAME_FMT), rareMark(pet.shiny),
             base, pet.level());
    drawHeader(name, gNight ? UI_INK_NIGHT : d.accent, statusMsg());
    gfx->setTextSize(3);
    drawGenderIcon(pet.gender, uiCenterX(name) + gfx->textWidth(name) + 4,
                   63, 1);
    drawStreakBadge();
    drawPet();
    drawBath();
    drawPoops();
    // panel inferior: base limpia para barras y botones sobre el paisaje
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    drawBars();
    drawButtons();
    drawCelebration();
    if (pet.wantEvolveButton()) drawEvolveButton();        // CTA rojo: evolucionar
    else if (pet.canRunawayNow()) drawRunawayButton();     // CTA sombrio: escapada (abandono)
  }

  drawPetNav();

  if (pet.sleeping) {
    gfx->setTextColor(UI_INK_NIGHT);
    gfx->setTextSize(3);
    gfx->setCursor(320, 130);
    gfx->print("Zz");
  }

  // selector de comida
  if (feedMenuUntil) {
    if (millis() > feedMenuUntil) {
      feedMenuUntil = 0;
    } else {
      gfx->fillRoundRect(101, 288, 264, 64, 14, UI_WHITE);
      gfx->drawRoundRect(101, 288, 264, 64, 14, inkColor());
      drawMap(SPR_ICON_FOOD, 16, 110, 296, 3, false);
      drawMap(SPR_ICON_BERRY_B, 16, 176, 296, 3, false);
      drawMap(SPR_ICON_BERRY_G, 16, 242, 296, 3, false);
      drawMap(SPR_ICON_CANDY, 16, 308, 296, 3, false);
    }
  }

  // dialogo de decision (evolucionar/mantener, despedirse/quedaros)
  if (choiceKind) {
    if (millis() > choiceUntil) choiceKind = 0;
    else drawChoiceDialog();
  }

  // "<name> joined the party!" after a wild capture
  if (partyBannerUntil) {
    if (millis() > partyBannerUntil) {
      partyBannerUntil = 0;
    } else {
      char b[40];
      snprintf(b, sizeof(b), T(S_PARTY_JOINED), partyBannerName);
      gfx->fillRoundRect(53, 176, 360, 74, 16, UI_BAR_OK);
      gfx->drawRoundRect(53, 176, 360, 74, 16, UI_INK);
      gfx->setTextColor(UI_WHITE);
      gfx->setTextSize(2);
      uiDrawCenteredIn(b, 53, 176, 360, 74);
    }
  }

  if (menuOpen) drawMenu();

  gfx->flush();
}

// ---------- quiz orchestration ----------

bool quizBlocking() {
  return quiz.active;
}

void settleCareInteraction(const CareAction &action, uint8_t percent, bool correct,
                           bool showGameResult, uint32_t now) {
  uint8_t gain = pet.settleCare(action, percent);
  if (!correct) sfxPlay(SFX_DENY);
  else if (action.kind == CARE_ACTION_FEED_BERRY ||
           action.kind == CARE_ACTION_FEED_CANDY) sfxPlay(SFX_EAT);
  else if (action.kind == CARE_ACTION_CARESS) sfxPlay(SFX_HEART);
  else sfxPlay(SFX_LEVEL);

  if (action.kind == CARE_ACTION_CLEAN && percent) {
    startBathAnimation(now);
    if (pmd.has(PMD_POSE)) {
      beh.mode = 2;
      beh.act = PMD_POSE;
      beh.t0 = now;
      beh.until = now + pmdActTotalMs(pmd.acts[PMD_POSE]) * 2;
    }
  }
  if (!showGameResult) return;
  if (action.kind == CARE_ACTION_PLAY) {
    gameOverUntil = now + 4000;
  } else if (action.kind == CARE_ACTION_TRAIN_STRENGTH) {
    sackGain = gain;
    sackOverUntil = now + 3500;
  } else if (action.kind == CARE_ACTION_TRAIN_SPEED) {
    spdGain = gain;
    spdOverUntil = now + 3500;
  }
}

bool beginCareQuiz(const CareAction &action, bool showGameResult) {
  if (action.kind == CARE_ACTION_NONE) return false;
  uint32_t now = millis();
  lastInteract = now;
  if (!quiz.begin(uiActiveLocaleCode())) {
    settleCareInteraction(action, 100, true, showGameResult, now);
    return true;
  }
  quizPurpose = QUIZ_PURPOSE_CARE;
  quizCareAction = action;
  quizShowsGameResult = showGameResult;
  return true;
}

bool beginBattleQuiz(uint8_t moveSlot) {
  if (moveSlot >= MOVE_SLOTS || !btlYou.moves[moveSlot]) return false;
  lastInteract = millis();
  if (!quiz.begin(uiActiveLocaleCode())) {
    commitBattleMove(moveSlot, 100);
    return true;
  }
  quizPurpose = QUIZ_PURPOSE_BATTLE;
  quizBattleMoveSlot = moveSlot;
  quizShowsGameResult = false;
  if (btlLink) lan.sendWait();
  return true;
}

void updateQuiz(uint32_t now) {
  quiz.update(now);
  uint8_t percent = 0;
  bool correct = false;
  if (!quiz.takeSettlement(now, percent, correct)) return;
  QuizPurpose completedPurpose = quizPurpose;
  quizPurpose = QUIZ_PURPOSE_NONE;
  if (completedPurpose == QUIZ_PURPOSE_BATTLE) {
    sfxPlay(correct ? SFX_LEVEL : SFX_DENY);
    commitBattleMove(quizBattleMoveSlot, percent);
    return;
  }
  if (completedPurpose != QUIZ_PURPOSE_CARE) return;
  CareAction completed = quizCareAction;
  settleCareInteraction(completed, percent, correct, quizShowsGameResult, now);
  quizShowsGameResult = false;
}

// ---------- minijuego: toques con la pokeball ----------

void startGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  gameOpen = true;
  gameOverUntil = 0;
  gameUntil = millis() + GAME_MS;
  gameScore = 0;
  gameMisses = 0;
  gameNewHi = false;
  hitTime = 0;
  gamePetX = 233;
  respawnBall();
}

void respawnBall() {
  ballX = 150 + random(166);
  ballY = 96;
  float sp = 1.6f + gameScore * 0.05f;  // mas viva segun avanzas
  if (sp > 4.0f) sp = 4.0f;
  ballVX = random(2) ? sp : -sp;
  ballVY = 0;
}

// Leaving a minigame early banks what was actually earned rather than voiding
// it. Quitting used to forfeit everything, which mattered little when the ball
// game trained a stat you could grind back -- but it is now purely about
// happiness, and a pet that just played should be happier for it. The
// gameOver/over guards stop a swipe during the results screen paying twice.
void leaveGame() {
  if (!gameOverUntil && beginCareQuiz({ CARE_ACTION_PLAY, gameScore }, false)) gameOpen = false;
}
void leaveSack() {
  if (!sackOverUntil && beginCareQuiz({ CARE_ACTION_TRAIN_STRENGTH, sackHits }, false))
    sackOpen = false;
}
void leaveSpeed() {
  if (!spdOverUntil && beginCareQuiz({ CARE_ACTION_TRAIN_SPEED, spdHits }, false)) spdOpen = false;
}

void gameTap(int16_t x, int16_t y) {
  if (gameOverUntil) return;
  // The ball is checked BEFORE the quit strip. The ball bounces inside a circle
  // of radius 205 about the centre, so it reaches y=28 -- well inside the y<72
  // header. Reaching up to hit a high ball used to abandon the game instead,
  // silently forfeiting the score, the speed training and the record.
  float dx = ballX - x, dy = ballY - y;
  bool onBall = (dx * dx + dy * dy < 74 * 74);
  if (!onBall && y < 72) {  // tocar la cabecera = salir, conservando lo ganado
    leaveGame();
    return;
  }
  if (onBall) {  // toque a la bola!
    gameScore++;
    sfxPlay(SFX_PLAY);
    // golpe mas suave: impulso moderado que crece poco a poco con la puntuacion
    float lift = 6.6f + (gameScore > 16 ? 3.5f : gameScore * 0.22f);
    ballVY = -lift;
    ballVX += dx * 0.12f;
    if (ballVX > 6.5f) ballVX = 6.5f;
    if (ballVX < -6.5f) ballVX = -6.5f;
    hitX = ballX;
    hitY = ballY;
    hitTime = millis();
  }
}

void stepGame() {
  float grav = 0.40f + gameScore * 0.013f;  // cae un poco mas rapido cada vez
  if (grav > 0.80f) grav = 0.80f;
  ballVY += grav;
  ballX += ballVX;
  ballY += ballVY;
  // rebote en la pared circular
  float dx = ballX - CX, dy = ballY - CY;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > 205) {
    float nx = dx / d, ny = dy / d;
    float dot = ballVX * nx + ballVY * ny;
    if (dot > 0) {
      ballVX = (ballVX - 2 * dot * nx) * 0.85f;
      ballVY = (ballVY - 2 * dot * ny) * 0.85f;
    }
    ballX = CX + nx * 205;
    ballY = CY + ny * 205;
  }
  // the clock, or three misses, whichever lands first
  if (gameUntil && millis() >= gameUntil && !gameOverUntil) {
    gameNewHi = (gameScore > player.gameHi);
    if (beginCareQuiz({ CARE_ACTION_PLAY, gameScore }, true)) gameUntil = 0;
    return;
  }
  if (ballY > 384) {  // al suelo
    if (++gameMisses >= 3) {
      gameNewHi = (gameScore > player.gameHi);
      if (beginCareQuiz({ CARE_ACTION_PLAY, gameScore }, true)) gameUntil = 0;
    } else {
      respawnBall();
    }
  }
  // el bicho la sigue por abajo
  float chase = (ballX - gamePetX) * 0.12f;
  if (chase > 7) chase = 7;
  if (chase < -7) chase = -7;
  gamePetX += chase;
}

// ---------- saco de entrenamiento (entrena la fuerza) ----------

void startSack() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  sackOpen = true;
  sackUntil = millis() + 10000;
  sackOverUntil = 0;
  sackHits = 0;
  sackShake = 0;
  sackNewHi = false;
}

void sackTap() {
  if (millis() >= sackUntil) return;  // ya termino el tiempo
  sackHits++;
  sackShake = 16;  // sacude el saco
}

void drawGameScene();  // prototipo (definida mas abajo)

void renderSack() {
  uint32_t now = millis();
  drawGameScene();  // fondo del habitat
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  // pantalla de resultado
  if (sackOverUntil) {
    if (now > sackOverUntil) { sackOpen = false; return; }
    char b[20];
    snprintf(b, sizeof(b), T(S_HITS_FMT), sackHits);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(uiCenterX(b), 150);
    gfx->print(b);
    char g[18];
    snprintf(g, sizeof(g), T(S_STR_GAIN_FMT), sackGain);
    gfx->setTextColor(UI_BAR_BAD);
    gfx->setTextSize(3);
    gfx->setCursor(uiCenterX(g), 210);
    gfx->print(g);
    gfx->setTextSize(2);
    if (sackNewHi && sackHits > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(uiCenterX(T(S_NEW_RECORD)), 256);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char r[18];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), player.strHi);
      gfx->setTextColor(ink);
      gfx->setCursor(uiCenterX(r), 256);
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  // se acabaron los 10 s: aplicar entrenamiento
  if (now >= sackUntil) {
    sackNewHi = (sackHits > player.strHi);
    beginCareQuiz({ CARE_ACTION_TRAIN_STRENGTH, sackHits }, true);
    gfx->flush();
    return;
  }

  // aporreo activo
  sackShake *= 0.84f;
  int off = (int)(sackShake * sinf(now * 0.05f));
  int sx = CX + off, top = 86, sy = 150;
  gfx->fillRect(CX - 3, 56, 6, top - 56, ink);          // gancho/cuerda
  gfx->fillRect(sx - 4, top - 30, 8, 34, ink);          // cadena
  gfx->fillRoundRect(sx - 42, top, 84, 150, 26, C565(0xb5, 0x3a, 0x3a));  // saco
  gfx->fillRoundRect(sx - 42, top, 84, 22, 18, C565(0x7e, 0x28, 0x28));   // tapa
  gfx->drawRoundRect(sx - 42, top, 84, 150, 26, ink);
  gfx->fillRect(sx - 42, top + 70, 84, 4, C565(0x7e, 0x28, 0x28));        // costura

  // contador de golpes
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", sackHits);
  gfx->setTextColor(ink);
  gfx->setTextSize(6);
  gfx->setCursor(uiCenterX(buf), 268);
  gfx->print(buf);

  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_HIT_FAST)), 322);
  gfx->print(T(S_HIT_FAST));

  // barra de tiempo
  uint32_t left = sackUntil - now;
  int bw = 280, fw = (int)((uint32_t)bw * left / 10000);
  gfx->fillRoundRect(CX - bw / 2, 350, bw, 16, 5, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, 350, fw, 16, 5, UI_BAR_OK);

  gfx->flush();
}

// fondo del minijuego: hatibat del bicho (cielo por hora + suelo del bioma)
void drawGameScene() {
  int hh = sceneHour();
  bool night = hh < 6 || hh >= 20;
  uint16_t top, bot;
  if (night)       { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (hh < 8) { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }
  else if (hh < 18){ top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }
  else             { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }
  int hor = 376;
  for (int y = 0; y < hor; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, hor));
  if (night)
    for (auto &st : STARS) gfx->fillRect(st[0], st[1], 4, 4, UI_WHITE);
  uint8_t bio = pet.isEgg() ? 0 : dexEntry(pet.speciesId).biome;
  uint16_t soil = BIOME_SOIL[bio < 6 ? bio : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  gfx->fillRect(0, hor, 466, 466 - hor, soil);
}

void renderGame() {
  // sin fillScreen(NEGRO): drawGameScene cubre los 466x466 completos. Si el
  // DMA del flush anterior aun lee el buffer, vera contenido valido (no negro
  // a medio pintar), que era el parpadeo a 25 fps.
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (gameOverUntil) {
    drawGameScene();
    if (millis() > gameOverUntil) {
      gameOpen = false;
      return;
    }
    char buf[22];
    snprintf(buf, sizeof(buf), T(S_SCORE_FMT), gameScore);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(uiCenterX(buf), 160);
    gfx->print(buf);
    gfx->setTextSize(2);
    if (gameNewHi && gameScore > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(uiCenterX(T(S_NEW_RECORD)), 214);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char rec[20];
      snprintf(rec, sizeof(rec), T(S_RECORD_FMT), player.gameHi);
      gfx->setTextColor(ink);
      gfx->setCursor(uiCenterX(rec), 214);
      gfx->print(rec);
    }
    const char *msg = gameScore >= 10 ? T(S_GREAT_JOY) : T(S_PLUS_JOY);
    gfx->setTextColor(ink);
    gfx->setCursor(uiCenterX(msg), 250);
    gfx->print(msg);
    gfx->flush();
    return;
  }

  drawGameScene();
  stepGame();

  // marcador, record y vidas
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", gameScore);
  gfx->setTextColor(ink);
  gfx->setTextSize(4);
  gfx->setCursor(uiCenterX(buf), 30);
  gfx->print(buf);
  char rec[12];
  snprintf(rec, sizeof(rec), T(S_REC_FMT), player.gameHi);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(rec), 76);
  gfx->print(rec);
  for (int i = 0; i < 3; i++) {
    if (i < 3 - gameMisses) gfx->fillCircle(180 + i * 28, 104, 6, UI_BAR_BAD);
    else gfx->drawCircle(180 + i * 28, 104, 6, UI_TRACK);
  }
  // The clock, drawn like the bag's and the reaction test's so all three games
  // read the same way. Thin and near the rim: the middle belongs to the ball.
  if (!gameOverUntil) {
    uint32_t now2 = millis();
    uint32_t left = (gameUntil > now2) ? gameUntil - now2 : 0;
    int bw = 200, fw = (int)((uint32_t)bw * left / GAME_MS);
    gfx->fillRoundRect(CX - bw / 2, 124, bw, 10, 4, UI_TRACK);
    if (fw > 2)
      gfx->fillRoundRect(CX - bw / 2, 124, fw, 10, 4,
                         left < 5000 ? UI_BAR_WARN : UI_BAR_OK);
  }

  if (pmd.loaded) {
    uint8_t act = (ballX > gamePetX + 4) ? PMD_WALKR : (ballX < gamePetX - 4) ? PMD_WALKL : PMD_IDLE;
    if (!pmd.has(act)) act = PMD_IDLE;
    drawPmdAct(act, (int)gamePetX, 394, millis(), true, false, 3);
  }

  // anillo de impacto que se expande y desvanece (feedback suave del golpe)
  uint32_t ht = millis() - hitTime;
  if (hitTime && ht < 260) {
    int rad = 22 + (int)(ht / 6);
    gfx->drawCircle((int)hitX, (int)hitY, rad, C565(0xff, 0xe7, 0x9f));
    gfx->drawCircle((int)hitX, (int)hitY, rad - 2, C565(0xff, 0xd9, 0x8a));
  }

  // la pokeball
  drawMap(SPR_ICON_PLAY, 16, (int)ballX - 24, (int)ballY - 24, 3, false);

  gfx->flush();
}

// ---------- ficha del bicho (deslizar vertical) ----------

// una fila de la ficha: etiqueta, barra, valor y (si iv != IV_NONE) el valor
// individual que fija el techo de ese stat
// (sin argumento por defecto: el generador de prototipos de Arduino los
// descarta y las llamadas que lo omitan no compilarian)
#define IV_NONE 0xFF
void drawCardStat(int y, const char *label, uint16_t val, uint16_t maxBar,
                  uint16_t color, uint8_t iv) {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  char num[8];
  snprintf(num, sizeof(num), "%u", val);
  int bw = uiLayoutMetric(UI_LAYOUT_STAT_BAR_WIDTH, 130);
  int labelX = uiLayoutMetric(UI_LAYOUT_STAT_LABEL_X, 70);
  int barX = uiLayoutMetric(UI_LAYOUT_STAT_BAR_X, 132);
  int numX = uiLayoutMetric(UI_LAYOUT_STAT_VALUE_X, 272);
  if (iv == IV_NONE) {
    int labelW = gfx->textWidth(label), numW = gfx->textWidth(num);
    int total = labelW + 12 + bw + 10 + numW;
    labelX = CX - total / 2;
    barX = labelX + labelW + 12;
    numX = barX + bw + 10;
  }
  gfx->setCursor(labelX, y);
  gfx->print(label);
  // The bar used to start at 112, which leaves 42px for a label drawn at size 2
  // -- three characters. BOND (EN), LIEN (FR) and LACO (PT) are four, so the
  // label ran under the bar. 132 fits five, with the bar narrowed to keep the
  // number clear of it.
  int fw = (int)val * bw / maxBar;
  if (fw > bw) fw = bw;
  gfx->fillRoundRect(barX, y + 2, bw, 11, 3, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(barX, y + 2, fw, 11, 3, color);
  gfx->setCursor(numX, y);
  gfx->print(num);
  if (iv != IV_NONE) {
    char b[10];
    snprintf(b, sizeof(b), T(S_IV_FMT), iv);
    // Keep the traditional perfect-IV threshold visible even though imported
    // or gym-rewarded IVs may continue above it.
    gfx->setTextColor(iv >= 31 ? UI_BAR_WARN : UI_TRACK);
    gfx->setCursor(344, y);
    gfx->print(b);
  }
}

// ---------- ajuste de hora en pantalla (deslizar abajo) ----------
// El usuario pone su hora LOCAL a ojo; el firmware la usa tal cual, asi que
// no hay que gestionar zona horaria. Preserva el dia (no rompe racha/edad).

void openClock() {
  uint32_t e = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  clockH = (e / 3600) % 24;
  clockM = (e / 60) % 60;
  clockOpen = true;
}

void applyClock() {
  uint32_t base = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  uint32_t e = (base / 86400) * 86400 + (uint32_t)clockH * 3600 + (uint32_t)clockM * 60;
  rtcSetEpoch(e);
  pet.setClock(e);
  clockOpen = false;
}

void drawClockBtn(int x, int y, const char *l) {
  gfx->fillRoundRect(x, y, 58, 58, 12, UI_WHITE);
  gfx->drawRoundRect(x, y, 58, 58, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(4);
  uiDrawCenteredIn(l, x, y, 58, 58);
}

// pildoras de idioma centradas en y; rellena la activa
#define LANG_PILL_Y 296
#define LANG_PILL_H 30
#define LANG_PILL_X 336          // pildora de idioma (cicla todos al tocar)
#define LANG_PILL_W 96
// the volume mixer sits in the gap between the sound switch and the language
// pill: minus, the level, plus
#define VOL_MINUS_X 146
#define VOL_PLUS_X 276
#define VOL_BTN_W 48
static_assert(BRIGHT_HIT_Y + BRIGHT_HIT_H < LANG_PILL_Y,
              "brightness and volume touch targets must not overlap");

void renderClock() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(T(S_SET_TIME)), 44);
  gfx->print(T(S_SET_TIME));

  char t[8];
  snprintf(t, sizeof(t), "%02d:%02d", clockH, clockM);
  gfx->setTextSize(7);
  gfx->setCursor(uiCenterX(t), 108);
  gfx->print(t);

  drawClockBtn(104, 190, "-");  // hora -
  drawClockBtn(170, 190, "+");  // hora +
  drawClockBtn(252, 190, "-");  // min -
  drawClockBtn(318, 190, "+");  // min +
  gfx->setTextSize(2);
  gfx->setTextColor(UI_MUTED);
  gfx->setCursor(uiCenterIn(T(S_HOUR), 104, 124), 256);
  gfx->print(T(S_HOUR));
  gfx->setCursor(uiCenterIn(T(S_MIN), 252, 124), 256);
  gfx->print(T(S_MIN));

  // brillo normal: arrastrable; los soles evitan sumar otra cadena localizada
  gfx->fillCircle(138, BRIGHT_TRACK_Y, 3, UI_BAR_WARN);
  gfx->drawLine(132, BRIGHT_TRACK_Y, 144, BRIGHT_TRACK_Y, UI_BAR_WARN);
  gfx->drawLine(138, BRIGHT_TRACK_Y - 6, 138, BRIGHT_TRACK_Y + 6, UI_BAR_WARN);
  gfx->fillCircle(328, BRIGHT_TRACK_Y, 5, UI_BAR_WARN);
  gfx->drawLine(319, BRIGHT_TRACK_Y, 337, BRIGHT_TRACK_Y, UI_BAR_WARN);
  gfx->drawLine(328, BRIGHT_TRACK_Y - 9, 328, BRIGHT_TRACK_Y + 9, UI_BAR_WARN);
  gfx->fillRoundRect(BRIGHT_TRACK_X, BRIGHT_TRACK_Y - 3, BRIGHT_TRACK_W, 6, 3, UI_TRACK);
  int brightX = BRIGHT_TRACK_X + BRIGHT_TRACK_W * (userBrightness - 1) / 9;
  if (brightX > BRIGHT_TRACK_X)
    gfx->fillRoundRect(BRIGHT_TRACK_X, BRIGHT_TRACK_Y - 3,
                       brightX - BRIGHT_TRACK_X, 6, 3, UI_BAR_WARN);
  gfx->fillCircle(brightX, BRIGHT_TRACK_Y, 7, UI_WHITE);
  gfx->drawCircle(brightX, BRIGHT_TRACK_Y, 7, UI_INK);

  // interruptor de sonido (izquierda de la fila de idioma)
  bool snd = audioEnabled();
  const char *sl = snd ? T(S_SND_ON) : T(S_SND_OFF);
  gfx->fillRoundRect(34, LANG_PILL_Y, 96, LANG_PILL_H, 8, snd ? UI_BAR_OK : UI_WHITE);
  gfx->drawRoundRect(34, LANG_PILL_Y, 96, LANG_PILL_H, 8, UI_INK);
  gfx->setTextColor(snd ? UI_BG_DAY : UI_INK);
  gfx->setTextSize(2);
  uiDrawCenteredIn(sl, 34, LANG_PILL_Y, 96, LANG_PILL_H);

  // volume: a level, not a toggle. The sound switch beside it is still the
  // master -- this is how loud it is when it is on, and 0 is silent.
  {
    uint8_t v = audioVolume();
    for (int i = 0; i < 2; i++) {
      int bx = i ? VOL_PLUS_X : VOL_MINUS_X;
      bool live = i ? (v < 10) : (v > 0);
      gfx->fillRoundRect(bx, LANG_PILL_Y, VOL_BTN_W, LANG_PILL_H, 8,
                         live ? UI_WHITE : UI_TRACK);
      gfx->drawRoundRect(bx, LANG_PILL_Y, VOL_BTN_W, LANG_PILL_H, 8, UI_INK);
      gfx->setTextColor(live ? UI_INK : 0x8410);
      gfx->setTextSize(2);
      uiDrawCenteredIn(i ? "+" : "-", bx, LANG_PILL_Y, VOL_BTN_W, LANG_PILL_H);
    }
    char vl[12];
    snprintf(vl, sizeof(vl), T(S_VOL_FMT), v);
    gfx->setTextColor(v ? UI_INK : UI_TRACK);
    gfx->setTextSize(1);
    gfx->setCursor(uiCenterIn(vl, 210, 56), LANG_PILL_Y + 1);
    gfx->print(vl);
    // a small bar under the number, so the level reads at a glance
    gfx->fillRoundRect(210, LANG_PILL_Y + 21, 56, 6, 3, UI_TRACK);
    if (v) gfx->fillRoundRect(210, LANG_PILL_Y + 21, 56 * v / 10, 6, 3, UI_BAR_OK);
  }

  // selector de idioma: una pildora que cicla todos los idiomas al tocar
  gfx->fillRoundRect(LANG_PILL_X, LANG_PILL_Y, LANG_PILL_W, LANG_PILL_H, 8, UI_WHITE);
  gfx->drawRoundRect(LANG_PILL_X, LANG_PILL_Y, LANG_PILL_W, LANG_PILL_H, 8, UI_INK);
  char lp[10];
  snprintf(lp, sizeof(lp), "%s >", langLabel(gLang));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  uiDrawCenteredIn(lp, LANG_PILL_X, LANG_PILL_Y, LANG_PILL_W, LANG_PILL_H);

  gfx->fillRoundRect(133, 340, 200, 48, 14, UI_BAR_OK);
  gfx->setTextColor(UI_BG_DAY);
  gfx->setTextSize(3);
  uiDrawCenteredIn("OK", 133, 340, 200, 48);

  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_CLOCK_CANCEL)), 410);
  gfx->print(T(S_CLOCK_CANCEL));

  // version del firmware (discreta, abajo del todo)
  char ver[64];
  snprintf(ver, sizeof(ver), "TamaPoke %s", FW_VERSION);
  gfx->setTextSize(1);
  gfx->setCursor(uiCenterX(ver), 436);
  gfx->print(ver);
  gfx->flush();
}

void clockTap(int16_t x, int16_t y) {
  if (y >= 190 && y <= 248) {  // fila de botones +/-
    if (x >= 104 && x < 162) clockH = (clockH + 23) % 24;
    else if (x >= 170 && x < 228) clockH = (clockH + 1) % 24;
    else if (x >= 252 && x < 310) clockM = (clockM + 59) % 60;
    else if (x >= 318 && x < 376) clockM = (clockM + 1) % 60;
    return;
  }
  if (x >= BRIGHT_HIT_X && x <= BRIGHT_HIT_X + BRIGHT_HIT_W &&
      y >= BRIGHT_HIT_Y && y <= BRIGHT_HIT_Y + BRIGHT_HIT_H) {
    setUserBrightness(brightnessLevelAt(x), true);
    return;
  }
  if (y >= LANG_PILL_Y && y <= LANG_PILL_Y + LANG_PILL_H) {
    if (x >= 34 && x < 130) {                  // interruptor de sonido
      audioSetEnabled(!audioEnabled());
      if (audioEnabled()) sfxPlay(SFX_TAP);    // confirma al encender
      return;
    }
    if (x >= VOL_MINUS_X && x < VOL_MINUS_X + VOL_BTN_W) {
      if (audioVolume() > 0) audioSetVolume(audioVolume() - 1);
      sfxPlay(SFX_TAP);                        // so the new level is audible
      return;
    }
    if (x >= VOL_PLUS_X && x < VOL_PLUS_X + VOL_BTN_W) {
      if (audioVolume() < 10) audioSetVolume(audioVolume() + 1);
      sfxPlay(SFX_TAP);
      return;
    }
    if (x >= LANG_PILL_X && x < LANG_PILL_X + LANG_PILL_W) {  // cicla idioma
      if (langCount()) setLang((Lang)((gLang + 1) % langCount()));
      refreshUiFont();
      sfxPlay(SFX_TAP);
      return;
    }
  }
  if (y >= 340 && y <= 388 && x >= 133 && x <= 333) { applyClock(); return; }
}

// llama + numero de racha arriba a la izquierda
void drawStreakBadge() {
  if (player.streak < 1) return;
  int x = 26, y = 16;
  gfx->fillTriangle(x + 8, y, x + 1, y + 17, x + 15, y + 17, UI_BAR_BAD);
  gfx->fillTriangle(x + 8, y + 7, x + 4, y + 17, x + 12, y + 17, UI_BAR_WARN);
  char s[6];
  snprintf(s, sizeof(s), "%u", player.streak);
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(x + 22, y + 2);
  gfx->print(s);
}

// banner temporal: medalla nueva o hito de racha
void drawCelebration() {
  const char *l1 = nullptr, *l2 = nullptr;
  char buf[20];
  if (pet.showMedal()) {
    for (int i = 0; i < MED_COUNT; i++)
      if (pet.newMedal & (1 << i)) { l2 = medalName(i); break; }
    l1 = T(S_MEDAL_BANNER);
  } else if (pet.showMilestone()) {
    snprintf(buf, sizeof(buf), T(S_STREAK_DAYS_FMT), player.streak);
    l1 = T(S_GREAT);
    l2 = buf;
  }
  if (!l1) return;
  gfx->fillRoundRect(73, 150, 320, 96, 16, UI_BAR_WARN);
  gfx->drawRoundRect(73, 150, 320, 96, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(l1), 176);
  gfx->print(l1);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(l2), 212);
  gfx->print(l2);
}

// medallas en la ficha: badge con etiqueta, color si conseguida
void drawMedalBadge(int x, int y, int i) {
  bool got = pet.hasMedal(1 << i);
  gfx->fillRoundRect(x, y, 100, 24, 6, got ? UI_BAR_OK : UI_TRACK);
  if (!got) gfx->drawRoundRect(x, y, 100, 24, 6, UI_TRACK);
  gfx->setTextColor(got ? UI_BG_DAY : 0x9492);
  gfx->setTextSize(2);
  uiDrawCenteredIn(medalLabel(i), x, y, 100, 24);
}

// pagina 0: perfil (retrato grande, identidad, racha, vinculo, baya)
void renderCardProfile() {
  const DexEntry &d = dexEntry(pet.speciesId);
  const char *nm = displaySpeciesName(pet.speciesId, pet.nick);
  char head[64];
  snprintf(head, sizeof(head), T(S_NAME_FMT), rareMark(pet.shiny),
           nm, pet.level());
  gfx->setTextColor(d.accent);
  // auto-encoge: a tamano 3 los nombres largos no caben en la franja estrecha de
  // arriba de la pantalla redonda, asi que se cortaban por el borde
  gfx->setTextSize(3);
  int hts = (gfx->textWidth(head) <= 198) ? 3 : 2;
  gfx->setTextSize(hts);
  int headX = uiCenterX(head, CX - 10);
  gfx->setCursor(headX, hts == 3 ? 34 : 40);
  gfx->print(head);
  drawGenderIcon(pet.gender, headX + gfx->textWidth(head) + 4,
                 hts == 3 ? 29 : 35, 1);
  if (pet.nick[0]) {  // especie real bajo el apodo
    char species[64];
    snprintf(species, sizeof(species), "(%s)", speciesName(pet.speciesId));
    gfx->setTextColor(UI_MUTED);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(species), 64);
    gfx->print(species);
  }

  // retrato grande animado
  if (pmd.loaded) drawPmdAct(PMD_IDLE, CX, 206, millis(), true, false, 4);

  // racha con llama
  char rl[30];
  snprintf(rl, sizeof(rl), T(S_STREAK_FMT), player.streak, player.bestStreak);
  gfx->setTextSize(2);
  int sx = CX - (15 + 9 + gfx->textWidth(rl)) / 2, sy = 224;
  gfx->fillTriangle(sx + 8, sy, sx + 1, sy + 18, sx + 15, sy + 18, UI_BAR_BAD);
  gfx->fillTriangle(sx + 8, sy + 7, sx + 4, sy + 18, sx + 12, sy + 18, UI_BAR_WARN);
  gfx->setTextColor(UI_INK);
  gfx->setCursor(sx + 24, sy + 2);
  gfx->print(rl);

  drawCardStat(258, T(S_VIN), pet.bond, 100, C565(0xd4, 0x52, 0x7e), IV_NONE);

  const char *berry = !pet.berryKnown ? T(S_BERRY_UNK)
                      : pet.lovesBerry(0) ? T(S_BERRY_RED)
                      : pet.lovesBerry(1) ? T(S_BERRY_BLUE)
                                          : T(S_BERRY_GREEN);
  char info[40];
  snprintf(info, sizeof(info), T(S_INFO_FMT), berry,
           (unsigned long)(pet.ageMinutes / 1440));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(info), 284);
  gfx->print(info);

  char natureLine[48];
  snprintf(natureLine, sizeof(natureLine), T(S_NATURE_FMT), natureName(pet.nature));
  gfx->setTextSize(2);
  int natureW = gfx->textWidth(natureLine) + 28;
  if (natureW < 150) natureW = 150;
  if (natureW > 286) natureW = 286;
  int natureX = CX - natureW / 2;
  gfx->fillRoundRect(natureX, 310, natureW, 36, 9, UI_WHITE);
  gfx->drawRoundRect(natureX, 310, natureW, 36, 9, UI_INK);
  gfx->setTextColor(UI_INK);
  uiDrawCenteredIn(natureLine, natureX, 310, natureW, 36);
}

// pagina 1: combate (4 barras + boton de entrenar)
void renderCardStats() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(T(S_BATTLE)), 44);
  gfx->print(T(S_BATTLE));

  // typing, in the accent colour of the species (English in every language,
  // same as the species names themselves)
  const DexEntry &de = dexEntry(pet.speciesId);
  char ty[24];
  if (de.type2 == T_NONE) snprintf(ty, sizeof(ty), "%s", typeName(de.type1));
  else snprintf(ty, sizeof(ty), "%s/%s", typeName(de.type1), typeName(de.type2));
  gfx->setTextColor(de.accent);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(ty), 76);
  gfx->print(ty);

  // 360 de tope de barra: a nivel 73 (fin de ciclo) el stat mas alto de toda
  // la dex es la vitalidad de CHANSEY (355). El 260 anterior ya se desbordaba.
  drawCardStat(104, T(S_STAT_ATK), pet.atkStat(), 360, UI_BAR_BAD, pet.ivAtk);
  drawCardStat(144, T(S_STAT_DEF), pet.defStat(), 360, 0x4C98, pet.ivDef);
  drawCardStat(184, T(S_STAT_SPE), pet.speStat(), 360, UI_BAR_WARN, pet.ivSpe);
  drawCardStat(224, T(S_STAT_VIT), pet.vitStat(), 360, UI_BAR_OK, pet.ivHp);
  drawCardStat(264, T(S_STAT_WGT), pet.weight, 100, 0xB3C8, IV_NONE);

  AbilityKey ability = speciesAbility(pet.speciesId, pet.abilitySlot);
  if (ability) {
    gfx->setTextColor(de.accent);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(abilityName(ability)), 306);
    gfx->print(abilityName(ability));
    const char *description = abilityDescription(ability, uiActiveLocaleCode());
    if (description) {
      gfx->setTextColor(UI_MUTED);
      gfx->setTextSize(1);
      drawWrappedText(description, 78, 330, 310, 3);
    }
  }

}

// Draws one move as a row: name, its type in the type's own colour, and either
// power or a STATUS marker. Shared by the moves page and the picker so a move
// looks the same wherever you meet it.
// A filled chip in the type's own colour, label in whichever of black/white
// reads on it. Returns its width so a caller can lay out beside it.
int drawTypeChip(int x, int y, uint8_t type) {
  const char *nm = typeName(type);
  gfx->setTextSize(1);
  int w = gfx->textWidth(nm) + 10;
  gfx->fillRoundRect(x, y, w, MOVE_CHIP_H, 4, typeColor(type));
  gfx->setTextColor(typeColorIsLight(type) ? UI_INK : UI_WHITE);
  uiDrawCenteredIn(nm, x, y, w, MOVE_CHIP_H);
  return w;
}

void drawMoveRow(int y, MoveId mv, bool highlight, int16_t dex) {
  gfx->fillRoundRect(70, y, 326, MOVE_ROW_H, 12, highlight ? UI_BAR_WARN : UI_BG_DAY);
  gfx->drawRoundRect(70, y, 326, MOVE_ROW_H, 12, UI_INK);
  if (!mv) {
    gfx->setTextColor(UI_MUTED);
    gfx->setTextSize(2);
    uiDrawCenteredIn(T(S_MOVE_EMPTY), 70, y, 326, MOVE_ROW_H);
    return;
  }
  const MoveEntry &m = moveEntry(mv);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(82, y + MOVE_NAME_TOP);
  gfx->print(moveName(mv));
  // Type colours come from the move pack. STAB is a 1.5x damage bonus and is
  // marked on the power figure, where it describes the actual effect.
  // The chip carries the TYPE; STAB moved onto the power figure, where it
  // belongs -- STAB is a damage bonus, so saying it next to the damage reads
  // straight, and it leaves the type free to be its own colour.
  bool stab = hasStab(dex, m.type) && m.cat != MC_STATUS;
  int chipY = y + MOVE_CHIP_TOP;
  int compactH = uiLayoutMetric(1, 8);
  int metaY = chipY + (MOVE_CHIP_H - compactH) / 2;
  int cw = drawTypeChip(82, chipY, m.type);
  if (stab) {
    gfx->setTextColor(dexEntry(dex).accent);
    gfx->setTextSize(1);
    gfx->setCursor(82 + cw + 6, metaY);
    gfx->print("STAB");
  }
  char pw[16];
  if (m.cat == MC_STATUS) snprintf(pw, sizeof(pw), "%s", T(S_MOVE_STATUS));
  else snprintf(pw, sizeof(pw), T(S_MOVE_PWR), m.power);
  gfx->setTextColor(stab ? dexEntry(dex).accent : UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(uiRightX(pw, 384), metaY);
  gfx->print(pw);
}

void moveRowVerticals(int *rowBottom, int *nameTop, int *nameBottom,
                      int *chipTop, int *chipBottom,
                      int *metaTop, int *metaBottom) {
  int compactH = uiLayoutMetric(1, 8);
  int mt = MOVE_CHIP_TOP + (MOVE_CHIP_H - compactH) / 2;
  if (rowBottom) *rowBottom = MOVE_ROW_H;
  if (nameTop) *nameTop = MOVE_NAME_TOP;
  if (nameBottom) *nameBottom = MOVE_NAME_TOP + 16;
  if (chipTop) *chipTop = MOVE_CHIP_TOP;
  if (chipBottom) *chipBottom = MOVE_CHIP_TOP + MOVE_CHIP_H;
  if (metaTop) *metaTop = mt;
  if (metaBottom) *metaBottom = mt + compactH;
}

// card page 4: the four known moves. Tapping a slot opens the picker.
void renderCardMoves() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(T(S_MOVES)), 44);
  gfx->print(T(S_MOVES));
  for (int i = 0; i < MOVE_SLOTS; i++) drawMoveRow(MOVE_ROW_Y(i), pet.moves[i], false, pet.speciesId);
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(1);
  gfx->setCursor(uiCenterX(T(S_MOVE_TAP)), 340);
  gfx->print(T(S_MOVE_TAP));
}

uint8_t learnableList(MoveId *out, uint8_t max) {
  if (!out || !max) return 0;
  MoveId *active = pickTargetMoves();
  MoveId *reserve = pickTargetReserveMoves();
  uint8_t count = 0;
  for (uint8_t i = 0; i < MOVE_SLOTS && count < max; i++)
    if (active[i]) out[count++] = active[i];
  for (uint8_t i = 0; i < RESERVE_MOVE_SLOTS && count < max; i++)
    if (reserve[i]) out[count++] = reserve[i];
  return count;
}

MoveId *pickTargetMoves() {
  return movePickParty ? party.slots[movePickParty - 1].moves : pet.moves;
}
MoveId *pickTargetReserveMoves() {
  return movePickParty ? party.slots[movePickParty - 1].reserveMoves
                       : pet.reserveMoves;
}
int16_t pickTargetDex() {
  return movePickParty ? party.slots[movePickParty - 1].dex : pet.speciesId;
}

void renderMoveInfo() {
  MoveId move = pickTargetMoves()[movePickSlot];
  if (!moveValid(move)) { moveInfoOpen = false; return; }
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(moveName(move)), 42);
  gfx->print(moveName(move));
  drawMoveRow(88, move, false, pickTargetDex());
  const char *description = moveDescription(move, uiActiveLocaleCode());
  if (description) {
    gfx->setTextColor(UI_INK);
    gfx->setTextSize((uint8_t)uiLayoutMetric(UI_LAYOUT_MOVE_DESCRIPTION_TEXT_SIZE, 1));
    drawWrappedText(description, uiLayoutMetric(UI_LAYOUT_MOVE_DESCRIPTION_X, 78),
                    uiLayoutMetric(UI_LAYOUT_MOVE_DESCRIPTION_Y, 158),
                    uiLayoutMetric(UI_LAYOUT_MOVE_DESCRIPTION_WIDTH, 310),
                    (uint8_t)uiLayoutMetric(UI_LAYOUT_MOVE_DESCRIPTION_LINES, 8));
  }
  int changeY = uiLayoutMetric(UI_LAYOUT_MOVE_CHANGE_Y, 320);
  gfx->fillRoundRect(93, changeY, 280, 52, 12, UI_BAR_WARN);
  gfx->drawRoundRect(93, changeY, 280, 52, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  uiDrawCenteredIn(T(S_MOVE_CHANGE), 93, changeY, 280, 52);
  gfx->setTextColor(UI_MUTED);
  gfx->setCursor(uiCenterX(T(S_BACK)), 402);
  gfx->print(T(S_BACK));
  gfx->flush();
}

void renderMovePick() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_MOVE_PICK)), 40);
  gfx->print(T(S_MOVE_PICK));

  MoveId all[64];
  uint8_t n = learnableList(all, sizeof(all) / sizeof(all[0]));
  uint8_t pages = n ? (n + MOVE_PICK_PER_PAGE - 1) / MOVE_PICK_PER_PAGE : 1;
  if (movePickPage >= pages) movePickPage = 0;
  for (uint8_t i = 0; i < MOVE_PICK_PER_PAGE; i++) {
    uint8_t idx = movePickPage * MOVE_PICK_PER_PAGE + i;
    if (idx >= n) break;
    // the move already in this slot is highlighted, so replacing like for like
    // is obvious rather than a guess
    drawMoveRow(MOVE_PICK_Y(i), all[idx], all[idx] == pickTargetMoves()[movePickSlot], pickTargetDex());
  }
  for (uint8_t i = 0; i < pages && pages > 1; i++) {
    if (i == movePickPage) gfx->fillCircle(CX - (pages - 1) * 13 + i * 26, 380, 5, UI_INK);
    else gfx->drawCircle(CX - (pages - 1) * 13 + i * 26, 380, 4, UI_INK);
  }
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_BACK)), 402);
  gfx->print(T(S_BACK));
  gfx->flush();
}

// ---------- battle ----------

static uint8_t *btlBackPixels = nullptr;
static const uint8_t *btlBackSource = nullptr;
static size_t btlBackCapacity = 0;

static void btlFreeBack() {
  free(btlBackPixels);
  btlBackPixels = nullptr;
  btlBackSource = nullptr;
  btlBackCapacity = 0;
}

static const uint8_t *btlLoadBack(const BackScene &b) {
  if (btlBackPixels && btlBackSource == b.compressed) return btlBackPixels;
  size_t required = (size_t)b.w * b.h;
  if (btlBackCapacity < required) {
    btlFreeBack();
    btlBackPixels = static_cast<uint8_t *>(ps_malloc(required));
    if (!btlBackPixels) return nullptr;
    btlBackCapacity = required;
  }
  if (!artInflate(b.compressed, b.compressedSize, btlBackPixels,
                  required)) {
    btlFreeBack();
    return nullptr;
  }
  btlBackSource = b.compressed;
  return btlBackPixels;
}

// Draws a battle backdrop at the requested scale. A cover height expands it
// proportionally when the destination is taller than the integer-scaled art.
// Runs of identical indices keep this much cheaper than writing every pixel.
static void drawBack(const BackScene &b, int y0, int scale, int coverHeight = 0) {
  const uint8_t *pixels = btlLoadBack(b);
  if (!pixels) {
    gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
    return;
  }
  int drawH = coverHeight ? coverHeight : b.h * scale;
  int drawW = coverHeight
      ? ((uint32_t)b.w * drawH + b.h - 1) / b.h
      : b.w * scale;
  int x0 = CX - drawW / 2;
  uint32_t xStep = ((uint32_t)drawW << 16) / b.w;
  uint32_t yStep = ((uint32_t)drawH << 16) / b.h;
  for (int r = 0; r < b.h; r++) {
    const uint8_t *row = pixels + (uint32_t)r * b.w;
    int y1 = y0 + ((uint32_t)r * yStep >> 16);
    int y2 = y0 + (r + 1 == b.h ? drawH
                                 : ((uint32_t)(r + 1) * yStep >> 16));
    int c = 0;
    while (c < b.w) {
      uint8_t v = row[c];
      int run = 1;
      while (c + run < b.w && row[c + run] == v) run++;
      uint16_t col = b.pal[v];
      int x1 = x0 + ((uint32_t)c * xStep >> 16);
      int x2 = x0 + (c + run == b.w ? drawW
                                    : ((uint32_t)(c + run) * xStep >> 16));
      gfx->fillRect(x1, y1, x2 - x1, y2 - y1, col);
      c += run;
    }
  }
}

// Which scene: the FOE's biome, since a battle happens where it lives, and the
// same day/night split the main screen already uses.
static void drawBattleBack(int y0, int scale, int coverHeight = 0) {
  int16_t dex = btlFoe.dex;
  if (dex < 1 || dex > dexCount()) { gfx->fillCircle(CX, CY, 231, UI_BG_DAY); return; }
  uint8_t bi = dexEntry(dex).biome;
  if (bi >= BACK_BIOMES) bi = 0;
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  drawBack(BACKS[bi][night ? 1 : 0], y0, scale, coverHeight);
}

static const char *btlWeatherName(BattleWeather weather) {
  static const StrId NAME[] = {
    S_FIELD_SUN, S_FIELD_RAIN, S_FIELD_SAND, S_FIELD_SNOW,
  };
  return weather > BWEATHER_NONE && weather <= BWEATHER_SNOW
      ? T(NAME[weather - 1]) : "";
}

static const char *btlTerrainName(BattleTerrain terrain) {
  static const StrId NAME[] = {
    S_FIELD_ELECTRIC, S_FIELD_GRASSY, S_FIELD_MISTY, S_FIELD_PSYCHIC,
  };
  return terrain > BTERRAIN_NONE && terrain <= BTERRAIN_PSYCHIC
      ? T(NAME[terrain - 1]) : "";
}

static void drawBattleFieldEffects(uint32_t now) {
  if (btlField.weather == BWEATHER_SUN) {
    gfx->fillCircle(401, 67, 18, 0xFFE0);
    static const int8_t DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static const int8_t DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    for (int i = 0; i < 8; i++) {
      gfx->drawLine(401 + DX[i] * 23, 67 + DY[i] * 23,
                    401 + DX[i] * 30, 67 + DY[i] * 30, 0xFFE0);
    }
  } else if (btlField.weather == BWEATHER_RAIN) {
    for (int i = 0; i < 18; i++) {
      int x = 14 + (i * 47 + (int)(now / 24)) % 438;
      int y = 38 + (i * 31 + (int)(now / 12)) % 194;
      gfx->drawLine(x, y, x - 5, y + 13, 0x45BF);
    }
  } else if (btlField.weather == BWEATHER_SAND) {
    for (int i = 0; i < 16; i++) {
      int x = 18 + (i * 53 + (int)(now / 18)) % 430;
      int y = 45 + (i * 29) % 185;
      gfx->drawLine(x, y, x + 11, y + 2, 0xD5A6);
    }
  } else if (btlField.weather == BWEATHER_SNOW) {
    for (int i = 0; i < 16; i++) {
      int x = 16 + (i * 59 + (int)(now / 60)) % 434;
      int y = 38 + (i * 37 + (int)(now / 28)) % 194;
      gfx->fillCircle(x, y, 2 + (i & 1), UI_WHITE);
    }
  }

  if (btlField.terrain == BTERRAIN_ELECTRIC) {
    for (int i = 0; i < 5; i++) {
      int x = 40 + i * 92 + (int)((now / 100 + i) % 7);
      gfx->drawLine(x, 247, x + 9, 238, 0xFFE0);
      gfx->drawLine(x + 9, 238, x + 16, 249, 0xFFE0);
    }
  } else if (btlField.terrain == BTERRAIN_GRASSY) {
    for (int i = 0; i < 14; i++) {
      int x = 24 + i * 32;
      gfx->drawLine(x, 252, x + ((i & 1) ? 5 : -5), 239, 0x47E8);
    }
  } else if (btlField.terrain == BTERRAIN_MISTY) {
    for (int i = 0; i < 8; i++)
      gfx->drawCircle(44 + i * 54, 242 - (i & 1) * 7, 10 + (i % 3), 0xE71C);
  } else if (btlField.terrain == BTERRAIN_PSYCHIC) {
    for (int i = 0; i < 6; i++)
      gfx->drawCircle(53 + i * 72, 244, 8 + (int)((now / 120 + i) % 5), 0xB81F);
  }
}

static void drawBattleSideLayers(uint8_t sideIndex, int cx, int ground) {
  const BattleSideConditions &side = btlField.sides[sideIndex];
  if (side.reflectTurns)
    gfx->drawCircle(cx, ground - 39, 47, 0x45BF);
  if (side.lightScreenTurns)
    gfx->drawCircle(cx, ground - 39, 51, 0xE71C);
  if (side.auroraVeilTurns)
    gfx->drawCircle(cx, ground - 39, 55, UI_WHITE);
  for (uint8_t i = 0; i < side.spikesLayers; i++) {
    int x = cx - 30 + i * 24;
    gfx->drawLine(x - 6, ground, x, ground - 12, UI_INK);
    gfx->drawLine(x, ground - 12, x + 6, ground, UI_INK);
  }
  if (side.toxicSpikesLayers) {
    for (uint8_t i = 0; i < side.toxicSpikesLayers; i++)
      gfx->fillCircle(cx + 20 + i * 10, ground - 3, 4, 0x981F);
  }
  if (side.stealthRock) {
    gfx->drawLine(cx - 44, ground - 4, cx - 37, ground - 17, 0xA514);
    gfx->drawLine(cx - 37, ground - 17, cx - 30, ground - 4, 0xA514);
  }
  if (side.stickyWeb) {
    gfx->drawCircle(cx + 38, ground - 8, 11, UI_WHITE);
    gfx->drawLine(cx + 27, ground - 8, cx + 49, ground - 8, UI_WHITE);
    gfx->drawLine(cx + 38, ground - 19, cx + 38, ground + 3, UI_WHITE);
  }
}

static void drawBattleFieldHud() {
  const char *labels[2];
  uint16_t colors[2];
  uint8_t count = 0;
  if (btlField.weather != BWEATHER_NONE) {
    labels[count] = btlWeatherName(btlField.weather);
    colors[count++] = btlField.weather == BWEATHER_RAIN ? 0x45BF
                      : btlField.weather == BWEATHER_SAND ? 0xD5A6
                      : btlField.weather == BWEATHER_SNOW ? UI_WHITE : 0xFFE0;
  }
  if (btlField.terrain != BTERRAIN_NONE) {
    labels[count] = btlTerrainName(btlField.terrain);
    colors[count++] = btlField.terrain == BTERRAIN_ELECTRIC ? 0xFFE0
                      : btlField.terrain == BTERRAIN_GRASSY ? 0x47E8
                      : btlField.terrain == BTERRAIN_MISTY ? 0xE71C : 0xB81F;
  }
  if (!count) return;
  gfx->setTextSize(1);
  int widths[2] = {0, 0};
  int total = count > 1 ? 6 : 0;
  for (uint8_t i = 0; i < count; i++) {
    widths[i] = gfx->textWidth(labels[i]) + 14;
    total += widths[i];
  }
  int x = CX - total / 2;
  for (uint8_t i = 0; i < count; i++) {
    gfx->fillRoundRect(x, 34, widths[i], 18, 7, colors[i]);
    gfx->drawRoundRect(x, 34, widths[i], 18, 7, UI_INK);
    gfx->setTextColor(UI_INK);
    uiDrawCenteredIn(labels[i], x, 34, widths[i], 18);
    x += widths[i] + 6;
  }
}

// Streams a side's sprite if its species, shiny state or battle form changed.
// Render may call this every frame; the compact key makes the steady path free.
static void btlSyncSprite(uint8_t who, const Combatant &c) {
  bool mega = c.activeMechanic == BMECH_MEGA;
  uint8_t megaKey = mega && c.megaForm != MEGA_FORM_NONE
      ? (uint8_t)(c.megaForm + 1) : 0;
  int32_t key = ((int32_t)c.dex << 16) | ((int32_t)megaKey << 12) |
                ((int32_t)c.form << 5) |
                (c.shiny ? 4 : 0) | ((uint8_t)c.gender & 3);
  if (btlPmdKey[who] == key && btlPmd[who].loaded) return;
  btlPmd[who].unload();
  btlPmdKey[who] = 0;
  if (c.dex < 1 || c.dex > dexCount()) return;
  if (btlPmd[who].load(c.dex, c.shiny, c.gender, mega, c.megaForm))
    btlPmdKey[who] = key;
}

static void btlFreeSprites() {
  for (int i = 0; i < 2; i++) { btlPmd[i].unload(); btlPmdKey[i] = 0; }
  btlFreeBack();
}

static void btlSay(const char *fmt, ...) {
  if (btlMsgCount >= 6) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(btlMsg[btlMsgCount], sizeof(btlMsg[0]), fmt, ap);
  va_end(ap);
  btlMsgCount++;
}

// Turns a TurnLog into narration. Everything here was already decided by the
// engine -- nothing is recomputed, so the text can never disagree with the maths.
// Picks the cue for an action from the TurnLog, so the sound can never
// disagree with what actually happened.
static void btlSfxFor(const TurnLog &lg) {
  if (lg.targetFainted) { sfxPlay(SFX_FAINT); return; }
  if (lg.inflicted) { sfxPlay(SFX_STATUS); return; }
  if (lg.damage && lg.effPct > 100) { sfxPlay(SFX_SUPER); return; }
  if (lg.damage) {
    sfxPlay(lg.move && moveEntry(lg.move).cat == MC_SPEC ? SFX_BEAM : SFX_HIT);
    return;
  }
  if (lg.move && moveEntry(lg.move).cat == MC_STATUS && !lg.missed) sfxPlay(SFX_STATUS);
}

static void btlNarrate(const Combatant &actor, const Combatant &target, const TurnLog &lg) {
  if (lg.skipped) return;
  btlSfxFor(lg);
  char transformedName[32];
  const char *usedName = lg.move ? moveName(lg.move) : "";
  if (lg.mechanic == BMECH_Z_MOVE) {
    snprintf(transformedName, sizeof(transformedName), "Z-%s", typeName(lg.moveType));
    usedName = transformedName;
  } else if (lg.mechanic == BMECH_DYNAMAX) {
    snprintf(transformedName, sizeof(transformedName), "MAX %s", typeName(lg.moveType));
    usedName = transformedName;
  }
  if (lg.hurtSelf) { btlSay(T(S_BTL_HURTSELF)); return; }
  if (lg.charged) { btlSay(T(S_BTL_USED), displayCombatantName(actor), usedName); return; }
  if (lg.move) btlSay(T(S_BTL_USED), displayCombatantName(actor), usedName);
  if (lg.missed) { btlSay(T(S_BTL_MISS), displayCombatantName(actor)); return; }
  if (lg.blockedByField) { btlSay("%s", T(S_BTL_FIELD_BLOCKED)); return; }
  if (lg.immune) { btlSay(T(S_BTL_IMMUNE)); return; }
  if (lg.weatherSet != BWEATHER_NONE)
    btlSay(T(S_BTL_FIELD_BEGAN), btlWeatherName(lg.weatherSet));
  if (lg.terrainSet != BTERRAIN_NONE)
    btlSay(T(S_BTL_FIELD_BEGAN), btlTerrainName(lg.terrainSet));
  if (lg.weatherDamage == BWEATHER_SAND)
    btlSay(T(S_BTL_SAND_HURT), displayCombatantName(actor));
  if (lg.crit) btlSay(T(S_BTL_CRIT));
  if (lg.damage && lg.effPct > 100) btlSay(T(S_BTL_SUPER));
  else if (lg.damage && lg.effPct < 100) btlSay(T(S_BTL_WEAK));
  if (lg.stageMask && lg.move) {
    static const uint8_t BIT[BATTLE_STAGE_COUNT] = {
      ST_ATK, ST_DEF, ST_SPA, ST_SPD, ST_SPE, ST_ACC, ST_EVA,
    };
    static const StrId LABEL[BATTLE_STAGE_COUNT] = {
      S_STAT_ATK, S_STAT_DEF, S_STAT_SPA, S_STAT_SPD, S_STAT_SPE,
      S_STAT_ACC, S_STAT_EVA,
    };
    const Combatant &changed = moveEntry(lg.move).target == TG_SELF ? actor : target;
    for (uint8_t stat = 0; stat < BATTLE_STAGE_COUNT; stat++)
      if (lg.stageMask & BIT[stat])
        btlSay(T(S_BTL_STAGE_FMT), displayCombatantName(changed), T(LABEL[stat]), lg.stageDelta);
  }
  if (lg.healed) btlSay(T(S_BTL_HEALED), displayCombatantName(actor));
  if (lg.inflicted) {
    static const StrId AIL_STR[] = { S_AIL_PARA, S_AIL_PARA, S_AIL_BURN, S_AIL_POISON,
                                     S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE };
    if (lg.inflicted < 7)
      btlSay(T(S_BTL_STATUS), displayCombatantName(target), T(AIL_STR[lg.inflicted]));
  }
  if (lg.targetFainted) btlSay(T(S_BTL_FAINT), displayCombatantName(target));
}

static void btlNarrateFieldEnd(const FieldLog &log) {
  if (log.weatherExpired != BWEATHER_NONE)
    btlSay(T(S_BTL_FIELD_ENDED), btlWeatherName(log.weatherExpired));
  if (log.weatherRestored != BWEATHER_NONE)
    btlSay(T(S_BTL_FIELD_BEGAN), btlWeatherName(log.weatherRestored));
  if (log.terrainExpired != BTERRAIN_NONE)
    btlSay(T(S_BTL_FIELD_ENDED), btlTerrainName(log.terrainExpired));
  if (log.terrainRestored != BTERRAIN_NONE)
    btlSay(T(S_BTL_FIELD_BEGAN), btlTerrainName(log.terrainRestored));
}

static void btlApplyEntry(uint8_t side) {
  if (side > 1) return;
  Combatant &entrant = side ? btlFoe : btlYou;
  Combatant &opponent = side ? btlYou : btlFoe;
  EntryLog log;
  battleOnEnter(entrant, opponent, btlField, side, log);
  btlHpShown[side] = entrant.hp;
  if (log.hazardDamage) btlHitUntil[side] = millis() + BTL_HIT_MS;
  if (log.weatherSet != BWEATHER_NONE)
    btlSay(T(S_BTL_FIELD_BEGAN), btlWeatherName(log.weatherSet));
  if (log.terrainSet != BTERRAIN_NONE)
    btlSay(T(S_BTL_FIELD_BEGAN), btlTerrainName(log.terrainSet));
  if (log.inflicted) {
    static const StrId AIL_STR[] = { S_AIL_PARA, S_AIL_PARA, S_AIL_BURN,
      S_AIL_POISON, S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE };
    if (log.inflicted < 7)
      btlSay(T(S_BTL_STATUS), displayCombatantName(entrant), T(AIL_STR[log.inflicted]));
  }
  if (entrant.fainted()) btlSay(T(S_BTL_FAINT), displayCombatantName(entrant));
}

static bool btlReplaceActive(uint8_t side, uint8_t next, bool announce) {
  if (side > 1) return false;
  Combatant *squad = side ? btlFoeSquad : btlSquad;
  uint8_t count = side ? btlFoeSquadN : btlSquadN;
  uint8_t &active = side ? btlFoeAt : btlSquadAt;
  Combatant &current = side ? btlFoe : btlYou;
  if (next >= count || next == active || squad[next].fainted()) return false;
  battleOnSwitchOut(current);
  squad[active] = current;
  active = next;
  current = squad[active];
  if (!side) btlMarkEntered(active);
  btlHpShown[side] = current.hp;
  btlSyncSprite(side, current);
  btlLungeUntil[side] = btlHitUntil[side] = btlFaintUntil[side] = 0;
  btlEnterUntil[side] = millis() + BTL_ENTER_MS;
  if (announce) {
    if (!side) btlSay(T(S_BTL_GO), displayCombatantName(current));
    else if (btlLink) btlSay(T(S_BTL_SENDS), lan.peerName, displayCombatantName(current));
    else if (btlTrainer >= 0)
      btlSay(T(S_BTL_SENDS), trainerName(btlRegion, btlTrainer),
             displayCombatantName(current));
  }
  btlApplyEntry(side);
  return true;
}

static bool btlReplaceFirstAvailable(uint8_t side) {
  Combatant *squad = side ? btlFoeSquad : btlSquad;
  uint8_t count = side ? btlFoeSquadN : btlSquadN;
  uint8_t active = side ? btlFoeAt : btlSquadAt;
  uint8_t choices[TRAINER_TEAM_MAX];
  uint8_t choiceCount = 0;
  for (uint8_t i = 0; i < count; i++)
    if (i != active && !squad[i].fainted()) choices[choiceCount++] = i;
  return choiceCount && btlReplaceActive(side, choices[random(choiceCount)], true);
}

static_assert((uint8_t)BMECH_Z_MOVE == ITEM_MECHANIC_Z_MOVE &&
              (uint8_t)BMECH_DYNAMAX == ITEM_MECHANIC_DYNAMAX &&
              (uint8_t)BMECH_MEGA == ITEM_MECHANIC_MEGA,
              "mechanic item flags must match battle mechanic IDs");

static BattleMechanic btlMechanicFromItem(const ItemEntry &item) {
  if (item.effect != ITEM_EFFECT_BATTLE_MECHANIC ||
      item.flags < ITEM_MECHANIC_Z_MOVE || item.flags > ITEM_MECHANIC_MEGA)
    return BMECH_NONE;
  return (BattleMechanic)item.flags;
}

static MegaFormKind btlMegaFormFromItem(const ItemEntry &item) {
  return btlMechanicFromItem(item) == BMECH_MEGA &&
         item.param >= MEGA_FORM_STANDARD && item.param <= MEGA_FORM_Z
      ? (MegaFormKind)item.param : MEGA_FORM_NONE;
}

static const ItemEntry *btlMechanicItem(BattleMechanic mechanic,
                                        MegaFormKind megaForm = MEGA_FORM_NONE) {
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item && btlMechanicFromItem(*item) == mechanic &&
        (mechanic != BMECH_MEGA || megaForm == MEGA_FORM_NONE ||
         btlMegaFormFromItem(*item) == megaForm)) return item;
  }
  return nullptr;
}

static void btlResetMechanics() {
  btlYourMechanics = BattleSideMechanics();
  btlFoeMechanics = BattleSideMechanics();
  btlPendingMechanic = BMECH_NONE;
  btlWildMechanic = BMECH_NONE;
  btlMyMechanic = BMECH_NONE;
  btlPendingMegaForm = MEGA_FORM_NONE;
  btlWildMegaForm = MEGA_FORM_NONE;
  btlField = BattleField();
}

static void btlScaleShownHp(uint8_t who, uint16_t oldMaxHp, uint16_t newMaxHp) {
  if (who > 1 || !oldMaxHp || oldMaxHp == newMaxHp) return;
  uint32_t scaled = ((uint32_t)btlHpShown[who] * newMaxHp + oldMaxHp - 1u) / oldMaxHp;
  btlHpShown[who] = scaled > newMaxHp ? newMaxHp : (uint16_t)scaled;
}

// Builds one opponent through Pet, so it gets the same stat formula and the
// same learnset-driven moveset the player's creatures do.
static void foeFromSpecies(Combatant &c, int16_t dex, uint8_t lvl, uint8_t iv) {
  Pet foe;
  foe.dbgHatchAs(dex, false);
  foe.ivAtk = foe.ivDef = foe.ivSpe = foe.ivHp = iv;
  foe.ageMinutes = (uint32_t)(lvl ? lvl - 1 : 0) * MINUTES_PER_LEVEL;
  foe.relearnFromLevel();
  combatantFromPet(c, foe);
}

// Your side is the six cultivation slots, ordered with the selected lead first.
// The active cultivation slot is read from Pet so actions performed immediately
// before battle cannot be hidden by a stale save.
//
// Both ladders cap your LEVEL to the leader's best, so a gym is always fought
// on its own terms and grinding is never the answer -- the type chart, the
// movesets and the choices are. Hard additionally caps your team SIZE to the
// leader's, so Brock is two-on-two. The caps are applied while BUILDING the
// combatants, so nothing is ever written back to the stored creature, exactly
// like ailments.
static void buildSquad(uint8_t maxLvl, uint8_t maxCount, uint16_t mask) {
  btlSquadN = 0;
  btlSquadAt = 0;
  btlPetIn = false;
  if (maxCount > TRAINER_TEAM_MAX) maxCount = TRAINER_TEAM_MAX;
  for (uint8_t order = 0; order < PARTY_SLOTS && btlSquadN < maxCount; order++) {
    uint8_t i = order == 0 ? party.leadIndex() : (uint8_t)(order - 1);
    if (order && i >= party.leadIndex()) i++;
    if (!(mask & (1 << i))) continue;
    if (i == party.activeIndex()) {
      if (pet.isEgg() || pet.isDead()) continue;
      Pet tmp = pet;                     // battle caps never mutate cultivation
      if (maxLvl && tmp.level() > maxLvl)
        tmp.ageMinutes = (uint32_t)(maxLvl - 1) * MINUTES_PER_LEVEL;
      btlSquadSource[btlSquadN] = (int8_t)i;
      combatantFromPet(btlSquad[btlSquadN++], tmp);
      btlPetIn = true;
      continue;
    }
    if (!party.slots[i].battleReady() || party.slots[i].dead()) continue;
    PartyMon m = party.slots[i];
    if (maxLvl && m.level > maxLvl) m.level = maxLvl;
    btlSquadSource[btlSquadN] = (int8_t)i;
    combatantFromParty(btlSquad[btlSquadN++], m);
  }
  if (btlSquadN) btlYou = btlSquad[0];
}

static void btlBeginParticipation() {
  btlEnteredMask = btlSquadN ? 1 : 0;
}

static void btlMarkEntered(uint8_t index) {
  if (index < btlSquadN) btlEnteredMask |= (uint8_t)(1u << index);
}

// How many you may bring: the leader's own count in hard mode, six otherwise.
uint8_t squadCap(uint8_t idx, bool hard) {
  if (idx >= regionBattleInfo(gymRegion).trainerCount) return TRAINER_TEAM_MAX;
  return hard ? trainerInfo(gymRegion, idx).count : TRAINER_TEAM_MAX;
}

// A fight against another device. The squads are already exchanged; the host
// owns resolution and the guest renders what it is sent.
void startLinkBattle() {
  if (!lan.mineN || !lan.theirsN) return;
  // Rebuilt from lan.mine, NOT from squadMask. What we fight with has to be
  // exactly what the peer was told we have -- rebuilding from the party would
  // silently diverge if anything changed between offering and starting.
  btlSquadN = 0;
  btlSquadAt = 0;
  for (uint8_t i = 0; i < lan.mineN && i < TRAINER_TEAM_MAX; i++) {
    btlSquadSource[btlSquadN] = i < lanMineSourceN
        ? lanMineSource[i] : BTL_SOURCE_NONE;
    linkMonTo(btlSquad[btlSquadN++], lan.mine[i]);
  }
  if (!btlSquadN) return;
  btlYou = btlSquad[0];
  btlBeginParticipation();
  btlLink = true;
  btlWild = false;
  btlLinkHost = lan.isHost;
  btlTrainer = -1;
  btlHard = false;
  btlFoeAt = 0;
  btlFoeSquadN = 0;
  for (uint8_t i = 0; i < lan.theirsN && i < TRAINER_TEAM_MAX; i++)
    linkMonTo(btlFoeSquad[btlFoeSquadN++], lan.theirs[i]);
  btlFoe = btlFoeSquad[0];
  btlResetMechanics();
  btlMyAct = 0;
  btlMyPercent = 0;
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlFoeDetailOpen = false;
  btlFoeDetailPage = 0;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlSwapPending = 0;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlCaptureAnimating = false;
  btlCaptureItem = ITEM_KEY_NONE;
  btlResetThrow();
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  btlResetTapDebounce();
  battleOpen = true;
  if (btlLinkHost) {
    btlApplyEntry(0);
    btlApplyEntry(1);
  }
}

void startTrainerBattle(uint8_t idx, bool hard) {
  const uint8_t region = gymRegion;
  if (idx >= regionBattleInfo(region).trainerCount ||
      pet.isEgg() || pet.ceremony != CER_NONE) return;
  const Trainer &tr = trainerInfo(region, idx);
  uint8_t top = 0;
  for (int k = 0; k < tr.count; k++)
    if (tr.team[k].level > top) top = tr.team[k].level;
  // BOTH ladders cap your level to the leader's best. Without it a L73 team
  // walks every trainer at 100% and the type chart never matters. Hard adds the
  // size cap on top, plus a smarter AI and better opposing IVs.
  buildSquad(top, hard ? tr.count : TRAINER_TEAM_MAX, squadMask);
  if (!btlSquadN) return;
  btlBeginParticipation();
  btlRegion = region;
  btlWild = false;
  btlTrainer = (int8_t)idx;
  btlHard = hard;
  btlFoeAt = 0;
  const RegionBattleInfo &battle = regionBattleInfo(btlRegion);
  btlFoeSquadN = 0;
  for (uint8_t i = 0; i < tr.count && i < TRAINER_TEAM_MAX; i++)
    foeFromSpecies(btlFoeSquad[btlFoeSquadN++], tr.team[i].dex, tr.team[i].level,
                   hard ? battle.hardIv : battle.easyIv);
  btlFoe = btlFoeSquad[0];
  btlResetMechanics();
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlFoeDetailOpen = false;
  btlFoeDetailPage = 0;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlSwapPending = 0;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlCaptureAnimating = false;
  btlCaptureItem = ITEM_KEY_NONE;
  btlResetThrow();
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  btlLungeUntil[0] = btlLungeUntil[1] = 0;
  btlHitUntil[0] = btlHitUntil[1] = 0;
  btlResetTapDebounce();
  battleOpen = true;
  btlApplyEntry(0);
  btlApplyEntry(1);
}

void startBattle(int16_t dex, uint8_t lvl) {
  if (pet.isEgg() || pet.ceremony != CER_NONE) return;
  if (dex < 1 || dex > dexCount()) return;
  buildSquad(0, TRAINER_TEAM_MAX, 0xFFFF);
  if (!btlSquadN) return;
  btlBeginParticipation();
  // The opponent is built through Pet so it gets the same stat formula and the
  // same learnset-driven moveset the player's creature does -- no special-cased
  // "enemy" maths that could quietly diverge.
  Pet foe;
  foe.dbgHatchAs(dex, false);
  foe.ivAtk = foe.ivDef = foe.ivSpe = foe.ivHp = 20;
  foe.ageMinutes = (uint32_t)(lvl ? lvl - 1 : 0) * MINUTES_PER_LEVEL;
  foe.relearnFromLevel();
  combatantFromPet(btlFoe, foe);
  btlFoeAt = 0;
  btlFoeSquadN = 1;
  btlFoeSquad[0] = btlFoe;
  btlResetMechanics();
  btlWild = false;
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlFoeDetailOpen = false;
  btlFoeDetailPage = 0;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlSwapPending = 0;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlCaptureAnimating = false;
  btlCaptureItem = ITEM_KEY_NONE;
  btlResetThrow();
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  btlLungeUntil[0] = btlLungeUntil[1] = 0;
  btlHitUntil[0] = btlHitUntil[1] = 0;
  btlResetTapDebounce();
  battleOpen = true;
  btlApplyEntry(0);
  btlApplyEntry(1);
}

static SpeciesId wildSpecies(uint8_t region) {
  if (region >= regionAll()) return SPECIES_NONE;
  const RegionInfo &info = regionInfo(region);
  bool legends = regionBattleInfo(region).trainerCount &&
      player.hasBadge(region, regionBattleInfo(region).trainerCount - 1, true);
  uint32_t total = 0;
  for (SpeciesId dex = info.lo; dex <= info.hi && dex <= dexCount(); dex++) {
    uint8_t rarity = dexEntry(dex).rarity;
    if (rarity == R_LEGENDARIO && !legends) continue;
    total += rarity == R_LEGENDARIO ? 1 : rarity == R_RARO ? 12 : rarity == R_EVO ? 22 : 60;
  }
  if (!total) return SPECIES_NONE;
  uint32_t pick = (uint32_t)random((long)total);
  for (SpeciesId dex = info.lo; dex <= info.hi && dex <= dexCount(); dex++) {
    uint8_t rarity = dexEntry(dex).rarity;
    if (rarity == R_LEGENDARIO && !legends) continue;
    uint16_t weight = rarity == R_LEGENDARIO ? 1 : rarity == R_RARO ? 12 : rarity == R_EVO ? 22 : 60;
    if (pick < weight) return dex;
    pick -= weight;
  }
  return SPECIES_NONE;
}

void startWildBattle(uint8_t region, bool hard) {
  if (pet.isEgg() || pet.ceremony != CER_NONE || region >= regionAll()) return;
  SpeciesId dex = wildSpecies(region);
  if (!dex) return;
  buildSquad(0, TRAINER_TEAM_MAX, 0xFFFF);
  if (!btlSquadN) return;
  btlBeginParticipation();
  uint8_t level = (uint8_t)random(1L,
      (long)wildEncounterMaxLevel(pet.level(), hard) + 1L);
  const RegionBattleInfo &battle = regionBattleInfo(region);
  uint8_t ivBase = hard ? battle.hardIv : battle.easyIv;
  Pet foe;
  foe.dbgHatchAs(dex, false);
  foe.abilitySlot = wildAbilitySlotForRoll(
      dex, hard, (uint8_t)random(100), (uint32_t)random(2));
  auto rollIv = [ivBase]() -> uint8_t {
    int value = (int)ivBase + (int)random(7) - 3;
    return value < 0 ? 0 : value > 31 ? 31 : (uint8_t)value;
  };
  foe.ivAtk = rollIv(); foe.ivDef = rollIv(); foe.ivSpe = rollIv(); foe.ivHp = rollIv();
  bool rare = wildRareForRoll((uint32_t)random((long)WILD_RARE_ROLL_SCALE),
                              player.wildRareBonus);
  foe.shiny = rare;
  wildApplyRare(rare, foe.ivAtk, foe.ivDef, foe.ivSpe, foe.ivHp);
  foe.ageMinutes = (uint32_t)(level - 1) * MINUTES_PER_LEVEL;
  foe.raisedMinutes = 0;
  foe.relearnFromLevel();
  btlWildMon = foe.toPartyMon();
  btlWildMon.setGigantamaxFactor(
      wildGigantamaxFactorForRoll(dex, (uint8_t)random(100)));
  combatantFromParty(btlFoe, btlWildMon);
  btlFoeAt = 0;
  btlFoeSquadN = 1;
  btlFoeSquad[0] = btlFoe;
  btlResetMechanics();
  btlField = wildBattleField(dexEntry(btlFoe.dex).biome, (uint8_t)random(100));
  // 252 is divisible by both possible pool sizes (2 or 3), so modulo selection
  // stays exactly uniform after filtering out unusable mechanics.
  btlWildMechanic = wildBattleMechanic(
      (uint8_t)random(100), (uint8_t)random(252), hard,
      battleMegaEligible(btlFoe.dex),
      battleMechanicAvailable(btlFoeMechanics, btlFoe, BMECH_Z_MOVE),
      battleDynamaxEligible(btlFoe.dex));
  if (btlWildMechanic == BMECH_MEGA) {
    const MegaFormEntry *form = megaFormFor(btlFoe.dex);
    btlWildMegaForm = form ? form->form : MEGA_FORM_NONE;
  }
  btlRegion = region;
  btlTrainer = -1;
  btlHard = hard;
  btlWild = true;
  btlLink = false;
  btlFoeAt = 0;
  btlMsgCount = 0;
  if (btlField.weather != BWEATHER_NONE)
    btlSay(T(S_BTL_FIELD_BEGAN), btlWeatherName(btlField.weather));
  if (btlField.terrain != BTERRAIN_NONE)
    btlSay(T(S_BTL_FIELD_BEGAN), btlTerrainName(btlField.terrain));
  btlOver = false;
  btlWon = false;
  btlFoeDetailOpen = false;
  btlFoeDetailPage = 0;
  btlMenu = 0;
  btlItemPage = 0;
  btlPendingItem = ITEM_KEY_NONE;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlSwapPending = 0;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlCaptureAnimating = false;
  btlCaptureItem = ITEM_KEY_NONE;
  btlResetThrow();
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  btlLungeUntil[0] = btlLungeUntil[1] = 0;
  btlHitUntil[0] = btlHitUntil[1] = 0;
  btlResetTapDebounce();
  battleOpen = true;
  btlApplyEntry(0);
  btlApplyEntry(1);
}

// The guest's whole turn: copy in what the host resolved and play the same
// animations the host is playing. It runs no battle logic at all -- that is the
// entire point of one side being authoritative (see link.h).
static void btlApplyResult() {
  if (lan.resultN < sizeof(LinkResult)) { lan.resultNew = false; return; }
  LinkResult r;
  memcpy(&r, lan.result, sizeof(r));
  lan.resultNew = false;
  BattleField previousField = btlField;

  // The wire says "host"/"guest"; here we are always the guest, so their fields
  // are the foe's and ours are ours.
  uint32_t now = millis();
  if (r.guestIdx < btlSquadN && r.guestIdx != btlSquadAt) {
    battleOnSwitchOut(btlYou);
    btlSquad[btlSquadAt] = btlYou;
    btlSquadAt = r.guestIdx;
    btlYou = btlSquad[btlSquadAt];
    btlMarkEntered(btlSquadAt);
    btlSyncSprite(0, btlYou);
    btlLungeUntil[0] = btlHitUntil[0] = btlFaintUntil[0] = 0;
    btlEnterUntil[0] = now + BTL_ENTER_MS;
  }
  if (r.hostIdx != btlFoeAt && r.hostIdx < lan.theirsN) {
    btlFoeAt = r.hostIdx;
    linkMonTo(btlFoe, lan.theirs[btlFoeAt]);
    btlHpShown[1] = btlFoe.maxHp;
    btlSyncSprite(1, btlFoe);
    btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
    btlEnterUntil[1] = now + BTL_ENTER_MS;
  }
  uint16_t oldYouMaxHp = btlYou.maxHp, oldFoeMaxHp = btlFoe.maxHp;
  btlYou.maxHp = r.guestMaxHp ? r.guestMaxHp : 1;
  btlFoe.maxHp = r.hostMaxHp ? r.hostMaxHp : 1;
  btlScaleShownHp(0, oldYouMaxHp, btlYou.maxHp);
  btlScaleShownHp(1, oldFoeMaxHp, btlFoe.maxHp);
  btlYou.hp = r.guestHp > btlYou.maxHp ? btlYou.maxHp : r.guestHp;
  btlFoe.hp = r.hostHp > btlFoe.maxHp ? btlFoe.maxHp : r.hostHp;
  btlYou.ailment = r.guestAil;
  btlFoe.ailment = r.hostAil;
  // GLUE: LinkResult is the stable byte-oriented wire shape; remove this
  // mapping only if the protocol gains a BattleField serializer of its own.
  btlField.baseWeather = r.baseWeather <= BWEATHER_SNOW
      ? (BattleWeather)r.baseWeather : BWEATHER_NONE;
  btlField.weather = r.weather <= BWEATHER_SNOW
      ? (BattleWeather)r.weather : BWEATHER_NONE;
  btlField.weatherTurns = r.weatherTurns <= BATTLE_FIELD_TURNS
      ? r.weatherTurns : BATTLE_FIELD_TURNS;
  btlField.baseTerrain = r.baseTerrain <= BTERRAIN_PSYCHIC
      ? (BattleTerrain)r.baseTerrain : BTERRAIN_NONE;
  btlField.terrain = r.terrain <= BTERRAIN_PSYCHIC
      ? (BattleTerrain)r.terrain : BTERRAIN_NONE;
  btlField.terrainTurns = r.terrainTurns <= BATTLE_FIELD_TURNS
      ? r.terrainTurns : BATTLE_FIELD_TURNS;
  for (uint8_t localSide = 0; localSide < 2; localSide++) {
    uint8_t wireSide = localSide ^ 1u;
    BattleSideConditions &side = btlField.sides[localSide];
    side.reflectTurns = min(r.sideReflectTurns[wireSide], BATTLE_FIELD_TURNS);
    side.lightScreenTurns = min(r.sideLightScreenTurns[wireSide], BATTLE_FIELD_TURNS);
    side.auroraVeilTurns = min(r.sideAuroraVeilTurns[wireSide], BATTLE_FIELD_TURNS);
    side.spikesLayers = min(r.sideSpikesLayers[wireSide], (uint8_t)3);
    side.toxicSpikesLayers = min(r.sideToxicSpikesLayers[wireSide], (uint8_t)2);
    side.stealthRock = (r.sideHazardFlags[wireSide] & 1u) != 0;
    side.stickyWeb = (r.sideHazardFlags[wireSide] & 2u) != 0;
  }
  if (!btlField.weatherTurns) btlField.weather = btlField.baseWeather;
  if (!btlField.terrainTurns) btlField.terrain = btlField.baseTerrain;
  btlYou.type1 = r.guestType1; btlYou.type2 = r.guestType2;
  btlFoe.type1 = r.hostType1; btlFoe.type2 = r.hostType2;
  btlYou.activeMechanic = r.guestActive;
  btlFoe.activeMechanic = r.hostActive;
  btlYou.megaForm = r.guestMegaForm;
  btlFoe.megaForm = r.hostMegaForm;
  btlYou.form = r.guestForm <= BFORM_PALAFIN_HERO ? r.guestForm : BFORM_BASE;
  btlFoe.form = r.hostForm <= BFORM_PALAFIN_HERO ? r.hostForm : BFORM_BASE;
  btlYou.formPrimed = r.guestFormPrimed != 0;
  btlFoe.formPrimed = r.hostFormPrimed != 0;
  btlYou.gigantamax = r.guestGigantamax != 0;
  btlFoe.gigantamax = r.hostGigantamax != 0;
  btlYou.dynamaxTurns = r.guestDynamaxTurns;
  btlFoe.dynamaxTurns = r.hostDynamaxTurns;
  btlYou.normalMaxHp = btlYou.activeMechanic == BMECH_DYNAMAX
      ? (uint16_t)((btlYou.maxHp + 1u) / 2u) : 0;
  btlFoe.normalMaxHp = btlFoe.activeMechanic == BMECH_DYNAMAX
      ? (uint16_t)((btlFoe.maxHp + 1u) / 2u) : 0;
  for (uint8_t i = 0; i < SI_COUNT; i++) {
    btlYou.base[i] = r.guestBase[i] ? r.guestBase[i] : 1;
    btlFoe.base[i] = r.hostBase[i] ? r.hostBase[i] : 1;
    btlYou.stage[i] = r.guestStage[i] < -6 ? -6 : r.guestStage[i] > 6 ? 6 : r.guestStage[i];
    btlFoe.stage[i] = r.hostStage[i] < -6 ? -6 : r.hostStage[i] > 6 ? 6 : r.hostStage[i];
  }
  btlYou.accuracyStage = r.guestAccuracyStage < -6 ? -6
      : r.guestAccuracyStage > 6 ? 6 : r.guestAccuracyStage;
  btlYou.evasionStage = r.guestEvasionStage < -6 ? -6
      : r.guestEvasionStage > 6 ? 6 : r.guestEvasionStage;
  btlFoe.accuracyStage = r.hostAccuracyStage < -6 ? -6
      : r.hostAccuracyStage > 6 ? 6 : r.hostAccuracyStage;
  btlFoe.evasionStage = r.hostEvasionStage < -6 ? -6
      : r.hostEvasionStage > 6 ? 6 : r.hostEvasionStage;
  btlYourMechanics.usedMask = r.guestUsedMask;
  btlFoeMechanics.usedMask = r.hostUsedMask;
  for (uint8_t i = 0; i < btlSquadN && i < TRAINER_TEAM_MAX; i++) {
    Combatant &member = i == btlSquadAt ? btlYou : btlSquad[i];
    member.usedMechanic = r.guestMemberMechanic[i];
    if (i != btlSquadAt) member.megaForm = r.guestMemberMegaForm[i];
    member.form = r.guestMemberForm[i] <= BFORM_PALAFIN_HERO
        ? r.guestMemberForm[i] : BFORM_BASE;
    member.formPrimed = r.guestMemberFormPrimed[i] != 0;
  }
  for (uint8_t i = 0; i < btlFoeSquadN && i < TRAINER_TEAM_MAX; i++) {
    Combatant &member = i == btlFoeAt ? btlFoe : btlFoeSquad[i];
    member.usedMechanic = r.hostMemberMechanic[i];
    if (i != btlFoeAt) member.megaForm = r.hostMemberMegaForm[i];
    member.form = r.hostMemberForm[i] <= BFORM_PALAFIN_HERO
        ? r.hostMemberForm[i] : BFORM_BASE;
    member.formPrimed = r.hostMemberFormPrimed[i] != 0;
  }
  if (btlYou.fainted()) btlSetPersistentDead(btlSquadAt, true);

  btlMsgCount = 0;
  char hostMoveName[32], guestMoveName[32];
  const char *hostUsed = r.hostMove ? moveName(r.hostMove) : "";
  const char *guestUsed = r.guestMove ? moveName(r.guestMove) : "";
  if (r.hostMoveMechanic == BMECH_Z_MOVE || r.hostMoveMechanic == BMECH_DYNAMAX) {
    snprintf(hostMoveName, sizeof(hostMoveName), "%s%s",
             r.hostMoveMechanic == BMECH_Z_MOVE ? "Z-" : "MAX ",
             typeName(moveEntry(r.hostMove).type));
    hostUsed = hostMoveName;
  }
  if (r.guestMoveMechanic == BMECH_Z_MOVE || r.guestMoveMechanic == BMECH_DYNAMAX) {
    snprintf(guestMoveName, sizeof(guestMoveName), "%s%s",
             r.guestMoveMechanic == BMECH_Z_MOVE ? "Z-" : "MAX ",
             typeName(moveEntry(r.guestMove).type));
    guestUsed = guestMoveName;
  }
  if (r.hostMove) btlSay(T(S_BTL_USED), displayCombatantName(btlFoe), hostUsed);
  if (r.guestMove) btlSay(T(S_BTL_USED), displayCombatantName(btlYou), guestUsed);
  if (previousField.weather != btlField.weather) {
    if (previousField.weather != BWEATHER_NONE)
      btlSay(T(S_BTL_FIELD_ENDED), btlWeatherName(previousField.weather));
    if (btlField.weather != BWEATHER_NONE)
      btlSay(T(S_BTL_FIELD_BEGAN), btlWeatherName(btlField.weather));
  }
  if (previousField.terrain != btlField.terrain) {
    if (previousField.terrain != BTERRAIN_NONE)
      btlSay(T(S_BTL_FIELD_ENDED), btlTerrainName(previousField.terrain));
    if (btlField.terrain != BTERRAIN_NONE)
      btlSay(T(S_BTL_FIELD_BEGAN), btlTerrainName(btlField.terrain));
  }
  if (r.guestDmg) { btlHitUntil[0] = now + BTL_HIT_MS; sfxPlay(SFX_HIT); }
  if (r.hostDmg) { btlHitUntil[1] = now + BTL_HIT_MS; sfxPlay(SFX_HIT); }
  if (btlYou.fainted()) {
    btlFaintUntil[0] = now + BTL_FAINT_MS;
    btlSay(T(S_BTL_FAINT), displayCombatantName(btlYou));
  }
  if (btlFoe.fainted()) {
    btlFaintUntil[1] = now + BTL_FAINT_MS;
    btlSay(T(S_BTL_FAINT), displayCombatantName(btlFoe));
  }
}

// Radio packets land on another task, so the guest picks them up here, once a
// frame, rather than rendering from inside an interrupt.
static void btlLinkPoll() {
  if (!btlLink) return;

  // A peer that stopped answering. Ending the fight is the only honest thing to
  // do -- there is no result coming, and pretending otherwise is the hang this
  // whole layer exists to remove.
  if (!lan.live() && !btlOver) {
    btlOver = true;
    btlWon = false;
    audioMusic(MUS_NONE);
    btlMsgCount = 0;
    btlSay("%s", T(S_LAN_GONE));
    return;
  }

  if (btlLinkHost) {
    // Our own action was latched when it was tapped; theirs arrives whenever
    // the radio manages it. Whichever is second sets the turn going.
    if (btlMyAct && lan.hasPeerAct() && !btlOver && !btlMsgCount &&
        btlSwapWho < 0) {
      uint8_t act = btlMyAct;
      uint8_t percent = btlMyPercent;
      BattleMechanic mechanic = btlMyMechanic;
      btlMyAct = 0;
      btlMyPercent = 0;
      btlMyMechanic = BMECH_NONE;
      if (LINK_ACT_IS_SWITCH(act)) btlSwitchTo(LINK_ACT_SLOT(act));
      else btlResolve(btlYou.moves[LINK_ACT_SLOT(act) % MOVE_SLOTS], percent, mechanic);
    }
    return;
  }

  if (lan.resultNew) btlApplyResult();
  if (lan.state == LINK_DONE && !btlOver) {
    btlOver = true;
    btlWon = lan.youWon;
    audioMusic(btlWon ? MUS_VICTORY : MUS_NONE);
    if (btlWon) sfxPlay(SFX_VICTORY);
    btlSay("%s", btlWon ? T(S_BTL_WIN) : T(S_BTL_LOSE));
  }
}

// Packs the outcome for the guest. Only the host ever calls this.
static void btlShipResult(const BattleMove &yourMove, const BattleMove &theirMove,
                          uint16_t hp0You, uint16_t hp0Foe) {
  LinkResult r = {};
  r.hostHp = btlYou.hp;   r.guestHp = btlFoe.hp;
  r.hostMaxHp = btlYou.maxHp; r.guestMaxHp = btlFoe.maxHp;
  r.hostAil = btlYou.ailment; r.guestAil = btlFoe.ailment;
  r.hostMove = yourMove.source;  r.guestMove = theirMove.source;
  r.hostDmg = (hp0You > btlYou.hp) ? hp0You - btlYou.hp : 0;
  r.guestDmg = (hp0Foe > btlFoe.hp) ? hp0Foe - btlFoe.hp : 0;
  r.hostIdx = btlSquadAt; r.guestIdx = btlFoeAt;
  r.hostType1 = btlYou.type1; r.hostType2 = btlYou.type2;
  r.guestType1 = btlFoe.type1; r.guestType2 = btlFoe.type2;
  r.hostActive = btlYou.activeMechanic; r.guestActive = btlFoe.activeMechanic;
  r.hostMegaForm = btlYou.megaForm; r.guestMegaForm = btlFoe.megaForm;
  r.hostForm = btlYou.form; r.guestForm = btlFoe.form;
  r.hostFormPrimed = btlYou.formPrimed ? 1 : 0;
  r.guestFormPrimed = btlFoe.formPrimed ? 1 : 0;
  r.hostGigantamax = btlYou.gigantamax ? 1 : 0;
  r.guestGigantamax = btlFoe.gigantamax ? 1 : 0;
  r.hostMoveMechanic = yourMove.mechanic; r.guestMoveMechanic = theirMove.mechanic;
  r.hostDynamaxTurns = btlYou.dynamaxTurns;
  r.guestDynamaxTurns = btlFoe.dynamaxTurns;
  r.hostUsedMask = btlYourMechanics.usedMask;
  r.guestUsedMask = btlFoeMechanics.usedMask;
  // GLUE: the protocol keeps fixed-width bytes while BattleField keeps the
  // domain enums; this is the single host-side conversion boundary.
  r.baseWeather = btlField.baseWeather;
  r.weather = btlField.weather;
  r.weatherTurns = btlField.weatherTurns;
  r.baseTerrain = btlField.baseTerrain;
  r.terrain = btlField.terrain;
  r.terrainTurns = btlField.terrainTurns;
  for (uint8_t sideIndex = 0; sideIndex < 2; sideIndex++) {
    const BattleSideConditions &side = btlField.sides[sideIndex];
    r.sideReflectTurns[sideIndex] = side.reflectTurns;
    r.sideLightScreenTurns[sideIndex] = side.lightScreenTurns;
    r.sideAuroraVeilTurns[sideIndex] = side.auroraVeilTurns;
    r.sideSpikesLayers[sideIndex] = side.spikesLayers;
    r.sideToxicSpikesLayers[sideIndex] = side.toxicSpikesLayers;
    r.sideHazardFlags[sideIndex] = (side.stealthRock ? 1u : 0u) |
                                   (side.stickyWeb ? 2u : 0u);
  }
  for (uint8_t i = 0; i < SI_COUNT; i++) {
    r.hostBase[i] = btlYou.base[i];
    r.guestBase[i] = btlFoe.base[i];
    r.hostStage[i] = btlYou.stage[i];
    r.guestStage[i] = btlFoe.stage[i];
  }
  r.hostAccuracyStage = btlYou.accuracyStage;
  r.hostEvasionStage = btlYou.evasionStage;
  r.guestAccuracyStage = btlFoe.accuracyStage;
  r.guestEvasionStage = btlFoe.evasionStage;
  for (uint8_t i = 0; i < btlSquadN && i < TRAINER_TEAM_MAX; i++) {
    const Combatant &member = i == btlSquadAt ? btlYou : btlSquad[i];
    r.hostMemberMechanic[i] = member.usedMechanic;
    r.hostMemberMegaForm[i] = member.megaForm;
    r.hostMemberForm[i] = member.form;
    r.hostMemberFormPrimed[i] = member.formPrimed ? 1 : 0;
  }
  for (uint8_t i = 0; i < btlFoeSquadN && i < TRAINER_TEAM_MAX; i++) {
    const Combatant &member = i == btlFoeAt ? btlFoe : btlFoeSquad[i];
    r.guestMemberMechanic[i] = member.usedMechanic;
    r.guestMemberMegaForm[i] = member.megaForm;
    r.guestMemberForm[i] = member.form;
    r.guestMemberFormPrimed[i] = member.formPrimed ? 1 : 0;
  }
  if (btlYou.fainted() || btlFoe.fainted()) r.flags |= 0x04;
  lan.sendResult((const uint8_t *)&r, (uint8_t)sizeof(r));
}

static void btlSetPersistentDead(uint8_t index, bool dead) {
  if (index >= btlSquadN) return;
  int8_t source = btlSquadSource[index];
  if (source < 0 || source >= PARTY_SLOTS) return;
  if (source == party.activeIndex()) pet.setDead(dead);
  else party.setDeadAt((uint8_t)source, dead);
}

static bool btlPlayerHasReplacement() {
  for (uint8_t i = 0; i < btlSquadN; i++)
    if (i != btlSquadAt && !btlSquad[i].fainted()) return true;
  return false;
}

static bool btlFoeHasReplacement() {
  for (uint8_t i = 0; i < btlFoeSquadN; i++)
    if (i != btlFoeAt && !btlFoeSquad[i].fainted()) return true;
  return false;
}

static void btlGrantWildRewards() {
  uint8_t rosterMask = 0;
  for (uint8_t i = 0; i < btlSquadN; i++) {
    int8_t source = btlSquadSource[i];
    if ((btlEnteredMask & (1u << i)) && source >= 0 && source < PARTY_SLOTS)
      rosterMask |= (uint8_t)(1u << source);
  }
  party.captureActive(pet, false);
  uint8_t before[PARTY_SLOTS][3];
  for (uint8_t i = 0; i < PARTY_SLOTS; i++) {
    before[i][0] = party.slots[i].trAtk;
    before[i][1] = party.slots[i].trDef;
    before[i][2] = party.slots[i].trSpe;
  }
  party.rewardRandomTraining(rosterMask, pet, 10);
  for (uint8_t i = 0; i < PARTY_SLOTS; i++) {
    if (!(rosterMask & (1u << i))) continue;
    btlRewardTraining[0] += party.slots[i].trAtk - before[i][0];
    btlRewardTraining[1] += party.slots[i].trDef - before[i][1];
    btlRewardTraining[2] += party.slots[i].trSpe - before[i][2];
  }
  MoveId foeMoves[LEARNED_MOVE_SLOTS] = {};
  for (uint8_t i = 0; i < MOVE_SLOTS; i++) foeMoves[i] = btlWildMon.moves[i];
  for (uint8_t i = 0; i < RESERVE_MOVE_SLOTS; i++)
    foeMoves[MOVE_SLOTS + i] = btlWildMon.reserveMoves[i];
  uint8_t dropCount = wildWeightedDropCount(
      btlHard, (uint8_t)random(100));
  for (uint8_t i = 0; i < dropCount; i++) {
    ItemRef drop = inventory.grantWeightedDrop(
        (uint32_t)random(0x7FFFFFFF), foeMoves, LEARNED_MOVE_SLOTS,
        btlRewardItems, btlRewardItemCount);
    btlRememberRewardItem(drop);
  }
}

void btlFinish(bool won) {
  btlOver = true;
  btlWon = won;
  btlResetRewardSummary();
  btlNewBadge = false;
  btlIvReward = GYM_IV_NONE;
  if (btlWon && btlTrainer >= 0) {
    if (!player.hasBadge(btlRegion, btlTrainer, btlHard)) {
      player.winBadge(btlRegion, btlTrainer, btlHard);
      btlNewBadge = true;
    }
    if (btlTrainer < regionBattleInfo(btlRegion).gymCount && btlPetIn)
      btlIvReward = pet.rewardGymIv(btlRegion, btlTrainer, btlIvWhich);
    // The v4 team2 snapshot contains both PlayerProgress and the active
    // PartyMon, so this one blob is the gym outcome's commit boundary.
    pet.saveNow();
  }
  audioMusic(btlWon ? MUS_VICTORY : MUS_NONE);
  if (btlWon) sfxPlay(SFX_VICTORY);
  if (btlLink && btlLinkHost) lan.sendEnd(btlWon);
  if (btlLink) { btlSay("%s", btlWon ? T(S_BTL_WIN) : T(S_BTL_LOSE)); return; }
  if (btlWon && btlTrainer >= 0) { btlWinUntil = millis() + 60000; return; }
  if (btlWon && btlWild) {
    inventory.beginBatch();
    btlGrantWildRewards();
    ItemRef mechanicDrop = inventory.grantMechanicReward(
        (ItemMechanicKind)btlWildMechanic, btlWildMegaForm);
    btlRememberRewardItem(mechanicDrop);
    inventory.commitBatch();
    btlWinUntil = millis() + 60000;
    return;
  }
  btlSay("%s", btlWon ? T(S_BTL_WIN) : T(S_BTL_LOSE));
}

static void btlHandleFaints() {
  bool youDown = btlYou.fainted();
  bool foeDown = btlFoe.fainted();
  if (!youDown && !foeDown) return;
  if (youDown && btlWild) btlSetPersistentDead(btlSquadAt, true);

  bool youHaveNext = youDown && btlPlayerHasReplacement();
  bool foeHasNext = foeDown && btlFoeHasReplacement();
  // A complete local wipe is a loss even when the final exchange was a draw.
  if (youDown && !youHaveNext) { btlFinish(false); return; }
  if (foeDown && !foeHasNext) { btlFinish(true); return; }

  btlSwapPending = 0;
  if (youHaveNext) {
    btlSwapPending |= 0x01;
    btlFaintUntil[0] = millis() + BTL_FAINT_MS;
  }
  if (foeHasNext) {
    btlSwapPending |= 0x02;
    btlFaintUntil[1] = millis() + BTL_FAINT_MS;
  }
  btlSwapWho = (btlSwapPending & 0x02) ? 1 : 0;
}

// The roll is supplied by the caller for the same reason as player escape:
// tests can prove the boundary values without depending on the global PRNG.
bool btlAttemptFoeRun(uint8_t roll) {
  if (!battleOpen || btlOver || !btlWild || btlLink || btlFoe.fainted()) return false;
  uint8_t chance = wildFoeEscapeChance(btlFoe.hp, btlFoe.maxHp, btlFoe.angry);
  if (!chance || roll >= chance) return false;
  btlOver = true;
  btlWon = false;
  btlMenu = 0;
  btlMsgCount = 0;
  audioMusic(MUS_NONE);
  btlSay(T(S_BTL_FOE_RAN), displayCombatantName(btlFoe));
  return true;
}

// One exchange: both sides act in speed order, then burn/poison chip.
static void btlResolve(MoveId yourMove, uint8_t yourPercent,
                       BattleMechanic yourMechanic) {
  TurnLog lg;
  // Against another device the opponent's move comes off the wire, never from
  // the AI -- and the host is the only side that runs this at all.
  MoveId foeMove;
  BattleMechanic foeMechanic = BMECH_NONE;
  uint8_t foePercent = 100;
  bool foeSwitched = false;
  bool foeWantsRun = false;
  uint8_t foeRunRoll = 100;
  uint32_t now = millis();
  if (btlLink) {
    // Off the wire, never from the AI. A switch is carried in the same message
    // as a move, and like our own switch it costs the turn: they change, we act.
    uint8_t act = lan.pendingAct;
    if (LINK_ACT_IS_SWITCH(act)) {
      uint8_t to = LINK_ACT_SLOT(act);
      if (to < btlFoeSquadN && to != btlFoeAt && !btlFoeSquad[to].fainted()) {
        foeSwitched = btlReplaceActive(1, to, true);
      }
      foeMove = 0;
    } else {
      foeMove = btlFoe.moves[LINK_ACT_SLOT(act) % MOVE_SLOTS];
      foePercent = lan.pendingPercent;
      BattleMechanic requested = lan.pendingMechanic;
      MegaFormKind requestedForm = lan.pendingMegaForm;
      uint16_t oldMaxHp = btlFoe.maxHp;
      if (battleActivateMechanic(btlFoeMechanics, btlFoe, requested, foeMove,
                                 requestedForm)) {
        foeMechanic = requested;
        btlScaleShownHp(1, oldMaxHp, btlFoe.maxHp);
      }
    }
    lan.pendingAct = 0;
    lan.pendingPercent = 0;
    lan.pendingMechanic = BMECH_NONE;
    lan.pendingMegaForm = MEGA_FORM_NONE;
  } else {
    if (btlWild) {
      uint8_t foeRunChance =
          wildFoeEscapeChance(btlFoe.hp, btlFoe.maxHp, btlFoe.angry);
      if (foeRunChance) {
        foeRunRoll = (uint8_t)random(100);
        foeWantsRun = foeRunRoll < foeRunChance;
      }
    }
    foeMove = foeWantsRun ? MOVE_NONE
                          : aiChooseMove(btlFoe, btlYou, btlField, btlHard);
  }
  (void)foeSwitched;

  if (!foeWantsRun && btlWild && btlWildMechanic != BMECH_NONE &&
      battleMechanicAvailable(btlFoeMechanics, btlFoe, btlWildMechanic, foeMove,
                              btlWildMegaForm)) {
    foeMechanic = btlWildMechanic;
    uint16_t oldMaxHp = btlFoe.maxHp;
    battleActivateMechanic(btlFoeMechanics, btlFoe, foeMechanic, foeMove,
                           btlWildMegaForm);
    btlScaleShownHp(1, oldMaxHp, btlFoe.maxHp);
    const ItemEntry *item = btlMechanicItem(foeMechanic, btlWildMegaForm);
    if (item) btlSay("%s: %s", displayCombatantName(btlFoe), itemName(item->key));
  }

  btlYou.protectedTurn = false;
  btlFoe.protectedTurn = false;
  BattleMove yourBattleMove = battleMoveFor(btlYou, yourMove, yourMechanic);
  BattleMove foeBattleMove = battleMoveFor(btlFoe, foeMove, foeMechanic);

  bool youFirst = battleMovesFirst(
      btlYou, yourBattleMove, btlFoe, foeBattleMove, btlField);
  Combatant *a = youFirst ? &btlYou : &btlFoe;
  Combatant *b = youFirst ? &btlFoe : &btlYou;
  BattleMove ma = youFirst ? yourBattleMove : foeBattleMove;
  BattleMove mb = youFirst ? foeBattleMove : yourBattleMove;
  uint8_t aSide = a == &btlYou ? 0 : 1;
  uint8_t bSide = aSide ^ 1u;

  uint16_t hp0You = btlYou.hp, hp0Foe = btlFoe.hp;
  if (a == &btlFoe && foeWantsRun) {
    if (btlAttemptFoeRun(foeRunRoll)) return;
  } else {
    battleAct(*a, *b, btlField, ma, lg, a == &btlYou ? yourPercent : foePercent,
              aSide);
    btlNarrate(*a, *b, lg);
    if (lg.damage && !lg.hurtSelf)
      btlLungeUntil[a == &btlYou ? 0 : 1] = now + BTL_LUNGE_MS;
  }
  bool skipSecond = false;
  if (lg.switchRequest == BSWITCH_TARGET && btlReplaceFirstAvailable(bSide))
    skipSecond = true;
  else if (lg.switchRequest == BSWITCH_USER)
    btlReplaceFirstAvailable(aSide);
  if (!skipSecond && !b->fainted()) {
    if (b == &btlFoe && foeWantsRun) {
      if (btlYou.fainted()) {
        btlHandleFaints();
        if (btlOver) return;
      }
      if (btlAttemptFoeRun(foeRunRoll)) return;
    } else {
      battleAct(*b, *a, btlField, mb, lg, b == &btlYou ? yourPercent : foePercent,
                bSide);
      btlNarrate(*b, *a, lg);
      if (lg.damage && !lg.hurtSelf)
        btlLungeUntil[b == &btlYou ? 0 : 1] = now + BTL_LUNGE_MS + BTL_LUNGE_MS;
      if (lg.switchRequest == BSWITCH_TARGET)
        btlReplaceFirstAvailable(aSide);
      else if (lg.switchRequest == BSWITCH_USER)
        btlReplaceFirstAvailable(bSide);
    }
  }
  // whoever actually lost health flinches, whichever side dealt it
  if (btlYou.hp < hp0You) btlHitUntil[0] = now + BTL_HIT_MS;
  if (btlFoe.hp < hp0Foe) btlHitUntil[1] = now + BTL_HIT_MS;
  TurnLog youEnd, foeEnd;
  FieldLog fieldEnd;
  battleEndRound(btlField, btlYou, btlFoe, youEnd, foeEnd, fieldEnd);
  if (youEnd.damage || youEnd.healed) btlNarrate(btlYou, btlYou, youEnd);
  if (foeEnd.damage || foeEnd.healed) btlNarrate(btlFoe, btlFoe, foeEnd);
  btlNarrateFieldEnd(fieldEnd);
  uint16_t oldYouMaxHp = btlYou.maxHp, oldFoeMaxHp = btlFoe.maxHp;
  if (yourMove) battleAfterAction(btlYou);
  if (foeMove) battleAfterAction(btlFoe);
  btlScaleShownHp(0, oldYouMaxHp, btlYou.maxHp);
  btlScaleShownHp(1, oldFoeMaxHp, btlFoe.maxHp);
  if (btlLink && btlLinkHost)
    btlShipResult(yourBattleMove, foeBattleMove, hp0You, hp0Foe);
  // Persist deaths and either queue replacements or finish the battle. The
  // replacement itself remains deferred until the faint message is dismissed.
  btlHandleFaints();
}

static void btlHpBar(int x, int y, int w, const Combatant &c, uint16_t shown) {
  int fw = c.maxHp ? (w - 4) * shown / c.maxHp : 0;
  uint16_t col = (shown * 2 > c.maxHp) ? UI_BAR_OK
                 : (shown * 4 > c.maxHp) ? UI_BAR_WARN : UI_BAR_BAD;
  gfx->fillRoundRect(x, y, w, 14, 4, UI_TRACK);
  if (fw > 0) gfx->fillRoundRect(x + 2, y + 2, fw, 10, 3, col);
  gfx->drawRoundRect(x, y, w, 14, 4, UI_INK);
}

static void btlMechanicAura(int cx, int groundY, BattleMechanic mechanic, uint32_t now) {
  if (mechanic == BMECH_DYNAMAX) {
    int pulse = (int)((now / 90) % 7);
    int cy = groundY - 70;
    for (int ring = 0; ring < 3; ring++)
      gfx->drawCircle(cx, cy, 52 + ring * 7 + pulse, UI_BAR_BAD);
    for (int i = 0; i < 6; i++) {
      int x = cx - 54 + ((i * 23 + (int)(now / 45)) % 108);
      int y = groundY - 24 - ((i * 31 + (int)(now / 30)) % 118);
      gfx->fillCircle(x, y, 2 + (i & 1), i & 1 ? UI_BAR_WARN : UI_BAR_BAD);
    }
  } else if (mechanic == BMECH_MEGA) {
    int cy = groundY - 66;
    int r = 54 + (int)((now / 120) % 5);
    gfx->drawLine(cx, cy - r, cx + r, cy, UI_BAR_WARN);
    gfx->drawLine(cx + r, cy, cx, cy + r, UI_BAR_WARN);
    gfx->drawLine(cx, cy + r, cx - r, cy, UI_BAR_WARN);
    gfx->drawLine(cx - r, cy, cx, cy - r, UI_BAR_WARN);
    for (int i = 0; i < 4; i++) {
      int x = cx + (i & 1 ? 1 : -1) * (34 + (i / 2) * 18);
      int y = cy + (i < 2 ? -1 : 1) * (28 + (i & 1) * 15);
      gfx->drawLine(x - 5, y, x + 5, y, UI_BAR_WARN);
      gfx->drawLine(x, y - 5, x, y + 5, UI_BAR_WARN);
    }
  }
}

static void btlDrawSprite(int sx, int sy, const Combatant &c, uint8_t who,
                          uint32_t now, uint8_t act, bool loop,
                          uint32_t actTime, bool flash) {
  int cx = sx + 24, ground = sy + 78;
  btlMechanicAura(cx, ground, c.activeMechanic, now);
  if (btlPmd[who].loaded) {
    if (!btlPmd[who].has(act)) { act = PMD_IDLE; loop = true; actTime = now; }
    uint8_t scaleBonus = c.activeMechanic == BMECH_DYNAMAX ? 1 : 0;
    uint8_t maxScale = c.activeMechanic == BMECH_DYNAMAX ? 5 : 4;
    drawPmdActM(btlPmd[who], act, cx, ground, actTime, loop,
                false, maxScale, scaleBonus);
    if (c.shiny) drawSparkleParticles(cx, ground, now);
    return;
  }
  const uint8_t *th = thumbs.get(c.dex);
  if (!th) return;
  drawThumb(th, sx, sy, 3, flash);
  if (c.shiny) drawSparkleParticles(cx, ground, now);
}

static void btlSide(int tx, int ty, int sx, int sy, const Combatant &c, uint8_t who) {
  // the scenes are busy, so the name and bar sit on their own plate rather
  // than fighting the artwork for contrast
  const int px = tx - 8, py = ty - 8, pw = 158, ph = 64;
  gfx->fillRoundRect(px, py, pw, ph, 8, UI_BG_DAY);
  gfx->drawRoundRect(px, py, pw, ph, 8, UI_INK);
  char name[64];
  snprintf(name, sizeof(name), "%s Lv.%u", displayCombatantName(c), c.level);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  uiDrawCenteredIn(name, px + 6, py + 4, pw - 12, 14);
  gfx->setTextColor(UI_BAR_WARN);
  uiDrawCenteredIn("HP", px + 6, py + 20, 20, 14);
  btlHpBar(px + 28, py + 20, pw - 34, c, btlHpShown[who]);

  // The last row is divided between whichever metadata actually exists, so
  // HP, status and mechanic labels stay centred without colliding.
  const char *meta[4];
  uint16_t metaColor[4];
  uint8_t metaCount = 0;
  char hp[16], mechanic[12];
  if (who == 0) {                 // your own numbers, as the games do
    snprintf(hp, sizeof(hp), "%u/%u", btlHpShown[who], c.maxHp);
    meta[metaCount] = hp;
    metaColor[metaCount++] = UI_INK;
  }
  if (c.ailment != AIL_NONE) {   // a status is the thing you most need to see
    static const StrId AIL_STR[] = { S_AIL_PARA, S_AIL_PARA, S_AIL_BURN, S_AIL_POISON,
                                     S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE };
    meta[metaCount] = T(AIL_STR[c.ailment]);
    metaColor[metaCount++] = UI_BAR_BAD;
  }
  if (c.activeMechanic != BMECH_NONE) {
    if (c.activeMechanic == BMECH_DYNAMAX)
      snprintf(mechanic, sizeof(mechanic), "%s %u",
               c.gigantamax ? "G-MAX" : "MAX", c.dynamaxTurns);
    else
      snprintf(mechanic, sizeof(mechanic), "MEGA");
    meta[metaCount] = mechanic;
    metaColor[metaCount++] = c.activeMechanic == BMECH_MEGA ? UI_BAR_WARN : UI_BAR_BAD;
  }
  if (c.form != BFORM_BASE) {
    meta[metaCount] = T(S_BTL_FORM);
    metaColor[metaCount++] = 0x45BF;
  }
  for (uint8_t i = 0; i < metaCount; i++) {
    int x0 = px + 6 + (pw - 12) * i / metaCount;
    int x1 = px + 6 + (pw - 12) * (i + 1) / metaCount;
    gfx->setTextColor(metaColor[i]);
    gfx->setTextSize(1);
    uiDrawCenteredIn(meta[i], x0, py + 40, x1 - x0, 16);
  }
  // a platform under each creature, so they stand in the scene rather than
  // floating over it
  uint32_t now = millis();
  int ox = 0, oy = 0;
  bool flash = false;
  if (now < btlFaintUntil[who]) {          // sinks out of frame as it faints
    uint32_t left = btlFaintUntil[who] - now;
    oy += (int)((BTL_FAINT_MS - left) * 70 / BTL_FAINT_MS);
  } else if (c.fainted() && btlSwapWho == (int8_t)who) {
    return;                                // gone, waiting to be replaced
  }
  if (now < btlEnterUntil[who]) {          // and the next one rises into place
    uint32_t left = btlEnterUntil[who] - now;
    oy += (int)(left * 70 / BTL_ENTER_MS);
  }
  if (now < btlLungeUntil[who]) {          // lean in, then back out
    uint32_t left = btlLungeUntil[who] - now;
    int amt = (int)(left > BTL_LUNGE_MS / 2 ? BTL_LUNGE_MS - left : left) * 22 / (BTL_LUNGE_MS / 2);
    ox = who == 0 ? amt : -amt;            // you lunge right, the foe lunges left
    oy = who == 0 ? -amt / 2 : amt / 2;
  }
  if (now < btlHitUntil[who]) {
    uint32_t left = btlHitUntil[who] - now;
    ox += ((left / 50) % 2) ? 5 : -5;      // jitter
  }
  // Real PMD playback when the sprite streamed: attack while lunging, hurt
  // while flinching, idle otherwise. `has()` guards every one, because not
  // every species ships every action -- falling through to idle, and to the
  // flat thumbnail if the sprite is missing entirely (no SD).
  bool angryLoop = who == 1 && c.angry;
  uint8_t act = PMD_IDLE;
  bool loop = true;
  uint32_t t = now;
  if (now < btlHitUntil[who]) {
    act = PMD_HURT; loop = false; t = now - (btlHitUntil[who] - BTL_HIT_MS);
  } else if (now < btlLungeUntil[who]) {
    act = PMD_ATTACK; loop = false; t = now - (btlLungeUntil[who] - BTL_LUNGE_MS);
  }
  if (btlPmd[who].loaded) {
    uint8_t facingAct = pmdFacingAction(act, who == 0);
    if (btlPmd[who].has(facingAct)) act = facingAct;
    else if (!btlPmd[who].has(act)) {
      act = pmdFacingAction(PMD_IDLE, who == 0);
      if (!btlPmd[who].has(act)) act = PMD_IDLE;
    }
  }
  if (angryLoop && act == PMD_IDLE)
    ox += ((int)(now / 90) % 3 - 1) * 3;
  if (now < btlHitUntil[who]) flash = ((btlHitUntil[who] - now) / 60) % 2 == 0;
  btlDrawSprite(sx + ox, sy + oy, c, who, now, act, loop, t, flash);
  if (angryLoop && act == PMD_IDLE) {
    int cx = sx + 24, top = sy + 18 - (int)((now / 120) % 3);
    gfx->drawLine(cx + 24, top + 10, cx + 31, top, UI_BAR_BAD);
    gfx->drawLine(cx + 31, top, cx + 34, top + 12, UI_BAR_BAD);
    gfx->drawLine(cx + 39, top + 12, cx + 42, top, UI_BAR_BAD);
    gfx->drawLine(cx + 42, top, cx + 49, top + 10, UI_BAR_BAD);
  }
}

// Bars drain rather than snap: a hit that removes half your health should be
// visible as it happens, not as a value that was already different.
static void btlEaseBars() {
  const uint16_t real[2] = { btlYou.hp, btlFoe.hp };
  for (int i = 0; i < 2; i++) {
    int diff = (int)real[i] - (int)btlHpShown[i];
    if (!diff) continue;
    int step = diff / 5;
    if (!step) step = diff > 0 ? 1 : -1;
    btlHpShown[i] = (uint16_t)((int)btlHpShown[i] + step);
  }
}

// The moment the ladder builds toward. It used to be one more line in the same
// message box as "It's super effective!", with the badge awarded silently.
void renderWin() {
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);

  gfx->setTextColor(UI_BAR_WARN);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(T(S_BTL_WIN)), 54);
  gfx->print(T(S_BTL_WIN));

  if (btlTrainer < 0) {
    const bool caught = !capturedMon.empty();
    if (caught) {
      gfx->fillRoundRect(83, 78, 300, 130, 12, UI_WHITE);
      const uint8_t *thumb = thumbs.get(capturedMon.dex);
      char name[48];
      snprintf(name, sizeof(name), "%s%s",
               rareMark(capturedMon.shiny || capturedMon.sparkle),
               speciesName(capturedMon.dex));
      gfx->setTextSize(2);
      uint8_t nameSize = gfx->textWidth(name) > 260 ? 1 : 2;
      char level[16];
      snprintf(level, sizeof(level), "Lv.%u", (unsigned)capturedMon.level);
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(1);
      gfx->setCursor(uiCenterX(T(S_CAUGHT)), 84);
      gfx->print(T(S_CAUGHT));
      if (thumb) drawThumb(thumb, CX - THUMB_CELL / 2, 105, 3, false);
      gfx->setTextColor(dexEntry(capturedMon.dex).accent);
      gfx->setTextSize(nameSize);
      int nameX = uiCenterX(name, CX - 10);
      gfx->setCursor(nameX, 166);
      gfx->print(name);
      drawGenderIcon(capturedMon.gender,
                     nameX + gfx->textWidth(name) + 4, 160, 1);
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(1);
      gfx->setCursor(uiCenterX(level), 190);
      gfx->print(level);
    }

    const int rewardTitleY = caught ? 214 : 98;
    const int rewardRowY = caught ? 238 : 132;
    const int rewardRowH = caught ? 26 : 40;
    const int rewardRowStep = caught ? 29 : 48;
    uint8_t rewardRows = btlRewardItemCount;
    for (uint8_t i = 0; i < 3; i++)
      if (btlRewardTraining[i]) rewardRows++;
    const int visibleRows = 5;
    const int viewportHeight = (visibleRows - 1) * rewardRowStep + rewardRowH;
    const int contentHeight = rewardRows
        ? (rewardRows - 1) * rewardRowStep + rewardRowH : 0;
    btlRewardScroll.configure(rewardRowY, viewportHeight, contentHeight,
                              rewardRowStep);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(T(S_REWARDS)), rewardTitleY);
    gfx->print(T(S_REWARDS));

    static const StrId STAT_NAMES[3] = { S_STAT_ATK, S_STAT_DEF, S_STAT_SPE };
    uint8_t row = 0;
    char line[64];
    for (uint8_t i = 0; i < 3; i++) {
      if (!btlRewardTraining[i]) continue;
      snprintf(line, sizeof(line), T(S_WIN_TRAINING_FMT), T(STAT_NAMES[i]),
               (unsigned)btlRewardTraining[i]);
      int y = btlRewardScroll.contentY(row * rewardRowStep);
      row++;
      if (!btlRewardScroll.fullyVisible(y, rewardRowH)) continue;
      gfx->fillRoundRect(83, y, 300, rewardRowH, 10, UI_WHITE);
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(2);
      uiDrawCenteredIn(line, 83, y, 300, rewardRowH);
    }
    for (uint8_t i = 0; i < btlRewardItemCount; i++) {
      const ItemEntry *item = itemByKey(btlRewardItems[i].key);
      char rewardName[64];
      itemRefName(btlRewardItems[i], rewardName, sizeof(rewardName));
      snprintf(line, sizeof(line), T(S_ITEM_FOUND_FMT), rewardName);
      int y = btlRewardScroll.contentY(row * rewardRowStep);
      row++;
      if (!btlRewardScroll.fullyVisible(y, rewardRowH)) continue;
      gfx->fillRoundRect(83, y, 300, rewardRowH, 10, UI_WHITE);
      if (item) drawItemIcon(*item, 99, y + rewardRowH / 2);
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setTextSize(1);
      uiDrawCenteredIn(line, 113, y, 260, rewardRowH);
    }
    if (btlRewardScroll.scrollable()) {
      const int trackX = 397;
      int thumbH = btlRewardScroll.thumbHeight(viewportHeight, 18);
      int thumbY = btlRewardScroll.thumbTop(rewardRowY, viewportHeight, 18);
      gfx->fillRoundRect(trackX, rewardRowY, 5, viewportHeight, 2, UI_MUTED);
      gfx->fillRoundRect(trackX, thumbY, 5, thumbH, 2, UI_BAR_WARN);
    }
    gfx->setTextColor(UI_MUTED);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(T(S_BACK)), 390);
    gfx->print(T(S_BACK));
    gfx->flush();
    return;
  }

  char l[40];
  snprintf(l, sizeof(l), T(S_BTL_BEAT), trainerName(btlRegion, btlTrainer));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(l), 96);
  gfx->print(l);

  // the badge, large, with the hard-mode halo if that is how it was won
  if (btlTrainer < regionBattleInfo(btlRegion).gymCount) {
    int by = 190;
    if (btlHard) {
      for (int r = 62; r >= 56; r--) gfx->drawCircle(CX, by, r, r % 2 ? 0xFEA0 : 0xFF60);
    }
    const BadgeArt &a = badgeArt(btlRegion, btlTrainer);
    for (int r = 0; a.idx && r < a.height; r++)
      for (int c = 0; c < a.width; c++) {
        uint8_t v = a.idx[r * a.width + c];
        if (v == 0xFF || v >= a.paletteCount) continue;
        // 3x, so it reads as a prize rather than a list entry
        gfx->fillRect(CX - a.width * 3 / 2 + c * 3, by - a.height * 3 / 2 + r * 3,
                      3, 3, a.pal[v]);
      }
    if (btlNewBadge) {
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(2);
      gfx->setCursor(uiCenterX(T(S_BTL_NEWBADGE)), 286);
      gfx->print(T(S_BTL_NEWBADGE));
    }
  }
  snprintf(l, sizeof(l), T(S_BADGES_FMT), player.badgeCountIn(btlRegion, btlHard));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(l), 316);
  gfx->print(l);

  // what this creature claimed beyond the player-wide badge
  if (btlIvReward == GYM_IV_GAINED) {
    static const StrId NAMES[4] = { S_STAT_ATK, S_STAT_DEF, S_STAT_SPE, S_STAT_VIT };
    snprintf(l, sizeof(l), T(S_WIN_TRAIN_FMT),
             T(NAMES[btlIvWhich % 4]), 1);
    gfx->setTextColor(UI_BAR_OK);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(l), 344);
    gfx->print(l);
  } else if (btlIvReward == GYM_IV_MAXED) {
    gfx->setTextColor(UI_MUTED);
    gfx->setTextSize(1);
    gfx->setCursor(uiCenterX(T(S_WIN_MAXED)), 348);
    gfx->print(T(S_WIN_MAXED));
  }

  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_BACK)), 380);
  gfx->print(T(S_BACK));
  gfx->flush();
}

static Combatant *btlMember(uint8_t index) {
  if (index >= btlSquadN) return nullptr;
  return index == btlSquadAt ? &btlYou : &btlSquad[index];
}

static bool btlWarehouseEffect(const ItemEntry &item) {
  if (item.effect == ITEM_EFFECT_BATTLE_MECHANIC) return true;
  if (btlLink) return false;
  return item.effect == ITEM_EFFECT_CATCH || item.effect == ITEM_EFFECT_HEAL_HP ||
         item.effect == ITEM_EFFECT_CURE_STATUS || item.effect == ITEM_EFFECT_REVIVE ||
         item.effect == ITEM_EFFECT_BATTLE_STAGE;
}

static uint8_t btlWarehouseCount() {
  uint8_t count = 0;
  for (uint16_t i = 0; i < inventory.stackCount(); i++) {
    const InventoryStack *stack = inventory.stackAt(i);
    const ItemEntry *item = stack ? itemByKey(stack->key) : nullptr;
    if (item && btlWarehouseEffect(*item)) count++;
  }
  return count;
}

static const InventoryStack *btlWarehouseAt(uint8_t index) {
  for (uint16_t i = 0; i < inventory.stackCount(); i++) {
    const InventoryStack *stack = inventory.stackAt(i);
    const ItemEntry *item = stack ? itemByKey(stack->key) : nullptr;
    if (!item || !btlWarehouseEffect(*item)) continue;
    if (!index--) return stack;
  }
  return nullptr;
}

static bool btlItemUsable(const ItemEntry &item) {
  BattleMechanic mechanic = btlMechanicFromItem(item);
  if (mechanic != BMECH_NONE)
    return battleMechanicAvailable(btlYourMechanics, btlYou, mechanic,
                                   MOVE_NONE, btlMegaFormFromItem(item));
  if (item.effect == ITEM_EFFECT_CATCH)
    return btlWild && !btlFoe.fainted();
  if (item.effect == ITEM_EFFECT_HEAL_HP || item.effect == ITEM_EFFECT_CURE_STATUS ||
      item.effect == ITEM_EFFECT_BATTLE_STAGE)
    return itemCanApplyToCombatant(item, btlYou);
  if (item.effect == ITEM_EFFECT_REVIVE)
    for (uint8_t i = 0; i < btlSquadN; i++) {
      Combatant *member = btlMember(i);
      if (member && itemCanApplyToCombatant(item, *member)) return true;
    }
  return false;
}

static bool btlFoeDetailHit(int16_t x, int16_t y) {
  return x >= 60 && x <= 406 && y >= 28 && y <= 150;
}

static void renderBattleFoeDetail() {
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  const DexEntry &entry = dexEntry(btlFoe.dex);

  if (btlFoeDetailPage == 0) {
    char head[64];
    snprintf(head, sizeof(head), T(S_NAME_FMT),
             rareMark(btlFoe.shiny),
             speciesName(btlFoe.dex), btlFoe.level);
    gfx->setTextColor(entry.accent);
    gfx->setTextSize(3);
    int titleSize = gfx->textWidth(head) <= 250 ? 3 : 2;
    gfx->setTextSize(titleSize);
    int titleX = uiCenterX(head, CX - 10);
    gfx->setCursor(titleX, titleSize == 3 ? 38 : 44);
    gfx->print(head);
    drawGenderIcon(btlFoe.gender,
                   titleX + gfx->textWidth(head) + 4,
                   titleSize == 3 ? 33 : 39, 1);

    char type[24];
    if (entry.type2 == T_NONE) snprintf(type, sizeof(type), "%s", typeName(entry.type1));
    else snprintf(type, sizeof(type), "%s/%s", typeName(entry.type1), typeName(entry.type2));
    gfx->setTextColor(entry.accent);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(type), 70);
    gfx->print(type);

    if (btlPmd[1].loaded)
      drawPmdActM(btlPmd[1], PMD_IDLE, CX, 198, millis(), true, false, 3, 0);
    else {
      const uint8_t *thumb = thumbs.get(btlFoe.dex);
      if (thumb) drawThumb(thumb, CX - 48, 98, 3, false);
    }

    const char *description = speciesDescription(btlFoe.dex, uiActiveLocaleCode());
    if (description && description[0]) {
      gfx->setTextColor(UI_INK);
      gfx->setTextSize((uint8_t)uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_TEXT_SIZE, 1));
      drawWrappedText(description, uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_X, 78),
                      220, uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_WIDTH, 310), 6);
    }
  } else if (btlFoeDetailPage == 1) {
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(3);
    gfx->setCursor(uiCenterX(T(S_STATS)), 36);
    gfx->print(T(S_STATS));

    char type[24];
    if (entry.type2 == T_NONE) snprintf(type, sizeof(type), "%s", typeName(entry.type1));
    else snprintf(type, sizeof(type), "%s/%s", typeName(entry.type1), typeName(entry.type2));
    gfx->setTextColor(entry.accent);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(type), 68);
    gfx->print(type);

    char nature[48];
    snprintf(nature, sizeof(nature), T(S_NATURE_FMT), natureName(btlWildMon.nature));
    gfx->setTextColor(UI_INK);
    gfx->setCursor(uiCenterX(nature), 94);
    gfx->print(nature);

    drawCardStat(122, T(S_STAT_ATK), btlFoe.base[SI_ATK], 800, UI_BAR_BAD, btlWildMon.ivAtk);
    drawCardStat(154, T(S_STAT_DEF), btlFoe.base[SI_DEF], 800, 0x4C98, btlWildMon.ivDef);
    drawCardStat(186, "Sp.A", btlFoe.base[SI_SPA], 800, UI_BAR_BAD, btlWildMon.ivAtk);
    drawCardStat(218, "Sp.D", btlFoe.base[SI_SPD], 800, 0x4C98, btlWildMon.ivDef);
    drawCardStat(250, T(S_STAT_SPE), btlFoe.base[SI_SPE], 800, UI_BAR_WARN, btlWildMon.ivSpe);
    drawCardStat(282, T(S_STAT_VIT), btlFoe.maxHp, 800, UI_BAR_OK, btlWildMon.ivHp);
  } else {
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(3);
    gfx->setCursor(uiCenterX(T(S_MOVES)), 44);
    gfx->print(T(S_MOVES));
    for (int i = 0; i < MOVE_SLOTS; i++)
      drawMoveRow(MOVE_ROW_Y(i), btlFoe.moves[i], false, btlFoe.dex);
  }

  for (uint8_t i = 0; i < BTL_FOE_DETAIL_PAGES; i++) {
    int x = CX - (BTL_FOE_DETAIL_PAGES - 1) * 13 + i * 26;
    if (i == btlFoeDetailPage) gfx->fillCircle(x, 374, 5, UI_INK);
    else gfx->drawCircle(x, 374, 4, UI_INK);
  }
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_DETAIL_BACK)), 398);
  gfx->print(T(S_DETAIL_BACK));
  gfx->flush();
}

uint8_t btlCaptureStageAt(uint32_t now) {
  if (!btlCaptureAnimating) return BTL_CAPTURE_NONE;
  uint32_t elapsed = now - btlCaptureStartedAt;
  if (elapsed < BTL_CAPTURE_CENTER_MS) return BTL_CAPTURE_CENTER;
  elapsed -= BTL_CAPTURE_CENTER_MS;
  if (elapsed < BTL_CAPTURE_THROW_MS) return BTL_CAPTURE_THROW;
  elapsed -= BTL_CAPTURE_THROW_MS;
  if (elapsed < BTL_CAPTURE_ABSORB_MS) return BTL_CAPTURE_ABSORB;
  elapsed -= BTL_CAPTURE_ABSORB_MS;
  if (elapsed < BTL_CAPTURE_SHAKE_MS) return BTL_CAPTURE_SHAKE;
  if (btlCaptureSuccess) return BTL_CAPTURE_SUCCESS;
  elapsed -= BTL_CAPTURE_SHAKE_MS;
  if (elapsed < BTL_CAPTURE_RESULT_MS) return BTL_CAPTURE_FAILURE;
  return BTL_CAPTURE_RETURN;
}

uint8_t btlCaptureFoeAct(uint8_t stage) {
  if (stage == BTL_CAPTURE_ABSORB) return PMD_HURT;
  if (stage == BTL_CAPTURE_FAILURE || stage == BTL_CAPTURE_RETURN) return PMD_ATTACK;
  return PMD_IDLE;
}

void btlUpdateCapture(uint32_t now) {
  if (!btlCaptureAnimating) return;
  uint32_t resultAt = BTL_CAPTURE_CENTER_MS + BTL_CAPTURE_THROW_MS +
                      BTL_CAPTURE_ABSORB_MS + BTL_CAPTURE_SHAKE_MS;
  uint32_t elapsed = now - btlCaptureStartedAt;
  if (elapsed >= resultAt && !btlCaptureCuePlayed) {
    btlCaptureCuePlayed = true;
    if (btlCaptureSuccess) {
      audioMusic(MUS_VICTORY);
      sfxPlay(SFX_VICTORY);
    } else {
      btlFoe.angry = true;
      sfxPlay(SFX_DENY);
    }
  }
  uint32_t finishAt = resultAt + BTL_CAPTURE_RESULT_MS;
  if (!btlCaptureSuccess) finishAt += BTL_CAPTURE_RETURN_MS;
  if (elapsed < finishAt) return;

  bool success = btlCaptureSuccess;
  ItemKey item = btlCaptureItem;
  btlCaptureAnimating = false;
  btlCaptureItem = ITEM_KEY_NONE;
  if (success) {
    btlCompleteCapture();
    return;
  }

  btlMenu = 0;
  btlSay("%s", itemName(item));
  btlResolve(0, 100);
}

static void renderBattleCapture(uint32_t now, uint8_t stage) {
  uint32_t elapsed = now - btlCaptureStartedAt;
  const int battleX = 300, battleY = 40;
  const int centerX = 209, centerY = 205;
  int foeX = centerX, foeY = centerY;
  bool drawFoe = stage == BTL_CAPTURE_CENTER || stage == BTL_CAPTURE_THROW ||
                 stage == BTL_CAPTURE_FAILURE || stage == BTL_CAPTURE_RETURN;
  if (stage == BTL_CAPTURE_CENTER) {
    uint32_t p = elapsed;
    foeX = battleX + (centerX - battleX) * (int32_t)p / (int32_t)BTL_CAPTURE_CENTER_MS;
    foeY = battleY + (centerY - battleY) * (int32_t)p / (int32_t)BTL_CAPTURE_CENTER_MS;
  } else if (stage == BTL_CAPTURE_RETURN) {
    uint32_t resultAt = BTL_CAPTURE_CENTER_MS + BTL_CAPTURE_THROW_MS +
                        BTL_CAPTURE_ABSORB_MS + BTL_CAPTURE_SHAKE_MS;
    uint32_t p = elapsed - resultAt - BTL_CAPTURE_RESULT_MS;
    if (p > BTL_CAPTURE_RETURN_MS) p = BTL_CAPTURE_RETURN_MS;
    foeX = centerX + (battleX - centerX) * (int32_t)p / (int32_t)BTL_CAPTURE_RETURN_MS;
    foeY = centerY + (battleY - centerY) * (int32_t)p / (int32_t)BTL_CAPTURE_RETURN_MS;
  } else if (stage == BTL_CAPTURE_ABSORB) {
    uint32_t p = elapsed - BTL_CAPTURE_CENTER_MS - BTL_CAPTURE_THROW_MS;
    drawFoe = p < BTL_CAPTURE_ABSORB_MS / 2;
  }

  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  drawBattleBack(65, 3);
  btlSyncSprite(1, btlFoe);
  if (drawFoe) {
    uint8_t act = btlCaptureFoeAct(stage);
    uint32_t actTime = now;
    bool loop = true;
    if (stage == BTL_CAPTURE_ABSORB) {
      actTime = elapsed - BTL_CAPTURE_CENTER_MS - BTL_CAPTURE_THROW_MS;
    } else if (stage == BTL_CAPTURE_FAILURE) {
      actTime = elapsed - BTL_CAPTURE_CENTER_MS - BTL_CAPTURE_THROW_MS -
                BTL_CAPTURE_ABSORB_MS - BTL_CAPTURE_SHAKE_MS;
      loop = false;
    }
    btlDrawSprite(foeX, foeY, btlFoe, 1, now, act, loop, actTime, false);
  }

  int bx = 233, by = 278;
  if (stage == BTL_CAPTURE_THROW) {
    uint32_t local = elapsed - BTL_CAPTURE_CENTER_MS;
    int32_t p = (int32_t)(local * 1000 / BTL_CAPTURE_THROW_MS);
    bx = 80 + (233 - 80) * p / 1000;
    by = 335 + (190 - 335) * p / 1000 -
         (int)(90LL * 4 * p * (1000 - p) / 1000000LL);
  } else if (stage == BTL_CAPTURE_ABSORB) {
    uint32_t p = elapsed - BTL_CAPTURE_CENTER_MS - BTL_CAPTURE_THROW_MS;
    by = 190 + (int)(p * 88 / BTL_CAPTURE_ABSORB_MS);
    int r = 14 + (int)(p * 42 / BTL_CAPTURE_ABSORB_MS);
    gfx->drawCircle(233, 200, r, UI_BAR_WARN);
    gfx->drawCircle(233, 200, r + 8, UI_WHITE);
  } else if (stage == BTL_CAPTURE_SHAKE) {
    uint32_t p = elapsed - BTL_CAPTURE_CENTER_MS - BTL_CAPTURE_THROW_MS -
                 BTL_CAPTURE_ABSORB_MS;
    static const int8_t SHAKE_X[6] = { 0, -9, 8, -6, 6, 0 };
    bx += SHAKE_X[(p / 90) % 6];
  } else if (stage == BTL_CAPTURE_SUCCESS) {
    int pulse = (int)((elapsed / 80) % 5);
    gfx->drawCircle(bx, by, 34 + pulse, UI_BAR_OK);
    for (int i = 0; i < 6; i++) {
      int sx = bx - 55 + i * 22;
      int sy = by - 50 + ((i * 19) % 34);
      gfx->drawLine(sx - 5, sy, sx + 5, sy, UI_BAR_WARN);
      gfx->drawLine(sx, sy - 5, sx, sy + 5, UI_BAR_WARN);
    }
  } else if (stage == BTL_CAPTURE_FAILURE) {
    by = 312;
  }

  bool drawBall = stage == BTL_CAPTURE_THROW || stage == BTL_CAPTURE_ABSORB ||
                  stage == BTL_CAPTURE_SHAKE || stage == BTL_CAPTURE_SUCCESS ||
                  stage == BTL_CAPTURE_FAILURE;
  const ItemEntry *ball = itemByKey(btlCaptureItem);
  if (drawBall && ball) drawItemIcon(*ball, bx, by, 2);
  if (stage == BTL_CAPTURE_SUCCESS) {
    gfx->drawLine(bx - 12, by + 2, bx - 3, by + 11, UI_BAR_OK);
    gfx->drawLine(bx - 3, by + 11, bx + 14, by - 7, UI_BAR_OK);
  } else if (stage == BTL_CAPTURE_FAILURE) {
    gfx->drawLine(bx - 34, by - 34, bx + 34, by + 34, UI_BAR_BAD);
    gfx->drawLine(bx + 34, by - 34, bx - 34, by + 34, UI_BAR_BAD);
  }
  gfx->flush();
}

void renderBattle() {
  if (btlWinUntil) { renderWin(); return; }
  if (btlFoeDetailOpen) { renderBattleFoeDetail(); return; }
  uint32_t now = millis();
  btlUpdateCapture(now);
  if (btlWinUntil) { renderWin(); return; }
  uint8_t captureStage = btlCaptureStageAt(now);
  if (captureStage != BTL_CAPTURE_NONE) {
    renderBattleCapture(now, captureStage);
    return;
  }
  btlEaseBars();
  // The previous frame can still be crossing the CO5300 DMA boundary here.
  // Keep it valid while repainting instead of exposing a full black clear.
  drawBattleBack(0, 2, 254);
  drawBattleFieldEffects(now);
  // the lower band stays flat so the move grid and the HP text keep their
  // contrast against it
  gfx->fillRect(0, 254, 466, 212, UI_BG_DAY);
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);

  // x=82 not 58: at y=60 the round bezel starts around x=77, and a longer
  // name like BLASTOISE was losing its first characters off the edge
  btlSide(82, 82, 300, 40, btlFoe, 1);    // foe reads top-left, sprite top-right
  btlSide(250, 190, 76, 168, btlYou, 0);  // you read bottom-right, sprite bottom-left
  drawBattleSideLayers(1, 324, 118);
  drawBattleSideLayers(0, 100, 246);
  drawBattleFieldHud();

  // Waiting on the other device. Without this the screen is identical to the
  // one where it is your turn, so a tap that has been sent and a tap that was
  // never registered look exactly the same.
  bool lanWait = btlLink && !btlOver &&
                 (btlLinkHost ? (btlMyAct && !lan.hasPeerAct())
                              : (lan.state == LINK_WAITING));
  if (lanWait && !btlMsgCount) {
    gfx->fillRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_CELL_H * 2 + 8, 12, UI_WHITE);
    gfx->drawRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_CELL_H * 2 + 8, 12, UI_INK);
    gfx->setTextColor(UI_MUTED);
    gfx->setTextSize(1);
    const char *w = T(S_LAN_WAITFOE);
    uiDrawCenteredIn(w, BTL_GRID_X, BTL_GRID_Y, 328, BTL_CELL_H * 2 + 8);
  } else if (btlThrowArmed) {
    gfx->fillRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_CELL_H * 2 + 8, 12, UI_WHITE);
    gfx->drawRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_CELL_H * 2 + 8, 12, UI_INK);
    const ItemEntry *ball = itemByKey(btlThrowItem);
    if (ball) drawItemIcon(*ball, BTL_GRID_X + 42, BTL_GRID_Y + 39, 2);
    gfx->setTextSize(1);
    gfx->setTextColor(UI_INK);
    uiDrawCenteredIn(T(S_BTL_THROW_PROMPT), BTL_GRID_X + 80, BTL_GRID_Y + 15,
                     232, 20);
    gfx->setTextColor(UI_MUTED);
    uiDrawCenteredIn(T(S_BTL_THROW_CANCEL), BTL_GRID_X + 80, BTL_GRID_Y + 42,
                     232, 18);
    uint32_t elapsed = millis() - btlThrowStartedAt;
    uint16_t remaining = elapsed < BTL_THROW_TIMEOUT_MS
        ? (uint16_t)((BTL_THROW_TIMEOUT_MS - elapsed) * 220UL / BTL_THROW_TIMEOUT_MS)
        : 0;
    gfx->fillRoundRect(BTL_GRID_X + 86, BTL_GRID_Y + 67, 220, 5, 2, UI_TRACK);
    if (remaining)
      gfx->fillRoundRect(BTL_GRID_X + 86, BTL_GRID_Y + 67, remaining, 5, 2, UI_BAR_OK);
  } else if (btlMsgCount) {            // narration takes over the menu area
    gfx->fillRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_CELL_H * 2 + 8, 12, UI_WHITE);
    gfx->drawRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_CELL_H * 2 + 8, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    uint8_t lines = btlMsgCount < 4 ? btlMsgCount : 4;
    int lineH = 16;
    int top = BTL_GRID_Y + 8 + (64 - lines * lineH) / 2;
    for (uint8_t i = 0; i < lines; i++)
      uiDrawCenteredIn(btlMsg[i], BTL_GRID_X + 8, top + i * lineH, 312, lineH);
    gfx->setTextColor(UI_MUTED);
    uiDrawCenteredIn("tap...", BTL_GRID_X, BTL_GRID_Y + 76, 328, 16);
  } else if (btlMenu == 0) {
    const char *actions[4] = { T(S_FIGHT), T(S_BAG), T(S_BTL_SWITCH), T(S_BTL_RUN) };
    for (int i = 0; i < 4; i++) {
      int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, UI_INK);
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      uiDrawCenteredIn(actions[i], x, y, BTL_CELL_W, BTL_CELL_H);
    }
  } else if (btlMenu == 3) {
    drawBtlBack();
    uint8_t count = btlWarehouseCount();
    uint8_t pages = count ? (uint8_t)((count + 3) / 4) : 1;
    if (btlItemPage >= pages) btlItemPage = pages - 1;
    for (uint8_t i = 0; i < 4; i++) {
      const InventoryStack *stack = btlWarehouseAt((uint8_t)(btlItemPage * 4 + i));
      if (!stack) break;
      const ItemEntry *item = itemByKey(stack->key);
      bool usable = item && btlItemUsable(*item);
      int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_INK : 0x8410);
      drawItemIcon(*item, x + 22, y + BTL_CELL_H / 2, 1, !usable);
      gfx->setTextColor(usable ? UI_INK : 0x8410);
      gfx->setTextSize(1);
      uiDrawCenteredIn(itemName(item->key), x + 42, y + 3, BTL_CELL_W - 48, 18);
      char amount[8];
      snprintf(amount, sizeof(amount), "x%u", stack->count);
      uiDrawCenteredIn(amount, x + 42, y + 23, BTL_CELL_W - 48, 16);
    }
    if (pages > 1)
      for (uint8_t page = 0; page < pages; page++) {
        int px = CX - (pages - 1) * 8 + page * 16;
        if (page == btlItemPage) gfx->fillCircle(px, 376, 3, UI_INK);
        else gfx->drawCircle(px, 376, 3, UI_TRACK);
      }
  } else if (btlMenu == 4) {
    drawBtlBack();
    const ItemEntry *item = itemByKey(btlPendingItem);
    for (uint8_t cell = 0; item && cell < 4; cell++) {
      uint8_t i = (uint8_t)(btlTargetPage * 4 + cell);
      if (i >= btlSquadN) break;
      Combatant *member = btlMember(i);
      bool usable = member && itemCanApplyToCombatant(*item, *member);
      int x = BTL_CELL_X(cell), y = BTL_CELL_Y(cell);
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_INK : 0x8410);
      gfx->setTextColor(usable ? UI_INK : 0x8410);
      gfx->setTextSize(1);
      uiDrawCenteredIn(member ? displayCombatantName(*member) : "-",
                       x + 6, y + 3, BTL_CELL_W - 12, 18);
      char hp[20];
      snprintf(hp, sizeof(hp), "%u/%u", member ? member->hp : 0, member ? member->maxHp : 0);
      uiDrawCenteredIn(hp, x + 6, y + 23, BTL_CELL_W - 12, 16);
    }
  } else if (btlMenu == 2) {
    drawBtlBack();
    // who to bring on instead; the current one and anything fainted is inert
    for (uint8_t cell = 0; cell < 4; cell++) {
      uint8_t i = (uint8_t)(btlTargetPage * 4 + cell);
      if (i >= btlSquadN) break;
      int x = BTL_CELL_X(cell), y = BTL_CELL_Y(cell);
      const Combatant &m = (i == btlSquadAt) ? btlYou : btlSquad[i];
      bool usable = (i != btlSquadAt) && !m.fainted();
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_INK : 0x8410);
      gfx->setTextColor(usable ? UI_INK : 0x8410);
      gfx->setTextSize(1);
      uiDrawCenteredIn(displayCombatantName(m), x + 6, y + 3, BTL_CELL_W - 12, 18);
      char hp[20];
      snprintf(hp, sizeof(hp), "%u/%u", m.hp, m.maxHp);
      uiDrawCenteredIn(hp, x + 6, y + 23, BTL_CELL_W - 12, 16);
    }
  } else {
    drawBtlBack();
    if (btlPendingMechanic != BMECH_NONE) {
      const ItemEntry *armed = itemByKey(btlPendingItem);
      const char *label = armed ? itemName(armed->key) : "";
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setTextSize(1);
      if (armed) drawItemIcon(*armed, BTL_GRID_X + 20, 263);
      uiDrawCenteredIn(label, BTL_GRID_X + 36, 254, 292, 18);
    }
    for (int i = 0; i < MOVE_SLOTS; i++) {
      int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
      MoveId mv = btlYou.moves[i];
      bool usable = mv && (btlPendingMechanic != BMECH_Z_MOVE ||
                           moveEntry(mv).cat != MC_STATUS);
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10,
                         usable ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, UI_INK);
      if (!mv) continue;
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(1);
      char moveLabel[64];
      const char *shown = (btlPendingMechanic == BMECH_Z_MOVE ||
                           btlYou.activeMechanic == BMECH_DYNAMAX)
                              ? typeName(moveEntry(mv).type) : moveName(mv);
      snprintf(moveLabel, sizeof(moveLabel), "%s%s",
               btlPendingMechanic == BMECH_Z_MOVE ? "Z-"
               : btlYou.activeMechanic == BMECH_DYNAMAX ? "MAX " : "",
               shown);
      int16_t left, top, right, bottom;
      if (gfx->textInkBounds(moveLabel, &left, &top, &right, &bottom))
        gfx->setCursor(x + 10 - left, y + 5 - top);
      else
        gfx->setCursor(x + 10, y + 5);
      gfx->print(moveLabel);

      // Match the move list's information order: name at top-left, type below,
      // and the effective power (including Z/Max conversion) on the right.
      BattleMove displayed = battleMoveFor(btlYou, mv, btlPendingMechanic);
      drawTypeChip(x + 10, y + 24, displayed.entry.type);
      bool stab = (btlYou.type1 == displayed.entry.type ||
                   btlYou.type2 == displayed.entry.type) &&
                  displayed.entry.cat != MC_STATUS;
      char power[24];
      if (displayed.entry.cat == MC_STATUS)
        snprintf(power, sizeof(power), "%s", T(S_MOVE_STATUS));
      else
        snprintf(power, sizeof(power), T(S_MOVE_PWR), displayed.entry.power);
      gfx->setTextColor(stab ? dexEntry(btlYou.dex).accent : UI_INK);
      gfx->setTextSize(1);
      if (gfx->textInkBounds(power, &left, &top, &right, &bottom))
        gfx->setCursor(x + BTL_CELL_W - 10 - (right - left) - left,
                       y + (BTL_CELL_H - (bottom - top)) / 2 - top);
      else
        gfx->setCursor(uiRightX(power, x + BTL_CELL_W - 10), y + 16);
      gfx->print(power);
    }
  }
  gfx->flush();
}

// Brings on the flagged replacement and starts its entrance.
static void btlDoSwap() {
  int8_t finishedWho = btlSwapWho;
  if (btlSwapWho == 1) {
    uint8_t nxt = 0;
    while (nxt < btlFoeSquadN &&
           (nxt == btlFoeAt || btlFoeSquad[nxt].fainted())) nxt++;
    if (nxt >= btlFoeSquadN) { btlSwapWho = -1; return; }
    btlReplaceActive(1, nxt, true);
  } else if (btlSwapWho == 0) {
    uint8_t nxt = 0;
    while (nxt < btlSquadN &&
           (nxt == btlSquadAt || btlSquad[nxt].fainted())) nxt++;
    if (nxt >= btlSquadN) { btlSwapWho = -1; return; }
    btlReplaceActive(0, nxt, true);
  }
  if (finishedWho == 0) btlSwapPending &= (uint8_t)~0x01;
  else if (finishedWho == 1) btlSwapPending &= (uint8_t)~0x02;
  btlSwapWho = (btlSwapPending & 0x02) ? 1
             : (btlSwapPending & 0x01) ? 0 : -1;
}

// Switching spends your turn: the opponent still acts. That is what stops it
// being a free look at the matchup every round.
static void btlSwitchTo(uint8_t i) {
  if (i >= btlSquadN || i == btlSquadAt) return;
  if (!battleCanSwitch(btlYou, btlFoe)) { sfxPlay(SFX_DENY); return; }
  if (!btlReplaceActive(0, i, true)) return;
  btlMenu = 0;
  btlResolve(0, 100);     // move 0 = no attack, so only the foe acts
}

void commitBattleMove(uint8_t moveSlot, uint8_t percent) {
  if (!battleOpen || btlOver || moveSlot >= MOVE_SLOTS || !btlYou.moves[moveSlot]) return;
  MoveId move = btlYou.moves[moveSlot];
  BattleMechanic mechanic = btlPendingMechanic;
  MegaFormKind megaForm = btlPendingMegaForm;
  btlPendingMechanic = BMECH_NONE;
  btlPendingMegaForm = MEGA_FORM_NONE;
  ItemKey armedKey = btlPendingItem;
  btlPendingItem = ITEM_KEY_NONE;
  if (mechanic != BMECH_NONE) {
    const ItemEntry *item = itemByKey(armedKey);
    uint16_t oldMaxHp = btlYou.maxHp;
    Combatant activated = btlYou;
    BattleSideMechanics activatedSide = btlYourMechanics;
    if (!item || !battleActivateMechanic(activatedSide, activated, mechanic, move,
                                         megaForm) ||
        !inventory.consume(item->key)) {
      sfxPlay(SFX_DENY);
      return;
    }
    btlYou = activated;
    btlYourMechanics = activatedSide;
    btlScaleShownHp(0, oldMaxHp, btlYou.maxHp);
    btlSay("%s: %s", displayCombatantName(btlYou), itemName(item->key));
  }
  uint8_t act = LINK_ACT_MOVE(moveSlot);
  if (btlLink && !btlLinkHost) {
    lan.sendAct(act, percent, mechanic, megaForm);  // the guest asks; the host decides
    return;
  }
  if (btlLink) {
    btlMyAct = act;
    btlMyPercent = percent;
    btlMyMechanic = mechanic;
    if (!lan.hasPeerAct()) return;     // resolved by btlLinkPoll when it lands
    btlMyAct = 0;
    btlMyPercent = 0;
    btlMyMechanic = BMECH_NONE;
  }
  btlResolve(move, percent, mechanic);
}

int btlCellIndexAt(int16_t x, int16_t y) {
  for (int i = 0; i < 4; i++)
    if (btlCellHit(i, x, y)) return i;
  return -1;
}

// The way out of the move and switch screens. Without it the only exits were
// choosing something or leaving the fight entirely.
static void drawBtlBack() {
  gfx->fillRoundRect(BTL_BACK_X, BTL_BACK_Y, BTL_BACK_W, BTL_BACK_H, 11, UI_TRACK);
  gfx->drawRoundRect(BTL_BACK_X, BTL_BACK_Y, BTL_BACK_W, BTL_BACK_H, 11, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  uiDrawCenteredIn(T(S_BACK), BTL_BACK_X, BTL_BACK_Y, BTL_BACK_W, BTL_BACK_H);
}

static bool btlBackTap(int16_t x, int16_t y) {
  if (x < BTL_BACK_X || x > BTL_BACK_X + BTL_BACK_W ||
      y < BTL_BACK_Y || y > BTL_BACK_Y + BTL_BACK_H) return false;
  btlMenu = 0;
  btlPendingMechanic = BMECH_NONE;
  btlPendingMegaForm = MEGA_FORM_NONE;
  btlPendingItem = ITEM_KEY_NONE;
  sfxPlay(SFX_TAP);
  return true;
}

// The roll is supplied by the caller so the policy boundary can be tested
// without coupling a regression test to the global PRNG sequence.
bool btlAttemptRun(uint8_t roll) {
  // In a linked fight RUN is the existing deliberate disconnect/forfeit, not a
  // simulated wild escape. Applying a local failed roll would desynchronise the
  // two authoritative battle copies.
  if (btlLink || (btlWild && battleGuaranteedEscape(btlYou)) ||
      roll < wildEscapeChance(btlYou.level, btlFoe.level)) {
    sfxPlay(SFX_TAP);
    btlFreeSprites();
    audioMusic(MUS_NONE);
    if (btlLink) { lanLeave(); btlLink = false; lanOpen = true; }
    battleOpen = false;
    btlWild = false;
    btlMenu = 0;
    return true;
  }

  sfxPlay(SFX_DENY);
  btlMenu = 0;
  btlMsgCount = 0;
  btlSay("%s", T(S_BTL_RUN_FAILED));
  btlResolve(MOVE_NONE, 100, BMECH_NONE);
  return false;
}

static void btlRun() {
  btlAttemptRun((uint8_t)random(100));
}

static void btlSpendItemTurn(const ItemEntry &item, uint8_t targetIndex) {
  Combatant *target = btlMember(targetIndex);
  if (!target || !itemApplyToCombatant(item, *target) ||
      !inventory.consume(item.key)) {
    sfxPlay(SFX_DENY);
    return;
  }
  if (item.effect == ITEM_EFFECT_REVIVE)
    btlSetPersistentDead(targetIndex, false);
  sfxPlay(SFX_TAP);
  btlMenu = 0;
  btlPendingItem = ITEM_KEY_NONE;
  btlResolve(0, 100);
}

void btlCompleteCapture() {
  capturedMon = btlWildMon;
  pet.registerCaught(capturedMon.dex, capturedMon.shiny);
  btlResetRewardSummary();
  btlGrantWildRewards();
  if (party.store(capturedMon) == PARTY_STORE_FULL) {
    partyPending = capturedMon;
    partyPick = true;
    boxOpen = true;
    boxSel = 0;
  }
  battleOpen = false;
  btlOver = true;
  btlWon = true;
  btlWinUntil = millis() + 60000;
  btlFreeSprites();
  audioMusic(MUS_VICTORY);
  sfxPlay(SFX_VICTORY);
}

bool btlStartCapture(const ItemEntry &item, uint8_t roll, uint32_t now) {
  btlResetThrow();
  if (!battleOpen || btlOver || btlCaptureAnimating || !btlWild ||
      item.effect != ITEM_EFFECT_CATCH || !inventory.consume(item.key)) {
    sfxPlay(SFX_DENY);
    return false;
  }
  uint8_t chance = wildCaptureChance(dexEntry(btlFoe.dex).rarity, btlFoe.hp,
                                     btlFoe.maxHp,
                                     btlFoe.ailment != AIL_NONE || btlFoe.confuseTurns,
                                     item.param);
  btlCaptureSuccess = roll < chance;
  btlCaptureAnimating = true;
  btlCaptureCuePlayed = false;
  btlCaptureStartedAt = now;
  btlCaptureItem = item.key;
  btlMenu = 0;
  btlMsgCount = 0;
  sfxPlay(SFX_PLAY);
  return true;
}

static void btlThrowBall(const ItemEntry &item) {
  uint32_t now = millis();
  if (!motionStart()) {
    btlStartCapture(item, (uint8_t)random(100), now);
    return;
  }
  btlThrowArmed = true;
  btlThrowStartedAt = now;
  btlThrowItem = item.key;
  btlThrowDetector.arm(now);
  btlMenu = 0;
  sfxPlay(SFX_TAP);
}

static void btlCancelThrow(bool timedOut) {
  btlResetThrow();
  btlMenu = 3;
  sfxPlay(timedOut ? SFX_DENY : SFX_TAP);
}

bool btlFeedThrowSample(const MotionSample &sample) {
  if (!btlThrowArmed || !btlThrowDetector.update(sample)) return false;
  const ItemEntry *item = itemByKey(btlThrowItem);
  if (!item) {
    btlCancelThrow(true);
    return false;
  }
  return btlStartCapture(*item, (uint8_t)random(100), sample.at);
}

void btlUpdateThrow(uint32_t now) {
  if (!btlThrowArmed) return;
  if (now - btlThrowStartedAt >= BTL_THROW_TIMEOUT_MS) {
    btlCancelThrow(true);
    return;
  }
  MotionSample sample;
  if (motionRead(sample, now)) btlFeedThrowSample(sample);
}

static void btlDismissWin() {
  if (!capturedMon.empty() && !partyPick) {
    snprintf(partyBannerName, sizeof(partyBannerName), "%s",
             displaySpeciesName(capturedMon.dex, capturedMon.nick));
    partyBannerUntil = millis() + 3500;
  }
  capturedMon = PartyMon();
  btlWinUntil = 0;
  btlFreeSprites();
  audioMusic(MUS_NONE);
  battleOpen = false;
  btlWild = false;
  if (btlLink) { btlLink = false; lanOpen = true; }
}

static bool btlDispatchTap(int16_t x, int16_t y) {
  if (btlCaptureAnimating) return false;
  if (btlThrowArmed) { btlCancelThrow(false); return true; }
  if (btlWinUntil) {          // dismiss the win screen and leave the fight
    btlDismissWin();
    return true;
  }
  if (btlFoeDetailOpen) {
    if (y >= 370) {
      btlFoeDetailOpen = false;
      sfxPlay(SFX_TAP);
    }
    return y >= 370;
  }
  if (btlWild && !btlOver && btlFoeDetailHit(x, y)) {
    btlFoeDetailOpen = true;
    btlFoeDetailPage = 0;
    sfxPlay(SFX_TAP);
    return true;
  }
  if (btlMsgCount) {          // a tap clears the narration and returns the menu
    btlMsgCount = 0;
    if (btlOver) {
      btlFreeSprites();
      battleOpen = false;
      btlWild = false;
      // Back to the LAN screen rather than all the way out: that is where a
      // rematch is offered, and re-pairing for every fight would be tedious.
      if (btlLink) { btlLink = false; lanOpen = true; }
      return true;
    }
    if (btlSwapWho >= 0) btlDoSwap();   // the replacement arrives on this beat
    return true;
  }
  if (btlMenu == 0) {
    if (btlCellHit(0, x, y)) {
      sfxPlay(SFX_TAP);
      btlMenu = 1;                       // FIGHT
      return true;
    }
    if (btlCellHit(1, x, y)) {
      sfxPlay(SFX_TAP);
      btlItemPage = 0;
      btlMenu = 3;
      return true;
    }
    if (btlCellHit(2, x, y)) {
      sfxPlay(SFX_TAP); btlTargetPage = 0; btlMenu = 2; return true;
    }
    if (btlCellHit(3, x, y)) { btlRun(); return true; }
    return false;
  }
  if (btlMenu == 3) {
    if (btlBackTap(x, y)) return true;
    for (uint8_t i = 0; i < 4; i++) {
      if (!btlCellHit(i, x, y)) continue;
      const InventoryStack *stack = btlWarehouseAt((uint8_t)(btlItemPage * 4 + i));
      const ItemEntry *item = stack ? itemByKey(stack->key) : nullptr;
      if (!item || !btlItemUsable(*item)) { sfxPlay(SFX_DENY); return true; }
      BattleMechanic mechanic = btlMechanicFromItem(*item);
      if (mechanic != BMECH_NONE) {
        btlPendingMechanic = mechanic;
        btlPendingMegaForm = btlMegaFormFromItem(*item);
        btlPendingItem = item->key;
        btlMenu = 1;
        sfxPlay(SFX_TAP);
        return true;
      }
      if (item->effect == ITEM_EFFECT_CATCH) { btlThrowBall(*item); return true; }
      if (item->effect == ITEM_EFFECT_REVIVE) {
        btlPendingItem = item->key;
        btlTargetPage = 0;
        btlMenu = 4;
        sfxPlay(SFX_TAP);
        return true;
      }
      btlSpendItemTurn(*item, btlSquadAt);
      return true;
    }
    return false;
  }
  if (btlMenu == 4) {
    if (btlBackTap(x, y)) { btlPendingItem = ITEM_KEY_NONE; return true; }
    const ItemEntry *item = itemByKey(btlPendingItem);
    for (uint8_t cell = 0; item && cell < 4; cell++) {
      uint8_t i = (uint8_t)(btlTargetPage * 4 + cell);
      if (i >= btlSquadN) break;
      if (!btlCellHit(cell, x, y)) continue;
      Combatant *member = btlMember(i);
      if (!member || !itemCanApplyToCombatant(*item, *member)) {
        sfxPlay(SFX_DENY);
        return true;
      }
      btlSpendItemTurn(*item, i);
      return true;
    }
    return false;
  }
  if (btlMenu == 2) {
    if (btlBackTap(x, y)) return true;
    for (uint8_t cell = 0; cell < 4; cell++) {
      uint8_t i = (uint8_t)(btlTargetPage * 4 + cell);
      if (i >= btlSquadN) break;
      if (!btlCellHit(cell, x, y)) continue;
      const Combatant &m = (i == btlSquadAt) ? btlYou : btlSquad[i];
      if (i == btlSquadAt || m.fainted()) { sfxPlay(SFX_DENY); return true; }
      sfxPlay(SFX_TAP);
      if (btlLink && !btlLinkHost) {
        // The guest asks; it never switches on its own. A switch rides the same
        // message as a move, so the host spends the turn on it exactly as it
        // would for us.
        lan.sendAct(LINK_ACT_SWITCH_TO(i));
        btlMenu = 0;
        return true;
      }
      if (btlLink) {            // host: latched like a move, see btlLinkPoll
        btlMyAct = LINK_ACT_SWITCH_TO(i);
        btlMyPercent = 100;
        btlMenu = 0;
        if (!lan.hasPeerAct()) return true;
        btlMyAct = 0;
        btlMyPercent = 0;
      }
      btlSwitchTo(i);
      return true;
    }
    btlMenu = 0;      // anywhere else backs out
    return true;
  }
  if (btlBackTap(x, y)) return true;
  for (int i = 0; i < MOVE_SLOTS; i++) {
    if (!btlYou.moves[i]) continue;
    if (!btlCellHit(i, x, y)) continue;
    if (btlPendingMechanic == BMECH_Z_MOVE &&
        moveEntry(btlYou.moves[i]).cat == MC_STATUS) {
      sfxPlay(SFX_DENY);
      return true;
    }
    sfxPlay(SFX_TAP);
    btlMenu = 0;
    beginBattleQuiz((uint8_t)i);
    return true;
  }
  btlMenu = 0;        // a tap off the grid goes back to FIGHT/POKEMON
  btlPendingMechanic = BMECH_NONE;
  btlPendingMegaForm = MEGA_FORM_NONE;
  btlPendingItem = ITEM_KEY_NONE;
  return true;
}

void battleTap(int16_t x, int16_t y) {
  uint32_t now = millis();
  if (btlTapDebounceArmed && now - btlLastAcceptedTap < BTL_TAP_DEBOUNCE_MS) return;
  if (!btlDispatchTap(x, y)) return;
  btlLastAcceptedTap = now;
  btlTapDebounceArmed = true;
}

// ---------- player card (swipe down) ----------
// Everything here is player-wide and outlives the creature: badges, the daily
// streak, the Pokedex and the party. No player sprite yet -- the SD carries
// PMD creature sprites only, so a trainer portrait needs new art.
// Page 1: who you are -- avatar, badges, totals. Tap the avatar to cycle it;
// the four sprites are bundled separately in avatars.h because SpriteCollab has no
// trainer art and ripped sprites would be unlicensed.
static void renderPlayerBadges() {
  // the player's name if they have set one, the generic title if not; either
  // way tapping it opens the keyboard
  const char *tn = player.trainerName[0] ? player.trainerName : T(S_TRAINER);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(tn), 40);
  gfx->print(tn);

  // Pages 1 and 2 are the other regions' ladders: name them, and drop the
  // avatar so the badges have the room. Only page 0 is "you".
  if (playerBadgeRegion != 0) {
    const char *rn = regionName(playerBadgeRegion);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(rn), 120);
    gfx->print(rn);
  } else if (gShowAllAvatars) {          // emulator only: every avatar at once
    for (uint8_t i = 0; i < AVATAR_COUNT; i++)
      drawAvatar(i, 60 + (i % 4) * 88, 60 + (i / 4) * 60, 3);
  } else {
    drawAvatar(player.avatar, CX - AVATAR_PX * 2, 72, 4);
    // One metadata line leaves the 16px CJK hint clear of the first badge row.
    const char *an = AVATARS[player.avatar % AVATAR_COUNT].name;
    char avatarMeta[48];
    snprintf(avatarMeta, sizeof(avatarMeta), "%s  %s", an, T(S_AVATAR_HINT));
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(uiCenterX(avatarMeta), 146);
    gfx->print(avatarMeta);
  }

  // The real badges, 2x4. Unearned ones draw as a faint outline so the shape
  // of what is missing is still visible.
  for (int i = 0; i < regionBattleInfo(playerBadgeRegion).gymCount; i++) {
    int bx = 140 + (i % 4) * 62, by = 188 + (i / 4) * 62;
    bool got = player.hasBadge(playerBadgeRegion, i, false);
    bool hard = player.hasBadge(playerBadgeRegion, i, true);
    if (hard) {
      // Beaten on hard: a golden halo. Concentric rings, not a filled disc --
      // a disc sat behind the art and read as a gold coin rather than a glow.
      gfx->drawCircle(bx, by, 25, 0xFDE0);
      gfx->drawCircle(bx, by, 24, 0xFEA0);
      gfx->drawCircle(bx, by, 23, 0xFF60);
      gfx->drawCircle(bx, by, 22, 0xFEA0);
      gfx->drawCircle(bx, by, 21, 0xFDE0);
    }
    if (!got) {
      gfx->drawCircle(bx, by, 20, UI_TRACK);
      continue;
    }
    const BadgeArt &a = badgeArt(playerBadgeRegion, i);
    for (int r = 0; a.idx && r < a.height; r++)
      for (int c = 0; c < a.width; c++) {
        uint8_t v = a.idx[r * a.width + c];
        if (v == 0xFF || v >= a.paletteCount) continue;
        gfx->fillRect(bx - a.width / 2 + c, by - a.height / 2 + r, 1, 1, a.pal[v]);
      }
  }
  char l[32];
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  snprintf(l, sizeof(l), T(S_STREAK_FMT), player.streak, player.bestStreak);
  gfx->setCursor(uiCenterX(l), 286);
  gfx->print(l);
  snprintf(l, sizeof(l), T(S_POKEDEX_FMT), player.registeredCount(), dexCount());
  gfx->setCursor(uiCenterX(l), 312);
  gfx->print(l);
  snprintf(l, sizeof(l), T(S_PARTY_FMT), party.count());
  gfx->setCursor(uiCenterX(l), 338);
  gfx->print(l);
}

// Page 2: the medals. They used to sit on the creature's card; they belong with
// the player, since totalMedals accumulates across every pet you raise.
static void renderPlayerMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[24];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(head), 44);
  gfx->print(head);

  for (int i = 0; i < MED_COUNT; i++) {
    int x = 46 + (i % 2) * 190, y = 96 + (i / 2) * 58;
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, 180, 48, 10, g ? UI_BAR_OK : UI_TRACK);
    gfx->drawRoundRect(x, y, 180, 48, 10, UI_INK);
    gfx->setTextColor(g ? UI_BG_DAY : UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterIn(medalLabel(i), x, 180), y + 6);
    gfx->print(medalLabel(i));
    gfx->setTextSize(1);
    gfx->setCursor(uiCenterIn(medalDesc(i), x, 180), y + 30);
    gfx->print(medalDesc(i));
  }
  char tot[28];
  snprintf(tot, sizeof(tot), T(S_MEDALS_TOTAL_FMT), player.totalMedals);
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(1);
  gfx->setCursor(uiCenterX(tot), 344);
  gfx->print(tot);
}

void renderPlayer() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (playerPage < regionAll()) renderPlayerBadges();
  else renderPlayerMedals();

  for (uint8_t i = 0; i < PLAYER_PAGES; i++) {
    int dx = CX - (PLAYER_PAGES - 1) * 13 + i * 26;
    if (i == playerPage) gfx->fillCircle(dx, 366, 5, UI_INK);
    else gfx->drawCircle(dx, 366, 4, UI_INK);
  }
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_BACK)), 392);
  gfx->print(T(S_BACK));
  gfx->flush();
}

// ---------- reaction test (trains SPEED) ----------
// The third training verb. The bag is a masher and the ball is a juggler, so
// this one is a reaction: a target appears somewhere on the panel and you tap
// it before it expires. The window shrinks as you go, which is what makes it
// read as speed rather than as endurance.
#define SPD_MS 15000UL       // session length
#define SPD_LIFE0 1100       // first target's window, ms
#define SPD_LIFE_MIN 380
#define SPD_R 46             // target radius

void spdSpawn() {
  // Keep the whole target inside the bezel: pick an angle and a radius that
  // leave SPD_R of margin, rather than a square that clips at the corners.
  int ang = random(360);
  int rad = random(150);
  float a = ang * 3.14159f / 180.0f;
  spdX = CX + (int)(cosf(a) * rad);
  spdY = CY + (int)(sinf(a) * rad);
  spdBorn = millis();
}

void startSpeedGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  spdOpen = true;
  spdUntil = millis() + SPD_MS;
  spdOverUntil = 0;
  spdHits = 0;
  spdMisses = 0;
  spdGain = 0;
  spdNewHi = false;
  spdSpawn();
}

static uint16_t spdLife() {
  int life = SPD_LIFE0 - spdHits * 32;
  return life < SPD_LIFE_MIN ? SPD_LIFE_MIN : life;
}

void spdTap(int16_t x, int16_t y) {
  if (spdOverUntil) return;
  int dx = x - spdX, dy = y - spdY;
  if (dx * dx + dy * dy <= (SPD_R + 14) * (SPD_R + 14)) {
    spdHits++;
    sfxPlay(SFX_TAP);
    spdSpawn();
  }
}

void renderSpeed() {
  uint32_t now = millis();
  drawGameScene();
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (spdOverUntil) {
    if (now > spdOverUntil) { spdOpen = false; return; }
    char b[24];
    snprintf(b, sizeof(b), T(S_SCORE_FMT), spdHits);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(uiCenterX(b), 150);
    gfx->print(b);
    char g[20];
    snprintf(g, sizeof(g), T(S_SPD_GAIN_FMT), spdGain);
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setTextSize(3);
    gfx->setCursor(uiCenterX(g), 210);
    gfx->print(g);
    gfx->setTextSize(2);
    if (spdNewHi && spdHits > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(uiCenterX(T(S_NEW_RECORD)), 256);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char r[20];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), player.spdHi);
      gfx->setTextColor(ink);
      gfx->setCursor(uiCenterX(r), 256);
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  // session over?
  if (now >= spdUntil) {
    spdNewHi = (spdHits > player.spdHi);
    beginCareQuiz({ CARE_ACTION_TRAIN_SPEED, spdHits }, true);
    gfx->flush();
    return;
  }
  // target expired?
  if (now - spdBorn > spdLife()) {
    spdMisses++;
    spdSpawn();
  }

  // the target, shrinking as its window runs out so the urgency is visible
  uint32_t age = now - spdBorn;
  int life = spdLife();
  int r = SPD_R - (int)((uint32_t)SPD_R * age / (life ? life : 1) / 2);
  if (r < 8) r = 8;
  gfx->fillCircle(spdX, spdY, r, UI_BAR_BAD);
  gfx->fillCircle(spdX, spdY, r * 2 / 3, UI_WHITE);
  gfx->fillCircle(spdX, spdY, r / 3, UI_BAR_BAD);

  char b[12];
  snprintf(b, sizeof(b), "%u", spdHits);
  gfx->setTextColor(ink);
  gfx->setTextSize(4);
  gfx->setCursor(uiCenterX(b), 30);
  gfx->print(b);
  // seconds left
  uint32_t left = (spdUntil > now) ? (spdUntil - now + 999) / 1000 : 0;
  snprintf(b, sizeof(b), "%us", (unsigned)left);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(b), 76);
  gfx->print(b);
  gfx->flush();
}

// ---------- team select ----------
// Which creatures come to this fight. It exists because hard mode caps your
// team to the leader's size, so the difference between a sweep and a wipe is
// bringing the right type -- and the squad used to be simply whoever sat first
// in the party.

// Candidate n is cultivation slot n; eggs and empty slots cannot battle.
bool pickExists(uint8_t n) {
  if (n >= PARTY_SLOTS) return false;
  if (n == party.activeIndex()) return party.slots[n].battleReady() && !pet.isEgg();
  return party.slots[n].battleReady();
}
bool pickUsable(uint8_t n) {
  if (!pickExists(n)) return false;
  return n == party.activeIndex() ? !pet.isDead() : !party.slots[n].dead();
}
uint8_t pickChosen() {
  uint8_t c = 0;
  for (uint8_t n = 0; n < PARTY_SLOTS; n++)
    if (pickUsable(n) && (squadMask & (1 << n))) c++;
  return c;
}
uint8_t pickCandidates() {
  uint8_t c = 0;
  for (uint8_t n = 0; n < PARTY_SLOTS; n++)
    if (pickExists(n)) c++;
  return c;
}
// Selects the lead first, then fills the remaining cap in cultivation-slot order.
void pickDefault(uint8_t cap) {
  squadMask = 0;
  uint8_t taken = 0;
  for (uint8_t order = 0; order < PARTY_SLOTS && taken < cap; order++) {
    uint8_t n = order == 0 ? party.leadIndex() : (uint8_t)(order - 1);
    if (order && n >= party.leadIndex()) n++;
    if (pickUsable(n)) { squadMask |= (1 << n); taken++; }
  }
}

static void drawPickCell(uint8_t n, int x, int y, uint8_t capLvl) {
  bool usable = pickUsable(n);
  bool on = usable && (squadMask & (1 << n)) != 0;
  int16_t dex; uint16_t lvl; const char *nm; bool rare;
  if (n == party.activeIndex()) {
    dex = pet.speciesId; lvl = pet.level(); rare = pet.shiny;
    nm = displaySpeciesName(dex, pet.nick);
  } else {
    const PartyMon &m = party.slots[n];
    dex = m.dex; lvl = m.level; rare = m.shiny || m.sparkle;
    nm = displaySpeciesName(dex, m.nick);
  }
  if (capLvl && lvl > capLvl) lvl = capLvl;   // show the level it will FIGHT at
  gfx->fillRoundRect(x, y, PICK_CELL_W, PICK_CELL_H, 10, on ? UI_BG_DAY : UI_TRACK);
  gfx->drawRoundRect(x, y, PICK_CELL_W, PICK_CELL_H, 10, on ? UI_INK : 0x8410);
  const uint8_t *th = thumbs.get(dex);
  if (th) drawThumb(th, x - 12, y - 6, 2, !on);
  gfx->setTextColor(on ? UI_INK : 0x8410);
  gfx->setTextSize(1);
  gfx->setCursor(x + 54, y + 14);
  gfx->print(nm);
  char l[16];
  snprintf(l, sizeof(l), "Lv.%u %s", (unsigned)lvl, rareMark(rare));
  gfx->setCursor(x + 54, y + 30);
  gfx->print(usable ? l : T(S_DEAD));
  // its typing is the whole reason you are on this screen
  const DexEntry &d = dexEntry(dex);
  gfx->setTextColor(on ? d.accent : 0x8410);
  gfx->setCursor(x + 54, y + 48);
  gfx->print(typeName(d.type1));
  if (d.type2 != T_NONE) {
    gfx->setCursor(x + 54, y + 60);
    gfx->print(typeName(d.type2));
  }
  if (on) {
    gfx->fillCircle(x + PICK_CELL_W - 16, y + 16, 9, UI_BAR_OK);
    gfx->setTextColor(UI_BG_DAY);
    gfx->setCursor(x + PICK_CELL_W - 19, y + 13);
    gfx->print("*");
  }
}

void renderPick() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  uint8_t cap = squadCap(pickTrainer, pickHard);
  uint8_t top = 0;          // the level cap shown on each cell; 0 = uncapped
  char head[40];
  if (pickTrainer == PICK_LAN) {
    snprintf(head, sizeof(head), "%s: %s", T(S_LAN),
             lanWantHost ? T(S_LAN_HOST) : T(S_LAN_JOIN));
  } else {
    const Trainer &t = trainerInfo(gymRegion, pickTrainer);
    for (int k = 0; k < t.count; k++)
      if (t.team[k].level > top) top = t.team[k].level;
    snprintf(head, sizeof(head), "%s  Lv.%u x%u",
             trainerName(gymRegion, pickTrainer), top, t.count);
  }
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(head), 44);
  gfx->print(head);
  char sub[28];
  snprintf(sub, sizeof(sub), T(S_PICK_FMT), pickChosen(), cap);
  gfx->setTextSize(1);
  gfx->setTextColor(pickChosen() > cap ? UI_BAR_BAD : UI_TRACK);
  gfx->setCursor(uiCenterX(sub), 68);
  gfx->print(sub);

  uint8_t seen = 0, drawn = 0;
  for (uint8_t n = 0; n < PARTY_SLOTS; n++) {
    if (!pickExists(n)) continue;
    if (seen++ < pickPage * PICK_PER_PAGE) continue;
    if (drawn >= PICK_PER_PAGE) break;
    drawPickCell(n, PICK_X(drawn), PICK_Y(drawn), top);
    drawn++;
  }
  uint8_t pages = (pickCandidates() + PICK_PER_PAGE - 1) / PICK_PER_PAGE;
  if (!pages) pages = 1;
  for (uint8_t i = 0; i < pages && pages > 1; i++) {
    int dx = CX - (pages - 1) * 13 + i * 26;
    if (i == pickPage) gfx->fillCircle(dx, 332, 5, UI_INK);
    else gfx->drawCircle(dx, 332, 4, UI_INK);
  }

  bool ok = pickChosen() > 0 && pickChosen() <= cap;
  gfx->fillRoundRect(PICK_BACK_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12, UI_TRACK);
  gfx->drawRoundRect(PICK_BACK_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  uiDrawCenteredIn(T(S_BACK), PICK_BACK_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H);
  gfx->fillRoundRect(PICK_GO_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12,
                     ok ? UI_BAR_OK : UI_TRACK);
  gfx->drawRoundRect(PICK_GO_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12, UI_INK);
  gfx->setTextColor(ok ? UI_BG_DAY : 0x8410);
  gfx->setTextSize(2);
  uiDrawCenteredIn(T(S_FIGHT), PICK_GO_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H);
  gfx->flush();
}

void pickTap(int16_t x, int16_t y) {
  if (y >= PICK_GO_Y && y <= PICK_GO_Y + PICK_BTN_H &&
      x >= PICK_BACK_X && x <= PICK_BACK_X + PICK_BTN_W) {   // BACK
    sfxPlay(SFX_TAP);
    pickOpen = false;
    if (pickTrainer == PICK_LAN) { lanOpen = true; }
    else { gymOpen = true; }
    return;
  }
  if (y >= PICK_GO_Y && y <= PICK_GO_Y + PICK_BTN_H &&
      x >= PICK_GO_X && x <= PICK_GO_X + PICK_BTN_W) {
    uint8_t cap = squadCap(pickTrainer, pickHard);
    if (pickChosen() == 0 || pickChosen() > cap) return;   // GO stays inert
    sfxPlay(SFX_TAP);
    pickOpen = false;
    if (pickTrainer == PICK_LAN) {
      // The squad is chosen BEFORE the radio comes up, so what gets offered to
      // the peer is what the player picked -- lanOffer() builds lan.mine from
      // squadMask, and the fight is then rebuilt from lan.mine rather than from
      // the party (see startLinkBattle).
      lanOffer(lanWantHost);
      lanOpen = true;
      return;
    }
    startTrainerBattle(pickTrainer, pickHard);
    return;
  }
  uint8_t seen = 0, drawn = 0;
  for (uint8_t n = 0; n < PARTY_SLOTS; n++) {
    if (!pickExists(n)) continue;
    if (seen++ < pickPage * PICK_PER_PAGE) continue;
    if (drawn >= PICK_PER_PAGE) break;
    int cx0 = PICK_X(drawn), cy0 = PICK_Y(drawn);
    drawn++;
    if (x < cx0 || x > cx0 + PICK_CELL_W || y < cy0 || y > cy0 + PICK_CELL_H) continue;
    if (!pickUsable(n)) { sfxPlay(SFX_DENY); return; }
    squadMask ^= (1 << n);
    sfxPlay(SFX_TAP);
    return;
  }
}

// ---------- LAN battle ----------
// Pairing on a touch-only screen: one device hosts, the other joins, and the
// protocol does the rest. There is no MAC entry because there is no keyboard
// worth typing one on.
void renderLan() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_LAN)), 44);
  gfx->print(T(S_LAN));

  const char *msg = T(S_LAN_PICK);
  switch (lan.state) {
    case LINK_HANDSHAKE:
    case LINK_LISTENING: msg = T(S_LAN_WAIT); break;
    case LINK_SQUADS:    msg = T(S_LAN_WAIT); break;
    case LINK_READY:     msg = T(S_LAN_READY); break;
    case LINK_REFUSED:   msg = T(S_LAN_REFUSED); break;
    case LINK_LOST:      msg = T(S_LAN_GONE); break;
    case LINK_DONE:      msg = lan.youWon ? T(S_BTL_WIN) : T(S_BTL_LOSE); break;
    default: break;
  }
  gfx->setTextColor((lan.state == LINK_REFUSED || lan.state == LINK_LOST)
                      ? UI_BAR_BAD : UI_TRACK);
  gfx->setTextSize(1);
  gfx->setCursor(uiCenterX(msg), 76);
  gfx->print(msg);

  if (lan.state == LINK_OFF || lan.state == LINK_REFUSED ||
      lan.state == LINK_LOST) {
    const char *lab[2] = { T(S_LAN_HOST), T(S_LAN_JOIN) };
    for (int i = 0; i < 2; i++) {
      int y = 120 + i * 70;
      gfx->fillRoundRect(90, y, 286, 56, 12, UI_BG_DAY);
      gfx->drawRoundRect(90, y, 286, 56, 12, UI_INK);
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      uiDrawCenteredIn(lab[i], 90, y, 286, 56);
    }
  } else if (lan.state == LINK_READY) {
    char l[40];
    if (lan.peerName[0]) {
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(uiCenterX(lan.peerName), 130);
      gfx->print(lan.peerName);
    }
    snprintf(l, sizeof(l), T(S_LAN_VS), lan.theirsN);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(l), 150);
    gfx->print(l);
    gfx->fillRoundRect(120, 220, 226, 56, 12, UI_BAR_OK);
    gfx->drawRoundRect(120, 220, 226, 56, 12, UI_INK);
    gfx->setTextColor(UI_BG_DAY);
    uiDrawCenteredIn(T(S_FIGHT), 120, 220, 226, 56);
  } else if (lan.state == LINK_DONE) {
    // Both squads are still in hand on both devices, so going again costs one
    // packet -- there is nothing to re-exchange.
    if (lan.peerName[0]) {
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(uiCenterX(lan.peerName), 140);
      gfx->print(lan.peerName);
    }
    gfx->fillRoundRect(120, 220, 226, 56, 12, UI_BG_DAY);
    gfx->drawRoundRect(120, 220, 226, 56, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    uiDrawCenteredIn(T(S_LAN_REMATCH), 120, 220, 226, 56);
  }
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_BACK)), 392);
  gfx->print(T(S_BACK));
  gfx->flush();
}

// Hands our chosen squad to the link, then announces.
// Leaving deliberately: tell the peer so it reports at once instead of sitting
// out the timeout, then free the radio -- it costs real current.
void lanLeave() {
  if (lan.live()) lan.sendBye();
  linkNowEnd();
  lan.state = LINK_OFF;
}

static void lanOffer(bool host) {
  // The squad is built BEFORE the radio is touched. What we advertise has to be
  // exactly what the player just chose in the picker, and that does not depend
  // on whether the radio comes up -- doing it the other way round meant a
  // failed radio skipped the squad entirely and left nothing to inspect.
  lan.begin(host, player.trainerName);
  snprintf(lan.peerName, sizeof(lan.peerName), "%s", player.trainerName);
  buildSquad(0, TRAINER_TEAM_MAX, squadMask);
  lanMineSourceN = btlSquadN;
  for (uint8_t i = 0; i < btlSquadN; i++) {
    lanMineSource[i] = btlSquadSource[i];
    LinkMon m;
    linkMonFrom(m, btlSquad[i]);
    lan.addMon(m);
  }
  if (!linkNowBegin(&lan)) {          // no radio: say so rather than hanging
    lan.state = LINK_REFUSED;
    return;
  }
  // BOTH sides announce. Which of them ends up hosting is settled by id inside
  // the hello, so the buttons are only a preference -- two players who both tap
  // HOST still get a working fight instead of two authorities, and two who both
  // tap JOIN still get one instead of mutual silence.
  lan.start();
}

void lanTap(int16_t x, int16_t y) {
  if (lan.state == LINK_OFF || lan.state == LINK_REFUSED ||
      lan.state == LINK_LOST) {
    for (int i = 0; i < 2; i++) {
      int by = 120 + i * 70;
      if (x < 90 || x > 376 || y < by || y > by + 56) continue;
      sfxPlay(SFX_TAP);
      lanWantHost = (i == 0);
      lanOpen = false;
      pickTrainer = PICK_LAN;
      pickHard = false;
      pickPage = 0;
      pickDefault(squadCap(PICK_LAN, false));
      pickOpen = true;
      return;
    }
  } else if (lan.state == LINK_READY) {
    if (x >= 120 && x <= 346 && y >= 220 && y <= 276) {
      sfxPlay(SFX_TAP);
      lanOpen = false;
      startLinkBattle();
      return;
    }
  } else if (lan.state == LINK_DONE) {
    if (x >= 120 && x <= 346 && y >= 220 && y <= 276) {
      sfxPlay(SFX_TAP);
      lan.sendRematch();      // both sides go back to READY and tap FIGHT
      return;
    }
  }
  if (y > 370) { lanLeave(); lanOpen = false; }   // back
}

// ---------- gym list ----------

void renderGyms() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  const char *title = T(S_BATTLE_CENTER);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(title), 42);
  gfx->print(title);
  // The badge count used to sit here, directly behind the difficulty pill. The
  // region chooser already shows it per region, which is where you are choosing
  // from, so it was both redundant and in the way.
  // difficulty pill: hard caps YOUR team to the leader's size and level, so it
  // is a different ladder with its own badges rather than a damage multiplier
  char dif[32];
  snprintf(dif, sizeof(dif), "%s %s", regionName(gymRegion),
           T(gymHard ? S_HARD : S_EASY));
  int dw = gfx->textWidth(dif) + 48;      // wider as well as taller
  if (dw < 120) dw = 120;
  gfx->fillRoundRect(CX - dw / 2, GYMDIF_Y, dw, GYMDIF_H, 12,
                     gymHard ? UI_BAR_BAD : UI_TRACK);
  gfx->setTextColor(gymHard ? UI_BG_DAY : UI_INK);
  gfx->setTextSize(2);
  uiDrawCenteredIn(dif, CX - dw / 2, GYMDIF_Y, dw, GYMDIF_H);

  for (int i = 0; i < GYM_ROWS; i++) {
    uint8_t entry = gymPage * GYM_ROWS + i;
    if (entry > regionBattleInfo(gymRegion).trainerCount) break;
    int y = GYM_ROW_Y(i);
    if (entry == 0) {
      gfx->fillRoundRect(70, y, 326, 44, 10, UI_BG_DAY);
      gfx->drawRoundRect(70, y, 326, 44, 10, UI_BAR_OK);
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(2);
      gfx->setCursor(84, y + 8);
      gfx->print(T(S_WILD));
      gfx->setTextColor(UI_MUTED);
      gfx->setTextSize(1);
      gfx->setCursor(84, y + 28);
      gfx->print(regionName(gymRegion));
      continue;
    }
    uint8_t idx = entry - 1;
    const Trainer &t = trainerInfo(gymRegion, idx);
    bool done = player.hasBadge(gymRegion, idx, gymHard);
    uint8_t ivReward = idx < regionBattleInfo(gymRegion).gymCount
                         ? pet.gymIvRewardAt(gymRegion, idx) : 0;
    bool open_ = gymUnlocked(idx, gymHard);
    gfx->fillRoundRect(70, y, 326, 44, 10, done ? UI_TRACK : UI_BG_DAY);
    gfx->drawRoundRect(70, y, 326, 44, 10, open_ ? UI_INK : UI_TRACK);
    gfx->setTextColor(open_ ? UI_INK : UI_TRACK);
    gfx->setTextSize(2);
    gfx->setCursor(84, y + 8);
    gfx->print(trainerName(gymRegion, idx));
    gfx->setTextSize(1);
    gfx->setTextColor(UI_MUTED);
    gfx->setCursor(84, y + 28);
    gfx->print(open_ ? trainerPlace(gymRegion, idx) : T(S_LOCKED));
    // the level of the strongest creature: the honest measure of the wall
    uint8_t top = 0;
    for (int k = 0; k < t.count; k++)
      if (t.team[k].level > top) top = t.team[k].level;
    char lv[16];
    snprintf(lv, sizeof(lv), "Lv.%u x%u", top, t.count);
    gfx->setTextColor(done ? UI_BAR_OK : (open_ ? UI_INK : UI_TRACK));
    gfx->setCursor(uiRightX(lv, 384), y + 28);
    gfx->print(lv);
    if (done) {
      gfx->setTextColor(UI_BAR_OK);
      gfx->setCursor(382, y + 8);
      gfx->print("*");
    }
    if (ivReward) {
      char ivMark[8];
      if (ivReward == GYM_IV_REWARD_LEGACY_CLAIMED)
        snprintf(ivMark, sizeof(ivMark), "IV+?");
      else {
        static const char stat[] = "ADSH";
        snprintf(ivMark, sizeof(ivMark), "IV+%c", stat[ivReward - 1]);
      }
      gfx->setTextSize(1);
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(uiRightX(ivMark, 370), y + 8);
      gfx->print(ivMark);
    }
  }
  uint8_t pages = (regionBattleInfo(gymRegion).trainerCount + 1 + GYM_ROWS - 1) / GYM_ROWS;
  for (uint8_t i = 0; i < pages; i++) {
    int dx = CX - (pages - 1) * 13 + i * 26;
    if (i == gymPage) gfx->fillCircle(dx, 366, 5, UI_INK);
    else gfx->drawCircle(dx, 366, 4, UI_INK);
  }
  // the other kind of battle lives here too
  gfx->fillRoundRect(148, 380, 170, 32, 9, UI_BG_DAY);
  gfx->drawRoundRect(148, 380, 170, 32, 9, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  uiDrawCenteredIn(T(S_LAN), 148, 380, 170, 32);
  gfx->flush();
}

// The region pill under a waiting egg. Tapping it cycles; Pet::setRegion swaps
// the egg to that region's creature, keeping the rarity it was granted and
// remembering each region's answer so flipping back and forth is not a re-roll.
#define EGGREG_X 133
#define EGGREG_Y 374
#define EGGREG_W 200
#define EGGREG_H 34
// The hit area is BIGGER than the pill, like the BOX button and the battle
// grid, and for the same reason: a 34 px target is under UI_TAP_MIN and a
// finger is not a stylus.
//
// The guard band matters more than the padding. Missing this pill fell through
// to pet.eggTap(), and THREE taps hatch the egg -- so fumbling at the region
// selector hatched the very egg you were trying to re-aim. A near miss now
// does nothing at all, which is the correct answer for a control whose
// neighbour is irreversible.
#define EGGREG_PAD 16
#define EGGREG_GUARD 14

static void drawEggRegion() {
  char l[24];
  snprintf(l, sizeof(l), "%s >", player.regionName());
  gfx->fillRoundRect(EGGREG_X, EGGREG_Y, EGGREG_W, EGGREG_H, 10, UI_WHITE);
  gfx->drawRoundRect(EGGREG_X, EGGREG_Y, EGGREG_W, EGGREG_H, 10, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  uiDrawCenteredIn(l, EGGREG_X, EGGREG_Y, EGGREG_W, EGGREG_H);
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(1);
  gfx->setCursor(uiCenterX(T(S_EGG_REGION)), EGGREG_Y + EGGREG_H + 6);
  gfx->print(T(S_EGG_REGION));
}

// True if the tap was on the region pill, so the egg does not also get cracked.
// The egg's region pill: its graphic, and the area that actually accepts a tap.
// Exposed so a test can prove the second is bigger than the first and that a
// near miss does not reach pet.eggTap().
void uiEggPillRect(int *x, int *y, int *w, int *h, bool hitArea) {
  int pad = hitArea ? EGGREG_PAD : 0;
  if (x) *x = EGGREG_X - pad;
  if (y) *y = EGGREG_Y - pad;
  if (w) *w = EGGREG_W + 2 * pad;
  if (h) *h = EGGREG_H + 2 * pad;
}

// 1 = cycled the region, -1 = a near miss that must NOT reach the egg, 0 = not
// ours at all.
static int eggRegionTap(int16_t x, int16_t y) {
  if (!pet.isEgg()) return 0;
  int inset = EGGREG_PAD, guard = EGGREG_PAD + EGGREG_GUARD;
  bool hit = x >= EGGREG_X - inset && x <= EGGREG_X + EGGREG_W + inset &&
             y >= EGGREG_Y - inset && y <= EGGREG_Y + EGGREG_H + inset;
  if (hit) {
    pet.setRegion(nextAvailableRegion(player.region));
    sfxPlay(SFX_TAP);
    return 1;
  }
  bool near = x >= EGGREG_X - guard && x <= EGGREG_X + EGGREG_W + guard &&
              y >= EGGREG_Y - guard && y <= EGGREG_Y + EGGREG_H + guard;
  if (near) { sfxPlay(SFX_DENY); return -1; }
  return 0;
}

// The region chooser used by the Pokedex and the gym ladder. Each row carries
// its own progress, so the screen answers "where am I up to" as well as "where
// do I want to go".
#define RPICK_X 74
#define RPICK_W 318
#define RPICK_H 62
#define RPICK_Y(i) (108 + (i) * 72)
#define RPICK_DOTS_Y 366
#define GYM_PICK_DOTS_Y 382
#define RPICK_BACK_Y 392
#define GYM_PICK_BACK_Y 408

// How many regions this mode lists. Every installed regional battle section is
// available to gyms; the Pokedex and starter screen list every real region.
//
// This used to share a fixed count for all three modes, with names read out of
// a trainer table driving the Pokedex chooser. So the moment
// Sinnoh landed it had no row, while the gallery's vertical swipe cycled it
// happily: built, reachable, and looking absent. That is the exact failure the
// chooser was added to prevent.
uint8_t rpickRegions(uint8_t mode) {
  return (mode == RPICK_FOR_GYMS) ? regionAll() : (uint8_t)GAL_REGIONS;
}

uint8_t rpickPageCount(uint8_t mode) {
  uint8_t n = rpickRegions(mode);
  uint8_t p = (uint8_t)((n + RPICK_PER_PAGE - 1) / RPICK_PER_PAGE);
  return p ? p : 1;
}

// The chooser WRAPS rather than closing off the end, unlike the other paged
// screens. It is the root of its own screen, and at first boot there is
// nowhere to go back to at all -- exiting would strand the player before they
// have chosen anything.
// Which chooser is on the panel, or 0xFF for none. THE single answer: the
// swipe handler and swipe_test both ask this rather than each deciding.
uint8_t rpickModeNow() {
  switch (uiCurrentScreen()) {
    case SCR_REGION:  return RPICK_FOR_START;
    case SCR_DEXPICK: return RPICK_FOR_DEX;
    case SCR_GYMPICK: return RPICK_FOR_GYMS;
    default: return 0xFF;
  }
}

static bool rpickSwipe(int dir) {
  uint8_t mode = rpickModeNow();
  if (mode == 0xFF) return false;
  uint8_t pages = rpickPageCount(mode);
  int p = (int)rpickPage + (dir > 0 ? -1 : 1);
  if (p < 0) p = pages - 1;
  if (p >= pages) p = 0;
  rpickPage = (uint8_t)p;
  sfxPlay(SFX_TAP);
  return true;
}

static void renderRegionPick(uint8_t mode) {
  bool forGyms = (mode == RPICK_FOR_GYMS);
  uint8_t nreg = rpickRegions(mode);
  uint8_t pages = rpickPageCount(mode);
  if (rpickPage >= pages) rpickPage = 0;
  uint8_t first = (uint8_t)(rpickPage * RPICK_PER_PAGE);
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char ttl[40];
  if (mode == RPICK_FOR_START) snprintf(ttl, sizeof(ttl), "%s", T(S_CHOOSE_REGION));
  else if (forGyms) snprintf(ttl, sizeof(ttl), "%s", T(S_BATTLE_CENTER));
  else snprintf(ttl, sizeof(ttl), T(S_POKEDEX_FMT), player.registeredCount(), dexCount());
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(ttl), 48);
  gfx->print(ttl);

  for (uint8_t row = 0; row < RPICK_PER_PAGE; row++) {
    uint8_t i = (uint8_t)(first + row);
    if (i >= nreg) break;
    int y = RPICK_Y(row);
    // The sprite pack is the gate. A region without it is drawn GREYED with a
    // reason rather than dropped from the list: hiding it would say "this
    // region does not exist" when what we mean is "download its pack".
    bool open = forGyms ? regionBattleAvailable(i) : regionAvailable(i);
    gfx->fillRoundRect(RPICK_X, y, RPICK_W, RPICK_H, 12, open ? UI_WHITE : UI_BG_DAY);
    gfx->drawRoundRect(RPICK_X, y, RPICK_W, RPICK_H, 12, open ? UI_INK : UI_TRACK);
    const char *nm = regionName(i);
    gfx->setTextColor(open ? UI_INK : UI_TRACK);
    gfx->setTextSize(3);
    gfx->setCursor(RPICK_X + 18, y + 12);
    gfx->print(nm);
    // At first boot there is no subtitle: naming the starter here would give
    // away the next screen, and the counts the other two modes show would all
    // read zero on a new save anyway.
    char sub[28];
    sub[0] = 0;
    if (!open)
      snprintf(sub, sizeof(sub), "%s", T(S_NEED_PACK));
    else if (mode == RPICK_FOR_GYMS)
      snprintf(sub, sizeof(sub), T(S_BADGES_FMT), player.badgeCountIn(i, gymHard));
    else if (mode == RPICK_FOR_DEX)
      snprintf(sub, sizeof(sub), "%u/%u",
               player.registeredCountIn(regionInfo(i).lo, regionInfo(i).hi),
               (unsigned)(regionInfo(i).hi - regionInfo(i).lo + 1));
    if (sub[0]) {
      gfx->setTextColor(UI_MUTED);
      gfx->setTextSize(2);
      gfx->setCursor(uiRightX(sub, RPICK_X + RPICK_W - 18), y + 22);
      gfx->print(sub);
    }
  }
  if (forGyms) {
    gfx->fillRoundRect(LANBTN_X, LANBTN_Y, LANBTN_W, LANBTN_H, 11, UI_BG_DAY);
    gfx->drawRoundRect(LANBTN_X, LANBTN_Y, LANBTN_W, LANBTN_H, 11, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    uiDrawCenteredIn(T(S_LAN), LANBTN_X, LANBTN_Y, LANBTN_W, LANBTN_H);
  }
  if (pages > 1) {                        // dots: which page of regions this is
    int total = pages * 16 - 8;
    int dotsY = forGyms ? GYM_PICK_DOTS_Y : RPICK_DOTS_Y;
    for (uint8_t d = 0; d < pages; d++) {
      int cx = CX - total / 2 + d * 16;
      if (d == rpickPage) gfx->fillCircle(cx, dotsY, 5, UI_INK);
      else gfx->drawCircle(cx, dotsY, 5, UI_TRACK);
    }
  }
  if (mode != RPICK_FOR_START) {          // first boot has nowhere to go back to
    gfx->setTextColor(UI_MUTED);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(T(S_BACK)), forGyms ? GYM_PICK_BACK_Y : RPICK_BACK_Y);
    gfx->print(T(S_BACK));
  }
  gfx->flush();
}

void gymPickerFooterRects(int *rowBottom, int *lanTop, int *lanBottom,
                          int *dotsTop, int *dotsBottom, int *backTop) {
  if (rowBottom) *rowBottom = RPICK_Y(RPICK_PER_PAGE - 1) + RPICK_H;
  if (lanTop) *lanTop = LANBTN_Y;
  if (lanBottom) *lanBottom = LANBTN_Y + LANBTN_H;
  if (dotsTop) *dotsTop = GYM_PICK_DOTS_Y - 5;
  if (dotsBottom) *dotsBottom = GYM_PICK_DOTS_Y + 5;
  if (backTop) *backTop = GYM_PICK_BACK_Y;
}

// Returns the region tapped, or -1. Takes the mode because the row on screen is
// an offset into the current page, not the region index -- and because a region
// whose sprite pack is missing must not be selectable, which is the whole point
// of the gate. The chooser still SHOWS it, greyed, saying why.
static int regionPickTap(int16_t x, int16_t y, uint8_t mode) {
  if (x < RPICK_X || x > RPICK_X + RPICK_W) return -1;
  uint8_t nreg = rpickRegions(mode);
  for (uint8_t row = 0; row < RPICK_PER_PAGE; row++) {
    uint8_t i = (uint8_t)(rpickPage * RPICK_PER_PAGE + row);
    if (i >= nreg) break;
    if (y >= RPICK_Y(row) && y <= RPICK_Y(row) + RPICK_H) {
      bool available = mode == RPICK_FOR_GYMS ? regionBattleAvailable(i) : regionAvailable(i);
      if (!available) {
        sfxPlay(SFX_DENY);      // locked: say no out loud rather than do nothing
        return -1;
      }
      return i;
    }
  }
  return -1;
}

// pagina 2: medallas con etiqueta descriptiva
void renderCardMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[20];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(head), 48);
  gfx->print(head);

  for (int i = 0; i < MED_COUNT; i++) {
    int x = 28 + (i % 2) * 206, y = 104 + (i / 2) * 54;
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, 196, 44, 10, g ? UI_BAR_OK : UI_TRACK);
    if (g) {  // marca de conseguida
      gfx->fillCircle(x + 22, y + 22, 11, UI_BG_DAY);
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(2);
      gfx->setCursor(x + 16, y + 13);
      gfx->print("v");
    }
    gfx->setTextColor(g ? UI_BG_DAY : 0x8410);
    gfx->setTextSize(2);
    gfx->setCursor(x + 44, y + 14);
    gfx->print(medalDesc(i));
  }
}

// pagina 3: progreso (nivel, evolucion, descuidos) — saca a la luz mecanicas
// que antes eran invisibles (cuanto falta para subir/evolucionar y por que)
void renderCardProgress() {
  const DexEntry &d = dexEntry(pet.speciesId);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(T(S_PROGRESS)), 44);
  gfx->print(T(S_PROGRESS));

  // nivel grande
  char lv[10];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), pet.level());
  gfx->setTextSize(5);
  gfx->setCursor(uiCenterX(lv), 86);
  gfx->print(lv);

  // barra de progreso al siguiente nivel (1 nivel = 60 min de juego)
  uint8_t into = pet.ageMinutes % MINUTES_PER_LEVEL;
  int bx = 93, bw = 280, by = 158, bh = 22;
  gfx->fillRoundRect(bx, by, bw, bh, 6, UI_TRACK);
  int fw = (bw - 4) * into / MINUTES_PER_LEVEL;
  if (fw > 0) gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 5, UI_BAR_OK);
  char nx[26];
  snprintf(nx, sizeof(nx), T(S_NEXT_LVL_FMT), MINUTES_PER_LEVEL - into, pet.level() + 1);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(nx), by + 32);
  gfx->print(nx);

  // estado de evolucion
  gfx->setTextColor(UI_MUTED);
  gfx->setCursor(uiCenterX(T(S_EVO_LABEL)), 230);
  gfx->print(T(S_EVO_LABEL));
  char evoBuf[28];
  const char *evo;
  uint16_t evoCol = UI_INK;
  if (!evolutionAvailable(pet.speciesId)) {
    evo = T(S_FINAL_FORM);
  } else {
    int needed = d.evolveLevel + pet.careMistakes;
    if (pet.level() >= needed) {
      if (pet.lowestStat() >= 40) { evo = T(S_EVO_READY); evoCol = UI_BAR_OK; }
      else { evo = T(S_EVO_BLOCKED); evoCol = UI_BAR_BAD; }
    } else {
      snprintf(evoBuf, sizeof(evoBuf), T(S_EVO_IN_FMT), needed - pet.level());
      evo = evoBuf;
    }
  }
  gfx->setTextColor(evoCol);
  gfx->setCursor(uiCenterX(evo), 256);
  gfx->print(evo);

  // descuidos (retrasan la evolucion)
  char ms[24];
  snprintf(ms, sizeof(ms), T(S_MISTAKES_FMT), pet.careMistakes);
  gfx->setTextColor(pet.careMistakes > 0 ? UI_BAR_BAD : UI_INK);
  gfx->setCursor(uiCenterX(ms), 312);
  gfx->print(ms);
}

static const char *natureStatLabel(NatureStat stat) {
  switch (stat) {
    case NATURE_STAT_ATK: return T(S_STAT_ATK);
    case NATURE_STAT_DEF: return T(S_STAT_DEF);
    case NATURE_STAT_SPE: return T(S_STAT_SPE);
    case NATURE_STAT_SPA: return T(S_STAT_SPA);
    case NATURE_STAT_SPD: return T(S_STAT_SPD);
    default: return "?";
  }
}

static NatureStat natureTrainingStat(NatureTraining training) {
  return training == NATURE_TRAIN_ATK ? NATURE_STAT_ATK
       : training == NATURE_TRAIN_DEF ? NATURE_STAT_DEF
                                      : NATURE_STAT_SPE;
}

void renderNatureInfo() {
  gfx->fillRoundRect(58, 70, 350, 330, 16, UI_WHITE);
  gfx->drawRoundRect(58, 70, 350, 330, 16, UI_INK);

  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  uiDrawCenteredIn(natureName(pet.nature), 76, 88, 314, 34);

  gfx->setTextSize(1);
  drawWrappedText(natureDescription(pet.nature), 82, 138, 302, 5);

  gfx->drawFastHLine(82, 232, 302, UI_TRACK);
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  uiDrawCenteredIn(T(S_NATURE_EFFECT), 82, 242, 302, 28);

  char effect[72];
  int effectY = 276;
  NatureStat raised = natureRaisedStat(pet.nature);
  NatureStat lowered = natureLoweredStat(pet.nature);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  if (raised != NATURE_STAT_NONE) {
    snprintf(effect, sizeof(effect), T(S_NATURE_STAT_EFFECT_FMT),
             natureStatLabel(raised), 10);
    uiDrawCenteredIn(effect, 76, effectY, 314, 24);
    snprintf(effect, sizeof(effect), T(S_NATURE_STAT_EFFECT_FMT),
             natureStatLabel(lowered), -10);
    uiDrawCenteredIn(effect, 76, effectY + 30, 314, 24);
  } else {
    gfx->setTextSize(1);
    for (uint8_t channel = 0; channel < 3; channel++) {
      NatureTraining training = (NatureTraining)channel;
      int8_t amount = natureTrainingEffect(pet.nature, training);
      if (!amount) continue;
      snprintf(effect, sizeof(effect), T(S_NATURE_TRAIN_EFFECT_FMT),
               natureStatLabel(natureTrainingStat(training)), amount * 10,
               natureTrainingDecayPercent(pet.nature, training));
      uiDrawCenteredIn(effect, 76, effectY, 314, 24);
      effectY += 30;
    }
  }

  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(1);
  uiDrawCenteredIn(T(S_BACK), 82, 358, 302, 24);
}

void renderCard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (cardPage == 0) renderCardProfile();
  else if (cardPage == 1) renderCardStats();
  else if (cardPage == 2) renderCardMoves();
  else renderCardProgress();

  if (natureInfoOpen) {
    renderNatureInfo();
  } else {
    // indicador de paginas + ayuda
    for (int i = 0; i < CARD_PAGES; i++) {
      int dx = CX - (CARD_PAGES - 1) * 13 + i * 26;
      if (i == cardPage) gfx->fillCircle(dx, 374, 5, UI_INK);
      else gfx->drawCircle(dx, 374, 4, UI_INK);
    }
    gfx->setTextColor(UI_MUTED);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(T(S_BACK)), 398);
    gfx->print(T(S_BACK));
  }
  gfx->flush();
}

// ---------- menu overlay ----------

// Row labels are built fresh because the lead state and Pokedex count are live.
static void menuRowLabel(int i, char *out, size_t n) {
  switch (i) {
    case 0: snprintf(out, n, "%s", T(party.activeIndex() == party.leadIndex()
                                     ? S_LEADING : S_LEAD)); break;
    case 1: snprintf(out, n, T(S_POKEDEX_FMT), player.registeredCount(), dexCount()); break;
    case 2: snprintf(out, n, "%s", T(S_SETTINGS)); break;
    case 3: snprintf(out, n, "%s",
                     pet.canFarewellNow() ? T(S_FAR_GO) : T(S_RETIRE)); break;
    default: snprintf(out, n, "%s", T(S_POWER_OFF)); break;
  }
}

void drawMenu() {
  // dim the game behind the panel so the overlay reads as modal, and so it is
  // obvious that tapping the darkened area is a way out
  for (int y = 0; y < 466; y += 2)
    gfx->drawFastHLine(0, y, 466, gNight ? 0x0000 : 0x2104);

  gfx->fillRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 18, UI_WHITE);
  gfx->drawRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 18, UI_INK);

  for (int i = 0; i < MENU_ROWS; i++) {
    int y = MENU_ROW_Y(i);
    bool close = (i == MENU_ROWS - 1);
    bool disabled = menuRowDisabled((uint8_t)i) ||
                    (i == 3 && !pet.canExitNow());
    gfx->fillRoundRect(MENU_X + 18, y, MENU_W - 36, MENU_ROW_H, 12,
                       close || disabled ? UI_TRACK : UI_BG_DAY);
    gfx->drawRoundRect(MENU_X + 18, y, MENU_W - 36, MENU_ROW_H, 12, UI_INK);
    char lbl[28];
    menuRowLabel(i, lbl, sizeof(lbl));
    gfx->setTextColor(disabled ? UI_MUTED : UI_INK);
    gfx->setTextSize(2);
    uiDrawCenteredIn(lbl, MENU_X + 18, y, MENU_W - 36, MENU_ROW_H);
  }
}

void renderNavMenu() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(T(S_MENU_TITLE)), 44);
  gfx->print(T(S_MENU_TITLE));

  char bagLabel[32], battleLabel[32], badges[32];
  snprintf(bagLabel, sizeof(bagLabel), "%s", T(S_BAG));
  snprintf(battleLabel, sizeof(battleLabel), "%s", T(S_BATTLE_CENTER));
  snprintf(badges, sizeof(badges), T(S_BADGES_FMT),
           player.badgeCountIn(0, false));
  const char *labels[NAVMENU_ROWS] = { bagLabel, battleLabel, badges };
  for (uint8_t i = 0; i < NAVMENU_ROWS; i++) {
    int y = NAVMENU_BTN_Y(i);
    gfx->fillRoundRect(NAVMENU_BTN_X, y, NAVMENU_BTN_W, NAVMENU_BTN_H,
                       14, UI_WHITE);
    gfx->drawRoundRect(NAVMENU_BTN_X, y, NAVMENU_BTN_W, NAVMENU_BTN_H,
                       14, UI_INK);
    if (i == 0) {
      drawMap(SPR_ICON_BAG, 16, NAVMENU_BTN_X + 20, y + 20, 2, false);
    } else if (i == 1) {
      drawMap(SPR_ICON_BATTLE, 16, NAVMENU_BTN_X + 20, y + 20, 2, false);
    } else {
      // A compact shield avoids depending on one region's earned badge art.
      int cx = NAVMENU_BTN_X + 36, cy = y + 34;
      gfx->fillTriangle(cx - 16, cy - 14, cx + 16, cy - 14,
                        cx, cy + 20, UI_BAR_WARN);
      gfx->drawLine(cx - 16, cy - 14, cx + 16, cy - 14, UI_INK);
      gfx->drawLine(cx + 16, cy - 14, cx, cy + 20, UI_INK);
      gfx->drawLine(cx, cy + 20, cx - 16, cy - 14, UI_INK);
      gfx->fillCircle(cx, cy - 4, 6, UI_WHITE);
    }
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    uiDrawCenteredIn(labels[i], NAVMENU_BTN_X + 64, y,
                     NAVMENU_BTN_W - 76, NAVMENU_BTN_H);
  }

  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_BACK)), 392);
  gfx->print(T(S_BACK));
  gfx->flush();
}

// ---------- training submenu (5th icon) ----------

// Bars here show progress toward the IV-capped ceiling, not a raw stat: 100%
// means this individual cannot train the stat any higher, which is the whole
// point of trMaxFor() gating training by IV.
static uint8_t trainPct(uint8_t cur, uint8_t cap) {
  return cap ? (uint8_t)((uint16_t)cur * 100 / cap) : 0;
}

void renderTrain() {
  for (int y = 0; y < 466; y += 2)
    gfx->drawFastHLine(0, y, 466, gNight ? 0x0000 : 0x2104);

  gfx->fillRoundRect(TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H, 18, UI_WHITE);
  gfx->drawRoundRect(TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H, 18, UI_INK);

  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_TRAIN)), TRAIN_Y + 20);
  gfx->print(T(S_TRAIN));

  const char *lbl[3] = { T(S_TR_ATK), T(S_TR_SPE), T(S_TR_DEF) };
  uint8_t cur[3] = { pet.trAtk, pet.trSpe, pet.trDef };
  uint8_t cap[3] = { pet.trMaxAtk(), pet.trMaxSpe(), pet.trMaxDef() };

  for (int i = 0; i < 3; i++) {
    int y = TRAIN_ROW_Y(i);
    bool passive = false;      // every row opens a game now, DEF included
    gfx->fillRoundRect(TRAIN_X + 18, y, TRAIN_W - 36, TRAIN_ROW_H, 12,
                       passive ? UI_TRACK : UI_BG_DAY);
    gfx->drawRoundRect(TRAIN_X + 18, y, TRAIN_W - 36, TRAIN_ROW_H, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(TRAIN_X + 32, y + 10);
    gfx->print(lbl[i]);

    uint8_t pct = trainPct(cur[i], cap[i]);
    int bx = TRAIN_X + 32, bw = TRAIN_W - 64, bh = 12, by = y + 34;
    gfx->fillRoundRect(bx, by, bw, bh, 4, UI_TRACK);
    int fw = (bw - 4) * pct / 100;
    if (fw > 0)
      gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 3, pct >= 100 ? UI_BAR_OK : UI_BAR_WARN);
  }

  gfx->flush();   // without this the panel never updates and the screen freezes
}

// ---------- the box ----------
// Box owns the complete cultivation-management flow. A Box cell is selected
// first. Empty Box cells deposit a chosen cultivation member; occupied Box
// cells can be withdrawn into a free slot or exchanged with an occupied slot.
// There is no separate party-management screen.
static bool boxCanWithdraw() {
  return !partyPick && boxSel && !party.box[boxSel - 1].empty() &&
         party.firstFree() >= 0;
}

static void drawCultivationSlot(uint8_t slot, int x, int y) {
  const PartyMon &m = party.slots[slot];
  gfx->fillRoundRect(x, y, PARTY_CELL_W, PARTY_CELL_H, 10,
                     UI_WHITE);
  gfx->drawRoundRect(x, y, PARTY_CELL_W, PARTY_CELL_H, 10, UI_INK);
  if (m.isEgg()) {
    drawMap(SPR_EGG, SPRITE_H, x + 14, y + 3, 2, false);
    gfx->setTextColor(slot == party.activeIndex() ? UI_BAR_WARN : UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(x + 72, y + 27);
    gfx->print(T(S_EGG_HDR));
    return;
  }
  const uint8_t *th = thumbs.get(m.dex);
  if (th) drawThumb(th, x + PARTY_THUMB_X_OFF, y + PARTY_THUMB_Y_OFF,
                    PARTY_THUMB_SCALE, false);
  const DexEntry &d = dexEntry(m.dex);
  const char *nm = displaySpeciesName(m.dex, m.nick);
  gfx->setTextColor(d.accent);
  gfx->setTextSize(1);
  gfx->setCursor(x + PARTY_TEXT_X_OFF, y + 18);
  gfx->print(nm);
  drawGenderIcon(m.gender, x + PARTY_CELL_W - 29, y + 6, 1);
  if (m.shiny || m.sparkle) {
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setCursor(x + PARTY_TEXT_X_OFF + gfx->textWidth(nm) + 3, y + 18);
    gfx->print(rareMark(true));
  }
  char lv[12];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), (unsigned)m.level);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(x + PARTY_TEXT_X_OFF, y + 36);
  gfx->print(lv);
  if (slot == party.activeIndex()) {
    gfx->fillCircle(x + PARTY_CELL_W - 12, y + 12, 5, UI_BAR_OK);
  }
}

void renderBox() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char head[32];
  snprintf(head, sizeof(head), T(S_BOX_FMT), party.boxCount(), BOX_SLOTS);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(head), 40);
  gfx->print(head);

  if (boxSel || partyPick) {
    if (partyPick) {
      gfx->setTextColor(UI_BAR_BAD);
      gfx->setTextSize(1);
      gfx->setCursor(uiCenterX(T(S_PARTY_FULL)), 68);
      gfx->print(T(S_PARTY_FULL));
    } else {
      const PartyMon &b = party.box[boxSel - 1];
      char sub[96];
      if (b.empty()) snprintf(sub, sizeof(sub), "%s", T(S_BOX_DEPOSIT));
      else snprintf(sub, sizeof(sub), T(S_BOX_SWAP),
                    displaySpeciesName(b.dex, b.nick));
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setTextSize(1);
      gfx->setCursor(uiCenterX(sub), 68);
      gfx->print(sub);
    }
    uint8_t shown = 0;
    for (uint8_t ordinal = 0; ordinal < party.count(); ordinal++) {
      int slot = partySlotAtOrdinal(ordinal);
      if (slot < 0) break;
      int x = PARTY_GRID_X + (shown % 2) * (PARTY_CELL_W + 10);
      int y = PARTY_GRID_Y + (shown / 2) * (PARTY_CELL_H + 8);
      drawCultivationSlot((uint8_t)slot, x, y);
      shown++;
    }
    const bool canWithdraw = boxCanWithdraw();
    const int backX = canWithdraw ? PARTY_GRID_X : BOXPICK_BACK_X;
    const int backW = canWithdraw ? PARTY_CELL_W : BOXPICK_BACK_W;
    const char *back = partyPick ? T(S_PARTY_LETGO) : T(S_BACK);
    gfx->fillRoundRect(backX, BOXPICK_BACK_Y, backW,
                       BOXPICK_BACK_H, 12, partyPick ? UI_BAR_BAD : UI_TRACK);
    gfx->drawRoundRect(backX, BOXPICK_BACK_Y, backW,
                       BOXPICK_BACK_H, 12, UI_INK);
    gfx->setTextColor(partyPick ? UI_WHITE : UI_INK);
    gfx->setTextSize(2);
    uiDrawCenteredIn(back, backX, BOXPICK_BACK_Y, backW, BOXPICK_BACK_H);
    if (canWithdraw) {
      gfx->fillRoundRect(BOXPICK_ACTION_X, BOXPICK_BACK_Y, BOXPICK_ACTION_W,
                         BOXPICK_BACK_H, 12, UI_BAR_OK);
      gfx->drawRoundRect(BOXPICK_ACTION_X, BOXPICK_BACK_Y, BOXPICK_ACTION_W,
                         BOXPICK_BACK_H, 12, UI_INK);
      gfx->setTextColor(UI_WHITE);
      uiDrawCenteredIn(T(S_BOX_WITHDRAW), BOXPICK_ACTION_X, BOXPICK_BACK_Y,
                       BOXPICK_ACTION_W, BOXPICK_BACK_H);
    }
    gfx->flush();
    return;
  }

  for (uint8_t i = 0; i < BOX_PER_PAGE; i++) {
    uint8_t idx = boxPage * BOX_PER_PAGE + i;
    if (idx >= BOX_SLOTS) break;
    const PartyMon &m = party.box[idx];
    int x = PARTY_GRID_X + (i % 2) * (PARTY_CELL_W + 10);
    int y = PARTY_GRID_Y + (i / 2) * (PARTY_CELL_H + 8);
    gfx->fillRoundRect(x, y, PARTY_CELL_W, PARTY_CELL_H, 10,
                       m.empty() ? UI_TRACK : UI_WHITE);
    gfx->drawRoundRect(x, y, PARTY_CELL_W, PARTY_CELL_H, 10, UI_INK);
    if (m.empty()) {
      gfx->setTextColor(0x8410);
      gfx->setTextSize(1);
      uiDrawCenteredIn(T(S_PARTY_EMPTY), x, y, PARTY_CELL_W, PARTY_CELL_H);
      continue;
    }
    const uint8_t *th = thumbs.get(m.dex);
    if (th) drawThumb(th, x + PARTY_THUMB_X_OFF, y + PARTY_THUMB_Y_OFF,
                      PARTY_THUMB_SCALE, false);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(x + PARTY_TEXT_X_OFF, y + 16);
    gfx->print(displaySpeciesName(m.dex, m.nick));
    drawGenderIcon(m.gender, x + PARTY_CELL_W - 29, y + 6, 1);
    char level[16];
    snprintf(level, sizeof(level), "Lv.%u %s", (unsigned)m.level,
             rareMark(m.shiny || m.sparkle));
    gfx->setCursor(x + PARTY_TEXT_X_OFF, y + 34);
    gfx->setTextColor(m.dead() ? UI_BAR_BAD : UI_INK);
    gfx->print(m.dead() ? T(S_DEAD) : level);
  }
  uint8_t pages = BOX_SLOTS / BOX_PER_PAGE;
  for (uint8_t i = 0; i < pages; i++) {
    int dx = CX - (pages - 1) * 13 + i * 26;
    if (i == boxPage) gfx->fillCircle(dx, 366, 5, UI_INK);
    else gfx->drawCircle(dx, 366, 4, UI_INK);
  }
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_BACK)), 392);
  gfx->print(T(S_BACK));
  gfx->flush();
}

void boxTap(int16_t x, int16_t y) {
  if (boxSel || partyPick) {
    for (uint8_t ordinal = 0; ordinal < party.count(); ordinal++) {
      int slot = partySlotAtOrdinal(ordinal);
      if (slot < 0) break;
      int cx0 = PARTY_GRID_X + (ordinal % 2) * (PARTY_CELL_W + 10);
      int cy0 = PARTY_GRID_Y + (ordinal / 2) * (PARTY_CELL_H + 8);
      if (x < cx0 || x > cx0 + PARTY_CELL_W ||
          y < cy0 || y > cy0 + PARTY_CELL_H) continue;
      if (partyPick) {
        party.replaceAt((uint8_t)slot, partyPending);
        snprintf(partyBannerName, sizeof(partyBannerName), "%s",
                 displaySpeciesName(partyPending.dex, partyPending.nick));
        partyBannerUntil = millis() + 3500;
        partyPick = false;
        partyPending = PartyMon();
        boxOpen = false;
        sfxPlay(SFX_MEDAL);
        return;
      }
      uint8_t boxIndex = boxSel - 1;
      const PartyMon &member = party.slots[slot];
      if (member.isEgg() || (party.box[boxIndex].empty() && party.count() <= 1)) {
        sfxPlay(SFX_DENY);
        return;
      }
      party.swapPartyBox((uint8_t)slot, boxIndex);
      boxSel = 0;
      sfxPlay(SFX_MEDAL);
      return;
    }
    if (boxCanWithdraw() &&
        y >= BOXPICK_BACK_Y && y <= BOXPICK_BACK_Y + BOXPICK_BACK_H &&
        x >= BOXPICK_ACTION_X && x <= BOXPICK_ACTION_X + BOXPICK_ACTION_W) {
      const uint8_t boxIndex = boxSel - 1;
      const int freeSlot = party.firstFree();
      if (freeSlot >= 0) {
        party.swapPartyBox((uint8_t)freeSlot, boxIndex);
        boxSel = 0;
        sfxPlay(SFX_MEDAL);
      }
      return;
    }
    const bool dualButtons = boxCanWithdraw();
    const int backX = dualButtons ? PARTY_GRID_X : BOXPICK_BACK_X;
    const int backW = dualButtons ? PARTY_CELL_W : BOXPICK_BACK_W;
    if ((y >= BOXPICK_BACK_Y && y <= BOXPICK_BACK_Y + BOXPICK_BACK_H &&
         x >= backX && x <= backX + backW) || y < 34) {
      if (partyPick) {
        partyPick = false;
        partyPending = PartyMon();
        boxOpen = false;
      } else {
        boxSel = 0;
      }
      sfxPlay(SFX_TAP);
    }
    return;
  }

  for (uint8_t i = 0; i < BOX_PER_PAGE; i++) {
    uint8_t idx = boxPage * BOX_PER_PAGE + i;
    if (idx >= BOX_SLOTS) break;
    int cx0 = PARTY_GRID_X + (i % 2) * (PARTY_CELL_W + 10);
    int cy0 = PARTY_GRID_Y + (i / 2) * (PARTY_CELL_H + 8);
    if (x < cx0 || x > cx0 + PARTY_CELL_W ||
        y < cy0 || y > cy0 + PARTY_CELL_H) continue;
    boxSel = idx + 1;
    sfxPlay(SFX_TAP);
    return;
  }
  boxOpen = false;
}

// ---------- teclado para renombrar ----------

static const char KB_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-";  // 28 + DEL + OK = 30
#define KB_COLS 6
#define KB_X 68
#define KB_Y 148
#define KB_W 55
#define KB_H 48

// The keyboard is shared, so it has to be told what it is naming. It used to
// hardcode pet.rename() on commit, which is why a second caller needed this.
void openKeyboardFor(uint8_t target) {
  kbTarget = target;
  kbOpen = true;
  const char *cur = (target == KB_TRAINER) ? player.trainerName : pet.nick;
  strncpy(nameBuf, cur, sizeof(nameBuf) - 1);
  nameBuf[sizeof(nameBuf) - 1] = 0;
  nameLen = strlen(nameBuf);
}
void openKeyboard() { openKeyboardFor(KB_PET); }

void renderKeyboard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_NAME)), 56);
  gfx->print(T(S_NAME));
  // buffer actual
  gfx->fillRoundRect(83, 84, 300, 40, 8, UI_WHITE);
  gfx->drawRoundRect(83, 84, 300, 40, 8, UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(95, 94);
  gfx->print(nameLen ? nameBuf : "_");

  for (int i = 0; i < 30; i++) {
    int x = KB_X + (i % KB_COLS) * KB_W, y = KB_Y + (i / KB_COLS) * KB_H;
    bool special = (i >= 28);
    gfx->fillRoundRect(x, y, KB_W - 6, KB_H - 6, 6, special ? UI_BAR_WARN : UI_WHITE);
    gfx->drawRoundRect(x, y, KB_W - 6, KB_H - 6, 6, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    if (i < 28) {
      char label[2] = { KB_KEYS[i], 0 };
      uiDrawCenteredIn(label, x, y, KB_W - 6, KB_H - 6);
    } else {
      const char *lab = (i == 28) ? "<-" : "OK";
      uiDrawCenteredIn(lab, x, y, KB_W - 6, KB_H - 6);
    }
  }
  gfx->flush();
}

void keyboardTap(int16_t x, int16_t y) {
  int col = (x - KB_X) / KB_W, row = (y - KB_Y) / KB_H;
  if (col < 0 || col >= KB_COLS || row < 0 || row >= 5) return;
  int i = row * KB_COLS + col;
  if (i >= 30) return;
  if (i == 28) {  // borrar
    if (nameLen) nameBuf[--nameLen] = 0;
  } else if (i == 29) {  // OK
    if (kbTarget == KB_TRAINER) player.renameTrainer(nameBuf);
    else pet.rename(nameBuf);
    kbOpen = false;
  } else if (nameLen < sizeof(nameBuf) - 1) {
    nameBuf[nameLen++] = KB_KEYS[i];
    nameBuf[nameLen] = 0;
  }
}

// ---------- galeria pokedex ----------

#define GAL_X 73
#define GAL_Y 84
#define GAL_CELL THUMB_CELL

// dibuja una miniatura centrada en su celda; sil=true la pinta en tinta
void drawThumb(const uint8_t *b, int x, int y, int s, bool sil) {
  uint8_t w = b[0], h = b[1], n = b[2];
  const uint8_t *pal = b + 3;
  const uint8_t *d = pal + n * 2;
  int ox = x + (GAL_CELL - w * s) / 2;
  int oy = y + (GAL_CELL - h * s) / 2;
  for (int r = 0; r < h; r++) {
    const uint8_t *row = d + r * w;
    for (int c = 0; c < w;) {
      uint8_t idx = row[c];
      if (idx == 0xFF) { c++; continue; }
      uint16_t col = sil ? INK_K : (uint16_t)(pal[idx * 2] | (pal[idx * 2 + 1] << 8));
      int start = c++;
      while (c < w && row[c] != 0xFF) {
        uint8_t next = row[c];
        uint16_t nextCol = sil ? INK_K
            : (uint16_t)(pal[next * 2] | (pal[next * 2 + 1] << 8));
        if (nextCol != col) break;
        c++;
      }
      gfx->fillRect(ox + start * s, oy + r * s, (c - start) * s, s, col);
    }
  }
}

static const char *nextUtf8Char(const char *at) {
  if (!at || !*at) return at;
  at++;
  while ((*at & 0xC0) == 0x80) at++;
  return at;
}

int drawWrappedTextWindow(const char *text, int x, int y, int width,
                          uint8_t skipLines, uint8_t maxLines, uint8_t *totalLines) {
  if (!text || !*text || !maxLines) return y;
  const char *at = text;
  char line[128];
  uint8_t logicalLine = 0, drawn = 0;
  while (*at) {
    while (*at == ' ') at++;
    const char *start = at, *scan = at, *fit = at, *lastSpace = nullptr;
    bool overflow = false;
    while (*scan && *scan != '\n') {
      const char *character = scan;
      scan = nextUtf8Char(scan);
      size_t bytes = (size_t)(scan - start);
      if (bytes >= sizeof(line)) { overflow = true; break; }
      memcpy(line, start, bytes);
      line[bytes] = 0;
      if (gfx->textWidth(line) > width) { overflow = true; break; }
      fit = scan;
      if (*character == ' ') lastSpace = character;
    }
    const char *end = fit, *next = scan;
    if (overflow) {
      if (lastSpace && lastSpace > start) {
        end = lastSpace;
        next = lastSpace + 1;
      } else {
        next = fit;
        if (end == start) end = next = scan;
      }
    } else if (*next == '\n') {
      next++;
    }
    while (end > start && end[-1] == ' ') end--;
    size_t bytes = (size_t)(end - start);
    if (bytes >= sizeof(line)) bytes = sizeof(line) - 1;
    memcpy(line, start, bytes);
    line[bytes] = 0;
    if (logicalLine >= skipLines && drawn < maxLines) {
      gfx->setCursor(x, y);
      gfx->print(line);
      y += gfx->textLineHeight();
      drawn++;
    }
    logicalLine++;
    if (next <= at) break;
    at = next;
  }
  if (totalLines) *totalLines = logicalLine;
  return y;
}

int drawWrappedText(const char *text, int x, int y, int width, uint8_t maxLines) {
  return drawWrappedTextWindow(text, x, y, width, 0, maxLines, nullptr);
}

// ---------- care-question modal ----------

#define QUIZ_OPTION_X 72
#define QUIZ_OPTION_Y 164
#define QUIZ_OPTION_W 322
#define QUIZ_OPTION_H 44
#define QUIZ_OPTION_GAP 6
#define QUIZ_KEY_X 84
#define QUIZ_KEY_Y 208
#define QUIZ_KEY_W 70
#define QUIZ_KEY_H 44
#define QUIZ_KEY_GAP 6

void quizOptionRect(uint8_t option, int *x, int *y, int *w, int *h) {
  if (x) *x = QUIZ_OPTION_X;
  if (y) *y = QUIZ_OPTION_Y + option * (QUIZ_OPTION_H + QUIZ_OPTION_GAP);
  if (w) *w = QUIZ_OPTION_W;
  if (h) *h = QUIZ_OPTION_H;
}

void quizKeyRect(uint8_t row, uint8_t column, int *x, int *y, int *w, int *h) {
  if (x) *x = QUIZ_KEY_X + column * (QUIZ_KEY_W + QUIZ_KEY_GAP);
  if (y) *y = QUIZ_KEY_Y + row * (QUIZ_KEY_H + QUIZ_KEY_GAP);
  if (w) *w = QUIZ_KEY_W;
  if (h) *h = QUIZ_KEY_H;
}

void quizTap(int16_t x, int16_t y) {
  if (!quiz.active || quiz.answered) return;
  if (quiz.kind == QUIZ_QUESTION_CHOICE) {
    for (uint8_t option = 0; option < quiz.choice.optionCount; option++) {
      int left, top, width, height;
      quizOptionRect(option, &left, &top, &width, &height);
      if (x >= left && x <= left + width && y >= top && y <= top + height) {
        quiz.choose(option, millis());
        return;
      }
    }
    return;
  }
  if (quiz.kind != QUIZ_QUESTION_ARITHMETIC) return;
  static const char keys[4][4] = {
    { '7', '8', '9', '<' },
    { '4', '5', '6', '-' },
    { '1', '2', '3', '.' },
    { '0', '/', 'C', 'O' },
  };
  for (uint8_t row = 0; row < 4; row++) {
    for (uint8_t column = 0; column < 4; column++) {
      int left, top, width, height;
      quizKeyRect(row, column, &left, &top, &width, &height);
      if (x < left || x > left + width || y < top || y > top + height) continue;
      char key = keys[row][column];
      if (key == '<') quiz.erase();
      else if (key == 'C') quiz.clearInput();
      else if (key == 'O') {
        if (!quiz.submit(millis())) sfxPlay(SFX_DENY);
      } else if (!quiz.append(key)) {
        sfxPlay(SFX_DENY);
      }
      return;
    }
  }
}

void renderQuiz() {
  uint32_t now = millis();
  gfx->fillCircle(CX, CY, 231, UI_WHITE);
  gfx->drawCircle(CX, CY, 231, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(T(S_QUIZ_TITLE)), 28);
  gfx->print(T(S_QUIZ_TITLE));

  uint32_t remaining = quiz.remainingMs(now);
  char seconds[12];
  snprintf(seconds, sizeof(seconds), "%lus", (unsigned long)((remaining + 999) / 1000));
  gfx->setTextSize(1);
  gfx->setCursor(uiCenterX(seconds), 52);
  gfx->print(seconds);
  int timerWidth = 310;
  gfx->fillRoundRect(78, 68, timerWidth, 8, 3, UI_TRACK);
  if (!quiz.answered && quiz.config.timeSeconds) {
    uint32_t duration = (uint32_t)quiz.config.timeSeconds * 1000u;
    int fill = (int)((uint64_t)timerWidth * remaining / duration);
    if (fill > 2) gfx->fillRoundRect(78, 68, fill, 8, 3,
                                    remaining < duration / 6 ? UI_BAR_WARN : UI_BAR_OK);
  }

  gfx->setTextSize(2);
  const char *question = quiz.kind == QUIZ_QUESTION_CHOICE ? quiz.choice.stem : quiz.expression;
  uint8_t questionLines = 0;
  drawWrappedTextWindow(question, 58, 84, 350, quiz.scrollLine, 3, &questionLines);
  quiz.maxScrollLine = questionLines > 3 ? questionLines - 3 : 0;
  if (quiz.scrollLine) {
    gfx->setTextSize(1);
    gfx->setCursor(410, 86);
    gfx->print("^");
  }
  if (quiz.scrollLine < quiz.maxScrollLine) {
    gfx->setTextSize(1);
    gfx->setCursor(410, 136);
    gfx->print("v");
  }

  if (quiz.kind == QUIZ_QUESTION_CHOICE) {
    gfx->setTextSize(1);
    for (uint8_t option = 0; option < quiz.choice.optionCount; option++) {
      int left, top, width, height;
      quizOptionRect(option, &left, &top, &width, &height);
      uint16_t color = 0xEF7D;
      if (quiz.answered && option == quiz.choice.correctIndex) color = UI_BAR_OK;
      else if (quiz.answered && option == quiz.selectedOption) color = UI_BAR_BAD;
      gfx->fillRoundRect(left, top, width, height, 10, color);
      gfx->drawRoundRect(left, top, width, height, 10, UI_INK);
      char label[4] = { (char)('A' + option), '.', 0, 0 };
      gfx->setCursor(left + 12, top + 14);
      gfx->print(label);
      drawWrappedText(quiz.choice.options[option], left + 40, top + 6, width - 50, 2);
    }
  } else {
    gfx->fillRoundRect(70, 154, 326, 40, 10, 0xEF7D);
    gfx->drawRoundRect(70, 154, 326, 40, 10, UI_INK);
    gfx->setTextSize(2);
    const char *answer = quiz.input[0] ? quiz.input : T(S_QUIZ_ANSWER);
    uiDrawCenteredIn(answer, 70, 154, 326, 40);
    static const char *keys[4][4] = {
      { "7", "8", "9", "<" },
      { "4", "5", "6", "-" },
      { "1", "2", "3", "." },
      { "0", "/", "C", "OK" },
    };
    for (uint8_t row = 0; row < 4; row++) {
      for (uint8_t column = 0; column < 4; column++) {
        int left, top, width, height;
        quizKeyRect(row, column, &left, &top, &width, &height);
        bool disabled = (keys[row][column][0] == '-' && !(quiz.config.flags & QUIZ_ALLOW_NEGATIVE)) ||
                        (keys[row][column][0] == '.' && !(quiz.config.flags & QUIZ_ALLOW_DECIMALS)) ||
                        (keys[row][column][0] == '/' && !(quiz.config.flags & QUIZ_ALLOW_FRACTIONS));
        gfx->fillRoundRect(left, top, width, height, 9,
                           disabled ? UI_TRACK : (column == 3 && row == 3 ? UI_BAR_OK : 0xEF7D));
        gfx->drawRoundRect(left, top, width, height, 9, UI_INK);
        gfx->setTextColor(disabled ? UI_WHITE : UI_INK);
        uiDrawCenteredIn(keys[row][column], left, top, width, height);
      }
    }
  }

  if (quiz.answered) {
    gfx->fillRoundRect(78, 170, 310, 126, 18, quiz.correct ? UI_BAR_OK : UI_BAR_BAD);
    gfx->drawRoundRect(78, 170, 310, 126, 18, UI_INK);
    gfx->setTextColor(UI_WHITE);
    gfx->setTextSize(3);
    const char *message = quiz.correct ? T(S_QUIZ_CORRECT)
                                      : (quiz.timedOut ? T(S_QUIZ_TIMEOUT) : T(S_QUIZ_WRONG));
    gfx->setCursor(uiCenterX(message), 198);
    gfx->print(message);
    char effect[32];
    snprintf(effect, sizeof(effect), T(S_QUIZ_EFFECT_FMT), quiz.effectPercent);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(effect), 248);
    gfx->print(effect);
  }
  gfx->flush();
}

void renderGallery() {
  if (galleryDetail) {  // vista detalle: se redibuja siempre (animada)
    gfx->fillScreen(RGB565_BLACK);
    gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
    const DexEntry &d = dexEntry(galleryDetail);
    bool reg = player.isRegistered(galleryDetail);
    const char *description = reg ? speciesDescription(galleryDetail, uiActiveLocaleCode()) : nullptr;
    char head[64];
    snprintf(head, sizeof(head), "N.%03d %s%s", galleryDetail,
             player.isShinyRegistered(galleryDetail) ? "*" : "",
             reg ? speciesName(galleryDetail) : "???");
    gfx->setTextColor(reg ? d.accent : UI_INK);
    gfx->setTextSize(3);
    int gts = (gfx->textWidth(head) <= 234) ? 3 : 2;
    gfx->setTextSize(gts);
    gfx->setCursor(uiCenterX(head), gts == 3 ? 56 : 60);
    gfx->print(head);
    if (galleryPmd.loaded) {
      // animado y a color si esta registrado; silueta estatica si no (estilo "?")
      int ground = description ? uiLayoutMetric(UI_LAYOUT_DETAIL_SPRITE_GROUND, 260) : 300;
      drawPmdActM(galleryPmd, PMD_IDLE, CX, ground, reg ? millis() : 0,
                  true, !reg, description ? 4 : 6, 0);
    } else {
      const uint8_t *t = thumbs.get(galleryDetail);
      if (t) drawThumb(t, CX - GAL_CELL / 2, description ? 105 : 135, 4, !reg);
    }
    if (description) {
      gfx->setTextColor(UI_INK);
      gfx->setTextSize((uint8_t)uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_TEXT_SIZE, 1));
      drawWrappedText(description, uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_X, 78),
                      uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_Y, 282),
                      uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_WIDTH, 310),
                      (uint8_t)uiLayoutMetric(UI_LAYOUT_DETAIL_DESCRIPTION_LINES, 5));
    }
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(uiCenterX(T(S_DETAIL_BACK)),
                   uiLayoutMetric(UI_LAYOUT_DETAIL_BACK_Y, 408));
    gfx->print(T(S_DETAIL_BACK));
    gfx->flush();
    return;
  }

  if (!galleryDirty) return;  // la rejilla es estatica
  galleryDirty = false;

  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  // the region's own name and its own tally: "how much of Johto have I seen"
  // is the question you are actually asking here
  char head[32];
  const RegionInfo &grg = regionInfo(galleryRegion % GAL_REGIONS);
  snprintf(head, sizeof(head), "%s %u/%u", regionName(galleryRegion % GAL_REGIONS),
           player.registeredCountIn(grg.lo, grg.hi), (unsigned)GAL_SPAN);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(head), 36);
  gfx->print(head);

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int16_t dex = GAL_LO + galleryPage * GAL_PER_PAGE + r * 4 + c;
      if (dex > GAL_HI || dex > dexCount()) break;
      int x = GAL_X + c * GAL_CELL, y = GAL_Y + r * GAL_CELL;
      const uint8_t *t = thumbs.get(dex);
      if (t) {
        drawThumb(t, x, y, 2, !player.isRegistered(dex));
        if (player.isShinyRegistered(dex)) {
          gfx->setTextColor(UI_BAR_WARN);
          gfx->setTextSize(2);
          gfx->setCursor(x + 62, y + 4);
          gfx->print("*");
        }
      } else {
        char num[6];
        snprintf(num, sizeof(num), "%d", dex);
        gfx->setTextColor(UI_MUTED);
        gfx->setTextSize(2);
        gfx->setCursor(x + 24, y + 32);
        gfx->print(num);
      }
    }
  }
  // A page number, not a row of dots. 25 dots do not fit across the bottom of
  // a round panel -- the chord at that height is only ~228 px -- and counting
  // them to find where you are is worse than reading the number.
  char pg[12];
  snprintf(pg, sizeof(pg), "%d/%d", galleryPage + 1, (int)GAL_PAGES);
  gfx->setTextColor(UI_MUTED);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(pg), 428);
  gfx->print(pg);
  gfx->flush();
}

void galleryTap(int16_t x, int16_t y) {
  if (galleryDetail) {  // volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  if (y < 72) {  // tocar la cabecera = salir
    galleryOpen = false;
    galleryPmd.unload();
    return;
  }
  int c = (x - GAL_X) / GAL_CELL, r = (y - GAL_Y) / GAL_CELL;
  if (c < 0 || c > 3 || r < 0 || r > 3) return;
  int16_t dex = GAL_LO + galleryPage * GAL_PER_PAGE + r * 4 + c;
  if (dex > GAL_HI || dex > dexCount()) return;
  galleryDetail = dex;
  galleryPmd.load(dex, player.isShinyRegistered(dex), GENDER_NONE);
}

void drawBattery() {
  int pc = batPercent();
  if (pc < 0) return;  // sin bateria conectada
  int x = CX - 14, y = 12, w = 24, h = 11;
  bool charging = batCharging();
  uint16_t col = charging ? UI_BAR_OK
                 : (pc >= 40) ? inkColor()
                 : (pc >= 15) ? UI_BAR_WARN
                              : UI_BAR_BAD;
  gfx->drawRoundRect(x, y, w, h, 2, col);
  gfx->fillRect(x + w, y + 3, 3, 5, col);  // borne
  if (charging) {
    // rayo de carga (zigzag) en vez de la barra de nivel
    uint16_t bolt = C565(0xff, 0xd9, 0x4a);
    int bx = x + w / 2;
    gfx->fillTriangle(bx + 3, y + 1, bx - 4, y + 6, bx + 1, y + 6, bolt);
    gfx->fillTriangle(bx - 1, y + 5, bx + 4, y + 5, bx - 3, y + 10, bolt);
  } else {
    int fw = (w - 4) * pc / 100;
    if (fw > 0) gfx->fillRect(x + 2, y + 2, fw, h - 4, col);
  }
}

void drawHeader(const char *name, uint16_t nameColor, const char *msg) {
  drawBattery();
  gfx->setTextColor(nameColor);
  gfx->setTextSize(3);
  gfx->setCursor(uiCenterX(name), 68);
  gfx->print(name);
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(msg), 104);
  gfx->print(msg);
}

void drawPetNav() {
  uint16_t ink = inkColor();
  uint8_t count = party.count();
  for (uint8_t ordinal = 0; ordinal < count; ordinal++) {
    int slot = partySlotAtOrdinal(ordinal);
    bool current = slot == party.activeIndex();
    int x = petNavDotX(ordinal, count);
    if (current) gfx->fillCircle(x, PETNAV_DOT_Y, 7, UI_BAR_OK);
    else gfx->fillCircle(x, PETNAV_DOT_Y, 5, ink);
  }
}

// animacion de la ceremonia (10s): despedida = reverencia con corazones y se
// aleja caminando; escapada = se asusta y sale corriendo. Sustituye al idle.
void drawCeremony() {
  if (!pmd.loaded) { drawPet(); return; }  // respaldo si no hay sprite PMD
  uint32_t now = millis();
  float t = pet.ceremonyT();               // 0..1 a lo largo de los 10s
  bool panic = (pet.ceremony == CER_RUNAWAY);
  int x = CX, y = PET_GROUND;
  uint8_t act = PMD_IDLE;

  if (panic) {
    // final triste: penumbra azulada + lluvia
    for (int i = 0; i < 46; i++) {
      int rx = (i * 47 + now / 3) % 466;
      int ry = (i * 91 + now / 2) % 470;
      gfx->drawLine(rx, ry, rx - 3, ry + 12, C565(0x6a, 0x84, 0xb0));
    }
    bool fade = false;
    if (t < 0.30f) {                       // cabizbajo, temblando
      act = pmd.has(PMD_HURT) ? PMD_HURT : PMD_IDLE;
      x = CX + (int)(4 * sinf(now * 0.04f));
    } else {                               // se aleja despacio y se desvanece
      act = pmd.has(PMD_WALKL) ? PMD_WALKL : PMD_IDLE;
      x = CX - (int)(((t - 0.30f) / 0.70f) * (CX + 120));
      fade = (t > 0.6f) && ((now / 160) % 2 == 0);  // parpadea hacia la silueta
    }
    drawPmdAct(act, x, y, now, true, fade, 5);  // fade=silueta: se difumina al irse
    // lagrima cayendo del bicho
    if (t < 0.55f) {
      int ty = y - 150 + (int)((now / 6) % 40);
      gfx->fillRect(x + 6, ty, 3, 6, C565(0x9a, 0xc4, 0xe8));
    }
    return;
  }

  // despedida epica: halo dorado pulsante + chispas y corazones que ascienden
  int gcy = PET_GROUND - 96;
  for (int k = 0; k < 4; k++) {
    int r = 60 + k * 34 + (int)(10 * sinf(now * 0.02f));
    gfx->drawCircle(CX, gcy, r, C565(0xff, 0xdf, 0x8a));
  }
  for (int i = 0; i < 16; i++) {
    int px = (i * 71 + 28) % 466;
    int py = 410 - (int)((now / 8 + i * 70) % 360);   // suben y reaparecen abajo
    if (py < 30) continue;
    if (i % 4 == 0) drawMap(SPR_HEART, 32, px - 8, py - 8, 1, false);  // corazoncito
    else gfx->fillRect(px, py, 4, 4, (i % 2) ? C565(0xff, 0xe7, 0x9f) : C565(0xff, 0x9a, 0xc0));
  }

  if (t < 0.45f) {                         // reverencia / pose de despedida
    act = pmd.has(PMD_POSE) ? PMD_POSE : (pmd.has(PMD_NOD) ? PMD_NOD : PMD_IDLE);
  } else {                                 // se aleja por la derecha
    act = pmd.has(PMD_WALKR) ? PMD_WALKR : PMD_IDLE;
    x = CX + (int)(((t - 0.45f) / 0.55f) * (CX + 140));
  }
  drawPmdAct(act, x, y, now, true, false, 5);
  if (pet.showHeart())                     // corazon grande siguiendo al bicho
    drawMap(SPR_HEART, 32, x + 50, y - 190, 2, false);
}

// Two stacked actions: evolve/keep or the contextual farewell/release.
void drawChoiceDialog() {
  const char *q, *o1, *o2;
  char contextual[64];
  uint16_t c1, c2, t1, t2;
  if (choiceKind == 1) {  // evolucion
    q = T(S_EVO_Q); o1 = T(S_EVO_TAP); o2 = T(S_EVO_KEEP);
    c1 = UI_BAR_BAD; t1 = UI_WHITE; c2 = UI_TRACK; t2 = UI_INK;
  } else if (choiceKind == 3 && pet.canFarewellNow()) {
    q = T(S_FAR_Q); o1 = T(S_FAR_GO); o2 = T(S_FAR_STAY);
    c1 = UI_BAR_WARN; t1 = UI_INK; c2 = UI_BAR_OK; t2 = UI_WHITE;
  } else if (choiceKind == 3) {
    snprintf(contextual, sizeof(contextual), T(S_RELEASE_FMT), speciesName(pet.speciesId));
    q = contextual; o1 = T(S_RETIRE); o2 = T(S_NO);
    c1 = UI_BAR_BAD; t1 = UI_WHITE; c2 = UI_BAR_OK; t2 = UI_WHITE;
  } else if (choiceKind == 4) {
    q = T(S_POWER_OFF_Q); o1 = T(S_POWER_OFF); o2 = T(S_NO);
    c1 = UI_BAR_BAD; t1 = UI_WHITE; c2 = UI_TRACK; t2 = UI_INK;
  } else {                // despedida
    q = T(S_FAR_Q); o1 = T(S_FAR_GO); o2 = T(S_FAR_STAY);
    c1 = UI_BAR_WARN; t1 = UI_INK; c2 = UI_BAR_OK; t2 = UI_WHITE;
  }
  gfx->fillRoundRect(73, 156, 320, 188, 16, UI_WHITE);
  gfx->drawRoundRect(73, 156, 320, 188, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(uiCenterX(q), 176);
  gfx->print(q);
  gfx->fillRoundRect(CHOICE_BTN_X, CHOICE_BTN1_Y, CHOICE_BTN_W, CHOICE_BTN_H, 12, c1);
  gfx->setTextColor(t1);
  uiDrawCenteredIn(o1, CHOICE_BTN_X, CHOICE_BTN1_Y, CHOICE_BTN_W, CHOICE_BTN_H);
  gfx->fillRoundRect(CHOICE_BTN_X, CHOICE_BTN2_Y, CHOICE_BTN_W, CHOICE_BTN_H, 12, c2);
  gfx->setTextColor(t2);
  uiDrawCenteredIn(o2, CHOICE_BTN_X, CHOICE_BTN2_Y, CHOICE_BTN_W, CHOICE_BTN_H);
}

void choiceDialogVerticals(int *titleBottom, int *button1Top, int *button1Bottom,
                           int *button2Top, int *button2Bottom) {
  if (titleBottom) *titleBottom = 176 + 16;
  if (button1Top) *button1Top = CHOICE_BTN1_Y;
  if (button1Bottom) *button1Bottom = CHOICE_BTN1_Y + CHOICE_BTN_H;
  if (button2Top) *button2Top = CHOICE_BTN2_Y;
  if (button2Bottom) *button2Bottom = CHOICE_BTN2_Y + CHOICE_BTN_H;
}

// boton-CTA rojo y grande para evolucionar (pulsa para llamar la atencion)
void drawEvolveButton() {
  uint32_t now = millis();
  int p = (int)(5 * sinf(now * 0.006f));  // late: -5..5
  int x = EVO_BTN_X - p, y = EVO_BTN_Y - p, w = EVO_BTN_W + 2 * p, h = EVO_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 18, UI_BAR_BAD);
  gfx->drawRoundRect(x, y, w, h, 18, UI_WHITE);
  gfx->drawRoundRect(x + 2, y + 2, w - 4, h - 4, 16, UI_WHITE);
  gfx->setTextColor(UI_WHITE);
  gfx->setTextSize(3);
  const char *t = T(S_EVO_TAP);
  uiDrawCenteredIn(t, x, y, w, h);
}

// boton-CTA sombrio de escapada por abandono: "<nombre> se siente abandonado..."
// (final triste: azul-gris oscuro, latido lento y apagado)
void drawRunawayButton() {
  uint32_t now = millis();
  int p = (int)(3 * sinf(now * 0.003f));
  int x = FAR_BTN_X - p, y = FAR_BTN_Y - p, w = FAR_BTN_W + 2 * p, h = FAR_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 16, C565(0x3a, 0x44, 0x5a));
  gfx->drawRoundRect(x, y, w, h, 16, C565(0x70, 0x80, 0x98));
  char buf[52];
  const char *nm = displaySpeciesName(pet.speciesId, pet.nick);
  snprintf(buf, sizeof(buf), T(S_RUNAWAY_BTN), nm);
  gfx->setTextColor(C565(0xc8, 0xd2, 0xe0));
  gfx->setTextSize(2);
  uiDrawCenteredIn(buf, x, y, w, h);
}

// animacion epica de evolucion: halo radial + rayos giratorios + parpadeo del
// sprite acelerando + chispas que salen disparadas + fogonazo final
void drawEvolveFX(uint32_t now) {
  float t = pet.evolveT();          // 0..1
  int cx = CX, cy = PET_GROUND - 96;

  // halo radial que crece y pulsa
  int halo = 36 + (int)(t * 150) + (int)(8 * sinf(now * 0.02f));
  for (int k = 0; k < 4; k++) {
    int r = halo - k * 7;
    if (r > 0) gfx->drawCircle(cx, cy, r, UI_WHITE);
  }
  // rayos giratorios desde el centro del bicho
  float base = now * 0.004f;
  for (int i = 0; i < 12; i++) {
    float a = base + i * (float)(PI / 6);
    int len = 90 + (int)(70 * (0.5f + 0.5f * sinf(now * 0.012f + i)));
    gfx->drawLine(cx, cy, cx + (int)(cosf(a) * len), cy + (int)(sinf(a) * len), UI_WHITE);
  }
  // parpadeo entre la forma ANTERIOR y la NUEVA (siluetas), acelerando; al
  // final (t>0.9) se queda fija en la nueva para el fogonazo de revelado
  int period = 60 + (int)(220 * (1.0f - t));
  bool showOld = t < 0.9f && evoPmd.loaded && ((now / period) % 2) == 0;
  if (showOld) drawPmdActM(evoPmd, PMD_IDLE, cx, PET_GROUND, 0, true, true, 5, 0);
  else drawPmdAct(PMD_IDLE, cx, PET_GROUND, 0, true, true, 5);
  // chispas que salen disparadas
  for (int i = 0; i < 10; i++) {
    float a = i * (float)(PI / 5) + t * 4.0f;
    int d = (int)((now / 14 + i * 33) % 200);
    int sx = cx + (int)(cosf(a) * d), sy = cy + (int)(sinf(a) * d);
    gfx->fillRect(sx - 2, sy - 2, 5, 5, (i & 1) ? C565(0xff, 0xe0, 0x70) : UI_WHITE);
  }
  // fogonazo final antes de revelar la forma nueva
  if (t > 0.9f) gfx->fillCircle(cx, cy, (int)(300 * (t - 0.9f) / 0.1f), UI_WHITE);
}

void drawSparkleParticles(int cx, int groundY, uint32_t now, uint8_t scale) {
  int rx = 68 * scale;
  int ry = 145 * scale;
  for (int i = 0; i < 8; i++) {
    if (((now / 110) + i) % 4 == 0) continue;
    int px = cx - rx + (int)((i * 47UL + now / 18) % (uint32_t)(rx * 2 + 1));
    int py = groundY - 24 - (int)((i * 67UL + now / 13) % (uint32_t)ry);
    int arm = 2 + (i & 1);
    uint16_t color = (i & 1) ? UI_WHITE : C565(0xff, 0xd9, 0x4a);
    gfx->drawFastHLine(px - arm, py, arm * 2 + 1, color);
    gfx->drawFastVLine(px, py - arm, arm * 2 + 1, color);
  }
}

void drawPet() {
  if (pmd.loaded) {
    drawPetPMD();
    return;
  }
  gfx->setTextColor(inkColor());
  gfx->setTextSize(6);
  gfx->setCursor(CX - 18, PET_CY - 80);
  gfx->print("?");
  gfx->setTextSize(2);
  const char *l1 = T(S_NO_SPRITES);
  gfx->setCursor(uiCenterX(l1), PET_CY - 4);
  gfx->print(l1);
  const char *l2 = T(S_LOAD_SPRITES);
  gfx->setCursor(uiCenterX(l2), PET_CY + 20);
  gfx->print(l2);
}

// ---------- escena de bano ----------

void startBath() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony || bathUntil) return;
  beginCareQuiz({ CARE_ACTION_CLEAN, 0 }, false);
}

void startBathAnimation(uint32_t now) {
  bathUntil = now + 3000;
  int cx = (int)beh.x;
  for (auto &b : bubbles) {
    b.x = cx - 70 + random(140);
    b.y = PET_GROUND - random(150);
    b.r = 8 + random(16);
    b.ph = random(64);
  }
}

void drawBath() {
  uint32_t now = millis();
  if (now > bathUntil) {
    bathUntil = 0;
    return;
  }
  uint32_t left = bathUntil - now;
  if (left > 800) {
    // espuma: pompas meciendose y subiendo poco a poco
    float t = now / 220.0f;
    for (auto &b : bubbles) {
      int bx = b.x + (int)(sinf(t + b.ph) * 6);
      int by = b.y - (int)((3000 - left) / 90);
      gfx->fillCircle(bx, by, b.r, UI_WHITE);
      gfx->drawCircle(bx, by, b.r, 0x7E3D);
      gfx->fillCircle(bx - b.r / 3, by - b.r / 3, b.r / 4, UI_BG_DAY);
    }
  } else {
    // las pompas revientan: destellos
    for (int i = 0; i < 8; i++) {
      auto &b = bubbles[i];
      int sx = b.x + (i % 3) * 6 - 6, sy = b.y - 18;
      uint16_t col = (i % 2) ? UI_BAR_WARN : UI_WHITE;
      gfx->fillRect(sx - 6, sy - 1, 13, 3, col);
      gfx->fillRect(sx - 1, sy - 6, 3, 13, col);
    }
  }
}

// ---------- mascota PMD: comportamiento ----------

uint32_t pmdActTotalMs(const PmdAct &a) {
  uint32_t t = 0;
  for (uint8_t i = 0; i < a.frames; i++) t += a.ms[i];
  return t ? t : 100;
}

uint8_t pmdFrameAt(const PmdAct &a, uint32_t t, bool loop) {
  uint32_t total = pmdActTotalMs(a);
  if (!loop && t >= total) return a.frames - 1;
  t %= total;
  uint8_t i = 0;
  while (t >= a.ms[i]) {
    t -= a.ms[i];
    i = (i + 1) % a.frames;
  }
  return i;
}

// dibuja una accion anclada por la base (centro-x, suelo) y devuelve su escala
// dibuja una accion de un PmdMon concreto (m); drawPmdAct usa el global pmd
void drawPmdActM(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t,
                 bool loop, bool sil, uint8_t maxS, uint8_t scaleBonus) {
  const PmdAct &a = m.acts[actId];
  uint8_t s = pmdDisplayScale(m, a, maxS, scaleBonus);
  if (!s) return;
  uint8_t fi = pmdFrameAt(a, t, loop);
  const uint8_t *fr = a.data + (uint32_t)fi * a.w * a.h;
  // anclar por los pies (a.base), no por el alto del lienzo: asi las acciones
  // con padding distinto (Hurt, Eat...) quedan todas a la misma altura de suelo
  int x0 = cx - a.w * s / 2, y0 = groundY - (a.base ? a.base : a.h) * s;
  for (int r = 0; r < a.h; r++) {
    const uint8_t *row = fr + r * a.w;
    for (int c = 0; c < a.w;) {
      uint8_t idx = row[c];
      if (idx == 0xFF) { c++; continue; }
      uint16_t col = sil ? INK_K : m.pal[idx];
      int start = c++;
      while (c < a.w && row[c] != 0xFF &&
             (sil || m.pal[row[c]] == col)) c++;
      gfx->fillRect(x0 + start * s, y0 + r * s, (c - start) * s, s, col);
    }
  }
}
void drawPmdAct(uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  drawPmdActM(pmd, actId, cx, groundY, t, loop, sil, maxS, 0);
}

// elige el siguiente capricho del bicho cuando esta contento
void behNext() {
  uint32_t now = millis();
  beh.t0 = now;
  int r = random(100);
  if (r < 35 && (pmd.has(PMD_WALKL) || pmd.has(PMD_WALKR))) {
    beh.mode = 1;  // paseo
    beh.targetX = 150 + random(176);
    beh.until = now + 15000;
  } else if (r < 60) {
    // gesto aleatorio entre los disponibles
    // (Hop fuera: salta demasiado alto; Sit fuera: mira hacia atras)
    static const uint8_t flair[] = { PMD_POSE, PMD_NOD, PMD_BREATH };
    uint8_t pick[3], n = 0;
    for (uint8_t f : flair)
      if (pmd.has(f)) pick[n++] = f;
    if (n) {
      beh.mode = 2;
      beh.act = pick[random(n)];
      beh.until = now + pmdActTotalMs(pmd.acts[beh.act]);
      return;
    }
    beh.mode = 0;
    beh.until = now + 2000 + random(3000);
  } else {
    beh.mode = 0;  // mirar al frente
    beh.until = now + 2000 + random(3000);
  }
}

void drawPetPMD() {
  uint32_t now = millis();

  if (pet.evolving()) {
    drawEvolveFX(now);
    return;
  }
  if (evoPmd.loaded) evoPmd.unload();  // termino la evolucion: libera la forma anterior

  PetMood m = pet.mood();
  uint8_t act;
  bool loop = true;
  if (m == MOOD_SLEEPING && pmd.has(PMD_SLEEP)) {
    act = PMD_SLEEP;
    beh.mode = 0;
  } else if (m == MOOD_EATING && pmd.has(PMD_EAT)) {
    act = PMD_EAT;
    beh.t0 = 0;
  } else if (m == MOOD_SAD && pmd.has(PMD_HURT)) {
    act = PMD_HURT;
  } else {
    // contento: el planificador decide (idle / paseo / gesto)
    if (now > beh.until) behNext();
    if (beh.mode == 1) {
      float d = beh.targetX - beh.x;
      if (fabsf(d) < 4) {
        behNext();
        act = PMD_IDLE;
      } else {
        beh.x += (d > 0 ? 3.0f : -3.0f);
        act = (d > 0) ? PMD_WALKR : PMD_WALKL;
      }
    } else {
      act = (beh.mode == 2) ? beh.act : PMD_IDLE;
      loop = false;
    }
    if (!pmd.has(act)) act = PMD_IDLE;
  }

  drawPmdAct(act, (int)beh.x, PET_GROUND, now - beh.t0, loop || act == PMD_IDLE, false, 5);

  if (pet.shiny) drawSparkleParticles((int)beh.x, PET_GROUND, now);

  if (pet.showHeart()) drawMap(SPR_HEART, 32, (int)beh.x + 50, PET_GROUND - 190, 2, false);
}

void drawPoops() {
  for (int i = 0; i < pet.poops; i++) {
    drawMap(SPR_POOP, 32, 36 + i * 46, 244, 2, false);
  }
}

void drawBars() {
  drawBar(78, 318, T(S_BAR_FOOD), pet.fullness);
  drawBar(244, 318, T(S_BAR_JOY), pet.joy);
  drawBar(78, 346, T(S_BAR_ENE), pet.energy);
  drawBar(244, 346, T(S_BAR_HYG), pet.hygiene);
}

void drawBar(int x, int y, const char *label, uint8_t val) {
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(x, y);
  gfx->print(label);
  int bx = x + 48, bw = 100, bh = 15;  // +48: deja sitio a etiquetas de 4 letras (EN)
  uint16_t fill = (val >= 50) ? UI_BAR_OK : (val >= 25) ? UI_BAR_WARN : UI_BAR_BAD;
  gfx->fillRoundRect(bx, y, bw, bh, 4, UI_TRACK);
  int fw = (bw - 4) * val / 100;
  if (fw > 0) gfx->fillRoundRect(bx + 2, y + 2, fw, bh - 4, 3, fill);
}

void drawButtons() {
  for (int i = 0; i < BTN_COUNT; i++) {
    bool off = uiButtonDisabled(i);   // durmiendo solo funciona LUZ
    int bx = buttons[i].cx - BTN_HALF, by = buttons[i].cy - BTN_HALF;
    if (!pet.sleeping) gfx->fillRoundRect(bx, by, 2 * BTN_HALF, 2 * BTN_HALF, 14, UI_WHITE);
    gfx->drawRoundRect(bx, by, 2 * BTN_HALF, 2 * BTN_HALF, 14, inkColor());
    if (!off) drawMap(buttons[i].icon, 16, buttons[i].cx - 16, buttons[i].cy - 16, 2, false);
  }
}

const char *eggMsg() {
  switch (pet.eggCracks()) {
    case 0: return T(S_EGG_TOUCH);
    case 1: return T(S_EGG_MOVES);
    default: return T(S_EGG_ALMOST);
  }
}

const char *statusMsg() {
  if (pet.evolving()) return T(S_EVOLVING);
  if (bathUntil) return "Splish splash!";  // onomatopeya universal
  if (pet.sleeping) return "Zzz...";
  if (pet.eating()) return T(S_EATING);
  if (pet.showHeart()) return T(S_LIKES);
  if (pet.fullness < 25) return T(S_HUNGRY);
  if (pet.hygiene < 25) return T(S_NEEDS_BATH);
  if (pet.energy < 25) return T(S_EXHAUSTED);
  if (pet.joy < 25) return T(S_SAD);
  if (pet.weight > 60) return T(S_CHUBBY);
  return T(S_HAPPY);
}

// dibuja un mapa de n x n pixeles escalado; silhouette=true lo pinta en tinta
// An 8bpp indexed avatar, same shape as the badge art: 0xFF is transparent.
void drawAvatar(uint8_t which, int x, int y, int s) {
  const AvatarArt &a = AVATARS[which % AVATAR_COUNT];
  for (int r = 0; r < AVATAR_PX; r++)
    for (int c = 0; c < AVATAR_PX; c++) {
      uint8_t v = a.idx[r * AVATAR_PX + c];
      if (v == 0xFF) continue;
      gfx->fillRect(x + c * s, y + r * s, s, s, a.pal[v]);
    }
}

void drawMap(const char *const *map, int n, int x, int y, int s, bool silhouette) {
  for (int r = 0; r < n; r++) {
    for (int c = 0; c < n; c++) {
      char ch = map[r][c];
      if (ch == '.') continue;
      gfx->fillRect(x + c * s, y + r * s, s, s, silhouette ? INK_K : spriteColor(ch));
    }
  }
}

void drawGenderIcon(PetGender gender, int x, int y, int scale) {
  const char *const *icon = nullptr;
  if (gender == GENDER_MALE) icon = SPR_ICON_GENDER_MALE;
  else if (gender == GENDER_FEMALE) icon = SPR_ICON_GENDER_FEMALE;
  else if (gender == GENDER_NONE) icon = SPR_ICON_GENDER_NONE;
  if (icon) drawMap(icon, 16, x, y, scale, false);
}

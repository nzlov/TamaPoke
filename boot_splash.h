#pragma once

#include <stdint.h>
#include "art_codec.h"

#if __has_include("boot_splash_art.h")
#include "boot_splash_art.h"
#define TAMAPOKE_HAS_BOOT_SPLASH_ART 1
#else
#define TAMAPOKE_HAS_BOOT_SPLASH_ART 0
#endif

// Generated art is inflated into the first byte of each framebuffer pixel,
// then expanded backwards in-place to avoid a second 466x466 allocation.
template <typename Canvas>
void drawBootSplashFrame(Canvas &canvas) {
  canvas.fillScreen(0x0000);

#if TAMAPOKE_HAS_BOOT_SPLASH_ART
  static_assert(BOOT_SPLASH_WIDTH == 466 && BOOT_SPLASH_HEIGHT == 466,
                "boot splash art must match the 466x466 panel");
  static_assert(BOOT_SPLASH_PIXEL_COUNT == 466UL * 466UL,
                "boot splash art has the wrong pixel count");
  static_assert(BOOT_SPLASH_PALETTE_SIZE == 256,
                "boot splash art must use a 256-color palette");
  uint16_t *framebuffer = canvas.getFramebuffer();
  uint8_t *indices = reinterpret_cast<uint8_t *>(framebuffer);
  bool drewArt = artInflate(BOOT_SPLASH_DEFLATE,
                            BOOT_SPLASH_COMPRESSED_SIZE,
                            indices, BOOT_SPLASH_PIXEL_COUNT);
  if (drewArt) {
    for (uint32_t i = BOOT_SPLASH_PIXEL_COUNT; i-- > 0;) {
      framebuffer[i] = BOOT_SPLASH_PALETTE_RGB565[indices[i]];
    }
  }
#else
  bool drewArt = false;
#endif

  if (!drewArt) {
    // Always leave a complete, branded boot screen when generated art is absent.
    const int16_t cx = 233;
    canvas.fillCircle(cx, cx, 231, 0x10C5);
    canvas.drawCircle(cx, cx, 205, 0x29AA);
    canvas.drawCircle(cx, cx, 204, 0x29AA);

    const int16_t ballY = 205;
    const int16_t radius = 92;
    canvas.fillCircle(cx, ballY, radius, 0xFFFF);
    for (int16_t y = -radius; y <= 0; y++) {
      int16_t half = 0;
      while ((half + 1) * (half + 1) + y * y <= radius * radius) half++;
      canvas.fillRect(cx - half, ballY + y, half * 2 + 1, 1, 0xE947);
    }
    canvas.drawCircle(cx, ballY, radius, 0x0862);
    canvas.drawCircle(cx, ballY, radius - 1, 0x0862);
    canvas.fillRect(cx - radius, ballY - 7, radius * 2 + 1, 15, 0x0862);
    canvas.fillCircle(cx, ballY, 29, 0x0862);
    canvas.fillCircle(cx, ballY, 18, 0xFFDF);
    canvas.drawCircle(cx, ballY, 18, 0x0862);

    canvas.setTextColor(0xFFDF);
    canvas.setTextSize(4);
    canvas.setCursor(137, 326);
    canvas.print("TamaPoke");
    canvas.setTextColor(0xBDF7);
    canvas.setTextSize(1);
    canvas.setCursor(200, 370);
    canvas.print("STARTING");
  }

  canvas.flush();
}

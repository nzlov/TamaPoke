#pragma once
#include <stdint.h>

struct BadgeArt {
  const uint16_t *pal;
  const uint8_t *idx;
  uint8_t width;
  uint8_t height;
  uint8_t paletteCount;
};

const BadgeArt &badgeArt(uint8_t region, uint8_t index);

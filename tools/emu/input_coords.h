#pragma once
#include <cstdint>

// GLUE: SDL reports pointer positions in the window's logical coordinate
// space, while the firmware always expects panel pixels. This boundary can go
// away only if SDL is configured to expose a fixed 466x466 logical window.
inline int emuPanelCoord(int pointerPos, int windowExtent, int panelExtent) {
  if (windowExtent <= 0 || panelExtent <= 0) return 0;
  int mapped = (int)((int64_t)pointerPos * panelExtent / windowExtent);
  if (mapped < 0) return 0;
  if (mapped >= panelExtent) return panelExtent - 1;
  return mapped;
}

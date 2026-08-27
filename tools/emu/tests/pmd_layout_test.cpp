#include "sdmon.h"
#include <cstdio>

static int failures = 0;
static void ck(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

int main() {
  PmdMon mon;
  mon.displayScale = 3;
  PmdAct idle;
  idle.w = 24; idle.h = 64; idle.frames = 1;

  ck(pmdDisplayScale(mon, idle, 5, 0) == 3,
     "the runtime uses the generated base scale");
  ck(pmdDisplayScale(mon, idle, 2, 0) == 2,
     "a page-specific maximum still clamps the generated scale");
  PmdAct compact = idle;
  compact.h = 50;
  ck(pmdDisplayScale(mon, compact, 5, 1) == 4,
     "Dynamax adds one scale step after pack metadata is loaded");

  PmdAct tall;
  tall.w = 24; tall.h = 104; tall.frames = 1;
  ck(pmdDisplayScale(mon, tall, 5, 0) == 2,
     "large action canvases retain the shared safety limit");

  mon.displayScale = 0;
  ck(pmdDisplayScale(mon, idle, 5, 0) == 0,
     "missing generated metadata cannot silently use a runtime fallback");
  return failures ? 1 : 0;
}

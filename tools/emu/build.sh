#!/bin/bash
# Builds the TamaPoke desktop emulator from the real firmware sources.
#
#   bash tools/emu/build.sh && tools/emu/tamapoke-emu
#
# Needs SDL2, FreeType and zlib. The hardware is stubbed, so the Arduino
# toolchain is not required.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
cd "$HERE"

TAMAPOKE_VERSION="${TAMAPOKE_VERSION:-$(python3 "$ROOT/tools/firmware_version.py")}"
export TAMAPOKE_VERSION
FW_DEFINE="$(python3 "$ROOT/tools/firmware_version.py" --cpp-define)"

if ! command -v sdl2-config >/dev/null 2>&1; then
  echo "SDL2 not found. macOS: brew install sdl2   Debian: apt install libsdl2-dev" >&2
  exit 1
fi
if ! pkg-config --exists freetype2; then
  echo "FreeType 2 not found. Debian: apt install libfreetype-dev" >&2
  exit 1
fi
if ! pkg-config --exists zlib; then
  echo "zlib not found. Debian: apt install zlib1g-dev" >&2
  exit 1
fi

# The Arduino build generates function prototypes for the .ino automatically;
# a plain g++ build does not, and the sketch calls plenty of functions before
# defining them. Regenerated every build so it can never go stale.
python3 genproto.py "$ROOT/TamaPoke.ino"

# .ino is C++ but g++ will not compile that extension
{ echo '#include "proto.h"'; cat "$ROOT/TamaPoke.ino"; } > sketch.cpp

g++ -std=c++17 -O1 -w \
  -I. -I"$ROOT" \
  "$FW_DEFINE" \
  -DCONTENT_DIR="\"$ROOT/web/packs\"" \
  $(sdl2-config --cflags) $(pkg-config --cflags freetype2 zlib) \
  -o tamapoke-emu \
  sketch.cpp wavout.cpp "$ROOT/gbsynth.cpp" "$ROOT/art_codec.cpp" "$ROOT/content.cpp" "$ROOT/font_engine.cpp" "$ROOT/nature.cpp" "$ROOT/pet.cpp" "$ROOT/quiz.cpp" "$ROOT/i18n.cpp" "$ROOT/party.cpp" "$ROOT/inventory.cpp" "$ROOT/items.cpp" "$ROOT/wild.cpp" "$ROOT/battle.cpp" "$ROOT/link.cpp" "$ROOT/save.cpp" \
  host_impl.cpp font.cpp clock.cpp main_sdl.cpp \
  $(sdl2-config --libs) $(pkg-config --libs freetype2 zlib)

echo "built: $HERE/tamapoke-emu ($TAMAPOKE_VERSION)"

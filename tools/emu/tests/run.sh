#!/bin/bash
# Headless tests. They compile the REAL firmware sources against the emulator's
# hardware stubs, so they assert against Pet/Party/Combatant themselves rather
# than restating their rules -- a harness that re-implements a formula only
# proves the transcription.
#
#   bash tools/emu/tests/run.sh          # everything
#   bash tools/emu/tests/run.sh battle   # just the ones matching "battle"
#
# Two of these exist because nothing else can catch what they catch:
#   flush_test  -- a screen that never calls gfx->flush() leaves the panel
#                  frozen, and headless screenshots CANNOT see it: --shot reads
#                  gfx->buffer() straight out and never consults frameReady.
#                  The training submenu shipped frozen exactly this way.
#   i18n_test   -- every generated UI pack must provide the complete positional
#                  StrId catalogue; a short locale would shift later labels.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
EMU="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$EMU/../.." && pwd)"
FILTER="${1:-}"

command -v sdl2-config >/dev/null || { echo "SDL2 not found (brew install sdl2)" >&2; exit 1; }
pkg-config --exists freetype2 || { echo "FreeType 2 not found (apt install libfreetype-dev)" >&2; exit 1; }

# sketch.cpp + proto.h come from the normal build; this also proves the emulator
# still compiles before anything is tested against it
OUT="$(mktemp -d)"
OUT_FW="$OUT/firmware.log"
OUT_FW_STDOUT="$OUT/firmware.out"
trap 'rm -rf "$OUT"' EXIT

bash "$EMU/build.sh" >/dev/null

# The emulator generates proto.h with every prototype at the top, so it will
# happily compile a sketch that arduino-cli rejects for using a function before
# it is declared. That shipped once. If arduino-cli is installed, the firmware
# build is the one that decides.
if [ "${SKIP_FIRMWARE:-0}" != 1 ] && command -v arduino-cli >/dev/null; then
  FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
  # GLUE: Arduino requires the sketch directory and main .ino to have the same
  # name, while task worktrees deliberately have descriptive directory names.
  # A source-only mirror keeps that tool rule out of the repository layout.
  FW_SKETCH="$OUT/TamaPoke"
  mkdir "$FW_SKETCH"
  for src in "$ROOT"/*.ino "$ROOT"/*.cpp "$ROOT"/*.h; do
    ln -s "$src" "$FW_SKETCH/"
  done
  [ ! -d "$ROOT/src" ] || ln -s "$ROOT/src" "$FW_SKETCH/src"
  [ ! -d "$ROOT/freetype" ] || ln -s "$ROOT/freetype" "$FW_SKETCH/freetype"
  [ ! -d "$ROOT/third_party" ] || ln -s "$ROOT/third_party" "$FW_SKETCH/third_party"
  LIB_ARGS=()
  [ -n "${ARDUINO_LIBRARIES:-}" ] && LIB_ARGS=(--libraries "$ARDUINO_LIBRARIES")
  if ! arduino-cli compile --build-path "$OUT/arduino-build" --fqbn "$FQBN" \
      "${LIB_ARGS[@]}" "$FW_SKETCH" >"$OUT_FW_STDOUT" 2>"$OUT_FW"; then
    echo "=== THE FIRMWARE DOES NOT COMPILE (the emulator does; that is not the same thing)"
    tail -30 "$OUT_FW"
    exit 1
  fi
  grep -E 'Sketch uses|Global variables' "$OUT_FW_STDOUT" "$OUT_FW" || true
fi

# Arrays, not a string: the pack directory has to reach the compiler still
# quoted, and passing these through eval silently strips it.
CORE=("$ROOT/gbsynth.cpp" "$ROOT/content.cpp" "$ROOT/font_engine.cpp" "$ROOT/pet.cpp" "$ROOT/quiz.cpp" "$ROOT/i18n.cpp" "$ROOT/party.cpp" "$ROOT/battle.cpp" "$ROOT/link.cpp" "$ROOT/save.cpp")
read -r -a FT_CFLAGS <<< "$(pkg-config --cflags freetype2)"
read -r -a FT_LIBS <<< "$(pkg-config --libs freetype2)"
FLAGS=(-std=c++17 -O1 -w -I"$EMU" -I"$ROOT" "${FT_CFLAGS[@]}" -DCONTENT_DIR="\"$ROOT/web/packs\"")

# these drive setup()/loop()/render(), so they need the sketch itself
needs_sketch() { case "$1" in touch_test|flush_test|joy_test|anim_test|swipe_test|lan_test|console_test|hit_test|brightness_test|starter_test|recovery_test) return 0;; *) return 1;; esac; }

# and these are standalone: gbsynth.cpp has no Arduino dependency at all, which
# is the point of it -- linking the game core in would only demand stubs for
# symbols the test never calls.
standalone() { case "$1" in synth_test) return 0;; *) return 1;; esac; }

# sprite_test drives PmdMon from a regional pack, so it needs the host's SD
# stubs but none of the sketch.
needs_host() { case "$1" in sprite_test) return 0;; *) return 1;; esac; }

pass=0; fail=0
for src in "$HERE"/*_test.cpp; do
  name="$(basename "$src" .cpp)"
  [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
  extra=()
  needs_sketch "$name" && extra=("$EMU/sketch.cpp" "$EMU/host_impl.cpp" "$EMU/font.cpp" "$EMU/clock.cpp")
  needs_host "$name" && extra=("$EMU/host_impl.cpp" "$EMU/font.cpp")
  srcs=("${CORE[@]}")
  standalone "$name" && srcs=("$ROOT/gbsynth.cpp")
  test_flags=("${FLAGS[@]}")
  if [ "$name" = recovery_test ]; then
    mkdir -p "$OUT/empty-packs"
    test_flags+=(-UCONTENT_DIR -DCONTENT_DIR="\"$OUT/empty-packs\"")
  elif [ "$name" = corrupt_test ]; then
    python3 "$HERE/make_corrupt_fixture.py" "$ROOT/web/packs" "$OUT/corrupt-packs"
    test_flags+=(-UCONTENT_DIR -DCONTENT_DIR="\"$OUT/corrupt-packs\"")
  elif [ "$name" = pack_reader_test ]; then
    mkdir -p "$OUT/reader-packs"
    python3 "$HERE/make_pack_fixture.py" "$OUT/reader-packs/reader-test.tregion"
    test_flags+=(-UCONTENT_DIR -DCONTENT_DIR="\"$OUT/reader-packs\""
                 -DPACK_READER_FIXTURE="\"$OUT/reader-packs/reader-test.tregion\"")
    extra+=(-Wl,--wrap=fread -Wl,--wrap=fseek)
  elif [ "$name" = quiz_content_test ]; then
    mkdir -p "$OUT/quiz-packs"
    python3 "$HERE/make_quiz_fixture.py" "$OUT/quiz-packs/quiz-reader.tquiz"
    test_flags+=(-UCONTENT_DIR -DCONTENT_DIR="\"$OUT/quiz-packs\""
                 -DQUIZ_READER_FIXTURE="\"$OUT/quiz-packs/quiz-reader.tquiz\"")
  fi
  # every test starts from a clean NVS so one cannot leak state into the next
  rm -f "$OUT/tamapoke.nvs"
  if ! g++ "${test_flags[@]}" -o "$OUT/$name" "$src" "${srcs[@]}" "${extra[@]}" "${FT_LIBS[@]}" 2>"$OUT/$name.log"; then
    echo "=== $name: DID NOT COMPILE"; tail -5 "$OUT/$name.log"; fail=$((fail+1)); continue
  fi
  echo "=== $name"
  # pipefail matters: piping the test through grep would otherwise report
  # grep's exit status and every failure would be counted as a pass
  if (cd "$OUT" && set -o pipefail && "./$name" 2>&1 | grep -vE '^(TamaPoke fw|emu:|RTC )'); then
    pass=$((pass+1))
  else
    echo "    ^ $name FAILED"
    fail=$((fail+1))
  fi
done

echo
echo "suites passed: $pass, failed: $fail"
[ "$fail" -eq 0 ]

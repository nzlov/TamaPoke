# TamaPoke desktop emulator

Runs the **real firmware** on your computer, in a window you can click.

![emulator](https://img.shields.io/badge/needs-SDL2%20%2B%20FreeType-1793D1)

```bash
brew install sdl2 freetype # macOS   (Debian: apt install libsdl2-dev libfreetype-dev)
bash tools/emu/build.sh
tools/emu/tamapoke-emu --scale 2 --fast 60
```

It compiles `TamaPoke.ino`, `pet.cpp`, `i18n.cpp` and `party.cpp` **unmodified**.
Only the hardware layer is replaced, so what you see is what the panel draws —
the same 466×466 RGB565 framebuffer and the same code paths. Latin UI packs keep
the compact 5×7 face. The Chinese `.tui` carries a hinted Noto Sans CJK Medium
OpenType subset and its pixel-size table; both desktop and ESP32 render it with
FreeType and the same bounded glyph cache.

## Why

The firmware is otherwise only testable by flashing a board and squinting at it.
This gives you a UI you can iterate on in seconds, and it makes layout mistakes
(text running off the round bezel, bars overflowing) obvious before they reach
hardware. The game logic can also be driven headlessly — see *Headless* below.

**It is not a substitute for the real thing.** Timing, touch behaviour, DMA
tearing, PSRAM pressure, audio and battery all only exist on the board.

## Using it

| | |
|---|---|
| **Click** | touch |
| **Drag** | swipe (gestures resolve on release, as on the device) |
| **Hold 3 s** on the pet | the release dialog |
| **Type in the terminal** | the serial console — `STATS`, `IV 31 31 31 31`, `EGG 150 1`, `PARTY`, `LVL 73`, `WIPE`… |
| **Esc** or close the window | quit (state is saved) |

Pixels outside the round bezel are **dimmed**, not hidden: the framebuffer is
square but the panel is a circle, so this shows you exactly what would be
clipped on real hardware.

### Options

| Flag | Meaning |
|---|---|
| `--scale N` | window zoom (default 2) |
| `--fast N` | run the clock N× faster — `--fast 60` turns an in-game minute into a second, so a full 3-day life takes about an hour. The speed-up is **suspended while you are touching the panel**: the firmware times taps and swipes off the same `millis()`, so scaling it during a gesture would shrink the tap window (`dt < 1500`) to `1500/N` real ms and make the screen unclickable |
| `--save FILE` | where to persist NVS (default `tamapoke.nvs` in the cwd) |
| `--lang LOCALE` | start with an installed runtime-pack locale, such as `zh-CN` |
| `--wipe` | delete the save first |

### Headless captures

Renders one screen to a PPM and exits — no display needed, so it works over SSH
and in CI:

```bash
tools/emu/tamapoke-emu --shot battle --lang zh-CN --lvl 73 --iv 31 --dex 149 --out shot.ppm
sips -s format png shot.ppm --out shot.png     # macOS; or use ImageMagick
```

`--shot` accepts the normal pages plus their actionable overlays and result
states. Representative names include `main`, `evolvecta`, `farewellcta`,
`runawaycta`, `sleep`, `feedmenu`,
`choicerelease`, `choicefarewell`, `choiceevolve`, `bath`, `keyboard`, `battle`, `btlmoves`,
`btlmechanics`, `btlzmove`, `btlnormal`, `btldynamax`, `btlmega`,
`btlrevive`, `profile`, `gallery`, `clock`, `menu`, `navmenu`, `mainroster`, `box`, `boxreplace`, `boxwithdraw`, `boxdeposit`, `bag`,
`bagdetail`, `train`, `ballgame`, `sack`, `speed`, `quiz`, `quizcorrect`,
`battlecenter`, `wildfight`, `wilditems`, `capture`, `reward`,
`wilddetail`,
`sparkle`, `color`, `bothrare` (all three legacy scene names now show the
combined shiny effect), `egg`,
`language`, `region`, `starter`, `dexdetail`, and `moveinfo`.
`--lvl`, `--iv` and
`--dex` set up the pet first.

## How it works

| File | Stands in for |
|---|---|
| `Arduino.h` | `random`, `String`, a `Serial` wired to stdin, and `attachInterrupt` (the SDL layer raises the touch INT by hand — the sketch gates `handleTouch` on it) |
| `clock.cpp` | `millis`, including `--fast` scaling; its own file so the headless tests link the same clock the window runs |
| `Preferences.h` | NVS, backed by a file so your pet survives restarts |
| `Arduino_GFX_Library.h` | the canvas — every primitive the sketch uses, into an RGB565 buffer |
| `TouchDrvCSTXXX.hpp` | the CST9217, fed by the mouse |
| `host_impl.cpp` | SD (reads sprites from disk), RTC, battery, audio |
| `font.cpp` | the classic 5×7 GFX glyphs, so text metrics match exactly |
| system FreeType | the same OpenType rasterizer used by the ESP32's minimal vendored build |
| `genproto.py` | the prototypes the Arduino build normally generates for you |

Everything except `font.cpp` and `genproto.py` is a
stub; the game itself is the real thing.

UI, move, species and sprite data are read from `web/packs/`, the same generated
packages deployed to the board. Run `tools/gen_data_packs.py` first. Missing
packages enter the same recovery path as a board with no usable data packs.

## Credits

`font.cpp` is the classic 5×7 bitmap font from **Adafruit_GFX**, © 2012 Adafruit
Industries, BSD licence — full notice at the top of that file.

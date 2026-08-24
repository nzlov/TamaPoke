#!/usr/bin/env python3
"""Generate the emulator's exact CJK subset from an Arduino_GFX U8g2 font.

Usage:
  python3 tools/emu/gen_cjk_font.py /path/to/u8g2_font_unifont_h_chinese4.h

The committed output keeps desktop builds self-contained. It contains printable
ASCII plus every non-Latin character used by i18n.cpp and TamaPoke.ino.
"""
from argparse import ArgumentParser
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
MAX_ROWS = 16


def c_string_bytes(path: Path) -> bytearray:
    text = path.read_text(encoding="latin1")
    body = text.split("=", 1)[1].split("#endif", 1)[0]
    tokens = re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', body)
    data = bytearray()
    escapes = {"n": 10, "r": 13, "t": 9, "\\": 92, '"': 34, "'": 39}
    for token in tokens:
        i = 0
        while i < len(token):
            if token[i] != "\\":
                data.append(ord(token[i]))
                i += 1
                continue
            i += 1
            if token[i] in "01234567":
                end = i
                while end < len(token) and end < i + 3 and token[end] in "01234567":
                    end += 1
                data.append(int(token[i:end], 8))
                i = end
            else:
                if token[i] not in escapes:
                    raise ValueError(f"unsupported C escape: \\{token[i]}")
                data.append(escapes[token[i]])
                i += 1
    data.append(0)  # implicit terminator included in the declared C array size
    return data


def required_codepoints() -> list[int]:
    source = "\n".join(
        (ROOT / name).read_text(encoding="utf-8")
        for name in ("i18n.cpp", "TamaPoke.ino")
    )
    strings = re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', source)
    non_latin = {ord(ch) for string in strings for ch in string if ord(ch) > 255}
    return sorted(set(range(32, 127)) | non_latin)


class BitReader:
    def __init__(self, data: bytearray, pos: int):
        self.data = data
        self.pos = pos
        self.bit = 0

    def unsigned(self, count: int) -> int:
        value = self.data[self.pos] >> self.bit
        next_bit = self.bit + count
        if next_bit >= 8:
            shift = 8 - self.bit
            self.pos += 1
            value |= self.data[self.pos] << shift
            next_bit -= 8
        self.bit = next_bit
        return value & ((1 << count) - 1)

    def signed(self, count: int) -> int:
        return self.unsigned(count) - (1 << (count - 1))


class U8g2Font:
    def __init__(self, data: bytearray):
        self.data = data

    def word(self, pos: int) -> int:
        return (self.data[pos] << 8) | self.data[pos + 1]

    def glyph_ptr(self, codepoint: int) -> int | None:
        if codepoint <= 255:
            pos = 23
            if codepoint >= ord("a"):
                pos += self.word(19)
            elif codepoint >= ord("A"):
                pos += self.word(17)
            while self.data[pos + 1]:
                if self.data[pos] == codepoint:
                    return pos + 2
                pos += self.data[pos + 1]
            return None

        pos = 23 + self.word(21)
        lookup = pos
        while True:
            pos += self.word(lookup)
            end_codepoint = self.word(lookup + 2)
            lookup += 4
            if end_codepoint >= codepoint:
                break
        while True:
            encoded = self.word(pos)
            if encoded == 0:
                return None
            if encoded == codepoint:
                return pos + 3
            pos += self.data[pos + 2]

    def decode(self, codepoint: int):
        ptr = self.glyph_ptr(codepoint)
        if ptr is None:
            return None
        bits = BitReader(self.data, ptr)
        width = bits.unsigned(self.data[4])
        height = bits.unsigned(self.data[5])
        x_offset = bits.signed(self.data[6])
        y_offset = bits.signed(self.data[7])
        advance = bits.signed(self.data[8])
        if width > 16 or height > MAX_ROWS:
            raise ValueError(f"U+{codepoint:04X}: unsupported {width}x{height} glyph")

        rows = [0] * MAX_ROWS
        x = y = 0

        def run(length: int, foreground: bool):
            nonlocal x, y
            while length:
                remaining = width - x
                current = min(length, remaining)
                if foreground and y < height:
                    for pixel in range(x, x + current):
                        rows[y] |= 1 << pixel
                if length < remaining:
                    x += length
                    break
                length -= remaining
                x = 0
                y += 1

        while y < height:
            background = bits.unsigned(self.data[2])
            foreground = bits.unsigned(self.data[3])
            while True:
                run(background, False)
                run(foreground, True)
                if bits.unsigned(1) == 0:
                    break
        return width, height, x_offset, y_offset, advance, rows


def render(font: U8g2Font, source_name: str, codepoints: list[int]) -> str:
    lines = [
        f"// Generated by gen_cjk_font.py from {source_name}.",
        "// GNU Unifont is available under SIL OFL 1.1 or GPLv2+ with the",
        "// GNU Font Embedding Exception; see the source font header.",
        "static const EmuCjkGlyph EMU_CJK_GLYPHS[] = {",
    ]
    missing = []
    for codepoint in codepoints:
        glyph = font.decode(codepoint)
        if glyph is None:
            missing.append(codepoint)
            continue
        width, height, x_offset, y_offset, advance, rows = glyph
        row_text = ", ".join(f"0x{row:04X}" for row in rows)
        lines.append(
            f"  {{ 0x{codepoint:04X}, {width}, {height}, {x_offset}, {y_offset}, "
            f"{advance}, {{ {row_text} }} }},"
        )
    if missing:
        chars = " ".join(f"U+{codepoint:04X}" for codepoint in missing)
        raise ValueError(f"CJK font is missing required glyphs: {chars}")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = ArgumentParser()
    parser.add_argument("font", type=Path)
    parser.add_argument("--output", type=Path, default=HERE / "font_cjk_data.inc")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    codepoints = required_codepoints()
    output = render(U8g2Font(c_string_bytes(args.font)), args.font.stem, codepoints)
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != output:
            raise SystemExit(f"stale generated font subset: {args.output}")
    else:
        args.output.write_text(output, encoding="utf-8")
    print(f"PASS: {len(codepoints)} CJK glyphs -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

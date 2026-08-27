#!/usr/bin/env python3
"""Battle backgrounds: tools/backs/*.png -> backs.h

    python3 tools/gen_backs.py

Twelve 240x112 scenes, one day and one night per biome, packed as DEFLATE-compressed
8bpp indices with a per-image RGB565 palette. 8bpp rather than 4bpp because the
worst image uses 65 colours and quantising a background to 16 would band the
gradients. The current scene is inflated into PSRAM on demand, so a battle still
looks like somewhere even on a board with no SD card.

The rows are emitted as-is. The drawing side turns them into runs of identical
indices at draw time -- this art is flat pixel art with long horizontal runs, so
that is far cheaper than a fillRect per pixel.

Biome mapping is by closest available scene: dex.h has six biomes and the art
has ten kinds, so forest borrows PATH (a wooded route) and volcano borrows CAVE
(the only rocky interior). Nothing is volcanic in the set.
"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'backs')
OUT = os.path.join(HERE, '..', 'backs.h')

# biome index in dex.h -> (day file, night file)
BIOMES = [
    ('TALL GRASS', 'pradera / meadow'),
    ('BEACH',      'playa / beach'),
    ('PATH',       'bosque / forest -- a wooded route is the closest scene'),
    ('CAVE',       'volcan / volcano -- no volcanic scene exists; rock is closest'),
    ('MOUNTAIN',   'montana / mountain'),
    ('SNOW',       'nieve / snow'),
]


def read_png(path):
    d = open(path, 'rb').read()
    pos, idat, w, h, bd, ct = 8, b'', 0, 0, 0, 0
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', data[:10])
        elif typ == b'IDAT':
            idat += data
        pos += 12 + ln
    if bd != 8:
        raise SystemExit('%s: only 8-bit channels supported' % path)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    raw = zlib.decompress(idat)
    rows, prev, i = [], bytearray(w * ch), 0
    for _ in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i + w * ch]); i += w * ch
        for x in range(len(line)):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if f == 1:   line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(bytes(line)); prev = line
    return w, h, ch, rows


def to_indexed(w, h, ch, rows):
    pal, idx = [], []
    lut = {}
    for line in rows:
        for x in range(0, len(line), ch):
            r, g, b = line[x], line[x + 1], line[x + 2]
            c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            if c not in lut:
                if len(pal) >= 256:
                    raise SystemExit('more than 256 colours')
                lut[c] = len(pal); pal.append(c)
            idx.append(lut[c])
    return pal, idx


def main():
    out = ['// GENERADO por tools/gen_backs.py desde tools/backs/*.png - no editar',
           '#pragma once', '#include <stdint.h>', '',
           '// Battle backgrounds, DEFLATE-compressed 8bpp indices with RGB565 palettes.',
           '// Indexed by biome (see dex.h) then 0 = day, 1 = night.', '']
    names = []
    total = 0
    for bi, (base, note) in enumerate(BIOMES):
        for ni, suffix in enumerate(('', ' NIGHT')):
            path = os.path.join(SRC, base + suffix + '.png')
            w, h, ch, rows = read_png(path)
            pal, idx = to_indexed(w, h, ch, rows)
            nm = 'BACK_%d_%d' % (bi, ni)
            raw = bytes(idx)
            compressed = zlib.compress(raw, 9)
            if zlib.decompress(compressed) != raw:
                raise SystemExit('%s: compression round-trip failed' % path)
            names.append((nm, w, h, len(pal), len(compressed)))
            total += len(compressed) + len(pal) * 2
            out.append('// %s  (%s)' % (base + suffix, note))
            out.append('static const uint16_t %s_PAL[%d] = {' % (nm, len(pal)))
            out.append('  ' + ', '.join('0x%04X' % c for c in pal))
            out.append('};')
            out.append('static const uint8_t %s_DEFLATE[%d] = {' %
                       (nm, len(compressed)))
            for r in range(0, len(compressed), 40):
                out.append('  ' + ','.join(str(v) for v in compressed[r:r + 40]) + ',')
            out.append('};')
            out.append('')
    out.append('struct BackScene {')
    out.append('  const uint16_t *pal;')
    out.append('  const uint8_t *compressed;')
    out.append('  uint32_t compressedSize;')
    out.append('  uint16_t w, h, palCount;')
    out.append('};')
    out.append('#define BACK_BIOMES %d' % len(BIOMES))
    out.append('static const BackScene BACKS[BACK_BIOMES][2] = {')
    for bi in range(len(BIOMES)):
        row = []
        for ni in range(2):
            nm, w, h, pc, compressed_size = names[bi * 2 + ni]
            row.append('{ %s_PAL, %s_DEFLATE, %d, %d, %d, %d }' %
                       (nm, nm, compressed_size, w, h, pc))
        out.append('  { %s },' % ', '.join(row))
    out.append('};')
    open(OUT, 'w').write('\n'.join(out) + '\n')
    print('backgrounds: %d, %.1f KB of compressed data -> %s'
          % (len(names), total / 1024.0, os.path.normpath(OUT)))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Genera la entrada thumbs.bin para los paquetes regionales.

Se derivan del frame frontal (Idle, frame 0) de los sprites PMD ya empaquetados
(tools/sdcard/mons/pNNN.bin, formato TPK2) -> miniaturas legales (CC BY-NC), mismo
estilo que la pantalla principal. Formato TPTH (little-endian):

  char[4] "TPTH"
  uint16  count
  uint32  offset[count]    (desde el inicio; 0 cuando no existe arte comunitario)
  blobs:  u8 w, u8 h, u8 palCount, u16 pal[palCount], u8 data[w*h] (0xFF transp.)

  python3 tools/make_thumbs.py
"""
import os
import json
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(HERE, 'pokemon_data.json'), encoding='utf-8') as source:
    DEX_COUNT = len(json.load(source)['species'])


DIR = os.path.join(HERE, 'sdcard', 'mons')
CELL = 40


def read_pmd_idle_frame0(path):
    """Frame 0 de la accion Idle (id 0 = vista frontal) de un sprite PMD TPK2."""
    with open(path, 'rb') as f:
        buf = f.read()
    if buf[:4] != b'TPK2':
        raise ValueError('magic TPK2')
    nacts = buf[4]
    (palcount,) = struct.unpack_from('<H', buf, 5)
    pal = list(struct.unpack_from(f'<{palcount}H', buf, 7))
    p = 7 + palcount * 2
    for _ in range(nacts):
        aid, w, h, nf = buf[p], buf[p + 1], buf[p + 2], buf[p + 3]
        p += 4 + nf * 2  # cabecera + ms[]
        if aid == 0:     # PMD_IDLE
            return w, h, pal, buf[p:p + w * h]  # frame 0
        p += w * h * nf
    raise ValueError('sin accion Idle (id 0)')


def shrink(w, h, pal, data):
    # escala a CELL x CELL con vecino mas cercano, conservando aspecto
    scale = min(CELL / w, CELL / h, 1.0)
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    out = bytearray()
    used = {}
    newpal = []
    for y in range(nh):
        sy = min(h - 1, int(y / scale)) if scale < 1 else y
        for x in range(nw):
            sx = min(w - 1, int(x / scale)) if scale < 1 else x
            idx = data[sy * w + sx]
            if idx == 0xFF:
                out.append(0xFF)
                continue
            c = pal[idx]
            if c not in used:
                used[c] = len(newpal)
                newpal.append(c)
            out.append(used[c])
    return nw, nh, newpal, bytes(out)


def main():
    blobs = []
    for dex in range(1, DEX_COUNT + 1):
        path = os.path.join(DIR, f'p{dex:03d}.bin')
        if not os.path.exists(path):
            blobs.append(None)
            continue
        w, h, pal, data = read_pmd_idle_frame0(path)
        nw, nh, npal, ndata = shrink(w, h, pal, data)
        if len(npal) > 255:
            raise ValueError(f'{dex}: paleta {len(npal)}')
        blob = struct.pack('<3B', nw, nh, len(npal))
        blob += struct.pack(f'<{len(npal)}H', *npal)
        blob += ndata
        blobs.append(blob)

    head = 4 + 2 + 4 * DEX_COUNT
    offsets, pos = [], head
    for b in blobs:
        offsets.append(pos if b is not None else 0)
        if b is not None:
            pos += len(b)

    out = os.path.join(DIR, 'thumbs.bin')
    with open(out, 'wb') as f:
        f.write(b'TPTH')
        f.write(struct.pack('<H', DEX_COUNT))
        f.write(struct.pack('<%dI' % DEX_COUNT, *offsets))
        for b in blobs:
            if b is not None:
                f.write(b)
    available = sum(b is not None for b in blobs)
    print(f"guardado {out}: {pos / 1024:.0f} KB, {available}/{len(blobs)} miniaturas")


if __name__ == '__main__':
    main()

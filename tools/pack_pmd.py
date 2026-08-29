#!/usr/bin/env python3
"""Genera entradas TPK2 desde las fuentes PMD locales de pokemon_data.json.

Genera tools/sdcard/mons/pNNN.bin, psNNN.bin shiny y, cuando SpriteCollab
ofrece una diferencia visual, pfNNN.bin / pfsNNN.bin para hembras:

  char[4] "TPK2"
  u8  nActs
  u16 palCount
  u16 pal[palCount]                  (RGB565)
  por accion:
    u8 id, u8 w, u8 h, u8 nFrames
    u16 ms[nFrames]
    u8 data[w*h*nFrames]             (indices, 0xFF transparente)

Acciones: 0 Idle, 1 WalkL, 2 WalkR, 3 Sleep, 4 Eat, 5 Hurt, 6 Attack,
7 Pose, 8 Hop, 9 Nod, 10 DeepBreath, 11 Sit. Las que falten se omiten.

  python3 tools/pack_pmd.py             # el dex entero, normal + shiny
  python3 tools/pack_pmd.py --workers 10 # diez tareas concurrentes (predeterminado)
  python3 tools/pack_pmd.py kanto       # solo una region (johto, hoenn)
  python3 tools/pack_pmd.py 7 25        # dex concretos
  python3 tools/pack_pmd.py normal 1 4  # solo normales
  python3 tools/pack_pmd.py --mega 6    # formas Mega configuradas, pmNNN-forma.bin

Necesita Pillow: pip3 install Pillow
"""
import argparse
import json
import os
import struct
import sys
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(HERE, 'pokemon_data.json'), encoding='utf-8') as source:
    POKEMON_DATA = json.load(source)
DEX_COUNT = len(POKEMON_DATA['species'])
SPECIES_BY_ID = {row['id']: row for row in POKEMON_DATA['species']}

OUT = os.path.join(os.path.dirname(__file__), 'sdcard', 'mons')
ART_SOURCE = POKEMON_DATA['artSource']
ART_ROOT = os.path.join(HERE, ART_SOURCE['root'])
PMD_REVISION = ART_SOURCE['revision']
SLOW = 1.4          # el ritmo original de PMD se siente rapido en el tamagotchi
MIN_MS = 70
ALPHA_T = 128

# (id, nombre de accion, fila del sheet) — fila None = 0 si solo hay una
# direcciones del sheet: 0 abajo, 2 DERECHA, 6 IZQUIERDA (verificado en placa)
ACTIONS = [
    (0, 'Idle', 0),
    (1, 'Walk', 6),   # izquierda
    (2, 'Walk', 2),   # derecha
    (3, 'Sleep', 0),
    (4, 'Eat', 0),
    (5, 'Hurt', 0),
    (6, 'Attack', 0),
    (7, 'Pose', 0),
    (8, 'Hop', 0),
    (9, 'Nod', 0),
    (10, 'DeepBreath', 0),
    (11, 'Sit', 0),
]
PMD_NACTS = len(ACTIONS)
PACK_ACTIONS = [(aid, name, row, False) for aid, name, row in ACTIONS]
PACK_ACTIONS += [
    (PMD_NACTS + aid, name, 4, True)
    for aid, name, _row in ACTIONS if aid in (0, 5, 6)
]


def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


def load_animdata(folder):
    path = os.path.join(folder, 'AnimData.xml')
    anims = {}
    tree = ET.parse(path)
    for a in tree.getroot().find('Anims'):
        name = a.find('Name').text
        if a.find('FrameWidth') is None:
            # alias (CopyOf): apunta a otra animacion
            copy = a.find('CopyOf')
            if copy is not None:
                anims[name] = ('copy', copy.text)
            continue
        anims[name] = (int(a.find('FrameWidth').text), int(a.find('FrameHeight').text),
                       [int(d.text) for d in a.find('Durations')], name)
    def resolve(name, seen=None):
        seen = set() if seen is None else seen
        if name in seen:
            return None
        seen.add(name)
        value = anims.get(name)
        if isinstance(value, tuple) and value and value[0] == 'copy':
            return resolve(value[1], seen)
        return value

    return {name: resolve(name) for name in anims}


def pack(dexnum, shiny=False, female=False, form_art=None, form_name='standard'):
    species_art = SPECIES_BY_ID[dexnum].get('art', {})
    if form_art:
        source_ref = form_art.get('shiny' if shiny else 'normal')
        suffix = f'm-{form_name}' + ('-shiny' if shiny else '')
    elif female:
        source_ref = species_art.get('femaleShiny' if shiny else 'female')
        suffix = ('f' if female else '') + ('s' if shiny else '')
    else:
        source_ref = species_art.get('shiny' if shiny else 'normal')
        suffix = 's' if shiny else ''
    if not source_ref:
        raise RuntimeError('sin fuente local')
    folder = os.path.join(ART_ROOT, source_ref)
    if not os.path.isfile(os.path.join(folder, 'AnimData.xml')):
        raise RuntimeError('sin AnimData.xml local')
    anims = load_animdata(folder)

    colmap, pal = {}, []
    packed = []
    for aid, name, row, back_only in PACK_ACTIONS:
        if name not in anims or anims[name] is None:
            continue
        fw, fh, durs, srcname = anims[name]
        png = os.path.join(folder, f'{srcname}-Anim.png')
        if not os.path.isfile(png):
            continue
        im = Image.open(png).convert('RGBA')
        rows = im.size[1] // fh
        if back_only and rows <= row:
            continue
        r = row if rows > row else 0
        nf = min(len(durs), im.size[0] // fw, 24)
        data = bytearray()
        for i in range(nf):
            fr = im.crop((i * fw, r * fh, (i + 1) * fw, (r + 1) * fh))
            pixels = (fr.get_flattened_data()
                      if hasattr(fr, 'get_flattened_data') else fr.getdata())
            for px in pixels:
                if px[3] < ALPHA_T:
                    data.append(0xFF)
                    continue
                k = px[:3]
                if k not in colmap:
                    if len(pal) >= 255:
                        # cercano (raro en PMD, paletas cortas)
                        k2 = min(colmap, key=lambda c: sum((a-b)**2 for a, b in zip(c, k)))
                        colmap[k] = colmap[k2]
                    else:
                        colmap[k] = len(pal)
                        pal.append(k)
                data.append(colmap[k])
        ms = [max(MIN_MS, round(d * 1000 / 60 * SLOW)) for d in durs[:nf]]
        packed.append((aid, fw, fh, nf, ms, bytes(data)))

    if not any(p[0] == 0 for p in packed):
        raise RuntimeError('sin Idle')

    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, f'p{suffix}{dexnum:03d}.bin') if not form_art else \
        os.path.join(OUT, f'pm{dexnum:03d}-{form_name}' + ('-shiny' if shiny else '') + '.bin')
    with open(path, 'wb') as f:
        f.write(b'TPK2')
        f.write(struct.pack('<BH', len(packed), len(pal)))
        for r, g, b in pal:
            f.write(struct.pack('<H', rgb565(r, g, b)))
        for aid, fw, fh, nf, ms, data in packed:
            f.write(struct.pack('<4B', aid, fw, fh, nf))
            f.write(struct.pack(f'<{nf}H', *ms))
            f.write(data)
    kb = os.path.getsize(path) / 1024
    print(f"  -> {os.path.basename(path)}: {len(packed)} acciones, "
          f"{len(pal)} colores, {kb:.0f} KB")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Empaqueta sprites PMD en formato TPK2')
    parser.add_argument('--workers', type=int, default=10,
                        help='tareas concurrentes de conversion (predeterminado: 10)')
    parser.add_argument('--mega', action='store_true',
                        help='pack configured local Mega forms')
    parser.add_argument('--mega-report', metavar='PATH',
                        help='write configured Mega sprite coverage as JSON')
    parser.add_argument('--report', metavar='PATH',
                        help='write base sprite packing results as JSON')
    parser.add_argument('selectors', nargs='*',
                        help='regiones, numeros del dex o "normal"')
    options = parser.parse_args()
    if options.workers < 1:
        parser.error('--workers debe ser al menos 1')
    if options.mega and options.report:
        parser.error('--report is for base sprites; use --mega-report with --mega')
    args = options.selectors
    solo_normal = 'normal' in args
    # The whole dex by default, not the old hardcoded 151. A region is easy to
    # ask for on its own, since the fetch is long and most people want Kanto:
    #   python3 tools/pack_pmd.py kanto
    span = {
        region['name'].lower(): tuple(region['range'])
        for region in POKEMON_DATA['regions']
    }
    picked = [span[a] for a in args if a in span]
    nums = [int(a) for a in args if a.isdigit()]
    if not nums:
        if picked:
            nums = [d for lo, hi in picked for d in range(lo, hi + 1)]
        else:
            nums = list(range(1, DEX_COUNT + 1))
    if options.mega:
        mega_data = [
            dict(form, species=species['id'])
            for species in POKEMON_DATA['species']
            for form in species['megaForms']
        ]
        gigantamax_data = [
            species['id'] for species in POKEMON_DATA['species']
            if species['gigantamax']
        ]
        selected = [row for row in mega_data if int(row['species']) in nums]
        jobs = []
        for row in selected:
            art = row.get('art')
            if not art or not art.get('normal'):
                continue
            form_name = row.get('form', 'standard')
            jobs.append((int(row['species']), False, False, art, form_name))
            if art.get('shiny'):
                jobs.append((int(row['species']), True, False, art, form_name))
        if options.mega_report:
            report = {
                'sourceRevision': PMD_REVISION,
                'forms': len(selected),
                'normalAvailable': sum(bool(row.get('art', {}).get('normal')) for row in selected),
                'shinyAvailable': sum(bool(row.get('art', {}).get('shiny')) for row in selected),
                'missingNormal': [
                    {'species': int(row['species']), 'form': row.get('form', 'standard')}
                    for row in selected if not row.get('art', {}).get('normal')
                ],
                'missingShiny': [
                    {'species': int(row['species']), 'form': row.get('form', 'standard')}
                    for row in selected if not row.get('art', {}).get('shiny')
                ],
                'gigantamaxForms': len(gigantamax_data),
                'gigantamaxNormalAvailable': 0,
                'gigantamaxShinyAvailable': 0,
                'missingGigantamaxNormal': gigantamax_data,
                'missingGigantamaxShiny': gigantamax_data,
            }
            with open(options.mega_report, 'w', encoding='utf-8') as report_file:
                json.dump(report, report_file, ensure_ascii=False, indent=2)
                report_file.write('\n')
    else:
        jobs = []
        for n in nums:
            art = SPECIES_BY_ID[n].get('art', {})
            jobs.extend((n, shiny, False, None, 'standard')
                        for shiny in ([False] if solo_normal else [False, True]))
            if art.get('female'):
                jobs.append((n, False, True, None, 'standard'))
            if not solo_normal and art.get('femaleShiny'):
                jobs.append((n, True, True, None, 'standard'))

    def run_job(job):
        n, sh, female, form_art, form_name = job
        try:
            label = (' Mega' if form_art else '') + \
                    (' female' if female else '') + (' shiny' if sh else '')
            print(f"#{n:03d}{label}", flush=True)
            pack(n, sh, female, form_art, form_name)
            return None
        except Exception as e:
            # Female art only exists for species with an authored visual
            # difference. Its absence is a supported fallback to the base art.
            if female:
                print(f"#{n:03d}{label} sin variante", flush=True)
                return None
            print(f"#{n:03d}{label} FALLO: {e}", flush=True)
            return n, sh

    with ThreadPoolExecutor(max_workers=options.workers) as executor:
        fallos = [failed for failed in executor.map(run_job, jobs) if failed]
    if options.report:
        failed_set = set(fallos)
        base_jobs = [(number, shiny) for number, shiny, female, art, _form in jobs
                     if not female and not art]
        report = {
            'sourceRevision': PMD_REVISION,
            'jobs': len(base_jobs),
            'succeeded': len(base_jobs) - len(fallos),
            'normalSucceeded': sum(
                not shiny and (number, shiny) not in failed_set
                for number, shiny in base_jobs
            ),
            'shinySucceeded': sum(
                shiny and (number, shiny) not in failed_set
                for number, shiny in base_jobs
            ),
            'failures': [
                {'species': number, 'shiny': shiny}
                for number, shiny in fallos
            ],
        }
        with open(options.report, 'w', encoding='utf-8') as report_file:
            json.dump(report, report_file, ensure_ascii=False, indent=2)
            report_file.write('\n')
    print(f"FALLOS: {fallos}" if fallos else "TODOS EMPAQUETADOS")

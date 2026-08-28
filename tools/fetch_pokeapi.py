#!/usr/bin/env python3
"""Pull the full authored dex from PokeAPI: base stats and compact learnsets.

  python3 tools/fetch_pokeapi.py

Writes two generated files, both committed:
  tools/dex_stats.py      num -> (hp, atk, def, spe, spa, spd)
  tools/dex_learnsets.py  num -> [(move slug, level), ...] for MOVES only

Responses are cached under tools/pokeapi_cache/ (gitignored), so a second run
costs nothing. Delete the directory to force a refetch.

Why 6 stats when the pet only rolls 4 IVs: the special split lives on the
SPECIES, not the individual. Alakazam has 50 Attack and 135 Special Attack --
without bSpA it would be a terrible attacker, which is wrong. Special moves
run off ivAtk/trAtk against bSpA, special defence off ivDef/trDef against
bSpD, so no new IVs and no save migration. See dex_moves.py.
"""
import json
import argparse
import csv
import os
import ssl
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
from dex_moves import MOVES

# The python.org macOS builds ship no CA bundle, so a plain urlopen against
# HTTPS dies with CERTIFICATE_VERIFY_FAILED. certifi comes with those same
# builds; fall back to the system default when it is genuinely absent.
try:
    import certifi
    SSL_CTX = ssl.create_default_context(cafile=certifi.where())
except ImportError:
    SSL_CTX = None

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, 'pokeapi_cache')
API = 'https://pokeapi.co/api/v2/pokemon/%d'
POKEAPI_REVISION = 'c40a25c6544b97334a1ae8b1965a378fa3317c28'
# Derived from dex_data.py rather than written here, so a hardcoded number
# cannot fall behind the table again. The old header generator had its own 151
# once and emitted a Kanto-sized LEARN_OFS against a dex that had grown, which
# would have read off the end of the array for every species past 151.
def _dex_count():
    import sys, os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from dex_data import DEX
    return max(d[0] for d in DEX)


DEX_COUNT = _dex_count()

# Which ways of learning a move count as "this species can use it". Level-up
# plus TM: that is how you would actually build a set, and level-up alone
# leaves several species without even a same-type attack.
METHODS = ('level-up', 'machine')

# Version group the level-up levels are read from, PER GENERATION -- a Johto or
# Hoenn species has no FireRed/LeafGreen learnset at all, so reading them all
# from Kanto would silently drop every gate for two thirds of the dex.
# Each is the last game of its own generation, where the gates are properly
# spread and not yet flattened by later rebalancing.
def level_vg(num):
    if num <= 151:
        return 'firered-leafgreen'
    if num <= 251:
        return 'crystal'
    if num <= 386:
        return 'emerald'
    if num <= 493:
        return 'platinum'
    if num <= 649:
        return 'black-2-white-2'
    if num <= 721:
        return 'x-y'
    if num <= 809:
        return 'ultra-sun-ultra-moon'
    if num <= 905:
        return 'sword-shield'
    return 'scarlet-violet'


def fetch(num):
    path = os.path.join(CACHE, '%03d.json' % num)
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    os.makedirs(CACHE, exist_ok=True)
    # GLUE: gen_dex_data keeps the full response under a URL-derived name,
    # while this older generator keeps a compact numeric cache. Remove this
    # bridge when both generators share one cache implementation.
    full_path = os.path.join(CACHE, 'pokemon_%d.json' % num)
    downloaded = not os.path.exists(full_path)
    if downloaded:
        # PokeAPI 403s the default python-urllib agent, same as SpriteCollab
        # does in pack_pmd.py.
        req = urllib.request.Request(API % num,
                                     headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req, timeout=30, context=SSL_CTX) as r:
            data = json.load(r)
    else:
        with open(full_path) as f:
            data = json.load(f)
    # keep only what we need; the raw payloads are ~200 KB each
    slim = {
        'name': data['name'],
        'stats': {s['stat']['name']: s['base_stat'] for s in data['stats']},
        'moves': [
            {
                'name': m['move']['name'],
                'details': [
                    (d['move_learn_method']['name'], d['level_learned_at'],
                     d['version_group']['name'])
                    for d in m['version_group_details']
                ],
            }
            for m in data['moves']
        ],
    }
    with open(path, 'w') as f:
        json.dump(slim, f)
    if downloaded:
        time.sleep(0.15)  # be polite to a free API
    return slim


def load_bulk_learnsets(csv_dir, wanted):
    """Read the same PokeAPI relation as fetch(), without 1025 HTTP calls."""
    def read(name):
        path = os.path.join(csv_dir, name + '.csv')
        if not os.path.exists(path):
            raise FileNotFoundError(path)
        with open(path, encoding='utf-8-sig', newline='') as source:
            return list(csv.DictReader(source))

    move_slugs = {int(row['id']): row['identifier']
                  for row in read('moves') if row['identifier'] in wanted}
    version_groups = {row['identifier']: int(row['id'])
                      for row in read('version_groups')}
    default_species = {int(row['id']): int(row['species_id'])
                       for row in read('pokemon') if row['is_default'] == '1'}
    representative = {
        num: version_groups[level_vg(num)] for num in range(1, DEX_COUNT + 1)
    }
    details = {}
    for row in read('pokemon_moves'):
        slug = move_slugs.get(int(row['move_id']))
        species = default_species.get(int(row['pokemon_id']))
        if slug is None or species is None or species > DEX_COUNT:
            continue
        method = int(row['pokemon_move_method_id'])
        if method not in (1, 4):  # level-up, machine
            continue
        details.setdefault((species, slug), []).append(
            (method, int(row['level']), int(row['version_group_id']))
        )

    learn = {}
    missing = set(wanted)
    for num in range(1, DEX_COUNT + 1):
        rows = []
        for slug in wanted:
            entries = details.get((num, slug), ())
            if not entries:
                continue
            missing.discard(slug)
            lvlup = [level for method, level, vg in entries
                     if method == 1 and vg == representative[num] and level > 0]
            if not lvlup:
                lvlup = [level for method, level, _vg in entries
                         if method == 1 and level > 1]
            has_lvl1 = any(method == 1 and level <= 1
                           for method, level, _vg in entries)
            is_tm = any(method == 4 for method, _level, _vg in entries)
            level = (min(lvlup) if lvlup else 1 if has_lvl1 else
                     0 if is_tm else None)
            if level is not None:
                rows.append((slug, level))
        rows.sort(key=lambda row: (row[1], row[0]))
        learn[num] = rows
    return learn, missing


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--learnsets-only", action="store_true",
                        help="leave dex_stats.py unchanged")
    parser.add_argument("--csv-dir",
                        help="read pinned PokeAPI CSVs instead of per-Pokemon API calls")
    args = parser.parse_args()
    if args.csv_dir and not args.learnsets_only:
        parser.error('--csv-dir currently requires --learnsets-only')
    wanted = {slug for _name, slug, *_ in MOVES if slug}  # None = not learnable
    stats, learn = {}, {}
    missing = set(wanted)

    if args.csv_dir:
        learn, missing = load_bulk_learnsets(args.csv_dir, wanted)
    else:
        for num in range(1, DEX_COUNT + 1):
            d = fetch(num)
            s = d['stats']
            stats[num] = (s['hp'], s['attack'], s['defense'], s['speed'],
                          s['special-attack'], s['special-defense'])

            rows = []
            for m in d['moves']:
                if m['name'] not in wanted:
                    continue
                missing.discard(m['name'])
            # A move's LEVEL GATE wins over its TM entry. The old rule took the
            # lowest of the two and scored a TM as 0, so anything ever sold as a
            # TM lost its gate -- Charizard's Flamethrower is TM38 in gen 1, so
            # it came out ungated, and 1907 of 2281 rows collapsed to level 0.
            #
            # Levels are read from ONE version group, not min()'d across all of
            # them: evolved forms relearn their basics at level 1 on evolving in
            # some games, and the minimum flattened Charizard's whole set to 1.
            # Each generation uses one representative version group so its
            # level gates remain meaningful instead of being mixed together.
                lvlup = [lvl for meth, lvl, vg in m['details']
                         if meth == 'level-up' and vg == level_vg(num) and lvl > 0]
                if not lvlup:   # not in that game (or only at level 1): any game
                    lvlup = [lvl for meth, lvl, _vg in m['details']
                             if meth == 'level-up' and lvl > 1]
                is_tm = any(meth == 'machine' for meth, _lvl, _vg in m['details'])
                has_lvl1 = any(meth == 'level-up' and lvl <= 1
                               for meth, lvl, _vg in m['details'])
                if lvlup:
                    lv = min(lvlup)
                elif has_lvl1:
                    lv = 1      # a starting move
                elif is_tm:
                    lv = 0      # TM only: no gate, taught on demand
                else:
                    lv = None
                if lv is not None:
                    rows.append((m['name'], lv))
            rows.sort(key=lambda r: (r[1], r[0]))
            learn[num] = rows
            if num % 25 == 0:
                print('  ...%d/%d' % (num, DEX_COUNT))

    hdr = '# GENERADO por tools/fetch_pokeapi.py desde PokeAPI - no editar\n'

    if not args.learnsets_only:
        with open(os.path.join(HERE, 'dex_stats.py'), 'w') as f:
            f.write(hdr)
            f.write('# num -> (hp, atk, def, vel, spa, spd). Los dos ultimos son\n'
                    '# nuevos: el reparto fisico/especial vive en la especie, no en\n'
                    '# el individuo (ver fetch_pokeapi.py y dex_moves.py).\n')
            f.write('BASE_STATS = {\n')
            for num in range(1, DEX_COUNT + 1):
                f.write('    %d: %r,\n' % (num, stats[num]))
            f.write('}\n')

    with open(os.path.join(HERE, 'dex_learnsets.py'), 'w') as f:
        f.write(hdr)
        f.write('# num -> [(slug del movimiento, nivel)], solo los de dex_moves.py.\n'
                '# nivel 0 = MT, sin requisito de nivel. El nivel se guarda pero\n'
                '# todavia no se usa para nada: si la seleccion de movimientos lo\n'
                '# aprovecha o no se decide en la fase de UI.\n')
        f.write('LEARNSETS = {\n')
        for num in range(1, DEX_COUNT + 1):
            f.write('    %d: %r,\n' % (num, learn[num]))
        f.write('}\n')

    total = sum(len(v) for v in learn.values())
    thin = [n for n in learn if len(learn[n]) < 4]
    if not args.learnsets_only:
        print('stats: %d especies x 6' % len(stats))
    if args.csv_dir:
        print('source: PokeAPI %s bulk CSV' % POKEAPI_REVISION)
    print('learnsets: %d filas, %.1f de media' % (total, total / DEX_COUNT))
    if missing:
        print('AVISO: %d movimientos que nadie aprende: %s'
              % (len(missing), ', '.join(sorted(missing))))
    if thin:
        print('AVISO: %d especies con menos de 4 movimientos: %s'
              % (len(thin), thin))


if __name__ == '__main__':
    main()

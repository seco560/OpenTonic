"""The custom layer's glTF imports that tools/convert_files.sh makes by itself (step 6c), so a fresh checkout with the
sources gets them too (until 2026-11-12 they were made by hand with import_gltf.py).

IMPORTS: (source under files/sources, level name, import_gltf options). A level is imported again only when it is
missing or its record in <out>/.stamps/import-<name> differs: the glTF importer's code (chunks KINDS 'gltf'), the
source's size and modification time (a copy to another machine re-imports once), the options. The custom layer's own
edits (custom.py: LibertyCity2001's car) go on afterwards, in step 7: a fresh import has no CUST chunk, so they are
redone.

Usage (from the repo root): python3 -m tools.ttre.imports OUT
"""
import os
import sys

from . import chunks

# LibertyCity2001: the GTA III map (LCG3); Ed from bar, the start found by the importer. At scale 1 this reproduces the
# user's hand-made import byte for byte (but for the custom layer's chunks; checked 2026-11-12). Scaled 1.15 since
# (user, 2026-11-12: Ed "slightly smaller" in Liberty City): the city grows round him; the car stays as it was, so Ed and
# the car keep their sizes to each other (the ride's hops, seat and reach unchanged); custom.py scales its spot.
IMPORTS = [
    ('GLTF Imports/LCG3/LCG3.glb', 'LibertyCity2001', ['--scale', '1.15']),
]


def scale(name):
    """The --scale an import of IMPORTS is made with (1 when none)."""
    for _, n, options in IMPORTS:
        if n == name and '--scale' in options:
            return float(options[options.index('--scale') + 1])
    return 1.0


def applied_scale(out, name):
    """The --scale the level NAME in <out>/ttlv was last imported with by main (its record in <out>/.stamps), 1 when
    it has none (not one of IMPORTS, or a hand-made import): what custom.py scales a car's spot by."""
    stamp = os.path.join(out, '.stamps', f'import-{name}')
    if not os.path.exists(stamp):
        return 1.0
    words = open(stamp).read().split()
    return float(words[words.index('--scale') + 1]) if '--scale' in words else 1.0


def record(src, options):
    st = os.stat(src)
    return f'{chunks.kind_stamp("gltf")} {st.st_size} {int(st.st_mtime)} {" ".join(options)}'


def main(argv):
    if len(argv) != 1:
        raise SystemExit(__doc__)
    out = argv[0]
    sources = os.path.join(os.path.dirname(os.path.abspath(out)), 'sources')
    from . import import_gltf
    chunks.KIND = 'gltf'
    for rel, name, options in IMPORTS:
        src = os.path.join(sources, rel)
        if not os.path.exists(src):
            print(f'== import {name}: {rel} not in files/sources (optional, skip)')
            continue
        level = os.path.join(out, 'ttlv', f'{name}.ttlv')
        stamp = os.path.join(out, '.stamps', f'import-{name}')
        want = record(src, options)
        have = open(stamp).read().strip() if os.path.exists(stamp) else None
        if os.path.exists(level) and have == want:
            print(f'== import {name}: current (skip)')
            continue
        if not os.path.exists(os.path.join(out, 'ttlv', 'bar.ttlv')):
            print(f'== import {name}: needs the retail levels (Ed comes from bar) (skip)')
            continue
        print(f'== import {name} (from {rel})')
        import_gltf.main([src, name] + options)
        os.makedirs(os.path.dirname(stamp), exist_ok=True)
        with open(stamp, 'w') as f:
            f.write(want + '\n')


if __name__ == '__main__':
    main(sys.argv[1:])

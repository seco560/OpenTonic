"""The custom layer: our own edits on top of the exported levels (2026-11-09, docs/PORTALS.md "Custom layer").

    python3 -m tools.ttre.custom [out folder, default files/out]

tools/convert_files.sh runs it after the levels' export. It reads the finished .ttlv / .ttai of each level in EDITS, adds
to them and writes them back keeping their STMP, so the converter's stamp does not change. __main__ must not import this
module: that would make every level stale whenever an edit here changes. Pristine copies wait in <out>/.custom-base
(a CUST chunk marks a file already edited: a re-run starts over from its copy, a fresh export replaces the copy).

The layer only splices data the core already reads. A portal is:
  - static MESH entries, the arch's parts (SECT: always drawn; LITE: the source's lighting), copied from a level that has
    that arch, textures added to TEXS by a path relative to this level's folder;
  - a PORT record with the retail walk-in's ops, its ZONE entry (none: radius 4, as retail's hub arches) and two PLAC
    entries for the arrival: Ed in front of the arch on the way out, the camera between him and the arch.

Each level's arch stands where Ed stood at the end of the recording that placed it (docs/PORTALS.md), front towards where
the camera was: local -Y = (sin h, -cos h), h = the recorded heading. A level name 'se:x' / 'retail:x' takes a portal to
that edition's level x; the frontends strip the prefix and pick the folder.

A car (VEHICLES; docs/ENGINE.md "Vehicles") is a GTA III-era .dff + .txd under files/sources, converted to a .glb in
<out>/Vehicles/<name> (tools/ttre/dff2gltf.py) and spliced in by tools/ttre/vehicle.py: a PRSO perso, its PCOL box and a
VEHI record, which the core drives (core/include/tonic/custom/tcx_vehicle.h: custom gameplay, not the game's). Placed, as the arches, where Ed stood at the end of a recording.

A level whose edits are already in it (its CUST chunk holds their digest: this layer's code, its entries for the level,
the cars' sources) is left as it is, so a re-run rewrites nothing (LibertyCity2001's .ttlv is about 370 MB).

Dependencies (2026-11-14): an arch names the files it needs besides its level (Arch.needs: an SSX course, the GTA III
import, the Special Edition's sud0); missing, that arch alone is left out, and a level left with no edits goes back to
its exported copy. The levels outside the retail game are its extensions: the SSX courses (an arch each in retail sud0's
ice cave, back from each course's end: SSX_FINISH), LibertyCity2001 (from the Special Edition's sud0 and back). An
imported level takes an arch like any other: an Arch row with its spot at the import's scale 1 (no SECT / LITE needed).
Made here too: the arches' looks (MADE_TEXTURES), the SSX courses' loading screens (their PS2LOAD badges) and, for an
imported level without one, a loading screen drawn from its collision (map_vignette).
"""
import collections
import hashlib
import math
import os
import shutil
import struct
import sys
import zlib

from . import chunks, imports, ssx
from .dff2gltf import png

POP_GUARD, POP_SET, POP_FORCE, POP_FORCE_REFLEX, POP_COVER, POP_LEVEL = 1, 2, 3, 4, 5, 9
TWO_SIDED = 1
BILLBOARD = 1 << 2
ARRIVE_DISTANCE = 5.5  # in front of the arch: outside its capture radius (4)
CAMERA_DISTANCE = 1.5  # in front of the arch, behind Ed; the arch's surface would hide him from any farther back
CAMERA_HEIGHT = 2.5
PLACE_ED, PLACE_CAMERA = 'custom_portal_ed', 'custom_portal_camera'
SECTOR_ANY = 0xFFFF  # always awake: which sector Ed is tracked in at one spot depends on the way he came
PERSO_BASE = 0xFE00  # a PORT perso id no level uses: dsg99 finds the portal by it (tc_ai.c)

ARCH_RETAIL = ('ttlv/carota', 'arche_south.png')    # the red swirls and the "South Plain" sign
ARCH_SE = ('se/ttlv/carota', 'gen_arch_sud.png')    # the Special Edition's arch with its South Plain picture
ARCH_SKI = ('ttlv/sud0', 'arche_ski.png')           # retail sud0's red arch to Totalski (ZOR_ArcheTotalSki), "Ski Slope" sign
ARCH_PLAIN = ('ttlv/sud0', 'gen_arch01.png')        # retail sud0's plain blue arch (FRB_Section1), no sign

# An arch: (level under out, pair, arch, position, heading, destination, the arch's Textures folder as seen from this
# level, needs, look). The two arches of a pair lead to each other: Ed arrives at the places their pair names, in
# front of the other; an arch without a partner (the SSX courses, LibertyCity2001) leads to its destination's start,
# and what comes back names its places (SSX_FINISH). needs: the files under out it depends on besides its level and its
# arch's level (a destination made from your own files: the SSX disc, the GTA III map): missing, the arch is left out,
# the level's other edits stay. look: its own textures (arch_look), or None.
Arch = collections.namedtuple('Arch', 'level pair arch position heading dest root needs look', defaults=((), None))
SE_TEXTURES, RETAIL_TEXTURES, OWN_TEXTURES = '../se/Textures', '../../Textures', '../Textures'
EDITS = [
    Arch('ttlv/sud0', 0, ARCH_SE, (-151.48, 63.1175, 1.62469), 0.532497, 'se:sud0', SE_TEXTURES),
    Arch('se/ttlv/sud0', 0, ARCH_RETAIL, (-140.612, 43.3026, -3.30366), 1.04693, 'retail:sud0', RETAIL_TEXTURES),
    Arch('ttlv/sud0', 1, ARCH_SE, (-173.811, 109.539, 2.4063), -10.0605, 'se:sud0', SE_TEXTURES),
    Arch('se/ttlv/sud0', 1, ARCH_RETAIL, (-175.307, 110.389, 3.10458), -3.98005, 'retail:sud0', RETAIL_TEXTURES),
]

# The SSX Tricky courses (tools/ttre/ssx.py; user, 2026-11-14): an arch each in retail sud0's ice cave (the Pyrex's
# cave, HEG_LacDeGlace), by title, round the walls the way Ed meets them coming in from the passage (HEG_TransGlace01):
# the north-east wall, the north wall (the user's recording 2026-11-14_004907 walked these two), the side of the
# Pyrex's alcove, the wall by the frozen pond. Picked from the collision by a scratch script: each square to its own
# stretch of wall (a chord 5.2 long), its back 0.5 off it, 8 apart, flat ground under its feet and 5.5 in front of it,
# nothing in the way. The ski arch with an orange swirl and the course's loading badge on its sign (arch_look); the
# course's end leads back here (SSX_FINISH).
SSX_SPOTS = [
    ((-214.217, 148.7, -3.749), -0.82791), ((-219.628, 154.592, -3.749), -0.82791),
    ((-229.581, 162.406, -3.753), -0.2485), ((-237.329, 164.304, -3.752), -0.23623), ((-245.108, 166.176, -3.752), -0.23623),
    ((-255.054, 172.746, -3.75), -0.97988), ((-259.511, 179.389, -3.752), -0.97988),
    ((-286.449, 179.984, -3.751), 0.52972), ((-291.521, 172.935, -3.749), 1.20266), ((-294.4, 165.472, -3.75), 1.20266),
    ((-298.002, 157.873, -3.749), 0.97943),
]
SSX_COURSES = sorted(ssx.TITLES.items(), key=lambda kv: kv[1])  # (course file name, title), by title
assert len(SSX_COURSES) <= len(SSX_SPOTS)
EDITS += [Arch('ttlv/sud0', f'ssx_{course}', ARCH_SKI, position, heading, f'SSX_{title}', OWN_TEXTURES,
               (f'ttlv/SSX_{title}.ttlv', f'ttlv/SSX_{title}.ttai'), ('ssx', course))
          for (course, title), (position, heading) in zip(SSX_COURSES, SSX_SPOTS)]
# LibertyCity2001 (imports.py, the GTA III map; user, 2026-11-14): in the Special Edition's sud0, against the west wall of
# Doc's house (ZOR_LifeCaveMaison inside), facing the middle of the map. Needs both: the Special Edition and the import.
# Its way back (user, 2026-11-14): the Special Edition's arch with its South Plain picture (as at the end of its
# Totalski), in the parking lot of the first car
# (VEHICLES), 11.6 west of it on its row, facing it; Ed arrives 5.5 in front, short of its bumper. Picked from the
# collision by a scratch script (flat, 4.5 clear all round at three heights, 7 clear in front). On an imported level
# an arch's spot is given at scale 1, as the cars' (imports.applied_scale).
EDITS += [Arch('se/ttlv/sud0', 'gta', ARCH_PLAIN, (-9.063, -3.099, -4.343), -1.5215, 'retail:LibertyCity2001',
               RETAIL_TEXTURES, ('ttlv/LibertyCity2001.ttlv',), ('gta',)),
          Arch('ttlv/LibertyCity2001', 'gta', ARCH_SE, (-137.391, 641.739, -5.0722), 1.5708, 'se:sud0', SE_TEXTURES,
               ('se/ttlv/sud0.ttlv',))]

# The way back from an SSX course: once the skier comes within FINISH_RADIUS of the course's end (ssx_course_end: the
# lowest point of its AI paths, the finish's run-out), the cover at once and, behind it, sud0 in front of the course's
# arch. A portal record in the course's .ttai, as Totalski's finish (ai.ski_portals): the skier's driver runs it.
FINISH_RADIUS = 15.0
SSX_FINISH = [(f'ttlv/SSX_{title}', course) for course, title in SSX_COURSES]

# Textures the layer makes (arch_look, ssx_vignette): under out, where both install scripts sync them (the Imported
# folder) and a fresh extraction of the game's textures does not wipe them.
MADE_TEXTURES = 'Imported/CustomArches'

# (level under out, model under files/sources, paint (R, G, B), position, heading). The core drops a car onto the ground
# below its position. Positions on an imported level (imports.py) are given at its scale 1 and scaled by the scale
# it was imported with (imports.applied_scale: 1 for a hand-made import).
VEHICLES = [
    # Ed's car: the Mazda MX-5 (NB) mod for GTA III (replaces the Manana), in the parking lot where Ed stood at the end of
    # the user's recording of 2026-11-12 (2026-11-12_002726_metal.ttrec, made at scale 1), facing his way
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (168, 18, 24),
     (-127.325, 641.63, -5.06258), 1.56694),
    # 30 more, parked round the city (user, 2026-11-12): street-level spots picked from the collision (scale 1) by a
    # scratch script: flat, level 14 x 14 open squares, no drop within 7, within 3 of the low end of the 120-unit area
    # round them, spread by farthest-point sampling (none closer than ~140 to another), along the longer street axis
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (73, 222, 149), (-912.45, 301.72, -7.28), 1.5708),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (72, 117, 229), (677.55, 369.72, -6.78), 3.1416),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (207, 83, 174), (23.55, -48.28, 0.43), 4.7124),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (49, 214, 82), (-686.45, 955.72, -10.9), 0.0),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (131, 110, 65), (393.55, 837.72, -7.8), 3.1416),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (223, 141, 158), (-432.45, 281.72, -9.04), 4.7124),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (73, 79, 146), (-660.45, 579.72, -7.14), 3.1416),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (130, 131, 26), (-316.45, 969.72, 19.09), 0.0),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (153, 97, 43), (305.55, 479.72, -5.04), 4.7124),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (155, 196, 83), (-76.45, 305.72, 0.43), 3.1416),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (191, 50, 107), (77.55, 861.72, -4.7), 4.7124),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (206, 234, 158), (-226.45, 61.72, -9.82), 4.7124),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (24, 47, 179), (657.55, 811.72, -8.05), 4.7124),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (103, 68, 125), (425.55, 243.72, -6.09), 4.7124),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (116, 95, 32), (-464.45, 769.72, -10.9), 0.0),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (33, 125, 211), (-670.45, 335.72, -7.14), 4.7124),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (181, 188, 91), (111.55, 621.72, -2.1), 1.5708),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (63, 130, 37), (171.55, 123.72, -4.57), 3.1416),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (71, 188, 188), (-476.45, 495.72, -8.36), 3.1416),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (163, 220, 39), (497.55, 443.72, -5.15), 4.7124),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (106, 173, 125), (127.55, 405.72, -4.57), 0.0),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (87, 78, 92), (431.55, 619.72, -5.26), 3.1416),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (76, 203, 186), (-16.45, 131.72, 0.43), 1.5708),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (53, 182, 20), (-40.45, 483.72, 0.43), 0.0),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (51, 162, 157), (-792.45, 463.72, -7.14), 3.1416),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (81, 174, 140), (-668.45, 745.72, 9.68), 3.1416),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (137, 141, 187), (-84.45, 845.72, -4.57), 4.7124),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (30, 129, 197), (-316.45, 805.72, -10.9), 3.1416),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (198, 56, 223), (-176.45, 199.72, 0.43), 3.1416),
    ('ttlv/LibertyCity2001', 'Vehicles/manana/manana.dff', (91, 186, 47), (-782.45, 245.72, -7.27), 1.5708),
]


def read_textures(b):
    n, = struct.unpack_from('<I', b)
    o, out = 4, []
    for _ in range(n):
        size, = struct.unpack_from('<H', b, o)
        out.append(b[o + 2:o + 2 + size].decode())
        o += 2 + size
    return out


def read_surface(b, o):
    """(texture, flags, scroll u, scroll v, [(texture, seconds)]), offset after."""
    tex, flags, su, sv, n, _ = struct.unpack_from('<iI2fHH', b, o)
    o += 20
    return (tex, flags, su, sv, [struct.unpack_from('<if', b, o + 8 * i) for i in range(n)]), o + 8 * n


def skip_geometry(b, o):
    nv, ni = struct.unpack_from('<II', b, o)
    return o + 8 + 52 * nv + 4 * ni


def persos(b):
    """Every PRSO entry: (world, channels, samples, [(channel, object, surface, geometry offset)], first sample's offset)."""
    n, = struct.unpack_from('<I', b)
    o, out = 4, []
    for _ in range(n):
        world = struct.unpack_from('<12f', b, o)
        channels, samples, _ = struct.unpack_from('<HHf', b, o + 48)
        o += 56 + 2 * channels
        count, = struct.unpack_from('<I', b, o)
        o += 4
        parts = []
        for _ in range(count):
            channel, obj = struct.unpack_from('<HH', b, o)
            surface, o = read_surface(b, o + 4)
            parts.append((channel, obj, surface, o))
            o = skip_geometry(b, o)
        out.append((world, channels, samples, parts, o))
        o += samples * channels * 50
    return out


def lite_mesh_table(b):
    """LITE's offset of n_meshes, and each mesh entry's (start, end) after it."""
    lights, = struct.unpack_from('<H', b)
    o = 4 + lights * 52
    sectors, = struct.unpack_from('<H', b, o)
    o += 4
    for _ in range(sectors):
        n, envs = struct.unpack_from('<HH', b, o)
        o += 4 + 2 * n + 24 * envs
    table, count = o, struct.unpack_from('<I', b, o)[0]
    o += 4
    spans = []
    for _ in range(count):
        runs, = struct.unpack_from('<I', b, o + 24)
        spans.append((o, o + 28 + 20 * runs))
        o = spans[-1][1]
    return table, spans


def compose(a, b):
    """Two 3x4 matrices, row-major: a after b."""
    out = []
    for r in range(3):
        row = [a[r * 4 + c] for c in range(4)]
        out += [sum(row[k] * b[k * 4 + c] for k in range(3)) for c in range(3)]
        out.append(sum(row[k] * b[k * 4 + 3] for k in range(3)) + row[3])
    return out


def apply_point(m, p):
    return tuple(m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3] for r in range(3))


def apply_direction(m, d):
    v = [m[r * 4] * d[0] + m[r * 4 + 1] * d[1] + m[r * 4 + 2] * d[2] for r in range(3)]
    n = math.sqrt(sum(x * x for x in v)) or 1
    return tuple(x / n for x in v)


def arch_parts(out, arch):
    """The arch of `arch`'s level: its LITE entries and each part as (surface, geometry, texture paths, to-perso matrix),
    the perso's scale included. Found as a portal back to sud0 whose parts use the named texture."""
    level, marker = arch
    c = chunks.read(os.path.join(out, level + '.ttlv'), 'TTLV')
    textures, table = read_textures(c['TEXS']), persos(c['PRSO'])
    prso = c['PRSO']
    k = next(i for i, p in enumerate(table) if any(s[2][0] >= 0 and textures[s[2][0]].endswith('/' + marker) for s in p[3]))
    world, channels, samples, parts, sample = table[k]
    assert samples, 'the arch has no pose'
    shown = struct.unpack_from(f'<{channels}H', prso, sample + samples * channels * 48)
    lite_c = struct.unpack_from('<I', c['MESH'])[0] + sum(len(p[3]) for p in table[:k])
    _, spans = lite_mesh_table(c['LITE'])
    scale = math.sqrt(sum(world[4 * r] ** 2 for r in range(3)))
    out_parts = []
    for i, (channel, obj, surface, g) in enumerate(parts):
        if surface[1] & BILLBOARD or shown[channel] != obj:
            continue
        pose = [x * scale for x in struct.unpack_from('<12f', prso, sample + 48 * channel)]
        s, e = spans[lite_c + i]
        out_parts.append((surface, prso[g:skip_geometry(prso, g)], [textures[t] if t >= 0 else None for t in surface_textures(surface)],
                          pose, c['LITE'][s:e]))
    return out_parts


def surface_textures(s):
    """A surface's textures in stored order: the base, then its flipbook's."""
    return [s[0]] + [t for t, _ in s[4]]


def rotation(position, heading):
    c, s = math.cos(heading), math.sin(heading)
    return [c, -s, 0, position[0], s, c, 0, position[1], 0, 0, 1, position[2]]


def texture_index(paths, table):
    """`paths`' indices in `table`, adding what it lacks."""
    return [-1 if p is None else (table.index(p) if p in table else table.append(p) or len(table) - 1) for p in paths]


def ground(coll, x, y, z):
    """The highest collision triangle under (x, y) no higher than z + 1 (the arrival point's floor), or None."""
    n, = struct.unpack_from('<I', coll)
    best = None
    for ax, ay, az, bx, by, bz, cx, cy, cz in struct.iter_unpack('<9f', coll[4:4 + 36 * n]):
        d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(d) < 1e-9:
            continue
        u = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / d
        v = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / d
        if u < 0 or v < 0 or u + v > 1:
            continue
        h = u * az + v * bz + (1 - u - v) * cz
        if h <= z + 1 and (best is None or h > best):
            best = h
    return best


def splice_meshes(c, parts, world, root, swap=None):
    """TEXS, MESH, SECT and LITE with the arch's parts appended to the static meshes. swap: {a part's texture path as its
    level has it: the path to use instead, relative to this level's folder} (arch_look)."""
    swap = swap or {}
    table = read_textures(c['TEXS'])
    before = len(table)
    mesh_count, = struct.unpack_from('<I', c['MESH'])
    n_persos, = struct.unpack_from('<I', c['PRSO'])
    meshes, ranges, lites = [], [], []
    for surface, geometry, paths, pose, lite in parts:
        full = compose(world, pose)
        nv, ni = struct.unpack_from('<II', geometry)
        vertices = bytearray(geometry)
        for i in range(nv):
            o = 8 + 52 * i
            p = apply_point(full, struct.unpack_from('<3f', geometry, o))
            n = apply_direction(full, struct.unpack_from('<3f', geometry, o + 12))
            struct.pack_into('<6f', vertices, o, *p, *n)
        indices = texture_index([p and swap.get(p, root + p[len('../Textures'):]) for p in paths], table)
        anim = surface[4]
        meshes.append(struct.pack('<iI2fHH', indices[0], surface[1], surface[2], surface[3], len(anim), 0) +
                      b''.join(struct.pack('<if', t, s) for t, (_, s) in zip(indices[1:], anim)) + bytes(vertices))
        ranges.append(struct.pack('<I', 1) + struct.pack('<HHII', 0xFFFF, 0, 0, ni))
        ambient = struct.unpack_from('<6f', lite)
        runs, = struct.unpack_from('<I', lite, 24)
        entry = struct.pack('<6fI', *ambient, runs)
        for r in range(runs):
            first, count = struct.unpack_from('<II', lite, 28 + 20 * r)
            entry += struct.pack('<II3f', first, count, *apply_point(full, struct.unpack_from('<3f', lite, 36 + 20 * r)))
        lites.append(entry)
    def strings(items):
        return b''.join(struct.pack('<H', len(s.encode())) + s.encode() for s in items)
    c['TEXS'] = struct.pack('<I', len(table)) + c['TEXS'][4:] + strings(table[before:])
    c['MESH'] = struct.pack('<I', mesh_count + len(meshes)) + c['MESH'][4:] + b''.join(meshes)
    # SECT and LITE when the level has them (an imported one has neither: its meshes always drawn, lit as baked)
    # SECT ends { per-mesh ranges } u32 n_persos, n x u16, u32 n_collision, n x u16.
    if 'SECT' in c:
        n_coll, = struct.unpack_from('<I', c['COLL'])
        sect = c['SECT']
        tail = 4 + 2 * n_persos + 4 + 2 * n_coll
        at = len(sect) - tail
        assert struct.unpack_from('<I', sect, at)[0] == n_persos, 'SECT: the per-perso table is not where expected'
        c['SECT'] = sect[:at] + b''.join(ranges) + sect[at:]
    if 'LITE' in c:
        lite = c['LITE']
        lite_table, spans = lite_mesh_table(lite)
        insert = spans[mesh_count][0] if mesh_count < len(spans) else len(lite)
        c['LITE'] = lite[:lite_table] + struct.pack('<I', len(spans) + len(lites)) + lite[lite_table + 4:insert] + b''.join(lites) + lite[insert:]


def add_portal(a, position, heading, dest, index, place_ed, place_camera):
    """PORT and ZONE of a .ttai's chunks `a` with an arch added: the retail walk-in (B66 / R4), then the level."""
    degrees = int(round(math.degrees(heading) + 90)) % 360  # PROC_242: deg = rotation about Z + 90
    rows = rotation(position, heading)
    caps = [(POP_GUARD, 0, 134, 0, 0, (0, 0, 0)), (POP_SET, 2, 99, 0, 0, (0, 0, 0)), (POP_SET, 0, 100, 0, 0, (0, 0, 0)),
            (POP_SET, 0, 136, degrees, 0, (0, 0, 0)), (POP_FORCE, 0, 66, 0, 0, (0, 0, 0)), (POP_FORCE_REFLEX, 0, 4, 0, 0, (0, 0, 0))]
    fire = [(POP_SET, 0, 103, 0, 0, (0, 0, 0)), ('level', dest, place_ed, place_camera)]
    add_record(a, position, 4.0, rows, PERSO_BASE + index, 0, TWO_SIDED, 103, 1, caps, fire)


def add_finish(a, position, dest, place_ed, place_camera):
    """PORT and ZONE with a course's finish added: within FINISH_RADIUS, the cover at once; covered, the level."""
    add_record(a, position, FINISH_RADIUS, rotation(position, 0), PERSO_BASE + 0x80, 1, 0, 0, 0,
               [(POP_COVER, 0, 0, 0, 0, (0, 0, 0))], [('level', dest, place_ed, place_camera)])


def add_record(a, position, radius, rows, perso, fire_kind, flags, fire_dsg, fire_value, caps, fire):
    """A portal record (tc_ai.c's PORT reader) with its ops; ('level', destination, place, camera place) is POP_LEVEL with
    the names looked up in (or added to) the chunk's name table. Its ZONE entry: none (the radius)."""
    port, zone = a['PORT'], a['ZONE']
    count, = struct.unpack_from('<H', port)
    o = 4
    for _ in range(count):
        o += struct.calcsize('<4f9fHBBHHiHH')
        n_caps, n_fires = struct.unpack_from('<HH', port, o)
        o += 4 + 24 * (n_caps + n_fires)
    names_at = o
    n_names, = struct.unpack_from('<H', port, o)
    o += 2
    names = []
    for _ in range(n_names):
        size, = struct.unpack_from('<H', port, o)
        names.append(port[o + 2:o + 2 + size].decode('latin-1'))
        o += 2 + size
    def sid(x):
        if x not in names:
            names.append(x)
        return names.index(x)
    fire = [(POP_LEVEL, 0, sid(op[3]), sid(op[1]), sid(op[2]), (0, 0, 0)) if op[0] == 'level' else op for op in fire]
    record = struct.pack('<4f9fHBBHHiHH', *position, radius, *rows[0:3], *rows[4:7], *rows[8:11], perso, fire_kind, flags,
                         fire_dsg, 0, fire_value, SECTOR_ANY, 0) + struct.pack('<HH', len(caps), len(fire))
    for op, kind, x, y, z, v in caps + fire:
        record += struct.pack('<BBHII3f', op, kind, x, y & 0xFFFFFFFF, z & 0xFFFFFFFF, *v)
    names_blob = struct.pack('<H', len(names)) + b''.join(struct.pack('<H', len(n)) + n.encode('latin-1') for n in names)
    assert struct.unpack_from('<H', zone)[0] == count, 'ZONE is not parallel to PORT'
    a['PORT'] = struct.pack('<H', count + 1) + port[2:names_at] + record + names_blob
    a['ZONE'] = struct.pack('<H', count + 1) + zone[2:] + struct.pack('<HH', 0, 0)


def add_places(a, places):
    plac = a['PLAC']
    count, = struct.unpack_from('<H', plac)
    extra = b''.join(struct.pack('<H', len(n)) + n.encode('latin-1') + struct.pack('<4f', *p, h) for n, p, h in places)
    a['PLAC'] = struct.pack('<H', count + len(places)) + plac[2:] + extra


# ---- the layer's own textures (MADE_TEXTURES) ----

def read_png(path):
    """(w, h, rgba) of an 8-bit RGB / RGBA PNG, not interlaced (the game's converted textures)."""
    d = open(path, 'rb').read()
    o, idat = 8, b''
    while o < len(d):
        n, t = struct.unpack_from('>I4s', d, o)
        body = d[o + 8:o + 8 + n]
        if t == b'IHDR':
            w, h, _, ctype = struct.unpack_from('>IIBB', body)
        elif t == b'IDAT':
            idat += body
        o += 12 + n
    bpp = {6: 4, 2: 3}[ctype]
    raw, stride = zlib.decompress(idat), w * bpp
    out, prev = bytearray(), bytearray(stride)
    for y in range(h):
        ft, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line if bpp == 4 else b''.join(bytes(line[i:i + 3]) + b'\xff' for i in range(0, stride, 3))
        prev = line
    return w, h, bytes(out)


def save_png(path, w, h, rgba):
    """Written only when it differs (the app's install copies what changed)."""
    data = png(w, h, bytes(rgba))
    if not os.path.exists(path) or open(path, 'rb').read() != data:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)


def recoloured(rgba, f):
    """Each pixel's (r, g, b) through f, alpha kept."""
    out = bytearray(rgba)
    for i in range(0, len(out), 4):
        out[i:i + 3] = bytes(max(0, min(255, int(x))) for x in f(out[i], out[i + 1], out[i + 2]))
    return out


def sample(img, x, y):
    """Bilinear RGBA of (w, h, rgba) at pixel coordinates (x, y), edges clamped."""
    w, h, px = img
    x, y = min(max(x - 0.5, 0), w - 1), min(max(y - 0.5, 0), h - 1)
    x0, y0 = min(int(x), w - 2), min(int(y), h - 2)
    fx, fy = x - x0, y - y0
    out = [0.0] * 4
    for dx, dy, k in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)), (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
        o = 4 * ((y0 + dy) * w + x0 + dx)
        for c in range(4):
            out[c] += k * px[o + c]
    return out


def opaque_box(img, threshold=16):
    """(x0, y0, x1, y1) round the texels with alpha over threshold."""
    w, h, px = img
    xs = [i % w for i in range(w * h) if px[4 * i + 3] > threshold]
    ys = [i // w for i in range(w * h) if px[4 * i + 3] > threshold]
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def blend(dst, img, box, at, scale):
    """img's `box` scaled by `scale`, alpha-blended onto dst (w, h, bytearray) with its corner at `at`."""
    w, h, px = dst
    x0, y0, x1, y1 = box
    for y in range(int((y1 - y0) * scale)):
        for x in range(int((x1 - x0) * scale)):
            tx, ty = at[0] + x, at[1] + y
            if not (0 <= tx < w and 0 <= ty < h):
                continue
            r, g, b, a = sample(img, x0 + (x + 0.5) / scale, y0 + (y + 0.5) / scale)
            k, o = a / 255, 4 * (ty * w + tx)
            px[o:o + 3] = bytes(int(px[o + c] * (1 - k) + v * k + 0.5) for c, v in enumerate((r, g, b)))


def scaled(img, w, h):
    """img resampled to w x h (w, h, bytearray)."""
    sw, sh, _ = img
    out = bytearray()
    for y in range(h):
        for x in range(w):
            out += bytes(int(v + 0.5) for v in sample(img, (x + 0.5) * sw / w, (y + 0.5) * sh / h))
    return w, h, out


SWIRLS = {  # an arch's swirl flipbook (its four frames) recoloured: 'ssx' orange, 'gta' green
    'ssx': ([f'gen_arch0{k}R.png' for k in range(1, 5)], lambda r, g, b: (r, g + (r - g) * 0.5, b * 0.6)),
    'gta': ([f'gen_arch0{k}.png' for k in range(1, 5)], lambda r, g, b: (r * 0.7, b, g * 0.7)),
}


def ssx_iso():
    try:
        path = ssx.find_iso()
        return ssx.Iso(path) if path else None
    except (OSError, ValueError):
        return None


def ssx_badge(iso, course):
    """(w, h, rgba) of the course's loading badge (DATA/TEXTURES/PS2LOAD.BIG, ldtrack<name>.ssh: the course's logo over
    its name, 256 x 256 with a transparent margin), or None (trick has none)."""
    name = {'alaska': 'alas', 'aloha': 'aloh', 'elysium': 'elys', 'gari': 'gari', 'megaple': 'toky', 'merquer': 'merq',
            'mesa': 'mesa', 'pipe': 'pipe', 'snow': 'snow', 'untrack': 'untr'}.get(course)
    if iso is None or name is None:
        return None
    raw = ssx.read_big(iso.read('DATA/TEXTURES/PS2LOAD.BIG')).get(f'ldtrack{name}.ssh')
    return ssx.read_ssh(raw)[0] if raw else None


def ssx_course_end(iso, course):
    """Where a run of the course ends, in TT units, or None: the lowest point of its AI paths (<course>.aip; they follow
    the racing line, so that is the finish's run-out). Checked 2026-11-14 on all 11: ground within 0.4 under each.
    The .aip as ssx.start_path reads it (repeated here: ssx.py's own stamp stays, so its courses aren't converted anew)."""
    aip = ssx.member(ssx.read_big(iso.read(f'DATA/MODELS/{course.upper()}.BIG')), course, '.aip')
    if not aip or len(aip) < 24:
        return None
    o = 16
    n_paths, n_starts = struct.unpack_from('<2I', aip, o)
    o += 8 + 4 * n_starts
    low = None
    for _ in range(n_paths):
        n_pts, n_ev = struct.unpack_from('<2I', aip, o + 28)
        p = struct.unpack_from('<3f', aip, o + 36)
        for k in range(n_pts):
            if low is None or p[2] < low[2]:
                low = p
            d = struct.unpack_from('<4f', aip, o + 72 + 16 * k)  # a leg: its unit direction and length
            p = tuple(p[c] + d[c] * d[3] for c in range(3))
        o += 72 + 16 * n_pts + 16 * n_ev
    return tuple(c * ssx.SCALE for c in low) if low else None


def arch_look(out, level, look, iso):
    """{texture path in the arch's level: replacement relative to `level`'s folder} for an arch's look, the textures
    made under MADE_TEXTURES: ('ssx', course) the orange swirl and the course's badge on the ski sign, ('gta',) the
    green swirl. Leaves out what it can't make (no retail textures, no disc: the plain look)."""
    if not look:
        return {}
    src, made = os.path.join(out, 'Textures', 'gen_arche'), os.path.join(out, MADE_TEXTURES)
    here = os.path.dirname(os.path.join(out, level))
    swap = {}
    names, colour = SWIRLS[look[0]]
    for n in names:
        if os.path.exists(os.path.join(src, n)):
            w, h, px = read_png(os.path.join(src, n))
            dst = os.path.join(made, f'{look[0]}_{n}')
            save_png(dst, w, h, recoloured(px, colour))
            swap['../Textures/gen_arche/' + n] = os.path.relpath(dst, here).replace(os.sep, '/')
    badge = ssx_badge(iso, look[1]) if look[0] == 'ssx' else None
    if badge and os.path.exists(os.path.join(src, 'arche_ski.png')):
        # the plank, twice the size, the badge's logo and name fitted on it with a margin
        w, h, sign = scaled(read_png(os.path.join(src, 'arche_ski.png')), 256, 128)
        box = opaque_box(badge)
        k = min((w - 24) / (box[2] - box[0]), (h - 16) / (box[3] - box[1]))
        blend((w, h, sign), badge, box, (int((w - (box[2] - box[0]) * k) / 2), int((h - (box[3] - box[1]) * k) / 2)), k)
        dst = os.path.join(made, f'ssx_sign_{look[1]}.png')
        save_png(dst, w, h, sign)
        swap['../Textures/gen_arche/arche_ski.png'] = os.path.relpath(dst, here).replace(os.sep, '/')
    return swap


def ssx_vignette(out, title, course, iso, force):
    """The course's loading screen (Vignette/Level/SSX_<title>.png, as the game's levels have theirs; the pickers show
    it too): its badge over the orange swirl, darkened, 640 x 480. Made when missing or `force`."""
    path = os.path.join(out, 'Vignette', 'Level', f'SSX_{title}.png')
    swirl = os.path.join(out, MADE_TEXTURES, 'ssx_gen_arch01R.png')
    if os.path.exists(path) and not force:
        return
    badge = ssx_badge(iso, course)
    if not badge or not os.path.exists(swirl):
        return
    tile = read_png(swirl)
    w, h = 640, 480
    tw, th, tp = tile
    bg = bytearray()
    for y in range(h):
        for x in range(w):
            o = 4 * ((y // 2 % th) * tw + x // 2 % tw)
            bg += bytes((tp[o] * 35 // 100, tp[o + 1] * 35 // 100, tp[o + 2] * 35 // 100, 255))
    box = opaque_box(badge)
    k = min((w * 0.8) / (box[2] - box[0]), (h * 0.8) / (box[3] - box[1]))
    blend((w, h, bg), badge, box, (int((w - (box[2] - box[0]) * k) / 2), int((h - (box[3] - box[1]) * k) / 2)), k)
    save_png(path, w, h, bg)


def map_vignette(out, name):
    """A loading screen for an imported level that has none (LibertyCity2001, SSX_Trick, the next glTF imports; user,
    2026-11-14: "a makeshift vignette"): its collision seen from above, 640 x 480, coloured by height (low: deep teal,
    high: warm sand) and shaded as by a sun from the north-west, on black. Made when missing."""
    vdir = os.path.join(out, 'Vignette', 'Level')
    if os.path.isdir(vdir) and any(f.lower() == name.lower() + '.png' for f in os.listdir(vdir)):
        return
    c = chunks.read(os.path.join(out, 'ttlv', name + '.ttlv'), 'TTLV')
    coll = c.get('COLL')
    if not coll:
        return
    n, = struct.unpack_from('<I', coll)
    pts = struct.unpack_from(f'<{9 * n}f', coll, 4)
    xs, ys, zs = pts[0::3], pts[1::3], pts[2::3]
    # the middle 98 % (strays far out would shrink the rest)
    def span(v):
        s = sorted(v[::max(1, len(v) // 20000)])
        return s[len(s) // 100], s[-len(s) // 100 - 1]
    (x0, x1), (y0, y1), (z0, z1) = span(xs), span(ys), span(zs)
    w, h = 640, 480
    k = min((w - 40) / max(x1 - x0, 1e-6), (h - 40) / max(y1 - y0, 1e-6))
    ox, oy = (w - (x1 - x0) * k) / 2, (h - (y1 - y0) * k) / 2
    top = [None] * (w * h)
    for t in range(n):  # each triangle sampled about a pixel apart (its corners alone leave gaps between them)
        ax, ay, az, bx, by, bz, cx, cy, cz = pts[9 * t:9 * t + 9]
        m = min(48, int(max(abs(ax - bx), abs(ay - by), abs(ax - cx), abs(ay - cy)) * k) + 1)
        for i in range(m + 1):
            for j in range(m + 1 - i):
                u, v = i / m, j / m
                x, y, z = (ax + (bx - ax) * u + (cx - ax) * v, ay + (by - ay) * u + (cy - ay) * v,
                           az + (bz - az) * u + (cz - az) * v)
                px, py = int(ox + (x - x0) * k), int(h - 1 - (oy + (y - y0) * k))
                if 0 <= px < w and 0 <= py < h:
                    q = py * w + px
                    if top[q] is None or z > top[q]:
                        top[q] = z
    for _ in range(1):  # what is left between them: a pixel takes its highest neighbour's height
        grown = top[:]
        for i, z in enumerate(top):
            if z is None:
                near = [top[j] for j in (i - 1, i + 1, i - w, i + w) if 0 <= j < w * h and top[j] is not None]
                if near:
                    grown[i] = max(near)
        top = grown
    low, high = (12, 40, 52), (226, 196, 140)
    rgba = bytearray()
    for i, z in enumerate(top):
        if z is None:
            rgba += b'\0\0\0\xff'
            continue
        t = min(max((z - z0) / max(z1 - z0, 1e-6), 0), 1) ** 0.6
        a, b = top[i - w - 1] if i > w and top[i - w - 1] is not None else z, z
        light = min(max(1.0 + (a - b) * k * 0.15, 0.45), 1.35)
        rgba += bytes(min(255, int((lo + (hi - lo) * t) * light)) for lo, hi in zip(low, high)) + b'\xff'
    save_png(os.path.join(vdir, name + '.png'), w, h, rgba)
    print(f'custom layer: {name}: a loading screen made from its collision (Vignette/Level/{name}.png)')


def write(path, magic, c):
    """Over `path`, keeping its chunks and STMP (chunks.write would stamp it anew)."""
    tmp = path + '.partial'
    with open(tmp, 'wb') as f:
        f.write(magic.encode() + struct.pack('<I', chunks.CONTAINER) + b''.join(chunks.chunk(t, p) for t, p in c.items()))
    os.replace(tmp, path)


def pristine(out, relative):
    """`relative` (a level's file under out) back to its exported state; its copy made or brought up to date."""
    path, base = os.path.join(out, relative), os.path.join(out, '.custom-base', relative)
    if 'CUST' in (chunks.read(path) or {}):
        shutil.copyfile(base, path)
    else:
        os.makedirs(os.path.dirname(base), exist_ok=True)
        shutil.copyfile(path, base)


def digest(level, edits, cars, sources, extra=()):
    """What a level's edits come from: this layer's code (and what it imports), its entries, its cars' model files,
    `extra` (what else its looks read: whether the SSX disc is there)."""
    h = hashlib.sha256(chunks.stamp(('custom', 'vehicle')).encode() + repr((level, edits, cars, extra)).encode())
    for car in cars:
        dff = os.path.join(sources, car[1])
        for f in (dff, os.path.splitext(dff)[0] + '.txd'):
            if os.path.exists(f):
                h.update(open(f, 'rb').read())
    return b'custom layer ' + h.hexdigest()[:16].encode()


def current(path, mark):
    try:
        return (chunks.read(path) or {}).get('CUST') == mark
    except (OSError, ValueError):
        return False


def unedited(out, relative):
    """Back to its exported state when an earlier run edited it (its edits' files have gone since)."""
    path = os.path.join(out, relative)
    if os.path.exists(path) and 'CUST' in (chunks.read(path) or {}):
        pristine(out, relative)
        print(f'custom layer: {relative}: no edits left, back as exported')


def run(out):
    from tools.ttre import vehicle
    sources = os.path.join(os.path.dirname(os.path.abspath(out)), 'sources')
    iso = ssx_iso()
    for level in dict.fromkeys([e.level for e in EDITS] + [v[0] for v in VEHICLES]):  # a level's edits go on together
        ttlv_path, ttai_path = os.path.join(out, level + '.ttlv'), os.path.join(out, level + '.ttai')
        if not os.path.exists(ttlv_path):
            print(f'custom layer: {level}: not there, skipped')
            continue
        # each arch by itself: the files it needs (its arch's level, its destination's) or it is left out
        edits = []
        for i, e in enumerate(EDITS):
            if e.level != level:
                continue
            missing = [f for f in [ttai_path, os.path.join(out, e.arch[0] + '.ttlv')] + [os.path.join(out, n) for n in e.needs]
                       if not os.path.exists(f)]
            if missing:
                print(f'custom layer: {level}: arch {e.pair} to {e.dest} left out ({", ".join(missing)} not there)')
            else:
                edits.append((i, e))
        # a car's spot scales with the imported level under it (the scale its import was made with)
        k = imports.applied_scale(out, os.path.basename(level))
        cars = []
        for v in VEHICLES:
            if v[0] != level:
                continue
            if os.path.exists(os.path.join(sources, v[1])):
                cars.append((v[0], v[1], v[2], tuple(x * k for x in v[3]), v[4]))
            else:
                print(f'custom layer: {level}: car {v[1]} left out (not in files/sources)')
        if not edits and not cars:
            unedited(out, level + '.ttlv')
            unedited(out, level + '.ttai')
            continue
        looks = any(e.look and e.look[0] == 'ssx' for _, e in edits)
        mark = digest(level, [e for _, e in edits], cars, sources, (looks and iso is not None,))
        ssx_titles = [(e.dest[len('SSX_'):], e.look[1]) for _, e in edits if e.look and e.look[0] == 'ssx']
        if current(ttlv_path, mark) and (not edits or current(ttai_path, mark)):
            print(f'custom layer: {level}: up to date')
            for title, course in ssx_titles:  # its loading screens, if a fresh extraction of the game's took them away
                ssx_vignette(out, title, course, iso, False)
            continue
        pristine(out, level + '.ttlv')
        if edits:
            pristine(out, level + '.ttai')
        else:
            unedited(out, level + '.ttai')
        c = chunks.read(ttlv_path, 'TTLV')
        a = chunks.read(ttai_path, 'TTAI') if edits else None
        for index, e in edits:
            position, heading = tuple(x * k for x in e.position), e.heading
            parts = arch_parts(out, e.arch)
            splice_meshes(c, parts, rotation(position, heading), e.root, arch_look(out, level, e.look, iso))
            front = (math.sin(heading), -math.cos(heading))

            def at(distance, height):
                x, y = position[0] + front[0] * distance, position[1] + front[1] * distance
                floor = ground(c['COLL'], x, y, position[2] + height + 2)
                return (x, y, (position[2] if floor is None else floor) + height)
            place_ed, place_camera = f'{PLACE_ED}_{e.pair}', f'{PLACE_CAMERA}_{e.pair}'
            add_portal(a, position, heading, e.dest, index, place_ed, place_camera)
            add_places(a, [(place_ed, at(ARRIVE_DISTANCE, 0.02), heading), (place_camera, at(CAMERA_DISTANCE, CAMERA_HEIGHT), heading)])
            print(f'custom layer: {level}: arch {e.pair} at {position}, leading to {e.dest}, {len(parts)} meshes')
        for title, course in ssx_titles:
            ssx_vignette(out, title, course, iso, True)
        for _, source, paint, position, heading in cars:
            name, glb = vehicle.model(sources, source, out, paint)
            n_parts, n_tris, (lo, hi), wheels = vehicle.splice(c, glb, name, os.path.dirname(glb),
                                                                os.path.dirname(ttlv_path), rotation(position, heading),
                                                                read_textures)
            print(f'custom layer: {level}: car {name} at {position}, {n_parts} meshes, {n_tris} triangles, '
                  f'{len(wheels)} wheels, body {[round(x, 2) for x in lo]}..{[round(x, 2) for x in hi]}')
        c['CUST'] = mark
        write(ttlv_path, 'TTLV', c)
        if edits:
            a['CUST'] = mark
            write(ttai_path, 'TTAI', a)
    run_finishes(out, iso)
    imported = os.path.join(out, 'Imported')
    for name in sorted(os.listdir(imported)) if os.path.isdir(imported) else []:
        if os.path.exists(os.path.join(out, 'ttlv', name + '.ttlv')):
            map_vignette(out, name)


def run_finishes(out, iso):
    """SSX_FINISH: each course's way back, in its .ttai only, to the places in front of its arch in sud0. Needs the disc
    (the course's end) and the arch (an edit of retail sud0 that went on)."""
    for level, course in SSX_FINISH:
        ttai_path = os.path.join(out, level + '.ttai')
        if not os.path.exists(ttai_path):
            continue
        pair = f'ssx_{course}'
        arch = next(e for e in EDITS if e.pair == pair)
        if iso is None:
            print(f'custom layer: {level}: no SSX disc, so no course end: the way back left out')
            unedited(out, level + '.ttai')
            continue
        dest = arch.level.split('/')[-1]
        mark = digest(level, (FINISH_RADIUS, dest, pair), [], '')
        if current(ttai_path, mark):
            print(f'custom layer: {level}: up to date')
            continue
        end = ssx_course_end(iso, course)
        if end is None:
            print(f'custom layer: {level}: no AI paths, no course end: the way back left out')
            unedited(out, level + '.ttai')
            continue
        pristine(out, level + '.ttai')
        a = chunks.read(ttai_path, 'TTAI')
        add_finish(a, end, dest, f'{PLACE_ED}_{pair}', f'{PLACE_CAMERA}_{pair}')
        a['CUST'] = mark
        write(ttai_path, 'TTAI', a)
        print(f'custom layer: {level}: the end at {tuple(round(x, 1) for x in end)} leads back to {dest}')


if __name__ == '__main__':
    run(sys.argv[1] if len(sys.argv) > 1 else 'files/out')

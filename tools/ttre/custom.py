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
"""
import math
import os
import shutil
import struct
import sys

from . import chunks

POP_GUARD, POP_SET, POP_FORCE, POP_FORCE_REFLEX, POP_LEVEL = 1, 2, 3, 4, 9
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

# (level under out, pair, arch, position, heading, destination, the arch's Textures folder as seen from this level).
# The two arches of a pair lead to each other: Ed arrives at the places their pair number names, in front of the other.
SE_TEXTURES, RETAIL_TEXTURES = '../se/Textures', '../../Textures'
EDITS = [
    ('ttlv/sud0', 0, ARCH_SE, (-151.48, 63.1175, 1.62469), 0.532497, 'se:sud0', SE_TEXTURES),
    ('se/ttlv/sud0', 0, ARCH_RETAIL, (-140.612, 43.3026, -3.30366), 1.04693, 'retail:sud0', RETAIL_TEXTURES),
    ('ttlv/sud0', 1, ARCH_SE, (-173.811, 109.539, 2.4063), -10.0605, 'se:sud0', SE_TEXTURES),
    ('se/ttlv/sud0', 1, ARCH_RETAIL, (-175.307, 110.389, 3.10458), -3.98005, 'retail:sud0', RETAIL_TEXTURES),
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


def splice_meshes(c, parts, world, root):
    """TEXS, MESH, SECT and LITE with the arch's parts appended to the static meshes."""
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
        indices = texture_index([p and root + p[len('../Textures'):] for p in paths], table)
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
    # SECT ends { per-mesh ranges } u32 n_persos, n x u16, u32 n_collision, n x u16.
    n_coll, = struct.unpack_from('<I', c['COLL'])
    sect = c['SECT']
    tail = 4 + 2 * n_persos + 4 + 2 * n_coll
    at = len(sect) - tail
    assert struct.unpack_from('<I', sect, at)[0] == n_persos, 'SECT: the per-perso table is not where expected'
    c['SECT'] = sect[:at] + b''.join(ranges) + sect[at:]
    lite = c['LITE']
    lite_table, spans = lite_mesh_table(lite)
    insert = spans[mesh_count][0] if mesh_count < len(spans) else len(lite)
    c['LITE'] = lite[:lite_table] + struct.pack('<I', len(spans) + len(lites)) + lite[lite_table + 4:insert] + b''.join(lites) + lite[insert:]


def add_portal(a, position, heading, dest, index, place_ed, place_camera):
    """PORT and ZONE of a .ttai's chunks `a` with the portal added."""
    port, zone = a['PORT'], a['ZONE']
    count, = struct.unpack_from('<H', port)
    o = 4
    for _ in range(count):
        o += struct.calcsize('<4f9fHBBHHiHH')
        caps, fires = struct.unpack_from('<HH', port, o)
        o += 4 + 24 * (caps + fires)
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
    degrees = int(round(math.degrees(heading) + 90)) % 360  # PROC_242: deg = rotation about Z + 90
    rows = rotation(position, heading)
    caps = [(POP_GUARD, 0, 134, 0, 0, (0, 0, 0)), (POP_SET, 2, 99, 0, 0, (0, 0, 0)), (POP_SET, 0, 100, 0, 0, (0, 0, 0)),
            (POP_SET, 0, 136, degrees, 0, (0, 0, 0)), (POP_FORCE, 0, 66, 0, 0, (0, 0, 0)), (POP_FORCE_REFLEX, 0, 4, 0, 0, (0, 0, 0))]
    fire = [(POP_SET, 0, 103, 0, 0, (0, 0, 0)), (POP_LEVEL, 0, sid(place_camera), sid(dest), sid(place_ed), (0, 0, 0))]
    record = struct.pack('<4f9fHBBHHiHH', *position, 4.0, *rows[0:3], *rows[4:7], *rows[8:11], PERSO_BASE + index, 0, TWO_SIDED,
                         103, 0, 1, SECTOR_ANY, 0) + struct.pack('<HH', len(caps), len(fire))
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


def run(out):
    for level in dict.fromkeys(e[0] for e in EDITS):  # a level's arches go on together, from its pristine files
        edits = [(i, e) for i, e in enumerate(EDITS) if e[0] == level]
        ttlv_path, ttai_path = os.path.join(out, level + '.ttlv'), os.path.join(out, level + '.ttai')
        needed = [ttlv_path, ttai_path] + [os.path.join(out, e[2][0] + '.ttlv') for _, e in edits]
        if not all(os.path.exists(p) for p in needed):
            print(f'custom layer: {level} or an arch\'s level is not exported, skipped')
            continue
        pristine(out, level + '.ttlv')
        pristine(out, level + '.ttai')
        c, a = chunks.read(ttlv_path, 'TTLV'), chunks.read(ttai_path, 'TTAI')
        for index, (_, pair, arch, position, heading, dest, root) in edits:
            parts = arch_parts(out, arch)
            splice_meshes(c, parts, rotation(position, heading), root)
            front = (math.sin(heading), -math.cos(heading))

            def at(distance, height):
                x, y = position[0] + front[0] * distance, position[1] + front[1] * distance
                floor = ground(c['COLL'], x, y, position[2] + height + 2)
                return (x, y, (position[2] if floor is None else floor) + height)
            place_ed, place_camera = f'{PLACE_ED}_{pair}', f'{PLACE_CAMERA}_{pair}'
            add_portal(a, position, heading, dest, index, place_ed, place_camera)
            add_places(a, [(place_ed, at(ARRIVE_DISTANCE, 0.02), heading), (place_camera, at(CAMERA_DISTANCE, CAMERA_HEIGHT), heading)])
            print(f'custom layer: {level}: arch {pair} at {position}, leading to {dest}, {len(parts)} meshes')
        c['CUST'] = a['CUST'] = b'custom layer'
        write(ttlv_path, 'TTLV', c)
        write(ttai_path, 'TTAI', a)


if __name__ == '__main__':
    run(sys.argv[1] if len(sys.argv) > 1 else 'files/out')

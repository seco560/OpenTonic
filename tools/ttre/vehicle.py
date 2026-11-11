"""The custom layer's cars (tools/ttre/custom.py VEHICLES; docs/ENGINE.md "Vehicles"): a car model, as tools/ttre/dff2gltf.py
writes it (.glb: a node per part, the wheels named wheel_lf / rf / lm / rm / lb / rb, GTA's axes through glTF's Y-up),
spliced into a finished level for the core's toy car (core/include/tonic/custom/tcx_vehicle.h: custom gameplay, not the game's):
  - PRSO: one perso, channel 0 the body (every other part, one mesh per material), a channel per wheel (its parts about
    its centre, the axle along X); one sample. The core poses the channels itself while playing.
  - PCOL: its collision, one box (the body's bounds from 0.2 above the ground up), so Ed bumps into it and can stand on
    it; a level without a PCOL gets one, with the player's collide set as retail Ed's (priority 127, meets persos).
  - VEHI: the record that makes it a car (tcx_vehicle.h has the layout), with the driver's seat (GTA's ped_frontseat
    dummy mirrored across X) where Ed sits, shrunk, while he drives.
  - TEXS: its textures (PNG, in <out>/Vehicles/<name>/), by path relative to the level's folder.
  - SECT, LITE when the level has them: the perso's sector (none: always drawn) and its parts' materials (the port's
    guess, ambient 0.35, diffuse 0.75: no car in the original to read).
Car axes, as every perso's: the front local -Y, the left +X, z = 0 where the wheels touch the ground (GTA's front is +Y:
turned half round).
"""
import hashlib
import os
import struct

from tools.ttre import chunks, dff2gltf, import_gltf

ALPHA_TEST, TRANSPARENT = import_gltf.ALPHA_TEST, import_gltf.TRANSPARENT
WHEELS = {'wheel_lf': True, 'wheel_rf': True, 'wheel_lm': False, 'wheel_rm': False, 'wheel_lb': False, 'wheel_rb': False}
COLLISION_BASE = 0xFD00  # PCOL perso ids for the cars: no level uses them (cast ids: perso k + 1; the SE's from 0x2000)
FLOOR_CLEARANCE = 0.2    # the box starts this far above the ground: Ed walks into the body, not under it
PCOL_MEETS, PCOL_FLOOR_OK, PCOL_HAS_SET, PCOL_ACTIVE = 1, 2, 8, 16
MAIN_PRIORITY, MAIN_FLAGS = 127, PCOL_MEETS | PCOL_FLOOR_OK | PCOL_HAS_SET  # retail Ed's collide set (ttlv.perso_collision)
LITE_AMBIENT, LITE_DIFFUSE = 0.35, 0.75
RIDER_SCALE = 0.42  # Ed in the seat, shrunk (2.17 tall -> about 0.9: his head over the windscreen of a small car)


def car_axes(p):
    """glTF (x, y, z) -> GTA (x, -z, y) -> the car's axes (GTA turned half round about Z): (-x, z, y)."""
    return (-p[0], p[2], p[1])


def source_digest(dff, txd, paint):
    h = hashlib.sha256(chunks.stamp(('dff2gltf',)).encode() + repr(paint).encode())
    for f in (dff, txd):
        if f and os.path.exists(f):
            h.update(open(f, 'rb').read())
    return h.hexdigest()[:16]


def model(sources, relative, out, paint):
    """The car's .glb in <out>/Vehicles/<name>/ from the .dff (and .txd beside it) at sources/relative, converted again only
    when the model, its paint or the converter's code changed. Returns (name, .glb path)."""
    dff = os.path.join(sources, relative)
    name = os.path.splitext(os.path.basename(dff))[0]
    folder = os.path.join(out, 'Vehicles', name)
    glb = os.path.join(folder, name + '.glb')
    txd = os.path.splitext(dff)[0] + '.txd'
    digest = source_digest(dff, txd if os.path.exists(txd) else None, paint)
    mark = glb + '.stamp'
    if not (os.path.exists(glb) and os.path.exists(mark) and open(mark).read().strip() == digest):
        os.makedirs(folder, exist_ok=True)
        dff2gltf.convert(dff, glb, None, paint)
        open(mark, 'w').write(digest + '\n')
    return name, glb


def read_parts(glb, tex_dir):
    """(body [((png, flags), [vertex], [index])], wheels [(name, steers, centre, radius, [((png, flags), verts, indices)])])
    in the car's axes, the ground at z = 0. A vertex: (position, normal, uv)."""
    doc, buffers, base = import_gltf.load_gltf(glb)
    cache = {}
    body, wheels = {}, []
    raw_wheels = []
    seat = None
    for node in doc['nodes']:
        if node.get('name', '').lower() == 'ped_frontseat':
            seat = node.get('translation', [0, 0, 0])
        if 'mesh' not in node:
            continue
        t = node.get('translation', [0, 0, 0])
        groups = {}
        lo, hi = [float('inf')] * 3, [float('-inf')] * 3
        for prim in doc['meshes'][node['mesh']]['primitives']:
            at = prim['attributes']
            pos = import_gltf.accessor(doc, buffers, at['POSITION'])
            nrm = import_gltf.accessor(doc, buffers, at['NORMAL']) if 'NORMAL' in at else [(0.0, 1.0, 0.0)] * len(pos)
            uv = import_gltf.accessor(doc, buffers, at['TEXCOORD_0']) if 'TEXCOORD_0' in at else [(0.0, 0.0)] * len(pos)
            idx = [i[0] for i in import_gltf.accessor(doc, buffers, prim['indices'])] if 'indices' in prim else list(range(len(pos)))
            png, flags = import_gltf.material_texture(doc, base, prim.get('material'), tex_dir, cache, buffers)
            verts, indices = groups.setdefault((png, flags), ([], []))
            first = len(verts)
            verts += [(p, n, tuple(u)) for p, n, u in zip(pos, nrm, uv)]
            indices += [first + i for i in idx[:len(idx) - len(idx) % 3]]
            for p in pos:
                for k in range(3):
                    lo[k], hi[k] = min(lo[k], p[k]), max(hi[k], p[k])
        name = node.get('name', '').lower()
        if name in WHEELS:
            centre = [t[k] + (lo[k] + hi[k]) / 2 for k in range(3)]  # glTF; its mesh about it
            radius = max(hi[1] - lo[1], hi[2] - lo[2]) / 2           # glTF y up, z along the car: the tyre's height
            raw_wheels.append((name, centre, radius, groups, [(lo[k] + hi[k]) / 2 for k in range(3)]))
        else:
            for key, (verts, indices) in groups.items():
                bv, bi = body.setdefault(key, ([], []))
                first = len(bv)
                bv += [(tuple(p[k] + t[k] for k in range(3)), n, u) for p, n, u in verts]
                bi += [first + i for i in indices]
    if not raw_wheels:
        raise SystemExit(f'{glb}: no wheel_* parts (is it a dff2gltf vehicle?)')
    lift = -min(car_axes(c)[2] - r for _, c, r, _, _ in raw_wheels)  # the lowest tyre's bottom -> z = 0

    def car(p, about=None):  # about: a wheel's centre (glTF, its mesh's own), else the body (lifted onto the ground)
        if about is not None:
            return car_axes(tuple(p[k] - about[k] for k in range(3)))
        x, y, z = car_axes(p)
        return (x, y, z + lift)
    out_body = [(key, [(car(p), car_axes(n), u) for p, n, u in verts], indices) for key, (verts, indices) in body.items()]
    for name, centre, radius, groups, mid in raw_wheels:
        c = car_axes(centre)
        parts = [(key, [(car(p, mid), car_axes(n), u) for p, n, u in verts], indices) for key, (verts, indices) in groups.items()]
        wheels.append((name, WHEELS[name], (c[0], c[1], c[2] + lift), radius, parts))
    if seat is not None:  # the passenger's seat dummy, mirrored to the driver's side
        x, y, z = car_axes(seat)
        seat = (-x, y, z + lift)
    return out_body, wheels, seat


def geometry(verts, indices):
    return (struct.pack('<II', len(verts), len(indices))
            + b''.join(struct.pack('<8f3I2f', *p, *n, *u, 0, 0, 0, -1.0, -1.0) for p, n, u in verts)
            + struct.pack(f'<{len(indices)}I', *indices))


def surface(texture, flags):
    return struct.pack('<iI2f2H', texture, flags | import_gltf.WRAP_U | import_gltf.WRAP_V, 0, 0, 0, 0)


def identity(t=(0, 0, 0)):
    return [1, 0, 0, t[0], 0, 1, 0, t[1], 0, 0, 1, t[2]]


def splice(c, glb, name, tex_dir, ttlv_dir, world, read_textures):
    """The car of `glb` added to the level's chunks `c` at `world` (row-major 3x4). Returns (parts, triangles, (box lo,
    hi), wheels)."""
    body, wheels, seat = read_parts(glb, tex_dir)
    table = read_textures(c['TEXS'])
    before = len(table)

    def tex(png):
        if png is None:
            return -1
        rel = os.path.relpath(os.path.abspath(png), os.path.abspath(ttlv_dir)).replace(os.sep, '/')
        if rel not in table:
            table.append(rel)
        return table.index(rel)
    lo, hi = [float('inf')] * 3, [float('-inf')] * 3
    for _, verts, _ in body:
        for p, _, _ in verts:
            for k in range(3):
                lo[k], hi[k] = min(lo[k], p[k]), max(hi[k], p[k])
    lo[2] = max(lo[2], FLOOR_CLEARANCE)
    if seat is None:  # no seat dummy: the middle, a little over the floor
        seat = ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2] + 0.3)
    parts = []  # (channel, object, surface, verts, indices)
    for (png, flags), verts, indices in body:
        parts.append((0, 0, surface(tex(png), flags), verts, indices))
    for k, (_, _, _, _, wparts) in enumerate(wheels):
        for (png, flags), verts, indices in wparts:
            parts.append((k + 1, k + 1, surface(tex(png), flags), verts, indices))
    channels = 1 + len(wheels)
    n_persos, = struct.unpack_from('<I', c['PRSO'])
    entry = struct.pack('<12f', *world) + struct.pack('<HHf', channels, 1, 1.0) + struct.pack(f'<{channels}h', -1, *[0] * len(wheels))
    entry += struct.pack('<I', len(parts)) + b''.join(struct.pack('<HH', ch, ob) + s + geometry(v, i) for ch, ob, s, v, i in parts)
    entry += struct.pack('<12f', *identity()) + b''.join(struct.pack('<12f', *identity(w[2])) for w in wheels)
    entry += struct.pack(f'<{channels}H', *range(channels))
    c['PRSO'] = struct.pack('<I', n_persos + 1) + c['PRSO'][4:] + entry
    c['TEXS'] = struct.pack('<I', len(table)) + c['TEXS'][4:] + b''.join(
        struct.pack('<H', len(t.encode())) + t.encode() for t in table[before:])

    # PCOL: one box
    vehicles = c.get('VEHI')
    index = struct.unpack_from('<H', vehicles)[0] if vehicles else 0
    cid = COLLISION_BASE + index
    pcol_entry = (struct.pack('<3H2B12f2H', cid, n_persos, 0xFFFF, 0, PCOL_MEETS | PCOL_FLOOR_OK | PCOL_HAS_SET | PCOL_ACTIVE,
                              *world, 1, 1)
                  + struct.pack('<2H', 0, 1) + struct.pack('<2BH2f9f', 8, 0xFF, 0, 1.0, 1.0, *lo, *hi, 0, 0, 0)
                  + struct.pack('<2H12f', 0, 0, *identity()))
    if 'PCOL' in c:
        count, mp, mf = struct.unpack_from('<H2B', c['PCOL'])
        c['PCOL'] = struct.pack('<H2B', count + 1, mp, mf) + c['PCOL'][4:] + pcol_entry
    else:
        c['PCOL'] = struct.pack('<H2B', 1, MAIN_PRIORITY, MAIN_FLAGS) + pcol_entry

    # VEHI
    record = struct.pack('<2H6f2H', n_persos, cid, *lo, *hi, len(wheels), 0)
    for k, (_, steers, centre, radius, _) in enumerate(wheels):
        record += struct.pack('<H2B4f', k + 1, 1 if steers else 0, 0, *centre, radius)
    record += struct.pack('<4f', *seat, RIDER_SCALE)
    record += struct.pack('<H', len(name.encode())) + name.encode()
    record += b'\0' * (-len(record) % 4)
    c['VEHI'] = struct.pack('<2H', index + 1, 0) + (vehicles[4:] if vehicles else b'') + record

    # SECT: its sector (none); LITE: its parts' materials, after the persos' parts already there
    if 'SECT' in c:
        n_coll, = struct.unpack_from('<I', c['COLL'])
        sect = c['SECT']
        at = len(sect) - (4 + 2 * n_persos + 4 + 2 * n_coll)
        assert struct.unpack_from('<I', sect, at)[0] == n_persos, 'SECT: the per-perso table is not where expected'
        c['SECT'] = (sect[:at] + struct.pack('<I', n_persos + 1) + sect[at + 4:at + 4 + 2 * n_persos] + struct.pack('<H', 0xFFFF)
                     + sect[at + 4 + 2 * n_persos:])
    if 'LITE' in c:
        from tools.ttre import custom
        lite = c['LITE']
        table_at, spans = custom.lite_mesh_table(lite)
        mesh_count, = struct.unpack_from('<I', c['MESH'])
        prso_parts = sum(len(p[3]) for p in custom.persos(c['PRSO'])[:n_persos])
        insert = spans[mesh_count + prso_parts][0] if mesh_count + prso_parts < len(spans) else len(lite)
        entries = b''
        for _, _, _, verts, _ in parts:
            centre = [sum(p[0][k] for p in verts) / max(1, len(verts)) for k in range(3)]
            entries += struct.pack('<6fI', *[LITE_AMBIENT] * 3, *[LITE_DIFFUSE] * 3, 1) + struct.pack('<II3f', 0, len(verts), *centre)
        c['LITE'] = (lite[:table_at] + struct.pack('<I', len(spans) + len(parts)) + lite[table_at + 4:insert] + entries
                     + lite[insert:])
    return len(parts), sum(len(i) // 3 for _, _, _, _, i in parts), (lo, hi), wheels

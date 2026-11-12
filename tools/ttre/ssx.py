"""SSX Tricky (PS2, 2001) courses as Open Tonic levels, ridden by the retail skier (docs/ENGINE.md "SSX Tricky courses").

A custom-layer import, like the glTF ones (tools/ttre/import_gltf.py): nothing here is Tonic Trouble. Your own disc image
(an .iso anywhere under files/sources, two folders deep at most) -> files/out/ttlv/SSX_<Course>.ttlv + .ttai and the
textures in files/out/Imported/SSX_<Course>/ (so the picker files them under "Imported").

What is read (format notes from the SSX modding community: GlitcherOG's SSX-Library, SsxMapper's map extractor; this
code is our own):
- The disc: plain ISO 9660; DATA/MODELS/<COURSE>.BIG per course. BIG = "C0FB" archive {u16 header size, u16 count,
  count x (u24 offset, u24 size, name\\0)} (big-endian), members EA RefPack compressed (10 FB).
- <course>.pbd (magic 00 15 1B 01): a header of 15 counts and 18 offsets; we read the patches (448 bytes each), the
  world's ground: a bicubic patch as 16 polynomial coefficients S(s, t) = sum c[r][k] t^r s^k (stored last row first),
  four corner UVs, a lightmap rectangle + lightmap id, a surface type (snow, ice, rock, ...), a texture index.
  Instances (models: buildings, trees, rails) are NOT read yet: their meshes are PS2 VIF packets (next step).
- <course>.ssh: SHPS textures, 8-bit indexed + a 256-colour palette (block 0x21); <course>_L.ssh: the lightmaps
  (32-bit, 128 x 128). PS2 colours: 0x80 = full, alpha 0x80 = opaque.
- <course>.aip: the AI paths; its start list = the start gates (we take the middle one's path: the first place down it
  with ground just under it that already slopes downhill, facing along the next ~10 m: start()).

Port choices (not SSX's behaviour): 1 SSX unit = 1 cm (a rider's bone chain is ~182 units), times SCALE so a rider is
~2.3 TT units and Ed (2.17) a touch smaller than one; patches tessellated N x N; the lightmap sampled into vertex light
(PS2 0x80 = 1.0 = .ttlv 128); surfaces -> floor materials for the skier (FLOORS): every rideable surface slides, the
slow ones with a little friction; type 17 (no collision) is drawn but not solid.

Usage (from the repo root; tools/convert_files.sh runs `all` when it finds the disc):
  python3 -m tools.ttre.ssx list                 (the courses on the disc)
  python3 -m tools.ttre.ssx COURSE [COURSE...]    (e.g. aloha; or `all`)
"""
import glob
import math
import os
import struct
import sys

from . import import_gltf as ig
from .dff2gltf import png

OUT = 'files/out'
SRC = 'files/sources'
SCALE = 0.0125          # SSX units (cm) -> TT units: 1.25 cm each, a rider ~2.28 tall next to Ed's 2.17
N = 8                   # patch tessellation: N x N quads
DONOR = 'Totalski'      # the retail skier (card type 2, docs/NOTES.md "Skiing"); his .ttai with the level-bound chunks emptied
SKIP = {'ssxfe'}        # the front end's backdrop, not a course

# Course file names -> titles (the picker shows SSX_<title>).
TITLES = {'alaska': 'Alaska', 'aloha': 'AlohaIceJam', 'elysium': 'ElysiumAlps', 'gari': 'Garibaldi',
          'megaple': 'TokyoMegaplex', 'merquer': 'MerquryCityMeltdown', 'mesa': 'Mesablanca', 'pipe': 'Pipedream',
          'snow': 'Snowdream', 'trick': 'Trick', 'untrack': 'Untracked'}

# Patch surface types (SSX-Library TrickySurfaceType) -> (grip, rebound) for the skier's card type 2: friction is
# -m grip^2 / dt x v (docs/NOTES.md "Skiing"), so grip 0 = retail snow, which never slows him; small grips brake gently
# (0.05: about 7 % of his speed a second at 30 Hz). Walls keep no material (grip 1): steep faces only push him away.
SNOW = (0.0, 0.8)
FLOORS = {
    0: SNOW,             # reset (out of bounds in SSX: respawns the rider; here only snow)
    1: SNOW,             # standard snow
    2: (0.06, 0.8),      # off track
    3: (0.04, 0.8),      # powder
    4: (0.07, 0.8),      # slow powder
    5: SNOW,             # ice
    6: (0.0, 1.0),       # bounce, unskiable
    7: SNOW,             # ice / water, no trail
    8: SNOW,             # glidy snow
    9: (0.08, 0.8),      # rock
    11: SNOW,            # ice crunch
    12: SNOW,            # wood
    13: SNOW,            # off-track metal
    14: SNOW,            # speed grinding
    15: SNOW,            # "standard unknown"
    16: (0.08, 0.8),     # sand
    18: SNOW, 19: SNOW,  # show-off ramp metal
}
WALL, NO_COLLISION = 10, 17


# ---- the disc ----

def find_iso():
    """The first SSX Tricky disc image under files/sources (two folders deep), or None."""
    for pattern in ('*.iso', '*/*.iso', '*/*/*.iso'):
        for path in sorted(glob.glob(os.path.join(SRC, pattern))):
            try:
                if Iso(path).find('DATA/MODELS') is not None and Iso(path).find('DATA/CHAR') is not None:
                    return path
            except (OSError, ValueError, struct.error):
                pass
    return None


class Iso:
    """Plain ISO 9660 (the PS2 DVD's primary volume): find(path) -> (sector, size) or None; read(path) -> bytes."""
    SECTOR = 2048

    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            f.seek(16 * self.SECTOR)
            pvd = f.read(self.SECTOR)
        if pvd[1:6] != b'CD001':
            raise ValueError(f'{path}: not an ISO 9660 image')
        self.root = self.record(pvd, 156)

    @staticmethod
    def record(buf, o):
        n = buf[o]
        sector, size = struct.unpack_from('<I', buf, o + 2)[0], struct.unpack_from('<I', buf, o + 10)[0]
        flags, name_len = buf[o + 25], buf[o + 32]
        name = buf[o + 33:o + 33 + name_len].decode('latin-1').split(';')[0]
        return n, sector, size, bool(flags & 2), name

    def listdir(self, sector, size):
        with open(self.path, 'rb') as f:
            f.seek(sector * self.SECTOR)
            buf = f.read(size)
        out, o = {}, 0
        while o < len(buf):
            if buf[o] == 0:  # records don't cross sectors: the rest of this one is padding
                o = (o // self.SECTOR + 1) * self.SECTOR
                continue
            n, s, z, is_dir, name = self.record(buf, o)
            if name not in ('\0', '\1'):
                out[name.upper()] = (s, z, is_dir)
            o += n
        return out

    def find(self, path):
        _, sector, size, _, _ = self.root
        entry = (sector, size, True)
        for part in path.strip('/').split('/'):
            if not entry[2]:
                return None
            entry = self.listdir(entry[0], entry[1]).get(part.upper())
            if entry is None:
                return None
        return entry

    def read(self, path):
        entry = self.find(path)
        if entry is None:
            raise FileNotFoundError(f'{self.path}: {path}')
        with open(self.path, 'rb') as f:
            f.seek(entry[0] * self.SECTOR)
            return f.read(entry[1])


def courses(iso):
    models = iso.find('DATA/MODELS')
    names = [n[:-4].lower() for n in iso.listdir(models[0], models[1]) if n.endswith('.BIG')]
    return sorted(n for n in names if n not in SKIP)


# ---- BIG archives and RefPack ----

def refpack(d):
    """EA RefPack (QFS) decompression."""
    p = 2 + (3 if d[0] & 0x01 else 0)
    n = 4 if d[0] & 0x80 else 3
    size = int.from_bytes(d[p:p + n], 'big')
    p += n
    out = bytearray()
    while p < len(d):
        b0 = d[p]
        if b0 < 0x80:
            b1 = d[p + 1]
            p += 2
            lit, ln, off = b0 & 3, ((b0 >> 2) & 7) + 3, ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = d[p + 1], d[p + 2]
            p += 3
            lit, ln, off = b1 >> 6, (b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = d[p + 1], d[p + 2], d[p + 3]
            p += 4
            lit, ln, off = b0 & 3, ((b0 & 0x0C) << 6) + b3 + 5, ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            lit, ln, off = ((b0 & 0x1F) << 2) + 4, 0, 0
            p += 1
        else:  # the end: up to 3 last literals
            lit = b0 & 3
            out += d[p + 1:p + 1 + lit]
            break
        out += d[p:p + lit]
        p += lit
        for _ in range(ln):  # byte by byte: the copy may overlap what it writes
            out.append(out[-off])
    if len(out) != size:
        raise ValueError(f'RefPack: {len(out)} bytes, header says {size}')
    return bytes(out)


def read_big(raw):
    """{member name (lower case, no folder): bytes} of a C0FB archive."""
    if raw[:2] != b'\xc0\xfb':
        raise ValueError('not a C0FB archive')
    count, = struct.unpack_from('>H', raw, 4)
    p, out = 6, {}
    for _ in range(count):
        off, size = int.from_bytes(raw[p:p + 3], 'big'), int.from_bytes(raw[p + 3:p + 6], 'big')
        e = raw.index(b'\0', p + 6)
        name = raw[p + 6:e].decode('latin-1').replace('\\', '/').rsplit('/', 1)[-1].lower()
        p = e + 1
        data = raw[off:off + size]
        if len(data) > 2 and data[1] == 0xFB and (data[0] & 0x3E) == 0x10:
            data = refpack(data)
        out[name] = data
    return out


def member(files, course, suffix):
    """The course's member ending in suffix ('.pbd', '_l.ssh', ...): names are not always the course's own
    (merquer's lightmaps are merque_L.ssh)."""
    exact = course + suffix
    if exact in files:
        return files[exact]
    hits = [n for n in files if n.endswith(suffix) and not n.endswith('_sky' + suffix)]
    return files[hits[0]] if len(hits) == 1 else None


# ---- textures (SHPS) ----

def read_ssh(raw, convert=True):
    """[(width, height, rgba bytes)] of an SHPS file: 8-bit indexed (type 2, its palette block 0x21) or 32-bit (5).
    convert: PS2 colours to 8-bit (ps2_rgba); else the bytes as stored (the lightmaps: light())."""
    conv = ps2_rgba if convert else bytes
    if raw[:4] != b'SHPS':
        raise ValueError('not an SHPS file')
    count, = struct.unpack_from('<I', raw, 8)
    out = []
    for i in range(count):
        off, = struct.unpack_from('<I', raw, 20 + 8 * i)
        kind, w, h = raw[off], *struct.unpack_from('<HH', raw, off + 4)
        nxt = int.from_bytes(raw[off + 1:off + 4], 'little')
        pixels = raw[off + 16:off + 16 + (w * h if kind == 2 else 4 * w * h)]
        blocks, o = {}, off
        while nxt:
            o += nxt
            blocks.setdefault(raw[o], o)
            nxt = int.from_bytes(raw[o + 1:o + 4], 'little')
        if kind == 2 and 0x21 in blocks:
            po = blocks[0x21]
            pal = [conv(raw[po + 16 + 4 * k:po + 20 + 4 * k]) for k in range(256)]
            pal = [pal[csm1(k)] for k in range(256)]
            rgba = b''.join(pal[c] for c in pixels)
        elif kind == 5:
            rgba = b''.join(conv(pixels[4 * k:4 * k + 4]) for k in range(w * h))
        else:
            rgba = None
        out.append((w, h, rgba))
    return out


def csm1(k):
    """The PS2's CSM1 palette order: in every 32 entries, the second and third runs of 8 are swapped."""
    return k ^ 0x18 if (k & 0x18) in (0x08, 0x10) else k


def ps2_rgba(px):
    """PS2 RGBA -> 8-bit RGBA: the textures keep their colours in 0..127 and alpha 0x80 = opaque (the lightmap's
    multiply doubles the colours back: light())."""
    return bytes((min(255, px[0] * 2), min(255, px[1] * 2), min(255, px[2] * 2), min(255, px[3] * 2)))


# ---- the world (.pbd) ----

PATCH = 448


def read_patches(pbd):
    """[dict(coef [16 (x, y, z)], uv [4 (u, v)], lm (x, y, w, h), lm_id, surface, texture)]."""
    if pbd[:4] != b'\x00\x15\x1b\x01':
        raise ValueError('not a PS2 SSX Tricky .pbd')
    counts = struct.unpack_from('<15i', pbd, 4)
    offsets = struct.unpack_from('<18i', pbd, 64)
    out = []
    for i in range(counts[1]):
        o = offsets[1] + i * PATCH
        v = [struct.unpack_from('<4f', pbd, o + 16 * j) for j in range(21)]
        surface, _kind, _vis, texture, lm_id = struct.unpack_from('<I4h', pbd, o + 424)
        out.append(dict(coef=[p[:3] for p in v[5:21][::-1]], uv=[p[:2] for p in v[1:5]], lm=v[0], lm_id=lm_id,
                        surface=surface, texture=texture))
    return out


# ---- models and their instances (.pbd), their collision (.ssf) ----

MATERIAL = 72      # .pbd material record: i16 texture (an .ssh index), ...
INSTANCE = 256     # .pbd instance: f32 matrix[16] (row vectors: p' = p M, translation in [12..14]), light vectors (4 x 4,
                   # stored as columns), light colours (4 x 4: three directional + ambient, PS2 0x80 = 1), u32 model, ...


def model_textures(pbd):
    """[[texture index per mesh slot] per material block]: a model mesh's texture = blocks[model block][mesh slot]."""
    counts = struct.unpack_from('<15i', pbd, 4)
    offsets = struct.unpack_from('<18i', pbd, 64)
    textures = [struct.unpack_from('<h', pbd, offsets[4] + MATERIAL * i)[0] for i in range(counts[4])]
    blocks, o = [], offsets[5]
    for _ in range(counts[5]):
        n, = struct.unpack_from('<I', pbd, o)
        ids = struct.unpack_from(f'<{n}i', pbd, o + 4)
        o += 4 + 4 * n
        blocks.append([textures[k] if 0 <= k < len(textures) else -1 for k in ids])
    return blocks


def _align16(o):
    return (o + 15) & ~15


def _mesh_packet(pbd, o, scale):
    """One PS2 packet of a model mesh at o: (next offset, strip lengths, uvs, normals, positions). Its layout (VIF
    unpacks): 48 bytes, u32 n_strips, u32, u32 n_vertices, u32, 16 bytes; i16 x n_strips strip lengths (x 3); 16 + 48
    bytes, uv i16 x 2 per vertex (/ 4096); 16 + 32 bytes, normals i16 x 3 (/ 32768); 16 bytes, positions i16 x 3
    (/ 32768 x the model's scale); each run padded to 16."""
    o += 48
    n_strips, _, nv, _ = struct.unpack_from('<4I', pbd, o)
    o += 32
    strips = [x // 3 for x in struct.unpack_from(f'<{n_strips}h', pbd, o)]
    o = _align16(o + 2 * n_strips) + 16 + 48
    raw = struct.unpack_from(f'<{2 * nv}h', pbd, o)
    uv = [(raw[2 * k] / 4096, raw[2 * k + 1] / 4096) for k in range(nv)]
    o = _align16(o + 4 * nv) + 16 + 32
    raw = struct.unpack_from(f'<{3 * nv}h', pbd, o)
    nrm = [(raw[3 * k] / 32768, raw[3 * k + 1] / 32768, raw[3 * k + 2] / 32768) for k in range(nv)]
    o = _align16(o + 6 * nv) + 16
    raw = struct.unpack_from(f'<{3 * nv}h', pbd, o)
    pos = [tuple(raw[3 * k + a] / 32768 * scale[a] for a in range(3)) for k in range(nv)]
    o = _align16(o + 6 * nv) + 16
    return o, strips, uv, nrm, pos


def read_models(pbd):
    """[(material block, [(object matrix or None, [(mesh slot, [triangles [(pos, normal, uv) x 3]])])])] per model
    (the high detail meshes; animated objects at their rest place). Strips become triangles winding as the PS2 strip
    alternates; facing is fixed later against the vertex normals."""
    counts = struct.unpack_from('<15i', pbd, 4)
    offsets = struct.unpack_from('<18i', pbd, 64)
    pointers = struct.unpack_from(f'<{counts[10]}i', pbd, offsets[10])
    out = []
    for ptr in pointers:
        b = offsets[11] + ptr
        _total, n_obj, obj_off, block = struct.unpack_from('<4I', pbd, b)
        scale = tuple(s or 1.0 for s in struct.unpack_from('<3f', pbd, b + 24))
        objects, o = [], b + obj_off
        for _ in range(n_obj):
            start = o
            _parent, high, _med, _low, _anim, mat_off = struct.unpack_from('<I5i', pbd, o)
            o += 24
            matrix = struct.unpack_from('<16f', pbd, start + mat_off) if mat_off != -1 else None
            meshes = []
            if high not in (0, -1):
                h = start + high
                n_mesh, = struct.unpack_from('<I', pbd, h + 32)
                mo = h + 44 + 4 * n_mesh
                for k in range(n_mesh):
                    _len, slot, _dlen, data = struct.unpack_from('<4I', pbd, mo + 28 * k)
                    p, tris = offsets[17] + data, []
                    while True:
                        p, strips, uv, nrm, pos = _mesh_packet(pbd, p, scale)
                        ends, first = set(), 0
                        for s in strips:
                            ends.add(first)
                            first += s
                        run = 0
                        for v in range(len(pos)):
                            if v in ends:
                                run = 1
                                continue
                            run += 1
                            if run < 3:
                                continue
                            a, c = (v - 2, v) if run % 2 else (v, v - 2)
                            tris.append(tuple((pos[i], nrm[i], uv[i]) for i in (a, v - 1, c)))
                        if p + 31 >= len(pbd) or pbd[p + 31] != 0x6C:  # another packet of this mesh follows
                            break
                    meshes.append((slot, tris))
            objects.append((matrix, meshes))
        out.append((block, objects))
    return out


def read_instances(pbd):
    """[dict(matrix, model, lights [(direction, colour) x 3], ambient)]."""
    counts = struct.unpack_from('<15i', pbd, 4)
    offsets = struct.unpack_from('<18i', pbd, 64)
    out = []
    for i in range(counts[2]):
        o = offsets[2] + INSTANCE * i
        m = struct.unpack_from('<16f', pbd, o)
        lv = struct.unpack_from('<16f', pbd, o + 64)
        cols = [struct.unpack_from('<4f', pbd, o + 128 + 16 * k) for k in range(4)]
        model, = struct.unpack_from('<I', pbd, o + 192)
        lights = [((lv[k], lv[4 + k], lv[8 + k]), cols[k][:3]) for k in range(3)]
        out.append(dict(matrix=m, model=model, lights=lights, ambient=cols[3][:3]))
    return out


def read_ssf(ssf):
    """(per instance: dict(visible, solid, mode, surface, collision) or None, collision models [[(indices, vertices,
    face normals)]]). Modes: 0 none, 1 triangle proxy (collision model `collision`), 2 the model's box, 3 physics
    spheres (moving props)."""
    if ssf is None or len(ssf) < 76:
        return [], []
    hdr = struct.unpack_from('<16I', ssf, 12)
    cm_n, cm_o, op_n, op_o, in_n, in_o = hdr[4], hdr[5], hdr[10], hdr[11], hdr[12], hdr[13]
    props = []
    for i in range(op_n):
        _mass, _bounce, _u, flags, surface, mode, index = struct.unpack_from('<ffHHihh', ssf, op_o + 24 * i)
        props.append(dict(visible=bool(flags & 1), solid=bool(flags & 32), mode=mode, surface=surface,
                          collision=index if mode != 3 else -1))
    states = struct.unpack_from(f'<{in_n}i', ssf, in_o)
    per_instance = [props[s] if 0 <= s < len(props) else None for s in states]
    models = []
    for i in range(cm_n):
        off, _size, n = struct.unpack_from('<3I', ssf, cm_o + 12 * i)
        o, group = off, []
        for _ in range(n):
            fc, vc, pad = struct.unpack_from('<3I', ssf, o)
            o += 12
            idx = struct.unpack_from(f'<{3 * fc}I', ssf, o)
            o += 12 * fc + pad
            verts = [struct.unpack_from('<3f', ssf, o + 16 * k) for k in range(vc)]
            o += 16 * vc
            normals = [struct.unpack_from('<3f', ssf, o + 16 * k) for k in range(fc)]
            o += 16 * fc
            group.append((idx, verts, normals))
        models.append(group)
    return per_instance, models


def transform(m, p, w=1.0):
    """Row vector p (w = 1 point, 0 direction) times the 4x4 m."""
    return tuple(p[0] * m[k] + p[1] * m[4 + k] + p[2] * m[8 + k] + w * m[12 + k] for k in range(3))


def start_path(aip):
    """The middle start gate's AI path as points [(x, y, z)] in SSX units, or None."""
    if len(aip) < 24:
        return None
    o = 16
    n_paths, n_starts = struct.unpack_from('<2I', aip, o)
    o += 8
    starts = struct.unpack_from(f'<{n_starts}I', aip, o)
    o += 4 * n_starts
    paths = []
    for _ in range(n_paths):
        n_pts, n_ev = struct.unpack_from('<2I', aip, o + 28)
        pos = struct.unpack_from('<3f', aip, o + 36)
        legs = [struct.unpack_from('<4f', aip, o + 72 + 16 * k) for k in range(n_pts)]
        paths.append((pos, legs))
        o += 72 + 16 * n_pts + 16 * n_ev
    if not starts:
        return None
    pos, legs = paths[starts[len(starts) // 2]]
    points = [tuple(pos)]
    for d in legs[:-1]:  # each leg: a unit direction and its length
        points.append(tuple(points[-1][k] + d[k] * d[3] for k in range(3)))
    return points


def ground_below(tris, x, y, top, depth, normal=False):
    """The highest collision surface at (x, y) between top and top - depth (TT units), or None; with normal,
    (z, the triangle's unit normal turned to face up)."""
    best = None
    for t in tris:
        if min(t[0], t[3], t[6]) > x or max(t[0], t[3], t[6]) < x or min(t[1], t[4], t[7]) > y or max(t[1], t[4], t[7]) < y:
            continue
        a, b, c = t[0:3], t[3:6], t[6:9]
        det = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(det) < 1e-12:
            continue
        w1 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / det
        w2 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / det
        w3 = 1 - w1 - w2
        if min(w1, w2, w3) < 0:
            continue
        z = w1 * a[2] + w2 * b[2] + w3 * c[2]
        if top - depth <= z <= top and (best is None or z > best[0]):
            u = [b[k] - a[k] for k in range(3)]
            v = [c[k] - a[k] for k in range(3)]
            n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
            k = math.sqrt(sum(q * q for q in n)) or 1.0
            best = (z, [q / k for q in n] if n[2] >= 0 else [-q / k for q in n])
    return best if normal or best is None else best[0]


MIN_FALL = 0.08   # the start: ground that drops at least this much a unit along the path (~5 deg), so he rides off
MAX_ABOVE = 4.0   # ... and lies at most this far under the path (the path runs at a rider's height; no long drop)


def start(tris, aip):
    """(spawn, heading, how) in TT units, or None: the first point down the middle start gate's path where the ground
    lies at most MAX_ABOVE under the path and falls at least MIN_FALL a unit along it, by its own normal (the gates
    stand on flat decks that are models, not converted yet: at the gate itself he would stand still, or hang over a
    drop); he faces along the path's next ~10 m."""
    points = start_path(aip) if aip else None
    if not points:
        return None
    pts = [tuple(c * SCALE for c in p) for p in points]
    # the triangles near the path only (the search below is brute force)
    lo_x, hi_x = min(p[0] for p in pts) - 20, max(p[0] for p in pts) + 20
    lo_y, hi_y = min(p[1] for p in pts) - 20, max(p[1] for p in pts) + 20
    near = [t for t in tris if max(t[0], t[3], t[6]) >= lo_x and min(t[0], t[3], t[6]) <= hi_x and
            max(t[1], t[4], t[7]) >= lo_y and min(t[1], t[4], t[7]) <= hi_y]
    # sample the path every unit: its points can be far apart
    samples = []
    for a, b in zip(pts, pts[1:]):
        n = max(1, int(math.dist(a, b)))
        samples += [tuple(a[k] + (b[k] - a[k]) * i / n for k in range(3)) for i in range(n)]
    for i, p in enumerate(samples[:-1]):
        hit = ground_below(near, p[0], p[1], p[2] + 1.0, MAX_ABOVE + 1.0, normal=True)
        if hit is None:
            continue
        z, n = hit
        j = min(len(samples) - 1, i + 10)
        dx, dy = samples[j][0] - p[0], samples[j][1] - p[1]
        k = math.hypot(dx, dy) or 1.0
        # the slope of the ground right under him along the path, from its normal (a look further on can find another
        # surface through a gap)
        fall = (n[0] * dx + n[1] * dy) / k / max(n[2], 1e-6)
        if n[2] < 0.5 or fall < MIN_FALL:
            continue
        return (p[0], p[1], z + 0.5), math.atan2(dy, dx), f'{i} units down the start path'
    return None


def evaluate(coef, s, t):
    tp, sp = (1, t, t * t, t * t * t), (1, s, s * s, s * s * s)
    return [sum(coef[r * 4 + k][a] * tp[r] * sp[k] for r in range(4) for k in range(4)) for a in range(3)]


def tangents(coef, s, t):
    tp, sp = (1, t, t * t, t * t * t), (1, s, s * s, s * s * s)
    dt, ds = (0, 1, 2 * t, 3 * t * t), (0, 1, 2 * s, 3 * s * s)
    d_s = [sum(coef[r * 4 + k][a] * tp[r] * ds[k] for r in range(4) for k in range(4)) for a in range(3)]
    d_t = [sum(coef[r * 4 + k][a] * dt[r] * sp[k] for r in range(4) for k in range(4)) for a in range(3)]
    return d_s, d_t


def bilinear(c, a, b):
    """Corners c (1 at a = b = 0, 2 at a = 1, 3 at b = 1, 4 at both; a runs with t, b with s)."""
    return tuple((1 - a) * (1 - b) * c[0][k] + a * (1 - b) * c[1][k] + (1 - a) * b * c[2][k] + a * b * c[3][k]
                 for k in range(len(c[0])))


def convert(iso, course, name=None):
    files = read_big(iso.read(f'DATA/MODELS/{course.upper()}.BIG'))
    name = name or 'SSX_' + TITLES.get(course, course.capitalize())
    pbd, ssh, lm_ssh, aip = (member(files, course, s) for s in ('.pbd', '.ssh', '_l.ssh', '.aip'))
    if pbd is None or ssh is None:
        raise SystemExit(f'{course}: no .pbd / .ssh in its BIG ({", ".join(sorted(files))})')
    patches = read_patches(pbd)
    textures = read_ssh(ssh)
    lightmaps = read_ssh(lm_ssh, convert=False) if lm_ssh else []

    tex_dir = os.path.join(OUT, 'Imported', name)
    os.makedirs(tex_dir, exist_ok=True)
    pngs = {}

    def texture(i):
        if i not in pngs:
            pngs[i] = None
            if 0 <= i < len(textures) and textures[i][2]:
                w, h, rgba = textures[i]
                path = os.path.join(tex_dir, f'{i:04d}.png')
                data = png(w, h, rgba)
                if not os.path.exists(path) or open(path, 'rb').read() != data:
                    with open(path, 'wb') as f:
                        f.write(data)
                cut = any(rgba[k] < 128 for k in range(3, len(rgba), 4))
                n = w * h
                mean = tuple(max(1.0, sum(rgba[c::4]) / n / 2) for c in range(3))  # in the PS2's 0..127
                pngs[i] = (path, ig.ALPHA_TEST if cut else 0, mean)
        return pngs[i]

    def light(p, s, t, mean):
        """The patch's light at (s, t) as .ttlv vertex light (128 = the texture unchanged). The PS2 draws a patch as
        (texture - L.rgb) x L.a / 128 (L its lightmap texel; GS alpha blending, 0x80 = 1; SSX-Library's
        TrickyLightmapConverter, SsxMapper's notes). A vertex light can only multiply, so: the factor that gives the
        same colour on the texture's mean, (1 - L / mean) x L.a / 128, on the texture doubled to full range: exact
        on flat textures, a little off on contrasty ones (the converter above least-squares fits per texel instead)."""
        lm_id = p['lm_id']
        if mean is None or not 0 <= lm_id < len(lightmaps) or not lightmaps[lm_id][2]:
            return ig.baked_light((1.0, 1.0, 1.0))
        w, h, rgba = lightmaps[lm_id]
        lx, ly, lw, lh = p['lm']
        # texel centres inside the patch's own tile (clamped: the tiles are packed edge to edge), bilinear
        x0, y0 = lx * w, ly * h
        fx = min(max(x0 + lw * w * s - 0.5, x0), x0 + lw * w - 1)
        fy = min(max(y0 + lh * h * t - 0.5, y0), y0 + lh * h - 1)
        ix, iy = min(int(fx), w - 2), min(int(fy), h - 2)
        ax, ay = fx - ix, fy - iy
        texel = [0.0] * 4
        for (dx, dy, k) in ((0, 0, (1 - ax) * (1 - ay)), (1, 0, ax * (1 - ay)), (0, 1, (1 - ax) * ay), (1, 1, ax * ay)):
            o = 4 * ((iy + dy) * w + ix + dx)
            for c in range(4):
                texel[c] += k * rgba[o + c]
        a = texel[3] / 128
        out = [max(0.0, (1 - texel[c] / mean[c]) * a) / 2 for c in range(3)]  # / 2: the PNG holds the texture x 2
        return ig.baked_light(out)

    def to_tt(p):
        return (p[0] * SCALE, p[1] * SCALE, p[2] * SCALE)  # SSX is Z-up, right-handed like TT

    groups, tris, tri_floors, floors, floor_ids = {}, [], [], [], {}
    lo, hi = [math.inf] * 3, [-math.inf] * 3
    missing = 0
    for p in patches:
        tex = texture(p['texture'])
        if tex is None:
            missing += 1
            tex = (None, 0, None)
        verts, indices = groups.setdefault((tex[0], tex[1] | ig.WRAP_U | ig.WRAP_V), ([], []))
        first = len(verts)
        for j in range(N + 1):
            for i in range(N + 1):
                s, t = i / N, j / N
                pos = to_tt(evaluate(p['coef'], s, t))
                d_s, d_t = tangents(p['coef'], s, t)
                n = (d_t[1] * d_s[2] - d_t[2] * d_s[1], d_t[2] * d_s[0] - d_t[0] * d_s[2],  # front = dS/dt x dS/ds
                     d_t[0] * d_s[1] - d_t[1] * d_s[0])
                k = math.sqrt(sum(x * x for x in n)) or 1.0
                n = tuple(x / k for x in n)
                uv = bilinear(p['uv'], t, s)
                # the lightmap tile runs the other way round (rotated a quarter turn against the texture)
                lm = light(p, s, t, tex[2])
                verts.append((pos, n, uv, lm))
                for a in range(3):
                    lo[a], hi[a] = min(lo[a], pos[a]), max(hi[a], pos[a])
        quads = []
        for j in range(N):
            for i in range(N):
                a = first + j * (N + 1) + i
                b, c, d = a + 1, a + N + 1, a + N + 2
                quads += [(a, c, b), (b, c, d)]
        # SSX patches face either way: make each patch's triangles agree with its own normal (front = up the normal)
        for tri in quads:
            pa, pb, pc = (verts[x][0] for x in tri)
            u = [pb[x] - pa[x] for x in range(3)]
            v = [pc[x] - pa[x] for x in range(3)]
            fn = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            if sum(fn[x] * verts[tri[0]][1][x] for x in range(3)) < 0:
                tri = (tri[0], tri[2], tri[1])
            indices += tri
            if p['surface'] != NO_COLLISION:
                tris.append(tuple(c for x in tri for c in verts[x][0]))
                f = FLOORS.get(p['surface'])
                if f is None:
                    tri_floors.append(0xFFFF)
                else:
                    if f not in floor_ids:
                        floor_ids[f] = len(floors)
                        floors.append(f)
                    tri_floors.append(floor_ids[f])

    path = start_path(aip) if aip else None
    sky = add_sky(files, tex_dir, groups, lo, hi)
    n_models = add_models(pbd, member(files, course, '.ssf'), texture, to_tt, groups, tris, tri_floors, floors,
                          floor_ids, lo, hi, gate=path[0] if path else None)

    gate = start(tris, aip)
    if gate:
        spawn, heading, how = gate
    else:
        spawn, heading, how = ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, hi[2]), None, 'none found: top of the bounds'

    def ttai(src, dst):
        from . import se
        se.write_ttai(src, dst, extra=[('AUTO', struct.pack('<BBH', 1, 0, 0))])  # he sets off by himself

    ig.write_level(name, groups, tris, spawn, donor=DONOR, heading=heading, floors=floors, tri_floors=tri_floors,
                   ttai=ttai, textures=sum(1 for v in pngs.values() if v), failed=missing, bounds=(lo, hi), quiet=True)
    problems = ([f'{missing} patches without a texture'] if missing else []) + \
               ([f'no start found ({how})'] if not gate else []) + ([f'sky: {sky}'] if 'triangles' not in sky else [])
    print(f'{name}.ttlv: ' + ('; '.join(problems) if problems else 'ok'))
    return name


SKY_ROOM = 1.3     # the sky dome scaled to this much of the course's half-diagonal
SKY_ALPHA = 128    # the .ttlv vertex alpha of a sky's baked light (ttlv.SKY_ALPHA): unlit, flagged as sky


def add_sky(files, tex_dir, groups, lo, hi):
    """The course's sky (<course>_sky.pbd: one dome model, ~28 m, drawn round the camera in SSX; its textures in
    <course>_sky.ssh) as a static dome round the course (port choice: the engine's skies are level geometry, as Tonic
    Trouble's are): centred on the course, scaled to SKY_ROOM of its half-diagonal, its horizon at the course's middle
    height, not solid. Call it after the patches and models: it reads their bounds (lo / hi) and leaves them as they are."""
    pbd = next((v for k, v in files.items() if k.endswith('_sky.pbd')), None)
    ssh = next((v for k, v in files.items() if k.endswith('_sky.ssh')), None)
    if pbd is None or ssh is None:
        return 'none on the disc'
    models, blocks, textures = read_models(pbd), model_textures(pbd), read_ssh(ssh)
    pts = [p for _, ob in models for _, me in ob for _, t in me for tri in t for p, _, _ in tri]
    if not pts:
        return 'empty'
    radius = max(math.hypot(p[0], p[1]) for p in pts) or 1.0
    centre = ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2)
    k = SKY_ROOM * math.hypot(hi[0] - lo[0], hi[1] - lo[1]) / 2 / radius
    pngs = {}
    n = 0
    for block, objects in models:
        for matrix, meshes in objects:
            for slot, tris in meshes:
                t = blocks[block][slot] if block < len(blocks) and slot < len(blocks[block]) else -1
                if t not in pngs:
                    pngs[t] = None
                    if 0 <= t < len(textures) and textures[t][2]:
                        w, h, rgba = textures[t]
                        path = os.path.join(tex_dir, f'sky_{t:04d}.png')
                        data = png(w, h, rgba)
                        if not os.path.exists(path) or open(path, 'rb').read() != data:
                            with open(path, 'wb') as f:
                                f.write(data)
                        pngs[t] = path
                verts, indices = groups.setdefault((pngs[t], ig.WRAP_U | ig.WRAP_V), ([], []))
                # unlit: the texture as stored (PS2 0x80 = 1 = the PNG's doubled colours / 2)
                light = ig.baked_light((0.5, 0.5, 0.5), SKY_ALPHA)
                for tri in tris:
                    pos = [tuple(centre[a] + (transform(matrix, p) if matrix is not None else p)[a] * k
                                 for a in range(3)) for p, _, _ in tri]
                    u = [pos[1][a] - pos[0][a] for a in range(3)]
                    v = [pos[2][a] - pos[0][a] for a in range(3)]
                    fn = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
                    mid = [sum(q[a] for q in pos) / 3 for a in range(3)]
                    inward = [centre[a] - mid[a] for a in range(3)]
                    order = (0, 1, 2) if sum(fn[a] * inward[a] for a in range(3)) >= 0 else (0, 2, 1)  # seen from inside
                    l = math.sqrt(sum(x * x for x in fn)) or 1.0
                    sign = 1 if order == (0, 1, 2) else -1
                    first = len(verts)
                    for j in order:
                        verts.append((pos[j], tuple(sign * x / l for x in fn), tri[j][2], light))
                    indices += [first, first + 1, first + 2]
                    n += 1
    return f'{n} triangles, {k / SCALE:.0f} x its size in SSX, round the course'


WALL_FLOOR = (0.0, 0.5)  # a prop's steep faces (more than 60 deg): slick
PROP = (0.1, 0.5)  # SSX surface -1 (props: their obstacle handling, not a surface): brakes the skier, doesn't stop him


def floor_index(f, floors, floor_ids):
    if f is None:
        return 0xFFFF
    if f not in floor_ids:
        floor_ids[f] = len(floors)
        floors.append(f)
    return floor_ids[f]


GATE_MARGIN = 100  # SSX units (1 m) round the start gate's point: what encloses it is the start's barrier


def add_models(pbd, ssf, texture, to_tt, groups, tris, tri_floors, floors, floor_ids, lo, hi, gate=None):
    """The instances: their models drawn (the ones the .ssf marks visible; lit per vertex by the instance's three
    lights and ambient: PS2 0x80 = 1, as the patches' light against the doubled textures), their collision from the
    .ssf (mode 1: its triangle proxy; 2: the model's box; 0 / 3 or not solid: none; invisible solid ones = walls).
    The start gates' barriers (instances whose box holds `gate`, the middle gate's point, SSX units) are drawn but not
    solid: SSX opens them at the race's start, which there isn't here. Returns counts for the report."""
    models = read_models(pbd)
    blocks = model_textures(pbd)
    props, cmodels = read_ssf(ssf)
    counts = {'instances drawn': 0, 'solid': 0, 'invisible walls': 0, 'moving props left static': 0,
              'start barriers opened': 0}
    offsets = struct.unpack_from('<18i', pbd, 64)

    def solid(tri, surface, normal=None):
        """A collision triangle (TT units), turned to face along normal (when given)."""
        a, b, c = tri
        if normal is not None:
            u = [b[k] - a[k] for k in range(3)]
            v = [c[k] - a[k] for k in range(3)]
            fn = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            if sum(fn[k] * normal[k] for k in range(3)) < 0:
                b, c = c, b
        tris.append(tuple(x for p in (a, b, c) for x in p))
        u = [b[k] - a[k] for k in range(3)]
        v = [c[k] - a[k] for k in range(3)]
        fn = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        steep = abs(fn[2]) < 0.5 * (math.sqrt(sum(x * x for x in fn)) or 1.0)
        # a prop's walls (and the invisible ones along the course) slick, so he glances off them: friction acts on
        # every contact, and their PROP grip stalled him against them; a prop's tops (roofs, decks) brake him
        f = WALL_FLOOR if surface < 0 and steep else PROP if surface < 0 else FLOORS.get(surface)
        tri_floors.append(floor_index(f, floors, floor_ids))

    for i, inst in enumerate(read_instances(pbd)):
        if not 0 <= inst['model'] < len(models):
            continue
        block, objects = models[inst['model']]
        m = inst['matrix']
        prop = props[i] if i < len(props) else None
        visible = prop is None or prop['visible']
        surface = prop['surface'] if prop else -1
        if prop and prop['mode'] == 3:
            counts['moving props left static'] += 1
        # world-space geometry of the model's objects
        world = []
        for matrix, meshes in objects:
            for slot, mtris in meshes:
                tex = blocks[block][slot] if block < len(blocks) and slot < len(blocks[block]) else -1
                out = []
                for t in mtris:
                    pts = []
                    for p, n, uv in t:
                        if matrix is not None:
                            p, n = transform(matrix, p), transform(matrix, n, 0.0)
                        p, n = transform(m, p), transform(m, n, 0.0)
                        k = math.sqrt(sum(x * x for x in n)) or 1.0
                        pts.append((p, tuple(x / k for x in n), uv))
                    out.append(pts)
                world.append((tex, out))
        if visible:
            counts['instances drawn'] += 1
            for tex, out in world:
                t = texture(tex)
                key = (t[0], t[1] | ig.WRAP_U | ig.WRAP_V) if t else (None, ig.WRAP_U | ig.WRAP_V)
                verts, indices = groups.setdefault(key, ([], []))
                for tri in out:
                    a, b, c = (q[0] for q in tri)
                    u = [b[x] - a[x] for x in range(3)]
                    v = [c[x] - a[x] for x in range(3)]
                    fn = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
                    if sum(fn[x] * (tri[0][1][x] + tri[1][1][x] + tri[2][1][x]) for x in range(3)) < 0:
                        tri = (tri[0], tri[2], tri[1])  # face the way its normals do
                    first = len(verts)
                    for p, n, uv in tri:
                        light = list(inst['ambient'])
                        for d, col in inst['lights']:
                            f = max(0.0, n[0] * d[0] + n[1] * d[1] + n[2] * d[2])
                            for x in range(3):
                                light[x] += f * col[x]
                        pos = to_tt(p)
                        verts.append((pos, n, uv, ig.baked_light([min(2.0, x / 128) for x in light])))
                        for x in range(3):
                            lo[x], hi[x] = min(lo[x], pos[x]), max(hi[x], pos[x])
                    indices += [first, first + 1, first + 2]
        if not prop or not prop['solid'] or prop['mode'] not in (1, 2):
            continue
        if gate is not None:
            box_lo = struct.unpack_from('<3f', pbd, offsets[2] + INSTANCE * i + 204)
            box_hi = struct.unpack_from('<3f', pbd, offsets[2] + INSTANCE * i + 216)
            if all(box_lo[x] - GATE_MARGIN <= gate[x] <= box_hi[x] + GATE_MARGIN for x in range(3)):
                counts['start barriers opened'] += 1
                continue
        counts['solid'] += 1
        if not visible:
            counts['invisible walls'] += 1
        if prop['mode'] == 1 and 0 <= prop['collision'] < len(cmodels):
            for idx, verts, normals in cmodels[prop['collision']]:
                for f in range(len(idx) // 3):
                    tri = [to_tt(transform(m, verts[idx[3 * f + j]])) for j in range(3)
                           if idx[3 * f + j] < len(verts)]
                    if len(tri) == 3:
                        solid(tri, surface, transform(m, normals[f], 0.0) if f < len(normals) else None)
        elif prop['mode'] == 2:  # the model's box, in its own axes
            local = [p for _, meshes in objects for _, mtris in meshes for t in mtris for p, _, _ in t]
            if not local:
                continue
            bl = [min(p[x] for p in local) for x in range(3)]
            bh = [max(p[x] for p in local) for x in range(3)]
            corner = [to_tt(transform(m, (bh[0] if i & 1 else bl[0], bh[1] if i & 2 else bl[1],
                                          bh[2] if i & 4 else bl[2]))) for i in range(8)]
            centre = [sum(c[x] for c in corner) / 8 for x in range(3)]
            for a, b, c, d in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
                q = [corner[a], corner[b], corner[c], corner[d]]
                mid = [sum(p[x] for p in q) / 4 for x in range(3)]
                out_n = [mid[x] - centre[x] for x in range(3)]  # faces point away from the box's centre
                solid((q[0], q[1], q[2]), surface, out_n)
                solid((q[0], q[2], q[3]), surface, out_n)
    return counts


def main(argv):
    from . import chunks
    chunks.KIND = 'ssx'
    if not argv:
        raise SystemExit(__doc__)
    path = find_iso()
    if path is None:
        raise SystemExit('no SSX Tricky disc image (.iso) under files/sources')
    iso = Iso(path)
    if argv == ['list']:
        for c in courses(iso):
            print(f'{c}\tSSX_{TITLES.get(c, c.capitalize())}')
        return
    for c in (courses(iso) if argv == ['all'] else [a.lower() for a in argv]):
        convert(iso, c)


if __name__ == '__main__':
    main(sys.argv[1:])

"""RenderWare 3.x models and textures as the GTA III-era games store them: .dff (a clump) and .txd (a texture
dictionary), PC (Direct3D 8 / 9) only. Read for tools/ttre/dff2gltf.py (GTA vehicles into glTF, then into our levels).

Not the original Tonic Trouble's formats: these come from other games' mods. Reference: the RenderWare stream format as
documented by the GTA modding community (gtamods.com wiki "RenderWare binary stream file") and librw (aap/librw), not
from any binary of ours.

    dff = read_dff(open(path, 'rb').read())
    dff.frames      [Frame(name, parent, rotation 3x3 rows, position)], LTMs with frame_ltm(dff, i)
    dff.geometries  [Geometry(positions, normals or None, uvs or None, triangles [(a, b, c, material)], materials)]
    dff.atomics     [(frame index, geometry index)]
    txd = read_txd(open(path, 'rb').read())   {lowercase name: Texture(name, width, height, rgba bytes)}
"""
import struct
from collections import namedtuple

STRUCT, STRING, EXTENSION, TEXTURE, MATERIAL, MATERIAL_LIST, FRAME_LIST, GEOMETRY, CLUMP, ATOMIC = \
    0x1, 0x2, 0x3, 0x6, 0x7, 0x8, 0xE, 0xF, 0x10, 0x14
TEXTURE_NATIVE, TEX_DICTIONARY, GEOMETRY_LIST, FRAME_NAME, BIN_MESH = 0x15, 0x16, 0x1A, 0x253F2FE, 0x50E

# rpGEOMETRY* flags
TRISTRIP, POSITIONS, TEXTURED, PRELIT, NORMALS, TEXTURED2, NATIVE = 0x1, 0x2, 0x4, 0x8, 0x10, 0x80, 0x01000000

Frame = namedtuple('Frame', 'name parent rotation position')  # rotation: rows right, up, at (p' = p M + pos)
Material = namedtuple('Material', 'color texture')              # color (r, g, b, a) 0..255, texture name or None
Geometry = namedtuple('Geometry', 'positions normals uvs triangles materials')
Clump = namedtuple('Clump', 'frames geometries atomics')
Texture = namedtuple('Texture', 'name width height rgba')       # rgba: width x height x 4 bytes, top row first


def version(library_id):
    """The RenderWare version (0x3XXYZ, as 0x34003 for 3.4.0.3) from a chunk's library id stamp."""
    if library_id & 0xFFFF0000:
        return ((library_id >> 14) & 0x3FF00) + 0x30000 | ((library_id >> 16) & 0x3F)
    return library_id << 8  # pre-3.1 streams: the bare version


def chunks(data, start, end):
    """(type, body offset, body size, version) of each chunk in data[start:end]."""
    o = start
    while o + 12 <= end:
        kind, size, lib = struct.unpack_from('<3I', data, o)
        if o + 12 + size > end:
            raise ValueError(f'chunk {kind:#x} at {o} runs past its parent')
        yield kind, o + 12, size, version(lib)
        o += 12 + size


def first(data, start, end, kind):
    return next((c for c in chunks(data, start, end) if c[0] == kind), None)


def string(data, o, size):
    return data[o:o + size].split(b'\0')[0].decode('latin-1')


# ---- models ----

def read_frames(data, o, size):
    s = first(data, o, o + size, STRUCT)
    n, = struct.unpack_from('<I', data, s[1])
    raw = [struct.unpack_from('<12fiI', data, s[1] + 4 + 56 * i) for i in range(n)]
    names = []
    for kind, eo, es, _ in chunks(data, o, o + size):
        if kind != EXTENSION:
            continue
        name = first(data, eo, eo + es, FRAME_NAME)
        names.append(string(data, name[1], name[2]) if name else '')
    names += [''] * (n - len(names))
    return [Frame(names[i], r[12], (r[0:3], r[3:6], r[6:9]), r[9:12]) for i, r in enumerate(raw)]


def read_material(data, o, size):
    s = first(data, o, o + size, STRUCT)
    _flags, r, g, b, a, _unused, textured = struct.unpack_from('<I4BII', data, s[1])
    name = None
    if textured:
        t = first(data, o, o + size, TEXTURE)
        if t:
            strings = [c for c in chunks(data, t[1], t[1] + t[2]) if c[0] == STRING]
            if strings:
                name = string(data, strings[0][1], strings[0][2]) or None
    return Material((r, g, b, a), name)


def read_materials(data, o, size):
    s = first(data, o, o + size, STRUCT)
    n, = struct.unpack_from('<I', data, s[1])
    refs = struct.unpack_from(f'<{n}i', data, s[1] + 4)
    own = [read_material(data, mo, ms) for kind, mo, ms, _ in chunks(data, o, o + size) if kind == MATERIAL]
    out, k = [], 0
    for r in refs:  # -1: the next material chunk; else a copy of an earlier entry
        if r < 0:
            out.append(own[k])
            k += 1
        else:
            out.append(out[r])
    return out


def read_geometry(data, o, size):
    s = first(data, o, o + size, STRUCT)
    p, ver = s[1], s[3]
    flags, n_uv, native, n_tri, n_vert, n_morph = struct.unpack_from('<HBBIII', data, p)
    if native:
        raise ValueError('a platform-native (console) geometry: PC models only')
    p += 16
    if ver < 0x34000:
        p += 12  # ambient, specular, diffuse
    if flags & PRELIT:
        p += 4 * n_vert
    if not n_uv:
        n_uv = 2 if flags & TEXTURED2 else 1 if flags & TEXTURED else 0
    uvs = None
    if n_uv:
        uvs = [struct.unpack_from('<2f', data, p + 8 * i) for i in range(n_vert)]
        p += 8 * n_vert * n_uv
    triangles = []
    for i in range(n_tri):  # stored as u16 v1, v0, material, v2: two u32 halves (v0 << 16 | v1), (v2 << 16 | material)
        w0, w1 = struct.unpack_from('<2I', data, p + 8 * i)
        triangles.append((w0 >> 16, w0 & 0xFFFF, w1 >> 16, w1 & 0xFFFF))
    p += 8 * n_tri
    positions = normals = None
    if n_morph:
        p += 16  # bounding sphere
        has_positions, has_normals = struct.unpack_from('<2I', data, p)
        p += 8
        if has_positions:
            positions = [struct.unpack_from('<3f', data, p + 12 * i) for i in range(n_vert)]
            p += 12 * n_vert
        if has_normals:
            normals = [struct.unpack_from('<3f', data, p + 12 * i) for i in range(n_vert)]
    m = first(data, o, o + size, MATERIAL_LIST)
    ext = first(data, o, o + size, EXTENSION)
    split = first(data, ext[1], ext[1] + ext[2], BIN_MESH) if ext else None
    if split:  # the material split the game draws by (GTA's tools leave the triangles' own material at 0)
        triangles = read_bin_mesh(data, split[1]) or triangles
    return Geometry(positions or [], normals, uvs, triangles, read_materials(data, m[1], m[2]) if m else [])


def read_bin_mesh(data, p):
    """The BinMesh PLG's triangles (a, b, c, material): per material a list or a strip of u32 indices."""
    strip, n_meshes, _total = struct.unpack_from('<3I', data, p)
    p += 12
    out = []
    for _ in range(n_meshes):
        n, material = struct.unpack_from('<2I', data, p)
        idx = struct.unpack_from(f'<{n}I', data, p + 8)
        p += 8 + 4 * n
        if strip & 1:
            for i in range(n - 2):
                a, b, c = (idx[i], idx[i + 1], idx[i + 2]) if i % 2 == 0 else (idx[i + 1], idx[i], idx[i + 2])
                if a != b and b != c and a != c:
                    out.append((a, b, c, material))
        else:
            out += [(idx[i], idx[i + 1], idx[i + 2], material) for i in range(0, n - 2, 3)]
    return out


def read_dff(data):
    clump = first(data, 0, len(data), CLUMP)
    if clump is None:
        raise ValueError('not a RenderWare clump (.dff)')
    o, end = clump[1], clump[1] + clump[2]
    frames, geometries, atomics = [], [], []
    for kind, co, cs, _ in chunks(data, o, end):
        if kind == FRAME_LIST:
            frames = read_frames(data, co, cs)
        elif kind == GEOMETRY_LIST:
            geometries = [read_geometry(data, go, gs) for k, go, gs, _ in chunks(data, co, co + cs) if k == GEOMETRY]
        elif kind == ATOMIC:
            s = first(data, co, co + cs, STRUCT)
            frame, geometry = struct.unpack_from('<2I', data, s[1])
            atomics.append((frame, geometry))
    return Clump(frames, geometries, atomics)


def frame_ltm(clump, i):
    """Frame i -> clump space as (rows 3x3, position): p' = p R + pos, the parents' applied."""
    rot, pos = clump.frames[i].rotation, clump.frames[i].position
    parent = clump.frames[i].parent
    while parent >= 0:
        prot, ppos = clump.frames[parent].rotation, clump.frames[parent].position
        rot = tuple(tuple(sum(r[k] * prot[k][c] for k in range(3)) for c in range(3)) for r in rot)
        pos = tuple(sum(pos[k] * prot[k][c] for k in range(3)) + ppos[c] for c in range(3))
        parent = clump.frames[parent].parent
    return rot, pos


# ---- textures ----

def _numpy():
    import numpy
    return numpy


def _expand565(np, c):
    r = ((c >> 11) & 31).astype(np.uint32)
    g = ((c >> 5) & 63).astype(np.uint32)
    b = (c & 31).astype(np.uint32)
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], axis=-1)


def _dxt_colors(np, blocks, one_bit_alpha):
    """blocks: n x 8 bytes (c0, c1, indices) -> n x 16 x 4 RGBA."""
    c0 = blocks[:, 0].astype(np.uint32) | (blocks[:, 1].astype(np.uint32) << 8)
    c1 = blocks[:, 2].astype(np.uint32) | (blocks[:, 3].astype(np.uint32) << 8)
    bits = blocks[:, 4:8].astype(np.uint32)
    bits = bits[:, 0] | (bits[:, 1] << 8) | (bits[:, 2] << 16) | (bits[:, 3] << 24)
    e0, e1 = _expand565(np, c0), _expand565(np, c1)
    four = (c0 > c1) | (not one_bit_alpha)
    four = four[:, None]
    c2 = np.where(four, (2 * e0 + e1) // 3, (e0 + e1) // 2)
    c3 = np.where(four, (e0 + 2 * e1) // 3, 0)
    palette = np.stack([e0, e1, c2, c3], axis=1)  # n x 4 x 3
    alpha = np.full((len(blocks), 4), 255, np.uint32)
    if one_bit_alpha:
        alpha[:, 3] = np.where(four[:, 0], 255, 0)
    index = (bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))[None, :]) & 3  # n x 16
    rows = np.arange(len(blocks))[:, None]
    rgb = palette[rows, index]
    a = alpha[rows, index]
    return np.concatenate([rgb, a[..., None]], axis=-1)


def decode_dxt(raw, width, height, kind):
    """kind 1 (DXT1), 3 (DXT3) or 5 (DXT5) -> RGBA bytes."""
    np = _numpy()
    bw, bh = max(1, (width + 3) // 4), max(1, (height + 3) // 4)
    size = 8 if kind == 1 else 16
    blocks = np.frombuffer(raw[:bw * bh * size], np.uint8).reshape(-1, size)
    if kind == 1:
        px = _dxt_colors(np, blocks, True)
    else:
        px = _dxt_colors(np, blocks[:, 8:], False)
        if kind == 3:
            nib = blocks[:, :8].astype(np.uint32)
            a = np.stack([nib & 15, nib >> 4], axis=-1).reshape(-1, 16)
            px[..., 3] = a * 17
        else:
            a0, a1 = blocks[:, 0].astype(np.int32), blocks[:, 1].astype(np.int32)
            bits = np.zeros(len(blocks), np.uint64)
            for k in range(6):
                bits |= blocks[:, 2 + k].astype(np.uint64) << np.uint64(8 * k)
            idx = ((bits[:, None] >> (3 * np.arange(16, dtype=np.uint64))[None, :]) & np.uint64(7)).astype(np.int32)
            eight = (a0 > a1)[:, None]
            lerp8 = ((7 - idx) * a0[:, None] + idx * a1[:, None]) // 7
            lerp6 = ((5 - idx) * a0[:, None] + idx * a1[:, None]) // 5
            six = np.where(idx == 6, 0, np.where(idx == 7, 255, lerp6))
            val = np.where(eight, lerp8, six)
            val = np.where(idx == 0, a0[:, None], np.where(idx == 1, a1[:, None], val))
            px[..., 3] = val
    img = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return img[:height, :width].astype(np.uint8).tobytes()


def decode_plain(raw, width, height, fmt, palette=None):
    """An uncompressed raster (Direct3D byte order) -> RGBA bytes. fmt: the raster format's pixel type (0x100 1555, 0x200
    565, 0x300 4444, 0x400 LUM8, 0x500 8888, 0x600 888, 0xA00 555) or 'pal8' / 'pal4' with palette (RGBA rows)."""
    np = _numpy()
    n = width * height
    if fmt in ('pal8', 'pal4'):
        pal = np.frombuffer(palette, np.uint8).reshape(-1, 4)
        if fmt == 'pal8':
            idx = np.frombuffer(raw[:n], np.uint8)
        else:  # two per byte, the low nibble first, rows padded to bytes
            stride = (width + 1) // 2
            b = np.frombuffer(raw[:stride * height], np.uint8).reshape(height, stride)
            idx = np.stack([b & 15, b >> 4], axis=-1).reshape(height, -1)[:, :width].reshape(-1)
        return pal[idx].tobytes()
    if fmt in (0x500, 0x600):
        px = np.frombuffer(raw[:4 * n], np.uint8).reshape(-1, 4)
        out = px[:, [2, 1, 0, 3]].copy()
        if fmt == 0x600:
            out[:, 3] = 255
        return out.tobytes()
    if fmt == 0x400:
        l = np.frombuffer(raw[:n], np.uint8)
        return np.stack([l, l, l, np.full(n, 255, np.uint8)], axis=-1).tobytes()
    c = np.frombuffer(raw[:2 * n], '<u2').astype(np.uint32)
    if fmt == 0x200:
        rgb = _expand565(np, c)
        a = np.full(n, 255, np.uint32)
    elif fmt == 0x300:
        rgb = np.stack([(c >> 8) & 15, (c >> 4) & 15, c & 15], axis=-1) * 17
        a = (c >> 12) * 17
    else:  # 1555, 555
        rgb = np.stack([(c >> 10) & 31, (c >> 5) & 31, c & 31], axis=-1)
        rgb = (rgb << 3) | (rgb >> 2)
        a = np.where(c >> 15, 255, 0) if fmt == 0x100 else np.full(n, 255, np.uint32)
    return np.concatenate([rgb, a[:, None]], axis=-1).astype(np.uint8).tobytes()


D3D9_FORMATS = {21: 0x500, 22: 0x600, 23: 0x200, 25: 0x100, 26: 0x300, 50: 0x400}


def read_texture_native(data, o, size):
    s = first(data, o, o + size, STRUCT)
    p = s[1]
    platform, _filter = struct.unpack_from('<2I', data, p)
    if platform not in (8, 9):
        raise ValueError(f'texture platform {platform}: PC (Direct3D 8 / 9) only')
    name = string(data, p + 8, 32)
    raster, d3d, width, height, _depth, levels, _rtype, extra = struct.unpack_from('<2I2H4B', data, p + 72)
    p += 88
    palette = None
    if raster & 0x2000:
        palette = data[p:p + 256 * 4]
        p += 256 * 4
    elif raster & 0x4000:
        n = 32 if platform == 8 else 16
        palette = data[p:p + n * 4]
        p += n * 4
    level_size, = struct.unpack_from('<I', data, p)
    raw = data[p + 4:p + 4 + level_size]
    dxt = 0
    if platform == 8:
        dxt = extra  # D3D8: the compression byte, 1..5
    elif d3d in (0x31545844, 0x33545844, 0x35545844):  # D3D9: the format's FOURCC
        dxt = {0x31545844: 1, 0x33545844: 3, 0x35545844: 5}[d3d]
    if dxt in (2, 4):
        dxt += 1  # premultiplied variants: decoded as their plain ones
    if dxt:
        rgba = decode_dxt(raw, width, height, dxt)
    elif palette is not None:
        rgba = decode_plain(raw, width, height, 'pal8' if raster & 0x2000 else 'pal4', palette)
    else:
        fmt = D3D9_FORMATS.get(d3d, raster & 0xF00) if platform == 9 else raster & 0xF00
        rgba = decode_plain(raw, width, height, fmt)
    # without the has-alpha flag (D3D8: the word after the raster format; D3D9: bit 0 of the last byte) the game draws it
    # opaque: DXT1's three-colour blocks would read as holes
    if not (d3d if platform == 8 else extra & 1):
        np = _numpy()
        px = np.frombuffer(rgba, np.uint8).reshape(-1, 4).copy()
        px[:, 3] = 255
        rgba = px.tobytes()
    return Texture(name, width, height, rgba)


def read_txd(data):
    d = first(data, 0, len(data), TEX_DICTIONARY)
    if d is None:
        raise ValueError('not a RenderWare texture dictionary (.txd)')
    out = {}
    for kind, o, size, _ in chunks(data, d[1], d[1] + d[2]):
        if kind == TEXTURE_NATIVE:
            t = read_texture_native(data, o, size)
            out[t.name.lower()] = t
    return out

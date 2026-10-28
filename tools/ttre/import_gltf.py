"""Any glTF 2.0 model as a .ttlv level for the viewer (format: tools/ttre/ttlv.py).

Usage: python3 -m tools.ttre.import_gltf MODEL.gltf|.glb NAME [--scale S] [--spawn X,Y,Z] [--ed DONOR]
Writes files/out/ttlv/NAME.ttlv (+ NAME.ttai, Ed's AI) and the textures as PNG in files/out/Imported/NAME/.

- Geometry: every triangle primitive of the default scene, node transforms applied, one mesh per material. glTF is
  Y-up, TT Z-up: (x, y, z) -> (x, -z, y), times --scale (TT units are about metres, as glTF's).
- Materials: the base colour texture (converted to PNG: the SDL build loads PNG only; ffmpeg or macOS sips), alphaMode
  MASK -> alpha test, BLEND -> transparent. Light: COLOR_0 becomes baked light (1.0 = the texture unchanged, as the
  glTF colour multiplies it), KHR_materials_unlit without COLOR_0 = 1.0; otherwise none (vertex alpha 0): the viewer
  lights it with its sun.
- Collision: the triangles of every non-blended material.
- Ed: his record (the EDPL chunk: meshes, animations) from the donor level's .ttlv (default bar, a cutscene level: no
  portals or triggers in its .ttai to fire at random places here), his textures added to this level's; his AI = the
  donor's .ttai (its per-triangle tables do not match this collision: the core drops them, tc_ai_model_link). Start: --spawn (glTF coordinates), else on the highest upward-facing surface at the model's centre.
"""
import base64
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys

from tools.ttre import chunks

ALPHA_TEST, TRANSPARENT = 1, 2
OUT = 'files/out'


# ---- glTF ----

def load_gltf(path):
    """(json, [buffer bytes]) for .gltf (external or data: URIs) or .glb."""
    raw = open(path, 'rb').read()
    bins = []
    if raw[:4] == b'glTF':
        off, doc = 12, None
        while off < len(raw):
            n, kind = struct.unpack_from('<I4s', raw, off)
            chunk = raw[off + 8:off + 8 + n]
            if kind == b'JSON':
                doc = json.loads(chunk)
            elif kind == b'BIN\0':
                bins.append(chunk)
            off += 8 + n
    else:
        doc = json.loads(raw)
    base = os.path.dirname(os.path.abspath(path))
    buffers = []
    for i, b in enumerate(doc.get('buffers', [])):
        uri = b.get('uri')
        if uri is None:
            buffers.append(bins[0] if bins else b'')
        elif uri.startswith('data:'):
            buffers.append(base64.b64decode(uri.split(',', 1)[1]))
        else:
            buffers.append(open(os.path.join(base, uri.replace('%20', ' ')), 'rb').read())
    return doc, buffers, base


COMPONENTS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
FORMATS = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}


def accessor(doc, buffers, k):
    """Accessor k as a list of tuples (normalised integers are scaled to 0..1)."""
    a = doc['accessors'][k]
    n, fmt = COMPONENTS[a['type']], FORMATS[a['componentType']]
    size = struct.calcsize(fmt)
    if 'bufferView' not in a:
        return [(0.0,) * n] * a['count']
    v = doc['bufferViews'][a['bufferView']]
    data, start = buffers[v['buffer']], v.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = v.get('byteStride') or n * size
    out = [struct.unpack_from('<' + fmt * n, data, start + stride * i) for i in range(a['count'])]
    if a.get('normalized') and fmt != 'f':
        top = float((1 << (8 * size - (1 if fmt.islower() else 0))) - 1)
        out = [tuple(x / top for x in t) for t in out]
    return out


def mat_mul(a, b):
    return [sum(a[r + 4 * k] * b[k + 4 * c] for k in range(4)) for c in range(4) for r in range(4)]


def node_matrix(node):
    """Column-major 4x4 of a node (matrix, or T * R * S)."""
    if 'matrix' in node:
        return list(node['matrix'])
    t = node.get('translation', [0, 0, 0])
    x, y, z, w = node.get('rotation', [0, 0, 0, 1])
    s = node.get('scale', [1, 1, 1])
    r = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
         2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
         2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
         t[0], t[1], t[2], 1]
    for c in range(3):
        for k in range(3):
            r[4 * c + k] *= s[c]
    return r


def apply(m, p, w=1.0):
    return tuple(m[k] * p[0] + m[4 + k] * p[1] + m[8 + k] * p[2] + m[12 + k] * w for k in range(3))


# ---- textures ----

def to_png(src, dst):
    if src.lower().endswith('.png'):
        shutil.copyfile(src, dst)
        return True
    for cmd in (['ffmpeg', '-v', 'error', '-y', '-i', src, dst], ['sips', '-s', 'format', 'png', src, '--out', dst]):
        try:
            if subprocess.run(cmd, capture_output=True).returncode == 0 and os.path.exists(dst):
                return True
        except FileNotFoundError:
            pass
    return False


def material_texture(doc, base, mi, tex_dir, cache, buffers):
    """(PNG path or None, flags) of material mi."""
    if mi is None:
        return None, 0
    m = doc['materials'][mi]
    mode = m.get('alphaMode', 'OPAQUE')
    flags = ALPHA_TEST if mode == 'MASK' else TRANSPARENT if mode == 'BLEND' else 0
    t = m.get('pbrMetallicRoughness', {}).get('baseColorTexture')
    if t is None:
        return None, flags
    key = doc['textures'][t['index']]['source']  # one PNG per image, however many materials share it
    img = doc['images'][key]
    if key not in cache:
        uri = img.get('uri', '')
        stem = img.get('name') or (os.path.splitext(os.path.basename(uri))[0] if uri and not uri.startswith('data:')
                                   else 'embedded')
        name = f"{len(cache):03d}_{re.sub(r'[^A-Za-z0-9_.-]', '_', stem)[:40]}.png"
        dst = os.path.join(tex_dir, name)
        if 'uri' in img and not img['uri'].startswith('data:'):
            ok = to_png(os.path.join(base, img['uri'].replace('%20', ' ')), dst)
        else:  # embedded (bufferView or data URI)
            if 'bufferView' in img:
                v = doc['bufferViews'][img['bufferView']]
                blob = buffers[v['buffer']][v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']]
            else:
                blob = base64.b64decode(img['uri'].split(',', 1)[1])
            tmp = dst + ('.png' if blob[:4] == b'\x89PNG' else '.jpg')
            open(tmp, 'wb').write(blob)
            ok = to_png(tmp, dst)
            os.remove(tmp)
        cache[key] = dst if ok else None
    return cache[key], flags


# ---- Ed from a donor .ttlv ----

def read_ed(donor):
    """(texture paths relative to the donor's folder, Ed record split into (start, parts [(object, surface tuple,
    geometry bytes)], anim bytes)) of a .ttlv, or None when it has no player."""
    c = chunks.read(donor, 'TTLV')
    if c is None:
        raise SystemExit(f'{donor}: not a current .ttlv (re-run the conversion)')
    if 'EDPL' not in c:
        return None
    t, o = c['TEXS'], 4
    textures = []
    for _ in range(struct.unpack_from('<I', t)[0]):
        n, = struct.unpack_from('<H', t, o)
        textures.append(t[o + 2:o + 2 + n].decode())
        o += 2 + n
    d, o = c['EDPL'], 0
    start = struct.unpack_from('<12f', d, o)
    o += 48
    o += 4  # static perso
    n_parts, = struct.unpack_from('<I', d, o)
    o += 4
    parts = []
    for _ in range(n_parts):
        obj, = struct.unpack_from('<H', d, o)
        o += 4
        tex, flags, su, sv, n_frames = struct.unpack_from('<iI2fH', d, o)
        frames = [struct.unpack_from('<if', d, o + 20 + 8 * k) for k in range(n_frames)]
        o += 20 + 8 * n_frames
        nv, ni = struct.unpack_from('<II', d, o)
        size = 8 + 52 * nv + 4 * ni
        parts.append((obj, (tex, flags, su, sv, frames), d[o:o + size]))
        o += size
    return textures, start, parts, d[o:]


# ---- writer ----

def surface(tex, flags, scroll=(0, 0), frames=()):
    return struct.pack('<iI2f2H', tex, flags, scroll[0], scroll[1], len(frames), 0) + b''.join(
        struct.pack('<if', t, s) for t, s in frames)


def baked_light(c):
    """glTF vertex colour (1.0 = texture unchanged) -> .ttlv rgba8 baked light (rgb / 128, see level.RADIOSITY_ONE)."""
    return struct.unpack('<I', bytes([min(255, max(0, round(x * 128))) for x in c[:3]] + [255]))[0]


def geometry(verts, indices):
    return (struct.pack('<II', len(verts), len(indices))
            + b''.join(struct.pack('<8f3I2f', *p, *nrm, *uv, light, 0, 0, -1.0, -1.0) for p, nrm, uv, light in verts)
            + struct.pack(f'<{len(indices)}I', *indices))


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    src, name = argv[0], argv[1]
    opts = dict(zip(argv[2::2], argv[3::2]))
    scale = float(opts.get('--scale', 1))
    donor = opts.get('--ed', 'bar')
    ttlv_dir = os.path.join(OUT, 'ttlv')
    tex_dir = os.path.join(OUT, 'Imported', name)
    os.makedirs(tex_dir, exist_ok=True)
    doc, buffers, base = load_gltf(src)

    def to_tt(p):
        return (p[0] * scale, -p[2] * scale, p[1] * scale)

    groups, cache = {}, {}  # (png, flags) -> (verts, indices)
    scene = doc['scenes'][doc.get('scene', 0)] if doc.get('scenes') else {'nodes': list(range(len(doc.get('nodes', []))))}

    def walk(k, parent):
        node = doc['nodes'][k]
        m = mat_mul(parent, node_matrix(node))
        if 'mesh' in node:
            normal_m = m  # fine for rotations and uniform scale
            for prim in doc['meshes'][node['mesh']]['primitives']:
                if prim.get('mode', 4) != 4 or 'POSITION' not in prim['attributes']:
                    continue
                at = prim['attributes']
                pos = [to_tt(apply(m, p)) for p in accessor(doc, buffers, at['POSITION'])]
                idx = [i[0] for i in accessor(doc, buffers, prim['indices'])] if 'indices' in prim else list(range(len(pos)))
                uv = accessor(doc, buffers, at['TEXCOORD_0']) if 'TEXCOORD_0' in at else [(0.0, 0.0)] * len(pos)
                if 'NORMAL' in at:
                    nrm = []
                    for nv in accessor(doc, buffers, at['NORMAL']):
                        x, y, z = to_tt(apply(normal_m, nv, 0.0))
                        l = math.sqrt(x * x + y * y + z * z) or 1
                        nrm.append((x / l, y / l, z / l))
                else:  # flat normals from the faces
                    nrm = [(0.0, 0.0, 1.0)] * len(pos)
                    for t in range(0, len(idx) - 2, 3):
                        a, b, c = (pos[idx[t + j]] for j in range(3))
                        u = [b[i] - a[i] for i in range(3)]
                        v = [c[i] - a[i] for i in range(3)]
                        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
                        l = math.sqrt(sum(x * x for x in n)) or 1
                        for j in range(3):
                            nrm[idx[t + j]] = tuple(x / l for x in n)
                png, flags = material_texture(doc, base, prim.get('material'), tex_dir, cache, buffers)
                mat = doc['materials'][prim['material']] if 'material' in prim else {}
                if 'COLOR_0' in at:
                    light = [baked_light(c) for c in accessor(doc, buffers, at['COLOR_0'])]
                elif 'KHR_materials_unlit' in mat.get('extensions', {}):
                    light = [baked_light((1.0, 1.0, 1.0))] * len(pos)
                else:
                    light = [0] * len(pos)
                verts, indices = groups.setdefault((png, flags), ([], []))
                first = len(verts)
                verts += [(pos[i], nrm[i], tuple(uv[i]), light[i]) for i in range(len(pos))]
                indices += [first + i for i in idx[:len(idx) - len(idx) % 3]]
        for c in node.get('children', []):
            walk(c, m)

    identity = [1.0 if r == c else 0.0 for c in range(4) for r in range(4)]
    for k in scene['nodes']:
        walk(k, identity)
    if not groups:
        raise SystemExit(f'{src}: no triangles')

    # textures: this model's, then Ed's (re-rooted from the donor's folder)
    tex_paths, tex_ids = [], {}

    def tex_id(path):
        if path is None:
            return -1
        rel = os.path.relpath(os.path.abspath(path), os.path.abspath(ttlv_dir)).replace(os.sep, '/')
        if rel not in tex_ids:
            tex_ids[rel] = len(tex_paths)
            tex_paths.append(rel)
        return tex_ids[rel]

    meshes = b''
    tris = []
    lo = [math.inf] * 3
    hi = [-math.inf] * 3
    for (png, flags), (verts, indices) in groups.items():
        for i in range(0, len(indices), 0xFFFFFF):  # one mesh per material
            meshes += surface(tex_id(png), flags) + geometry(verts, indices)
            break
        for p, *_ in verts:
            for k in range(3):
                lo[k], hi[k] = min(lo[k], p[k]), max(hi[k], p[k])
        if not flags & TRANSPARENT:
            for t in range(0, len(indices), 3):
                tris.append(tuple(c for j in range(3) for c in verts[indices[t + j]][0]))

    # spawn: --spawn (glTF coordinates), else the highest upward-facing surface at the centre (skips sky domes and
    # ceilings, whose faces point down)
    if '--spawn' in opts:
        spawn = to_tt([float(x) for x in opts['--spawn'].split(',')])
    else:
        cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
        best = None
        for t in tris:
            a, b, c = t[0:3], t[3:6], t[6:9]
            det = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            if det < 1e-12:  # det = 2 x the face normal's z (glTF fronts are counter-clockwise): vertical or facing down
                continue
            w1 = ((b[1] - c[1]) * (cx - c[0]) + (c[0] - b[0]) * (cy - c[1])) / det
            w2 = ((c[1] - a[1]) * (cx - c[0]) + (a[0] - c[0]) * (cy - c[1])) / det
            w3 = 1 - w1 - w2
            if min(w1, w2, w3) >= 0:
                z = w1 * a[2] + w2 * b[2] + w3 * c[2]
                best = z if best is None else max(best, z)
        spawn = (cx, cy, (best if best is not None else hi[2]) + 0.3)

    coll = struct.pack('<I', len(tris)) + b''.join(struct.pack('<9f', *t) for t in tris)
    flor = struct.pack('<HH', 0, 0) + struct.pack(f'<{len(tris)}H', *([0xFFFF] * len(tris)))
    ed = read_ed(os.path.join(ttlv_dir, f'{donor}.ttlv'))
    edpl = None
    if ed:
        donor_tex, start, parts, anims = ed
        start = list(start)
        start[3], start[7], start[11] = spawn
        ed_parts = b''
        for obj, (tex, flags, su, sv, frames), geo in parts:
            def remap(t):
                return tex_id(os.path.join(ttlv_dir, donor_tex[t])) if 0 <= t < len(donor_tex) else -1
            ed_parts += struct.pack('<2H', obj, 0) + surface(remap(tex), flags, (su, sv),
                                                              [(remap(t), s) for t, s in frames]) + geo
        edpl = struct.pack('<12fiI', *start, -1, len(parts)) + ed_parts + anims
        if os.path.exists(os.path.join(ttlv_dir, f'{donor}.ttai')):
            shutil.copyfile(os.path.join(ttlv_dir, f'{donor}.ttai'), os.path.join(ttlv_dir, f'{name}.ttai'))

    texs = struct.pack('<I', len(tex_paths)) + b''.join(struct.pack('<H', len(p.encode())) + p.encode() for p in tex_paths)
    out = os.path.join(ttlv_dir, f'{name}.ttlv')
    chunks.write(out, 'TTLV', [('LEVL', struct.pack('<I5f', 1, 0, 0, 0, 0, 0)), ('TEXS', texs),
                               ('MESH', struct.pack('<I', len(groups)) + meshes), ('PRSO', struct.pack('<I', 0)),
                               ('COLL', coll), ('FLOR', flor)] + ([('EDPL', edpl)] if edpl else []))
    n_tri = sum(len(i) // 3 for _, i in groups.values())
    print(f'wrote {out}: {len(groups)} meshes, {n_tri} triangles, {len(cache)} textures '
          f'({sum(1 for v in cache.values() if v is None)} failed), {len(tris)} collision triangles, Ed '
          f'{"from " + donor if ed else "none"} at ({spawn[0]:.1f}, {spawn[1]:.1f}, {spawn[2]:.1f}); '
          f'bounds ({lo[0]:.0f}, {lo[1]:.0f}, {lo[2]:.0f})..({hi[0]:.0f}, {hi[1]:.0f}, {hi[2]:.0f})')


if __name__ == '__main__':
    main(sys.argv[1:])

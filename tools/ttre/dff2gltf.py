"""A GTA III-era vehicle (.dff + .txd, PC RenderWare: tools/ttre/rw.py) as one self-contained glTF 2.0 binary (.glb).

Usage: python3 -m tools.ttre.dff2gltf MODEL.dff OUT.glb [--txd FILE.txd] [--paint R,G,B] [--paint2 R,G,B]
                                        [--extras 1,2] [--all]

The .glb opens in any glTF viewer (Blender, ...), goes into tools/ttre/import_gltf.py as a level of its own, and is what
the custom layer's vehicles are made from (tools/ttre/vehicle.py). What it holds:
- One node per part the game would draw on an undamaged car, named as its frame (chassis_hi, bonnet_hi_ok, wheel_lf,
  ...), translated to the frame's place in the car; its mesh in the car's axes about that point (the frame's rotation
  baked in, so wheels are upright and their nodes carry no rotation). Left out: the damaged variants (*_dam), the low
  details (*_vlo, *_lo), the extras (extra1, extra2 ...: GTA shows a random one or none) unless --extras names them;
  --all keeps every atomic. Empty nodes mark GTA's seat dummies (ped_frontseat: the front passenger's; the driver sits
  mirrored across X), at the frame's place.
- Wheels: every wheel_*_dummy frame gets the wheel mesh that sits under it; GTA III's cars that model one wheel only get
  that one copied to the other dummies, mirrored for the other side.
- Materials: GTA paints a car at runtime by its material colours. The placeholders are replaced: primary (60, 255, 0) by
  --paint (default a red), secondary (255, 0, 175) by --paint2 (default a light grey), the light markers ((255, 175, 0),
  (0, 255, 200), (185, 255, 0), (255, 60, 0)) by white; then each material's colour is baked into its own copy of its
  texture (glTF consumers such as import_gltf.py read the texture only), untextured materials get a small plain texture of
  their colour. Material alpha below 240, or a texture with soft alpha, is BLEND; a texture with hard holes is MASK.
- glTF is Y-up: GTA (x, y, z) -> (x, z, -y), so import_gltf.py's (x, -z, y) gives GTA's axes back (the car's front +Y).
  GTA units are metres, as glTF's.
Not converted: reflections and specular (the MatEffects / specular plugins), damage, lights, the collision (.col).
"""
import json
import math
import os
import struct
import sys
import zlib

from tools.ttre import rw

PRIMARY, SECONDARY = (60, 255, 0), (255, 0, 175)
LIGHT_MARKERS = {(255, 175, 0), (0, 255, 200), (185, 255, 0), (255, 60, 0)}
DEFAULT_PAINT, DEFAULT_PAINT2 = (168, 18, 24), (200, 200, 200)
WHEELS = ('lf', 'rf', 'lm', 'rm', 'lb', 'rb')
SEATS = ('ped_frontseat',)
OPAQUE_ALPHA = 240  # material alpha at or above this draws opaque (GTA's dials and trims at 242-253 are not see-through)


def to_gltf(p):
    return (p[0], p[2], -p[1])


def kept(name, extras, everything):
    if everything:
        return True
    n = name.lower()
    if n.endswith('_dam') or n.endswith('_vlo') or n.endswith('_lo'):
        return False
    if n.startswith('extra'):
        return n[5:] in extras
    return True


def mat_vec(v, rot):
    """v R (RenderWare's row-vector convention)."""
    return tuple(sum(v[k] * rot[k][c] for k in range(3)) for c in range(3))


def det3(r):
    return (r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
            + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]))


def png(width, height, rgba):
    rows = b''.join(b'\0' + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))

    def block(tag, body):
        return struct.pack('>I', len(body)) + tag + body + struct.pack('>I', zlib.crc32(tag + body) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + block(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + block(b'IDAT', zlib.compress(rows, 6)) + block(b'IEND', b''))


def tinted(texture, color):
    """The texture's RGBA times the material colour (0..255 each), as the game's modulate-material-colour flag draws it."""
    import numpy as np
    px = np.frombuffer(texture.rgba, np.uint8).reshape(-1, 4).astype(np.uint32)
    px = (px * np.array(color, np.uint32)[None, :] + 127) // 255
    return px.astype(np.uint8).tobytes()


def alpha_mode(rgba, material_alpha):
    if material_alpha < OPAQUE_ALPHA:
        return 'BLEND'
    a = rgba[3::4]
    if min(a) == 255:
        return 'OPAQUE'
    soft = sum(1 for x in a[::7] if 16 < x < 240)
    return 'BLEND' if soft * 7 > len(a) // 100 else 'MASK'


def paint(color, primary, secondary):
    rgb, a = tuple(color[:3]), color[3]
    if rgb == PRIMARY:
        rgb = primary
    elif rgb == SECONDARY:
        rgb = secondary
    elif rgb in LIGHT_MARKERS:
        rgb = (255, 255, 255)
    return rgb + (a,)


def facing_flip(positions, normals, triangles):
    """True when the triangles wind against the vertex normals (glTF fronts are counter-clockwise)."""
    if not normals:
        return False
    s = 0.0
    for a, b, c, _ in triangles:
        pa, pb, pc = positions[a], positions[b], positions[c]
        u = [pb[i] - pa[i] for i in range(3)]
        v = [pc[i] - pa[i] for i in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        m = [normals[a][i] + normals[b][i] + normals[c][i] for i in range(3)]
        s += n[0] * m[0] + n[1] * m[1] + n[2] * m[2]
    return s < 0


class Writer:
    """glTF JSON + one binary buffer."""

    def __init__(self):
        self.doc = {'asset': {'version': '2.0', 'generator': 'Open Tonic tools/ttre/dff2gltf.py'}, 'scene': 0,
                    'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [], 'materials': [], 'textures': [],
                    'images': [], 'samplers': [{'wrapS': 10497, 'wrapT': 10497}], 'accessors': [],
                    'bufferViews': [], 'buffers': []}
        self.bin = bytearray()

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin += b'\0'
        v = {'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': len(data)}
        if target:
            v['target'] = target
        self.bin += data
        self.doc['bufferViews'].append(v)
        return len(self.doc['bufferViews']) - 1

    def accessor(self, values, kind, component, target):
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[kind]
        fmt = {5126: 'f', 5125: 'I'}[component]
        flat = [x for t in values for x in t] if n > 1 else list(values)
        a = {'bufferView': self.view(struct.pack(f'<{len(flat)}{fmt}', *flat), target), 'componentType': component,
             'count': len(values), 'type': kind}
        if kind == 'VEC3':
            a['min'] = [min(t[i] for t in values) for i in range(3)]
            a['max'] = [max(t[i] for t in values) for i in range(3)]
        self.doc['accessors'].append(a)
        return len(self.doc['accessors']) - 1

    def glb(self):
        while len(self.bin) % 4:
            self.bin += b'\0'
        self.doc['buffers'] = [{'byteLength': len(self.bin)}]
        js = json.dumps(self.doc, separators=(',', ':')).encode()
        js += b' ' * (-len(js) % 4)
        return (struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(js) + 8 + len(self.bin))
                + struct.pack('<I4s', len(js), b'JSON') + js + struct.pack('<I4s', len(self.bin), b'BIN\0') + bytes(self.bin))


def convert(dff_path, out_path, txd_path=None, primary=DEFAULT_PAINT, secondary=DEFAULT_PAINT2, extras=(), everything=False):
    clump = rw.read_dff(open(dff_path, 'rb').read())
    if txd_path is None:
        stem = os.path.splitext(dff_path)[0] + '.txd'
        here = os.path.dirname(os.path.abspath(dff_path))
        txds = [os.path.join(here, f) for f in sorted(os.listdir(here)) if f.lower().endswith('.txd')]
        txd_path = stem if os.path.exists(stem) else (txds[0] if txds else None)
    textures = rw.read_txd(open(txd_path, 'rb').read()) if txd_path else {}
    names = [f.name for f in clump.frames]
    lower = [n.lower() for n in names]

    # the parts: (name, frame rotation, frame position, geometry, mirrored copy)
    parts = [(names[f], *rw.frame_ltm(clump, f), clump.geometries[g], False)
             for f, g in clump.atomics if kept(names[f], extras, everything)]
    # wheels: each dummy without a wheel mesh under it gets one of the others, mirrored when it is on the other side
    have = {}
    for p in parts:
        n = p[0].lower()
        if n.startswith('wheel_') and n[6:8] in WHEELS:
            have[n[6:8]] = p
    for w in WHEELS:
        dummy = f'wheel_{w}_dummy'
        if dummy not in lower or w in have or not have:
            continue
        src = have.get(('r' if w[0] == 'l' else 'l') + w[1]) or have.get(w[0] + 'f') or next(iter(have.values()))
        _, srot, _, geo, _ = src
        _, dpos = rw.frame_ltm(clump, lower.index(dummy))
        mirror = src[0].lower()[6] != w[0]
        parts.append((f'wheel_{w}', srot, dpos, geo, mirror))

    out = Writer()
    images, materials = {}, {}

    def material(m):
        color = paint(m.color, primary, secondary)
        tex = textures.get(m.texture.lower()) if m.texture else None
        key = (tex.name if tex else None, color)
        if key in materials:
            return materials[key]
        if tex:
            rgba = tinted(tex, color[:3] + (255,)) if color[:3] != (255, 255, 255) else tex.rgba
            if color[3] < OPAQUE_ALPHA:
                rgba = tinted(rw.Texture(tex.name, tex.width, tex.height, rgba), (255, 255, 255, color[3]))
            w, h, label = tex.width, tex.height, tex.name
        else:
            solid = color[:3] + ((255,) if color[3] >= OPAQUE_ALPHA else color[3:])
            rgba, w, h, label = bytes(solid) * 16, 4, 4, 'colour_%02x%02x%02x' % color[:3]
        mode = alpha_mode(rgba, color[3])
        ikey = (label, color)
        if ikey not in images:
            out.doc['images'].append({'name': label, 'mimeType': 'image/png', 'bufferView': out.view(png(w, h, rgba))})
            out.doc['textures'].append({'sampler': 0, 'source': len(out.doc['images']) - 1})
            images[ikey] = len(out.doc['textures']) - 1
        entry = {'name': label, 'pbrMetallicRoughness': {'baseColorTexture': {'index': images[ikey]},
                                                         'metallicFactor': 0.0, 'roughnessFactor': 0.8},
                 'alphaMode': mode}
        if mode == 'MASK':
            entry['alphaCutoff'] = 0.5
        out.doc['materials'].append(entry)
        materials[key] = len(out.doc['materials']) - 1
        return materials[key]

    triangles = 0
    for name, rot, pos, geo, mirror in parts:
        if not geo.positions or not geo.triangles:
            continue
        pos_car = [mat_vec(p, rot) for p in geo.positions]
        if mirror:  # mirrored across the car's X after the frame's rotation (the winding follows from the normals)
            pos_car = [(-p[0], p[1], p[2]) for p in pos_car]
        nrm_car = None
        if geo.normals:
            nrm_car = []
            for n in geo.normals:
                x, y, z = mat_vec(n, rot)
                if mirror:
                    x = -x
                l = math.sqrt(x * x + y * y + z * z) or 1
                nrm_car.append((x / l, y / l, z / l))
        flip = facing_flip(pos_car, nrm_car, geo.triangles)
        prims = []
        by_mat = {}
        for a, b, c, mi in geo.triangles:
            by_mat.setdefault(mi, []).append((a, c, b) if flip else (a, b, c))
        for mi, tris in sorted(by_mat.items()):
            used = sorted({i for t in tris for i in t})
            remap = {v: k for k, v in enumerate(used)}
            attrs = {'POSITION': out.accessor([to_gltf(pos_car[i]) for i in used], 'VEC3', 5126, 34962)}
            if nrm_car:
                attrs['NORMAL'] = out.accessor([to_gltf(nrm_car[i]) for i in used], 'VEC3', 5126, 34962)
            if geo.uvs:
                attrs['TEXCOORD_0'] = out.accessor([geo.uvs[i] for i in used], 'VEC2', 5126, 34962)
            prim = {'attributes': attrs, 'mode': 4,
                    'indices': out.accessor([remap[i] for t in tris for i in t], 'SCALAR', 5125, 34963)}
            if mi < len(geo.materials):
                prim['material'] = material(geo.materials[mi])
            prims.append(prim)
            triangles += len(tris)
        out.doc['meshes'].append({'name': name, 'primitives': prims})
        out.doc['nodes'].append({'name': name, 'mesh': len(out.doc['meshes']) - 1, 'translation': list(to_gltf(pos))})
        out.doc['scenes'][0]['nodes'].append(len(out.doc['nodes']) - 1)
    for marker in SEATS:
        if marker in lower:
            _, pos = rw.frame_ltm(clump, lower.index(marker))
            out.doc['nodes'].append({'name': marker, 'translation': list(to_gltf(pos))})
            out.doc['scenes'][0]['nodes'].append(len(out.doc['nodes']) - 1)
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    tmp = out_path + '.partial'
    open(tmp, 'wb').write(out.glb())
    os.replace(tmp, out_path)
    return len(out.doc['nodes']), triangles, len(out.doc['images'])


def rgb(text):
    v = tuple(int(x) for x in text.split(','))
    if len(v) != 3 or not all(0 <= x <= 255 for x in v):
        raise SystemExit(f'colour {text!r}: R,G,B with 0..255 each')
    return v


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    src, dst = argv[0], argv[1]
    flags = [a for a in argv[2:] if a == '--all']
    rest = [a for a in argv[2:] if a != '--all']
    opts = dict(zip(rest[::2], rest[1::2]))
    nodes, tris, images = convert(src, dst, opts.get('--txd'), rgb(opts.get('--paint', '%d,%d,%d' % DEFAULT_PAINT)),
                                  rgb(opts.get('--paint2', '%d,%d,%d' % DEFAULT_PAINT2)),
                                  tuple(x for x in opts.get('--extras', '').split(',') if x), bool(flags))
    print(f'wrote {dst}: {nodes} parts, {tris} triangles, {images} textures')


if __name__ == '__main__':
    main(sys.argv[1:])

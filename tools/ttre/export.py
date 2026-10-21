"""Level geometry export: Wavefront OBJ + MTL, plus a stdlib-only software-rendered preview PNG.

Engine space is right-handed Z-up; OBJ gets (x, z, -y) so it is Y-up with the same handedness.
UVs: v=0 is GF row 0, which is the bottom row of the upright image (GF is stored bottom-up,
gf.decode flips it), so OBJ's bottom-left convention takes the engine UVs unchanged.
"""
import math
import os
import struct
import zlib

from . import level


def texture_index(tex_root):
    """Lower-case relative path without extension -> PNG path (Textures.cnt names are case-mixed)."""
    idx = {}
    for d, _, files in os.walk(tex_root):
        for f in files:
            if f.lower().endswith('.png'):
                p = os.path.join(d, f)
                idx[os.path.splitext(os.path.relpath(p, tex_root))[0].replace(os.sep, '/').lower()] = p
    return idx


def tex_png(idx, tex):
    if tex is None:
        return None
    return idx.get(os.path.splitext(tex.name.replace('\\', '/'))[0].lower())


def triangles(data, persos=True, t=None):
    """Yield (material, ((x, y, z), (u, v)) * 3, key, rgb * 3 or None) in engine world space, LOD 0
    only (rgb = baked radiosity as light factors, level.baked_light; None for persos); persos
    at frame 0, or t seconds into their animation (and scrolling UVs advanced to t).
    Mirrored transforms (negative determinant, e.g. perso object scale -1) get their winding
    flipped so front faces stay counter-clockwise."""
    for key, geo, m in data.meshes(persos, t):
        verts = [level.mat_apply(m, v) for v in geo.vertices]
        flip = level.mat_det(m) < 0
        cols = level.vertex_light(key)
        for el in geo.elements:
            su, sv = (-x * level.SCROLL_RATE * t for x in el.material.scroll) if t and el.material else (0, 0)
            for tri, uvt in zip(el.triangles, el.uv_tris or [(0, 0, 0)] * len(el.triangles)):
                if flip:
                    tri, uvt = tri[::-1], uvt[::-1]
                uv = [(el.uvs[i][0] + su, el.uvs[i][1] + sv) if el.uvs else (0.0, 0.0) for i in uvt]
                yield el.material, tuple(zip((verts[i] for i in tri), uv)), key, cols and tuple(cols[i] for i in tri)


def frame_texture(mat, t):
    """The material's texture at t seconds (animated textures loop through mat.frames)."""
    if not mat or not mat.frames or not t:
        return mat.texture if mat else None
    total = sum(d for _, d in mat.frames)
    x = (t * level.SCROLL_RATE) % total if total > 0 else 0
    for tex, d in mat.frames:
        if x < d:
            return tex
        x -= d
    return mat.frames[-1][0]


def write_obj(data, out_dir, tex_root='out/Textures'):
    os.makedirs(out_dir, exist_ok=True)
    name = data.lv.name
    idx = texture_index(tex_root)
    mats, missing = {}, set()
    obj = [f'# Tonic Trouble level {name}: father sector geometry (static IPOs)\n', f'mtllib {name}.mtl\n']
    mtl = []
    nv = nt = 0
    for so in data.walk():
        if not so.lods:
            continue
        geo = so.lods[0]
        obj.append(f'o ipo_{so.loc[1]:x}\n')
        for v in geo.vertices:
            x, y, z = level.mat_apply(so.world, v)
            obj.append(f'v {x:.5f} {z:.5f} {-y:.5f}\n')
        base = nv + 1
        nv += len(geo.vertices)
        for el in geo.elements:
            m = el.material
            if m not in mats:
                mats[m] = mn = f'mat{len(mats)}'
                png = tex_png(idx, m.texture)
                r, g, b, _ = m.diffuse
                mtl.append(f'newmtl {mn}\nKd {r:.4f} {g:.4f} {b:.4f}\n')
                if m.texture:
                    mtl.append(f'# {m.texture.name} flags={m.texture.flags:#x} flags_byte={m.texture.flags_byte:#x}\n')
                if png:
                    rel = os.path.relpath(png, out_dir).replace(os.sep, '/')
                    mtl.append(f'map_Kd {rel}\n')
                elif m.texture:
                    missing.add(m.texture.name)
            obj.append(f'usemtl {mats[m]}\n')
            tb = nt + 1
            for u, v in el.uvs:
                obj.append(f'vt {u:.5f} {v:.5f}\n')
            nt += len(el.uvs)
            for k, tri in enumerate(el.triangles):
                if el.uvs and el.uv_tris:
                    obj.append('f ' + ' '.join(f'{base + a}/{tb + t}' for a, t in zip(tri, el.uv_tris[k])) + '\n')
                else:
                    obj.append('f ' + ' '.join(str(base + a) for a in tri) + '\n')
    with open(os.path.join(out_dir, f'{name}.obj'), 'w') as f:
        f.writelines(obj)
    with open(os.path.join(out_dir, f'{name}.mtl'), 'w') as f:
        f.writelines(mtl)
    return len(mats), sorted(missing)


def read_png(path):
    """Minimal PNG reader (8-bit RGB/RGBA, non-interlaced) -> (w, h, rgba bytes)."""
    d = open(path, 'rb').read()
    o, idat = 8, b''
    while o < len(d):
        n, t = struct.unpack_from('>I4s', d, o)
        body = d[o + 8:o + 8 + n]
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack_from('>IIBB', body)
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


def render_preview(data, path, size=(960, 720), view='persp', tex_root='out/Textures', persos=True, focus=None, t=None, eye=None,
                   baked=True):
    """Textured z-buffered software render. view: 'top' (orthographic, looking down -Z) or
    'persp' (from above one corner of the bounding box toward its centre). Back faces are culled
    (front = counter-clockwise in engine space, which agrees with the stored vertex normals for
    ~95% of triangles), so inward-facing shells such as sky domes and room walls vanish when
    seen from outside. focus = (lo, hi) world box to frame instead of the whole level (close-ups;
    geometry outside the frustum is still drawn); eye = camera position (default: above a corner);
    t = animation time in seconds (see triangles). baked: static meshes use their radiosity
    (Gouraud, texture x light); persos and baked=False keep the placeholder key light."""
    from . import gf
    W, H = size
    tris = list(triangles(data, persos, t))
    eye0 = eye
    if not tris:
        raise ValueError('no geometry')
    if focus:
        lo, hi = [list(v) for v in focus]
    else:
        pts = [p for _, t, _, _ in tris for p, _ in t]
        lo = [min(p[i] for p in pts) for i in range(3)]
        hi = [max(p[i] for p in pts) for i in range(3)]
    ctr = [(a + b) / 2 for a, b in zip(lo, hi)]
    rad = max(1e-3, math.dist(lo, hi) / 2)
    if view == 'top':
        eye = None
        s = 0.95 * min(W / (hi[0] - lo[0] + 1e-6), H / (hi[1] - lo[1] + 1e-6))

        def project(p):
            return W / 2 + (p[0] - ctr[0]) * s, H / 2 - (p[1] - ctr[1]) * s, hi[2] - p[2] + 1
    else:
        eye = list(eye0) if eye0 else [ctr[0] - rad * 1.1, ctr[1] - rad * 1.1, ctr[2] + rad * 0.9]
        f = [c - e for c, e in zip(ctr, eye)]
        f = [x / math.hypot(*f) for x in f]
        cr = [f[1], -f[0], 0]
        cr = [x / math.hypot(*cr) for x in cr]
        cu = [cr[1] * f[2] - cr[2] * f[1], cr[2] * f[0] - cr[0] * f[2], cr[0] * f[1] - cr[1] * f[0]]
        foc = W / 2 / math.tan(math.radians(30))

        def project(p):
            d = [a - b for a, b in zip(p, eye)]
            z = sum(a * b for a, b in zip(d, f))
            if z < 1e-3:
                return None
            return W / 2 + foc * sum(a * b for a, b in zip(d, cr)) / z, H / 2 - foc * sum(a * b for a, b in zip(d, cu)) / z, z
    idx = texture_index(tex_root)
    texcache = {}
    img = bytearray(b'\x20\x20\x28\xff' * (W * H))
    zb = [math.inf] * (W * H)
    light = (0.4, -0.3, 0.85)
    for mat, tri, _, cols in tris:
        P = [project(p) for p, _ in tri]
        if None in P:
            continue
        (a, b, c) = [p for p, _ in tri]
        n = [(b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
             (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
             (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])]
        nl = math.hypot(*n) or 1
        if (n[2] <= 0) if eye is None else (sum(x * (p - e) for x, p, e in zip(n, a, eye)) >= 0):
            continue
        shade = 0.45 + 0.55 * abs(sum(x * y for x, y in zip(n, light)) / nl)
        if not baked:
            cols = None
        png = tex_png(idx, frame_texture(mat, t))
        if png not in texcache:
            texcache[png] = read_png(png) if png else None
        tex = texcache[png]
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = P
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-9:
            continue
        uv = [t for _, t in tri]
        iz = [1 / z0, 1 / z1, 1 / z2]
        for py in range(max(0, int(min(y0, y1, y2))), min(H, int(max(y0, y1, y2)) + 1)):
            for px in range(max(0, int(min(x0, x1, x2))), min(W, int(max(x0, x1, x2)) + 1)):
                qx, qy = px + 0.5, py + 0.5
                w0 = ((x1 - qx) * (y2 - qy) - (x2 - qx) * (y1 - qy)) / area
                w1 = ((x2 - qx) * (y0 - qy) - (x0 - qx) * (y2 - qy)) / area
                w2 = 1 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                q = w0 * iz[0] + w1 * iz[1] + w2 * iz[2]
                z = 1 / q
                i = py * W + px
                if z >= zb[i]:
                    continue
                if tex:
                    tw, th, tp = tex
                    u = (w0 * uv[0][0] * iz[0] + w1 * uv[1][0] * iz[1] + w2 * uv[2][0] * iz[2]) * z
                    v = (w0 * uv[0][1] * iz[0] + w1 * uv[1][1] * iz[1] + w2 * uv[2][1] * iz[2]) * z
                    k = 4 * ((th - 1 - int(v * th) % th) * tw + int(u * tw) % tw)  # v=0: bottom row
                    if tp[k + 3] < 128:
                        continue
                    col = tp[k:k + 3]
                else:
                    col = bytes(int(255 * x) for x in (mat.diffuse[:3] if mat else (0.8, 0.8, 0.8)))
                zb[i] = z
                if cols:
                    sh = [max(0.0, (w0 * cols[0][j] * iz[0] + w1 * cols[1][j] * iz[1] + w2 * cols[2][j] * iz[2]) * z) for j in range(3)]
                else:
                    sh = (shade,) * 3
                img[4 * i:4 * i + 4] = bytes((min(255, int(col[0] * sh[0])), min(255, int(col[1] * sh[1])),
                                              min(255, int(col[2] * sh[2])), 255))
    with open(path, 'wb') as f:
        f.write(gf.to_png(W, H, bytes(img)))
    return len(tris), lo, hi


class _CollisionView:
    """Adapter so render_preview draws LevelData.collision() (untextured, key-lit grey)."""
    def __init__(self, tris):
        verts = [p for t in tris for p in t]
        self.geo = level.Geometric(verts, [], [level.Element(None, [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(len(tris))], [], [])])

    def meshes(self, persos=True, t=None):
        yield None, self.geo, level.IDENTITY


def render_collision_compare(data, path, size=(640, 480), view='top', **kw):
    """Side-by-side preview: visual static geometry (left) and collision triangles (right), same
    framing (the collision's bounds), to check that the collide sets coincide with the visuals."""
    from . import gf
    tris = data.collision()
    pts = [p for t in tris for p in t]
    focus = ([min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)])
    tmp = path + '.part%d.png'
    render_preview(data, tmp % 0, size=size, view=view, persos=False, focus=focus, **kw)
    render_preview(_CollisionView(tris), tmp % 1, size=size, view=view, persos=False, focus=focus, **kw)
    (w, h, a), (_, _, b) = read_png(tmp % 0), read_png(tmp % 1)
    rows = b''.join(a[4 * w * y:4 * w * (y + 1)] + b[4 * w * y:4 * w * (y + 1)] for y in range(h))
    with open(path, 'wb') as f:
        f.write(gf.to_png(2 * w, h, rows))
    for i in (0, 1):
        os.replace(tmp % i, path[:-4] + ('_visual.png', '_collision.png')[i])
    return len(tris)

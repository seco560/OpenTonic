"""Concept Art 3 of Tonic Adventure 2000 (a hall with a grandfather clock and a curved staircase) as a glTF scene and a
level for the viewer.

Usage: python3 -m tools.concept_foyer [ART] [--name ConceptFoyer] [--no-import]

ART defaults to files/sources/tonic2_art/784px-Tonic_Adventure_2000_Concept_Art_3.jpg (bring your own copy; it is not
in the repo). Writes files/out/gltf/NAME/NAME.gltf (+ .bin and PNG textures), then imports it with
tools.ttre.import_gltf (Ed starts where the painting's camera stands, looking the same way). Needs numpy; the JPEG is
decoded with ffmpeg or, on macOS, sips.

How the painting became a room:
- Camera: 32 corners of the floor's checkerboard, picked in the image and fitted with a pinhole camera (least squares,
  square tiles of side 1): f = 193 px, principal point (444, 299), eye 3.55 tiles above the floor, pitched up 10
  degrees, 133 degrees across: a very wide lens, hence the huge near tiles. The far medallion fits it less well (the
  painting draws it 0.7 times as deep as wide); it is rebuilt as a regular octagon.
- World: one floor tile is the unit (i right, j away from the camera, z up), S game units in the level. Things
  standing on the floor get their place from where they touch it and their heights from where their tops are in the
  image; the rest (the gallery, the stair's turn, the ceiling) is read off the image the same way, then tidied up.
- Textures, all from the painting: faces it shows get UVs by projection through the camera (from there the scene
  looks like the painting), checked against a depth buffer of the scene so nothing takes the colours of what stands
  in front of it. Faces it does not show take the colours of their visible opposite (the clock's back = its front),
  or a mirrored swatch cut from a surface it does show. Walls and ceiling are baked: the painting where it shows them,
  their swatch elsewhere, blended at the seams. The floor is redrawn from a 2 x 2 tile block near the camera, its
  medallion folded from the nearest of its eight sectors, and shaded like the painting (vertex colours).
- Light: the painting's own, so every face is unlit (KHR_materials_unlit + COLOR_0 for the floor's shading).
"""
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import zlib

import numpy as np

ART = 'files/sources/tonic2_art/784px-Tonic_Adventure_2000_Concept_Art_3.jpg'
OUT = 'files/out'
S = 0.6  # game units per floor tile (Ed is 2.17 tall, about 3.6 tiles)

# ---- the painting's camera (see the docstring) ----

F, CX, CY = 193.101, 443.897, 299.149
ROT = np.array([[0.969184, 0.245807, -0.016142],     # world -> camera rows: image right, image down, forward
                [-0.058148, 0.164611, -0.984643],
                [-0.239375, 0.955239, 0.173832]])
EYE = np.array([0.2098, -3.6530, 3.5472])


def project(P):
    """World points (..., 3) -> image (x, y) in pixels (pixel centres at integers) and depth along the view axis."""
    pc = (np.asarray(P, float) - EYE) @ ROT.T
    z = pc[..., 2]
    zs = np.where(np.abs(z) < 1e-6, 1e-6, z)
    return np.stack([CX + F * pc[..., 0] / zs, CY + F * pc[..., 1] / zs], -1), z


# ---- room layout (tiles) ----

XL, XR = -9.5, 5.6           # left and right walls
YF, YB = -12.0, 15.0         # front wall (behind the camera) and back wall
ZC = 30.0                    # ceiling
GY, GZ, GU = 6.4, 8.0, 7.4   # gallery: front edge, floor, underside
RAIL = 3.0                   # handrail height above the gallery floor and the stair's nosings
PIVOT = np.array([0.6, GY - 1.0])  # the stair turns left around it, from heading +j to heading -i
R_IN, R_OUT = 1.0, 4.0       # stair's inner and outer edge (the straight flight: i = 1.6 .. 4.6)
SJ0 = 1.2                    # first riser
N_STRAIGHT, N_TURN = 9, 6    # steep, as painted: its middle steps are at the horizon, so it is near
RISE = GZ / (N_STRAIGHT + N_TURN)
NEWEL = np.array([1.35, 1.45])
MEDALLION = (np.array([-3.7, 7.0]), 4.0)  # centre, apothem (regular octagon, flat sides facing the axes)
LANTERN = np.array([-0.8, 4.3, 6.8])      # bottom of the lantern's body


# Outlines (image pixels) of the painting's objects, so that no wall or floor takes their colours where the rebuilt
# objects do not cover them exactly: the clock, the curtain, the lantern and its chain, the newel, the stair.
OBJECTS = [[(122, 100), (358, 100), (362, 230), (348, 330), (345, 480), (232, 518), (126, 505)],
           [(292, 112), (398, 112), (398, 145), (350, 425), (306, 432), (296, 300)],
           [(418, 125), (512, 125), (512, 292), (418, 292)], [(450, 0), (472, 0), (472, 130), (450, 130)],
           [(500, 268), (572, 268), (578, 512), (528, 512)],
           [(585, 148), (655, 148), (705, 180), (745, 240), (772, 330), (784, 420), (784, 600), (560, 600),
            (560, 250)]]


def polygon_mask(polys, w, h):
    """Pixels (h, w) inside any of the polygons (even-odd rule per polygon)."""
    y, x = np.mgrid[0:h, 0:w]
    mask = np.zeros((h, w), bool)
    for poly in polys:
        inside = np.zeros((h, w), bool)
        for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]):
            if y0 == y1:
                continue
            crosses = (y >= min(y0, y1)) & (y < max(y0, y1))
            xc = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
            inside ^= crosses & (x < xc)
        mask |= inside
    return mask


# ---- images ----

def read_image(path):
    """RGB uint8 array of an image file (ffmpeg -> PPM, else sips -> BMP)."""
    try:
        r = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'image2pipe', '-vcodec', 'ppm', '-'],
                           capture_output=True)
        if r.returncode == 0 and r.stdout[:2] == b'P6':
            head = r.stdout.split(maxsplit=4)
            w, h = int(head[1]), int(head[2])
            return np.frombuffer(head[4][:w * h * 3], np.uint8).reshape(h, w, 3)
    except FileNotFoundError:
        pass
    with tempfile.TemporaryDirectory() as tmp:
        bmp = os.path.join(tmp, 'art.bmp')
        subprocess.run(['sips', '-s', 'format', 'bmp', path, '--out', bmp], capture_output=True, check=True)
        d = open(bmp, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h, _, bpp = struct.unpack_from('<iiHH', d, 18)
    stride = (w * bpp // 8 + 3) & ~3
    rows = np.frombuffer(d[off:off + stride * abs(h)], np.uint8).reshape(abs(h), stride)[:, :w * bpp // 8]
    px = rows.reshape(abs(h), w, bpp // 8)[..., 2::-1]  # BGR(A) -> RGB
    return np.ascontiguousarray(px[::-1] if h > 0 else px)


def write_png(path, a):
    a = np.ascontiguousarray(np.clip(np.rint(a), 0, 255).astype(np.uint8))
    h, w, c = a.shape
    raw = np.concatenate([np.zeros((h, 1), np.uint8), a.reshape(h, w * c)], 1).tobytes()

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, {3: 2, 4: 6}[c], 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def bilinear(img, x, y):
    """img (h, w, c) sampled at pixel coordinates x, y (any shape; clamped to the edges)."""
    h, w = img.shape[:2]
    x = np.clip(x, 0, w - 1.001)
    y = np.clip(y, 0, h - 1.001)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = (x - x0)[..., None], (y - y0)[..., None]
    return (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x0 + 1] * fx * (1 - fy)
            + img[y0 + 1, x0] * (1 - fx) * fy + img[y0 + 1, x0 + 1] * fx * fy)


def box_blur(a, r):
    """Mean over a (2r+1)^2 window (edges clamped), twice (close to a Gaussian)."""
    for _ in range(2):
        for axis in (0, 1):
            p = np.pad(a, [(r + 1, r) if k == axis else (0, 0) for k in range(a.ndim)], mode='edge')
            c = np.cumsum(p, axis)
            a = (np.take(c, range(2 * r + 1, c.shape[axis]), axis) - np.take(c, range(0, c.shape[axis] - 2 * r - 1),
                                                                               axis)) / (2 * r + 1)
    return a


def resize(img, w, h):
    sh, sw = img.shape[:2]
    x = (np.arange(w) + 0.5) * sw / w - 0.5
    y = (np.arange(h) + 0.5) * sh / h - 0.5
    return bilinear(img, x[None, :].repeat(h, 0), y[:, None].repeat(w, 1))


# ---- geometry helpers ----

def unit(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


def normal_of(pts):
    p = np.asarray(pts, float)
    n = np.cross(p[1] - p[0], p[2] - p[0])
    if len(p) == 4:
        n = n + np.cross(p[2] - p[0], p[3] - p[0])
    return unit(n)


def wall_axes(n):
    """Texture axes of a face with normal n, as seen from its front: (right, down)."""
    if abs(n[2]) > 0.7:
        return np.array([1.0, 0, 0]), np.array([0, -math.copysign(1.0, n[2]), 0])
    right = unit(np.cross([0, 0, 1.0], n))
    return right, np.array([0, 0, -1.0])


class Swatch:
    """A texture cut from the painting: the rectangle O + a U + b V (0 <= a < w, 0 <= b < h tiles; U right, V down as
    seen from the front) of a surface it shows, mirrored into a 2 x 2 tile so it repeats without seams. A solid colour
    when given a colour instead."""

    def __init__(self, scene, name, O=None, U=None, V=None, w=1.0, h=1.0, ppt=48, color=None, gain=1.0):
        self.name, self.w, self.h = name, w, h
        scene.swatches[name] = self
        if color is not None:
            self.O, self.U, self.V = np.zeros(3), np.array([1.0, 0, 0]), np.array([0, 0, -1.0])
            self.patch = np.tile(np.array(color, np.float32), (4, 4, 1))
        else:
            self.O, self.U, self.V = np.asarray(O, float), unit(U), unit(V)
            nx, ny = max(2, int(w * ppt)), max(2, int(h * ppt))
            a = (np.arange(nx) + 0.5) / nx * w
            b = (np.arange(ny) + 0.5) / ny * h
            P = self.O + a[None, :, None] * self.U + b[:, None, None] * self.V
            xy, _ = project(P)
            self.patch = bilinear(scene.art, xy[..., 0], xy[..., 1]) * gain
        top = np.concatenate([self.patch, self.patch[:, ::-1]], 1)
        scene.textures[name] = np.concatenate([top, top[::-1]], 0)

    def sample(self, P, U=None, V=None):
        """Colours at world points P (..., 3) on a plane through the patch with the same axes (or U, V)."""
        U = self.U if U is None else U
        V = self.V if V is None else V
        a = np.mod((P - self.O) @ U, 2 * self.w)
        b = np.mod((P - self.O) @ V, 2 * self.h)
        a = np.where(a < self.w, a, 2 * self.w - a)
        b = np.where(b < self.h, b, 2 * self.h - b)
        ph, pw = self.patch.shape[:2]
        return bilinear(self.patch, a / self.w * pw - 0.5, b / self.h * ph - 0.5)


class Scene:
    def __init__(self, art):
        self.art = art.astype(np.float32)
        self.H, self.W = art.shape[:2]
        self.faces = []        # (points, look, shade)
        self.textures = {}     # name -> image (h, w, 3|4)
        self.swatches = {}     # name -> Swatch
        self.alpha = set()     # textures drawn with alpha test
        self.zbuf = None
        self.objects = polygon_mask(OBJECTS, self.W, self.H)

    def add(self, pts, look, shade=1.0):
        """A planar convex polygon (3 or 4 points, counter-clockwise seen from its front) and how it is textured:
        ('proj', fallback, via): the painting projected where it shows the face (or, with via, the point via(P),
        e.g. the opposite face), else fallback; ('swatch', Swatch); ('planar', name, O, U, V, w, h);
        ('bake', name): a texture of its own, made in bake()."""
        self.faces.append((np.asarray(pts, float), look, shade))

    def quad(self, a, b, c, d, look, shade=1.0):
        self.add([a, b, c, d], look, shade)

    def hexahedron(self, c, looks, shade=1.0):
        """8 corners: bottom 0-3 then top 4-7, each counter-clockwise seen from above. looks: dict of
        'bottom' 'top' 'sides' (or side index 0-3: the side from corner k to k+1) -> look."""
        b, t = c[:4], c[4:]
        if 'bottom' in looks:
            self.quad(b[0], b[3], b[2], b[1], looks['bottom'], shade)
        if 'top' in looks:
            self.quad(t[0], t[1], t[2], t[3], looks['top'], shade)
        for k in range(4):
            look = looks.get(k, looks.get('sides'))
            if look is not None:
                self.quad(b[k], b[(k + 1) % 4], t[(k + 1) % 4], t[k], look, shade)

    def box(self, i0, i1, j0, j1, z0, z1, looks, grow_top=0.0, shade=1.0):
        bot = [(i0, j0, z0), (i1, j0, z0), (i1, j1, z0), (i0, j1, z0)]
        g = grow_top
        top = [(i0 - g, j0 - g, z1), (i1 + g, j0 - g, z1), (i1 + g, j1 + g, z1), (i0 - g, j1 + g, z1)]
        self.hexahedron(np.array(bot + top, float), looks, shade)

    def prism(self, centre, profile, sides, looks, phase=0.0):
        """A solid of revolution with `sides` flat faces: profile = [(z, radius), ...] bottom to top."""
        ang = phase + np.arange(sides) * 2 * math.pi / sides
        rings = [np.stack([centre[0] + r * np.cos(ang), centre[1] + r * np.sin(ang), np.full(sides, z)], 1)
                 for z, r in profile]
        for lo, hi in zip(rings, rings[1:]):
            for k in range(sides):
                k1 = (k + 1) % sides
                self.quad(lo[k], lo[k1], hi[k1], hi[k], looks['sides'])
        if 'top' in looks:
            top = rings[-1]
            c = np.array([centre[0], centre[1], profile[-1][0]])
            for k in range(sides):
                self.add([c, top[k], top[(k + 1) % sides]], looks['top'])
        if 'bottom' in looks:
            bot = rings[0]
            c = np.array([centre[0], centre[1], profile[0][0]])
            for k in range(sides):
                self.add([c, bot[(k + 1) % sides], bot[k]], looks['bottom'])

    # ---- tessellation, depth, visibility ----

    def pieces(self, pts, fine):
        """Sub-polygons of a face: small enough for the projection's affine error (fine) or just for depth."""
        d = max(0.05, float(np.min(np.linalg.norm(np.vstack([pts, pts.mean(0)]) - EYE, axis=1))))
        L = min(3.0, max(0.12, (0.07 if fine else 0.25) * d))
        if len(pts) == 3:
            n = max(1, int(math.ceil(max(np.linalg.norm(pts[k] - pts[k - 1]) for k in range(3)) / L)))
            out = []
            for a in range(n):
                for b in range(n - a):
                    p = lambda u, v: pts[0] + (pts[1] - pts[0]) * u / n + (pts[2] - pts[0]) * v / n
                    out.append([p(a, b), p(a + 1, b), p(a, b + 1)])
                    if a + b < n - 1:
                        out.append([p(a + 1, b), p(a + 1, b + 1), p(a, b + 1)])
            return out
        nu = max(1, int(math.ceil(max(np.linalg.norm(pts[1] - pts[0]), np.linalg.norm(pts[2] - pts[3])) / L)))
        nv = max(1, int(math.ceil(max(np.linalg.norm(pts[3] - pts[0]), np.linalg.norm(pts[2] - pts[1])) / L)))
        u = np.linspace(0, 1, nu + 1)[None, :, None]
        v = np.linspace(0, 1, nv + 1)[:, None, None]
        g = (pts[0] * (1 - u) * (1 - v) + pts[1] * u * (1 - v) + pts[2] * u * v + pts[3] * (1 - u) * v)
        return [[g[b, a], g[b, a + 1], g[b + 1, a + 1], g[b + 1, a]] for b in range(nv) for a in range(nu)]

    def depth_buffer(self):
        zb = np.full((self.H, self.W), np.inf)
        for pts, _, _ in self.faces:
            for sub in self.pieces(pts, False):
                for k in range(1, len(sub) - 1):
                    self.raster(zb, np.array([sub[0], sub[k], sub[k + 1]]))
        self.zbuf = zb

    def raster(self, zb, tri):
        xy, z = project(tri)
        if np.any(z < 0.05):
            return
        x0, y0 = np.floor(xy.min(0)).astype(int)
        x1, y1 = np.ceil(xy.max(0)).astype(int)
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, self.W - 1), min(y1, self.H - 1)
        if x0 > x1 or y0 > y1:
            return
        X, Y = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        (ax, ay), (bx, by), (cx, cy) = xy
        det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(det) < 1e-9:
            return
        l0 = ((by - cy) * (X - cx) + (cx - bx) * (Y - cy)) / det
        l1 = ((cy - ay) * (X - cx) + (ax - cx) * (Y - cy)) / det
        l2 = 1 - l0 - l1
        inside = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        depth = 1 / (l0 / z[0] + l1 / z[1] + l2 / z[2])
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        upd = inside & (depth < sub)
        sub[upd] = depth[upd]

    def visible(self, P, n, slack=0.0, background=False):
        """Which world points (..., 3) on a face with normal n the painting shows (background: walls and floor,
        which the painting's objects hide wherever they are drawn)."""
        xy, z = project(P)
        x, y = xy[..., 0], xy[..., 1]
        ok = (z > 0.05) & (x >= 0) & (x <= self.W - 1) & (y >= 0) & (y <= self.H - 1)
        xi = np.clip(np.rint(x), 0, self.W - 1).astype(int)
        yi = np.clip(np.rint(y), 0, self.H - 1).astype(int)
        ok &= z <= self.zbuf[yi, xi] * 1.015 + 0.03 + slack
        if background:
            ok &= ~self.objects[yi, xi]
        view = EYE - P
        ok &= (view @ n) / np.linalg.norm(view, axis=-1) > 0.12
        return ok

    def art_uv(self, P):
        xy, _ = project(P)
        return np.stack([(xy[..., 0] + 0.5) / self.W, (xy[..., 1] + 0.5) / self.H], -1)

    # ---- output ----

    def emit(self):
        """Triangles per texture: {name: (positions, uvs, colours, indices)}."""
        meshes = {}

        def put(name, P, uv, col):
            m = meshes.setdefault(name, ([], [], [], []))
            base = len(m[0])
            m[0].extend(P)
            m[1].extend(uv)
            m[2].extend(col)
            if len(P) == 3:
                m[3].extend([base, base + 1, base + 2])
            else:
                m[3].extend([base, base + 1, base + 2, base, base + 2, base + 3])

        def colours(P, shade):
            if callable(shade):
                return [shade(p) for p in P]
            return [(shade,) * 3] * len(P)

        def flat(sub, look, n, shade):
            kind = look[0]
            if kind == 'swatch':
                sw = look[1]
                U, V = wall_axes(n)
                uv = [((p - sw.O) @ U / (2 * sw.w), (p - sw.O) @ V / (2 * sw.h)) for p in sub]
                put(sw.name, sub, uv, colours(sub, shade))
            elif kind == 'planar':
                _, name, O, U, V, w, h = look
                put(name, sub, [((p - O) @ U / w, (p - O) @ V / h) for p in sub], colours(sub, shade))
            else:
                raise ValueError(kind)

        for pts, look, shade in self.faces:
            n = normal_of(pts)
            if look[0] == 'bake':
                _, name, O, U, V, w, h = look
                for sub in self.pieces(pts, False):
                    put(name, sub, [((p - O) @ U / w, (p - O) @ V / h) for p in sub], colours(sub, shade))
                continue
            if look[0] != 'proj':
                for sub in self.pieces(pts, False):
                    flat(np.array(sub), look, n, shade)
                continue
            _, fallback, via = look
            for sub in self.pieces(pts, True):
                sub = np.array(sub)
                src = np.array([via(p) for p in sub]) if via else sub
                probe = np.vstack([src, src.mean(0)])
                n_src = normal_of(src) if via else n
                if self.visible(probe, n_src).all():
                    put('art', sub, self.art_uv(src), colours(sub, shade))
                elif fallback[0] == 'proj':  # a second try (e.g. via the opposite face)
                    _, fb2, via2 = fallback
                    src2 = np.array([via2(p) for p in sub]) if via2 else sub
                    if self.visible(np.vstack([src2, src2.mean(0)]), normal_of(src2)).all():
                        put('art', sub, self.art_uv(src2), colours(sub, shade))
                    else:
                        flat(sub, fb2, n, shade)
                else:
                    flat(sub, fallback, n, shade)
        return meshes

    def bake(self, name, O, U, V, w, h, ppt, fill, n, gain=1.0):
        """Texture of the rectangle O + a U + b V (a < w, b < h): the painting where it shows it, fill elsewhere."""
        nx = 1 << max(4, int(math.ceil(math.log2(w * ppt))))
        ny = 1 << max(4, int(math.ceil(math.log2(h * ppt))))
        nx, ny = min(nx, 2048), min(ny, 2048)
        a = (np.arange(nx) + 0.5) / nx * w
        b = (np.arange(ny) + 0.5) / ny * h
        P = O + a[None, :, None] * U + b[:, None, None] * V
        vis = self.visible(P, n, background=True).astype(np.float32)
        xy, _ = project(P)
        art = bilinear(self.art, xy[..., 0], xy[..., 1]) * gain
        r = max(2, nx // 128)
        wgt = vis * np.clip((box_blur(vis, r) - 0.5) * 2.2, 0, 1)
        self.textures[name] = art * wgt[..., None] + fill(P) * (1 - wgt[..., None])
        return ('bake', name, O, U, V, w, h)


# ---- the hall ----

def build(sc):
    art = sc.art
    proj = lambda fallback, via=None: ('proj', fallback, via)

    # swatches cut from surfaces the painting shows (rectangles found by eye on the rectified surfaces)
    wallpaper = Swatch(sc, 'wallpaper', O=(XL, -2.6, 11.75), U=(0, 1, 0), V=(0, 0, -1), w=1.0, h=4.0, ppt=48)
    wainscot = Swatch(sc, 'wainscot', O=(XL, -1.4, 2.9), U=(0, 1, 0), V=(0, 0, -1), w=0.8, h=2.7, ppt=48)
    damask = Swatch(sc, 'damask', O=(3.0, YB, 19.0), U=(1, 0, 0), V=(0, 0, -1), w=2.5, h=7.5, ppt=24)
    salmon = Swatch(sc, 'salmon', O=(-6.0, YB, 7.3), U=(1, 0, 0), V=(0, 0, -1), w=4.0, h=3.2, ppt=32)
    dado = Swatch(sc, 'dado', O=(-6.0, YB, 2.3), U=(1, 0, 0), V=(0, 0, -1), w=4.0, h=2.3, ppt=32)
    ceiling = Swatch(sc, 'ceiling', O=(-9.0, 15.0, ZC), U=(1, 0, 0), V=(0, -1, 0), w=5.6, h=7.8, ppt=16)
    redwall = Swatch(sc, 'redwall', O=(XR, 7.5, 21.0), U=(0, 1, 0), V=(0, 0, -1), w=5.8, h=7.8, ppt=16)
    wood = Swatch(sc, 'wood', color=(52, 34, 24))
    darkwood = Swatch(sc, 'darkwood', color=(34, 22, 18))
    gold = Swatch(sc, 'gold', color=(176, 150, 70))
    cream = Swatch(sc, 'cream', color=(214, 200, 150))
    Swatch(sc, 'spindle', color=(92, 40, 30))
    runner = Swatch(sc, 'runner', color=(98, 30, 30))
    velvet = Swatch(sc, 'velvet', color=(120, 22, 28))
    chain = Swatch(sc, 'chain', color=(80, 80, 76))
    jade = Swatch(sc, 'jade', color=(120, 170, 140))

    def wall_fill(upper, lower, z_split):
        def f(P):
            return np.where((P[..., 2] < z_split)[..., None], lower.sample(P), upper.sample(P))
        return f

    walls = []  # baked once every face is in the depth buffer
    shading = {}  # the floor's, made after the depth pass
    floor_shade = lambda p: shading['fn'](p)

    # floor: the checkerboard block, the medallion and the painting's shading
    i0, j0 = -2.0, 0.0  # the 2 x 2 block, nearest the camera: 50 px a tile in the painting
    a = (np.arange(128) + 0.5) / 64
    P = np.stack(np.meshgrid(i0 + a, j0 + 2 - a), -1)
    P = np.concatenate([P, np.zeros(P.shape[:2] + (1,))], -1)
    xy, _ = project(P)
    block = bilinear(art, xy[..., 0], xy[..., 1])
    lum = block.mean(-1, keepdims=True)
    flat_block = block / np.maximum(box_blur(lum, 20), 1) * lum.mean()  # even out the painting's light across it
    sc.textures['tiles'] = flat_block
    tile_look = ('planar', 'tiles', np.array([i0, j0 + 2, 0]), np.array([1.0, 0, 0]), np.array([0, -1.0, 0]), 2, 2)
    tile_lum = float(flat_block.mean())

    (mc, ma) = MEDALLION
    medallion_texture(sc, ma)
    R = ma / math.cos(math.pi / 8)  # circumradius
    mlook = ('planar', 'medallion', np.array([mc[0] - ma, mc[1] + ma, 0]), np.array([1.0, 0, 0]),
             np.array([0, -1.0, 0]), 2 * ma, 2 * ma)
    sc.alpha.add('medallion')

    floor_pts = [(XL, YF, 0), (XR, YF, 0), (XR, YB, 0), (XL, YB, 0)]
    sc.add(floor_pts, tile_look, shade=floor_shade)
    sc.add([(mc[0] - ma, mc[1] - ma, 0.012), (mc[0] + ma, mc[1] - ma, 0.012), (mc[0] + ma, mc[1] + ma, 0.012),
            (mc[0] - ma, mc[1] + ma, 0.012)], mlook, shade=floor_shade)

    # shell (baked after the depth pass)
    walls.append(('left', (XL, YF, ZC), (0, 1, 0), (0, 0, -1), YB - YF, ZC, wall_fill(wallpaper, wainscot, 3.0)))
    walls.append(('right', (XR, YB, ZC), (0, -1, 0), (0, 0, -1), YB - YF, ZC, wall_fill(redwall, wainscot, 3.0)))
    walls.append(('front', (XR, YF, ZC), (-1, 0, 0), (0, 0, -1), XR - XL, ZC, wall_fill(wallpaper, wainscot, 3.0)))
    walls.append(('back_low', (XL, YB, GU), (1, 0, 0), (0, 0, -1), XR - XL, GU, wall_fill(salmon, dado, 2.3)))
    walls.append(('back_high', (XL, YB, ZC), (1, 0, 0), (0, 0, -1), XR - XL, ZC - GZ, lambda P: damask.sample(P)))
    walls.append(('ceiling', (XL, YF, ZC), (1, 0, 0), (0, 1, 0), XR - XL, YB - YF, lambda P: ceiling.sample(P)))

    # gallery: floor (the checkerboard), underside, fascia, balustrade
    for (a0, a1, b0, b1) in ((XL, PIVOT[0], GY, YB), (PIVOT[0], XR, PIVOT[1] + R_OUT, YB)):
        sc.quad((a0, b0, GZ), (a1, b0, GZ), (a1, b1, GZ), (a0, b1, GZ), tile_look, shade=0.8)
        sc.quad((a0, b1, GU), (a1, b1, GU), (a1, b0, GU), (a0, b0, GU), proj(('swatch', wood)))
        sc.quad((a0, b0, GU), (a1, b0, GU), (a1, b0, GZ), (a0, b0, GZ), proj(('swatch', gold)))
    sc.quad((PIVOT[0], GY, GU), (PIVOT[0], PIVOT[1] + R_OUT, GU), (PIVOT[0], PIVOT[1] + R_OUT, GZ),
            (PIVOT[0], GY, GZ), proj(('swatch', wood)))
    balustrade(sc, [(XL, GY), (PIVOT[0], GY)], lambda t: GZ)
    balustrade(sc, [(PIVOT[0], PIVOT[1] + R_OUT), (XR, PIVOT[1] + R_OUT)], lambda t: GZ)

    staircase(sc, proj, ('swatch', runner), ('swatch', darkwood), ('swatch', wood))
    clock(sc, proj, ('swatch', darkwood))
    newel(sc, proj, ('swatch', darkwood))
    curtain(sc, proj, ('swatch', velvet))
    lantern(sc, proj, ('swatch', cream), ('swatch', gold), ('swatch', chain), ('swatch', jade))
    lamp(sc, proj, ('swatch', gold), ('swatch', jade))

    # walls go in the depth buffer as plain quads first, then get their textures
    placeholders = []
    for name, O, U, V, w, h, fill in walls:
        O, U, V = np.asarray(O, float), unit(U), unit(V)
        placeholders.append(len(sc.faces))
        sc.quad(O + h * V, O + h * V + w * U, O + w * U, O, ('planar', name, O, U, V, w, h))
    sc.depth_buffer()
    for k, (name, O, U, V, w, h, fill) in zip(placeholders, walls):
        O, U, V = np.asarray(O, float), unit(U), unit(V)
        n = unit(np.cross(V, U))
        look = sc.bake(name, O, U, V, w, h, 16 if name in ('ceiling', 'front') else 24, fill, n)
        sc.faces[k] = (sc.faces[k][0], look, sc.faces[k][2])

    # floor shading: the painting's brightness over 2 x 2 tiles (half red, half green wherever it is) against the
    # redrawn tiles'; outside the painting a little darker than its mean
    g = np.array([(dx, dy, 0.0) for dx in np.linspace(-1, 1, 7) for dy in np.linspace(-1, 1, 7)])
    up = np.array([0, 0, 1.0])
    cache = {}

    def shade_at(p):
        key = (round(p[0], 3), round(p[1], 3))
        if key not in cache:
            q = np.array([p[0], p[1], 0.0]) + g
            vis = sc.visible(q, up, slack=0.05, background=True)
            if vis.mean() < 0.6:
                cache[key] = None
            else:
                xy, _ = project(q[vis])
                cache[key] = float(bilinear(art, xy[:, 0], xy[:, 1]).mean()) / tile_lum
        v = cache[key]
        return (v if v is not None else 0.85,) * 3
    shading['fn'] = shade_at


def balustrade(sc, path, height, spacing=0.45, post='cream'):
    """Handrail, base rail and balusters along a polyline of (i, j); height(t) = the floor under it at t (0..1)."""
    path = np.asarray(path, float)
    seg = np.linalg.norm(np.diff(path, axis=0), axis=1)
    total = seg.sum()

    def at(t):
        d = t * total
        for k, L in enumerate(seg):
            if d <= L or k == len(seg) - 1:
                return path[k] + (path[k + 1] - path[k]) * min(1.0, d / L)
            d -= L
    rail_fb = ('swatch', sc.swatches['darkwood'])  # too thin to take the painting: flat colours
    post_fb = ('swatch', sc.swatches[post])
    n = max(1, int(total / spacing))
    for k in range(n + 1):
        t = k / n
        p = at(t)
        z = height(t)
        sc.prism(p, [(z, 0.09), (z + 0.5, 0.11), (z + 1.0, 0.07), (z + 1.6, 0.16), (z + 2.2, 0.07),
                     (z + RAIL - 0.15, 0.09)], 4, {'sides': post_fb}, phase=math.pi / 4)
    for k in range(len(path) - 1):
        a, b = path[k], path[k + 1]
        side = unit(np.array([-(b - a)[1], (b - a)[0]])) * 0.14
        t0, t1 = sum(seg[:k]) / total, sum(seg[:k + 1]) / total
        for z_off, th in ((RAIL - 0.2, 0.3), (0.0, 0.18)):
            za, zb = height(t0) + z_off, height(t1) + z_off
            c = np.array([(*(a - side), za), (*(b - side), zb), (*(b + side), zb), (*(a + side), za),
                          (*(a - side), za + th), (*(b - side), zb + th), (*(b + side), zb + th),
                          (*(a + side), za + th)])
            sc.hexahedron(c, {'sides': rail_fb, 'top': rail_fb, 'bottom': rail_fb})


def staircase(sc, proj, tread_fb, riser_fb, side_fb):
    g = (PIVOT[1] - SJ0) / N_STRAIGHT
    i_in, i_out = PIVOT[0] + R_IN, PIVOT[0] + R_OUT
    tread, riser, side = proj(tread_fb), proj(riser_fb), proj(side_fb)
    for k in range(N_STRAIGHT):
        ja, jb = SJ0 + k * g, SJ0 + (k + 1) * g
        z0, z1 = k * RISE, (k + 1) * RISE
        sc.quad((i_in, ja, z1), (i_out, ja, z1), (i_out, jb, z1), (i_in, jb, z1), tread)
        sc.quad((i_in, ja, z0), (i_out, ja, z0), (i_out, ja, z1), (i_in, ja, z1), riser)
        sc.quad((i_in, jb, 0), (i_in, ja, 0), (i_in, ja, z1), (i_in, jb, z1), side)
        sc.quad((i_out, ja, 0), (i_out, jb, 0), (i_out, jb, z1), (i_out, ja, z1), side)
    step = (math.pi / 2) / N_TURN
    for k in range(N_TURN):
        z0, z1 = (N_STRAIGHT + k) * RISE, (N_STRAIGHT + k + 1) * RISE
        segs = 3
        for s in range(segs):
            a0 = k * step + s * step / segs
            a1 = a0 + step / segs
            d0, d1 = np.array([math.cos(a0), math.sin(a0)]), np.array([math.cos(a1), math.sin(a1)])
            pin0, pin1 = PIVOT + R_IN * d0, PIVOT + R_IN * d1
            pout0, pout1 = PIVOT + R_OUT * d0, PIVOT + R_OUT * d1
            sc.quad((*pin0, z1), (*pout0, z1), (*pout1, z1), (*pin1, z1), tread)
            sc.quad((*pin1, 0), (*pin0, 0), (*pin0, z1), (*pin1, z1), side)
            sc.quad((*pout0, 0), (*pout1, 0), (*pout1, z1), (*pout0, z1), side)
        d0 = np.array([math.cos(k * step), math.sin(k * step)])
        pin0, pout0 = PIVOT + R_IN * d0, PIVOT + R_OUT * d0
        sc.quad((*pin0, z0), (*pout0, z0), (*pout0, z1), (*pin0, z1), riser)

    # rails: the nosing line along each edge, from the bottom step to the gallery
    def edge(r_turn, i_edge):
        pts, zs = [], []
        for k in range(N_STRAIGHT):
            pts.append((i_edge, SJ0 + (k + 0.5) * g))
            zs.append((k + 1) * RISE)
        for k in range(N_TURN):
            a = (k + 0.5) * step
            pts.append(tuple(PIVOT + r_turn * np.array([math.cos(a), math.sin(a)])))
            zs.append((N_STRAIGHT + k + 1) * RISE)
        return pts, zs
    for r, i_edge, inset in ((R_IN, i_in, 0.2), (R_OUT, i_out, -0.2)):
        pts, zs = edge(r + inset, i_edge + inset)
        if r == R_IN:
            pts, zs = [tuple(NEWEL)] + pts, [RISE] + zs
        ts = np.linspace(0, 1, len(pts))
        balustrade(sc, pts, lambda t, ts=ts, zs=zs: float(np.interp(t, ts, zs)), spacing=0.5, post='spindle')


def clock(sc, proj, wood_fb):
    """Stacked sections, each wider at the top: the painting draws its verticals less converging than its floor's
    camera would, so the clock leans out a little, as Tonic Trouble's furniture does. Hidden faces show the face
    opposite (turned half way round)."""
    #   z0    z1    bottom (i0, i1, j0, j1)        top
    parts = [(0.0, 0.5, (-8.15, -5.4, -0.2, 1.9), (-8.1, -5.45, -0.15, 1.85)),     # plinth
             (0.5, 3.6, (-8.0, -5.5, -0.1, 1.8), (-8.3, -5.6, -0.25, 1.75)),      # trunk
             (3.6, 4.2, (-8.45, -5.6, -0.35, 1.85), (-8.5, -5.6, -0.4, 1.9)),     # waist moulding
             (4.2, 7.2, (-8.55, -5.65, -0.4, 1.9), (-8.75, -5.75, -0.5, 2.0)),    # hood (the dial faces +i)
             (7.2, 7.6, (-8.8, -5.75, -0.55, 2.05), (-9.1, -5.45, -0.8, 2.3)),    # cornice
             (7.6, 8.8, (-9.1, -5.45, -0.8, 2.3), (-9.2, -5.0, -1.4, 2.4))]
    for z0, z1, b, t in parts:
        centre = np.array([(t[0] + t[1]) / 2, (t[2] + t[3]) / 2])

        def turned(p, c=centre):
            q = np.array(p, float)
            q[:2] = 2 * c - q[:2]
            return q
        corners = [(b[0], b[2], z0), (b[1], b[2], z0), (b[1], b[3], z0), (b[0], b[3], z0),
                   (t[0], t[2], z1), (t[1], t[2], z1), (t[1], t[3], z1), (t[0], t[3], z1)]
        sc.hexahedron(np.array(corners, float), {'sides': proj(proj(wood_fb, turned)), 'top': proj(wood_fb),
                                                 'bottom': proj(wood_fb)})


def newel(sc, proj, wood_fb):
    def mirror(p):  # the far side shows the near side, turned half way round
        q = np.array(p, float)
        q[:2] = 2 * NEWEL - q[:2]
        return q
    look = proj(proj(wood_fb, mirror))
    sc.prism(NEWEL, [(0.0, 0.62), (0.6, 0.62), (0.6, 0.45), (1.2, 0.42), (3.0, 0.55), (3.3, 0.66), (3.4, 0.5),
                     (3.6, 0.5), (3.9, 0.95), (4.1, 0.95), (4.25, 0.45), (4.8, 0.62)],
             8, {'sides': look, 'top': look}, phase=math.pi / 8)


def curtain(sc, proj, velvet_fb):
    """A drape in folds at j ~ 4 off the left wall, its foot gathered towards the hall."""
    top, n_folds, cols, rows = 14.8, 5, 24, 14
    grid = np.zeros((rows + 1, cols + 1, 3))
    for r in range(rows + 1):
        t = r / rows  # 0 top, 1 bottom
        right = -4.4 - 2.6 * t ** 0.8  # the painting's diagonal edge, (-4.4, 14.8) to (-7.0, 0)
        for c in range(cols + 1):
            u = c / cols
            fold = 0.12 * math.sin(u * n_folds * 2 * math.pi) * (0.3 + 0.7 * t)
            grid[r, c] = (XL + 0.2 + (right - XL - 0.2) * u, 4.0 + fold - 0.5 * t * u, top * (1 - t))
    look = proj(velvet_fb)
    for r in range(rows):
        for c in range(cols):
            sc.quad(grid[r + 1, c], grid[r + 1, c + 1], grid[r, c + 1], grid[r, c], look)


def lantern(sc, proj, cream_fb, gold_fb, chain_fb, jade_fb):
    """The hexagonal lantern on its chain, with the green clock-like ornament above it."""
    p = LANTERN

    def turned(q):
        q = np.array(q, float)
        q[:2] = 2 * p[:2] - q[:2]
        return q
    body = proj(proj(cream_fb, turned))
    z = p[2]
    sc.prism(p[:2], [(z - 1.0, 0.08), (z, 1.2), (z + 2.5, 1.9), (z + 2.75, 2.1), (z + 3.0, 1.2), (z + 4.1, 0.25)],
             6, {'sides': body, 'top': proj(gold_fb)}, phase=math.pi / 6)
    sc.prism(p[:2], [(z + 4.1, 0.05), (ZC, 0.05)], 4, {'sides': chain_fb})
    c = np.array([p[0] + 0.3, p[1], z + 5.9])
    face = unit(np.array([EYE[0] - c[0], EYE[1] - c[1], 0]))
    right = np.array([face[1], -face[0], 0])
    ring = [c + 1.3 * (math.cos(a) * right + math.sin(a) * np.array([0, 0, 1])) for a in
            np.arange(8) * math.pi / 4 + math.pi / 8]
    back = [q - 0.3 * face for q in ring]
    jade = proj(proj(jade_fb, lambda q: q + 0.3 * face))
    for k in range(8):
        k1 = (k + 1) % 8
        sc.add([c, ring[k1], ring[k]], jade)
        sc.add([c - 0.3 * face, back[k], back[k1]], jade)
        sc.quad(ring[k], ring[k1], back[k1], back[k], jade)


def lamp(sc, proj, gold_fb, jade_fb):
    """The little lamp on its table against the back wall."""
    c = np.array([-3.4, YB - 0.8])
    sc.box(c[0] - 0.5, c[0] + 0.5, c[1] - 0.4, c[1] + 0.4, 0.0, 2.4, {'sides': proj(gold_fb), 'top': proj(gold_fb)})
    sc.prism(c, [(2.4, 0.25), (2.6, 0.08), (3.3, 0.08)], 6, {'sides': proj(gold_fb)})
    sc.prism(c, [(3.3, 0.6), (4.0, 0.3)], 8, {'sides': proj(jade_fb), 'top': proj(jade_fb)})


def medallion_texture(sc, apothem, size=512):
    """The floor medallion: the painting's octagon (drawn 0.7 times as deep as wide: rectified here) folded into
    its nearest sector (pointing at the camera) and repeated around the eight-fold star; transparent outside."""
    c_art, a_art, b_art = np.array([-3.7, 7.3]), 4.9, 3.5  # the octagon in the painting: centre, half width, depth
    t = (np.arange(size) + 0.5) / size * 2 - 1
    u, v = np.meshgrid(t, -t)  # u right (+i), v up (+j)
    r = np.hypot(u, v)
    ang = np.arctan2(v, u) + math.pi / 2  # 0 = towards the camera (-j)
    sector = math.pi / 4
    k = np.mod(ang + sector / 2, sector) - sector / 2
    k = np.abs(k)  # mirror inside the sector
    a = -math.pi / 2 + k
    su, sv = r * np.cos(a), r * np.sin(a)
    P = np.stack([c_art[0] + a_art * su, c_art[1] + b_art * sv, np.zeros_like(su)], -1)
    xy, _ = project(P)
    img = bilinear(sc.art, xy[..., 0], xy[..., 1])
    # octagon: flat sides on the axes, apothem 1
    d = np.maximum(np.maximum(np.abs(u), np.abs(v)), (np.abs(u) + np.abs(v)) / math.sqrt(2))
    alpha = np.where(d <= 1.0, 255.0, 0.0)
    sc.textures['medallion'] = np.concatenate([img, alpha[..., None]], -1)


# ---- glTF ----

def write_gltf(sc, meshes, out_dir, name):
    os.makedirs(out_dir, exist_ok=True)
    blob = bytearray()
    views, accessors, materials, textures, images, prims = [], [], [], [], [], []

    def add_view(data, target):
        while len(blob) % 4:
            blob.append(0)
        views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data), 'target': target})
        blob.extend(data)
        return len(views) - 1

    def add_accessor(arr, kind, comp, target, minmax=False):
        arr = np.ascontiguousarray(arr)
        acc = {'bufferView': add_view(arr.tobytes(), target), 'componentType': comp, 'count': len(arr), 'type': kind}
        if minmax:
            acc['min'] = arr.min(0).tolist()
            acc['max'] = arr.max(0).tolist()
        accessors.append(acc)
        return len(accessors) - 1

    for tex_name in sorted(meshes):
        pos, uv, col, idx = meshes[tex_name]
        if not idx:
            continue
        img = sc.textures[tex_name]
        png = f'{tex_name}.png'
        write_png(os.path.join(out_dir, png), img)
        images.append({'uri': png})
        textures.append({'source': len(images) - 1, 'sampler': 0})
        mat = {'name': tex_name, 'pbrMetallicRoughness': {'baseColorTexture': {'index': len(textures) - 1},
                                                          'metallicFactor': 0.0, 'roughnessFactor': 1.0},
               'extensions': {'KHR_materials_unlit': {}}}
        if tex_name in sc.alpha:
            mat['alphaMode'] = 'MASK'
        materials.append(mat)
        P = np.array(pos, np.float32) * S
        P = np.stack([P[:, 0], P[:, 2], -P[:, 1]], 1)  # tiles (i, j, z) -> glTF (x, y up, z towards the camera)
        tri = np.array(idx, np.uint32).reshape(-1, 3)
        nrm = np.zeros_like(P)
        fn = np.cross(P[tri[:, 1]] - P[tri[:, 0]], P[tri[:, 2]] - P[tri[:, 0]])
        for k in range(3):
            np.add.at(nrm, tri[:, k], fn)
        nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
        attrs = {'POSITION': add_accessor(P, 'VEC3', 5126, 34962, True),
                 'NORMAL': add_accessor(nrm.astype(np.float32), 'VEC3', 5126, 34962),
                 'TEXCOORD_0': add_accessor(np.array(uv, np.float32), 'VEC2', 5126, 34962),
                 'COLOR_0': add_accessor(np.clip(np.array(col, np.float32), 0, 1), 'VEC3', 5126, 34962)}
        prims.append({'attributes': attrs, 'indices': add_accessor(np.array(idx, np.uint32), 'SCALAR', 5125, 34963),
                      'material': len(materials) - 1})
    with open(os.path.join(out_dir, f'{name}.bin'), 'wb') as f:
        f.write(blob)
    doc = {'asset': {'version': '2.0', 'generator': 'tools/concept_foyer.py'},
           'extensionsUsed': ['KHR_materials_unlit'],
           'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [{'name': name, 'mesh': 0}],
           'meshes': [{'name': name, 'primitives': prims}],
           'materials': materials, 'textures': textures, 'images': images,
           'samplers': [{'magFilter': 9729, 'minFilter': 9987, 'wrapS': 10497, 'wrapT': 10497}],
           'buffers': [{'uri': f'{name}.bin', 'byteLength': len(blob)}],
           'bufferViews': views, 'accessors': accessors}
    path = os.path.join(out_dir, f'{name}.gltf')
    with open(path, 'w') as f:
        json.dump(doc, f, indent=1)
    return path


def main(argv):
    args = [a for a in argv if not a.startswith('--')]
    opts = dict(zip(argv, argv[1:]))
    src = args[0] if args and args[0] != opts.get('--name') else ART
    name = opts.get('--name', 'ConceptFoyer')
    if not os.path.exists(src):
        raise SystemExit(f'{src}: not found (the concept art is not in the repo; see the docstring)')
    art = read_image(src)
    if art.shape[:2] != (600, 784):  # the camera is in this image's pixels
        art = resize(art.astype(np.float32), 784, 600)
    sc = Scene(art)
    build(sc)
    sc.textures['art'] = resize(sc.art, 1024, 1024)  # projected UVs are fractions of the painting: any size works
    meshes = sc.emit()
    out_dir = os.path.join(OUT, 'gltf', name)
    path = write_gltf(sc, meshes, out_dir, name)
    n_tri = sum(len(m[3]) // 3 for m in meshes.values())
    print(f'wrote {path}: {len(meshes)} materials, {n_tri} triangles')
    if '--no-import' not in argv:
        from tools.ttre import import_gltf
        spawn = np.array([-0.3, -1.6, 0.0]) * S  # just ahead of the painting's camera
        import_gltf.main([path, name, '--spawn', f'{spawn[0]:.3f},{spawn[2] + 0.3:.3f},{-spawn[1]:.3f}'])


if __name__ == '__main__':
    main(sys.argv[1:])

"""Crazy Town: the Special Edition's cut level, rebuilt from what is left of it, as a custom-layer level (2026-11-15).

What survives (docs/SE.md "Crazy Town"): no level (no snapshot, geometry, collision or scripts on either disc), but the
Special Edition keeps its textures (Textures.cnt "crazy town": 112 of them), its world-map region and name ("Crazy
Town", INVENTOR/MONDE.INV: "crazytown_not_used") and its music; the retail game keeps an unused gate sign, "Crazy Tower"
(gen_arche/arche_crazy), and the soundtrack's "Bonus (Crazy Town)" in Bnk_89 (tools/music.tsv). The layout below is
ours, not a restoration: a crooked village round a leaning tower, built only from those textures; since 2026-11-22
the neighbourhoods of Crazy City round it, past its cliffs (crazycity.py).

    python3 -m tools.ttre.crazytown [out folder, default files/out]

Needs <out>/se/Textures/crazy town (the Special Edition's textures, tools/convert_files.sh step 4b) and the retail bar
(Ed's donor, as for the glTF imports). It is one of the Special Edition's levels (user, 2026-11-15; the pickers list it
there): <out>/se/ttlv/CrazyTown.ttlv (with EDIT 1, as se.py's) + .ttai (import_gltf.write_level), the textures it uses
in <out>/se/Imported/CrazyTown (a re-extraction of se/Textures does not wipe them; the checks pass a level with an
Imported folder by, docs: core/tests/testlevels.h) and its loading screen <out>/se/Vignette/Level/CrazyTown.png (made
again when missing: a re-extraction of se/Vignette takes it). Made again only when its record in
<out>/.stamps/crazytown changes (this code, the donor's stamp, the textures folder). The arches (the Special Edition
sud0's "Crazy Tower" gate and the way back) are the custom layer's (tools/ttre/custom.py EDITS, pair 'crazy'); the
arrival spot is read from there.

Port choices (all ours): 1 unit = Ed's metre (he is 2.2 tall, jumps ~2.8; STEP_RISE for what he walks up); a
height field 2.5 apart for the ground, river and cliffs; the walls, roofs and props as prisms; light baked per vertex
from a low sun in the south-west (the sky texture's glow is turned to face it); the sky a dome drawn round the camera
(TCX_FLAG_ROUND_CAMERA, as the SSX courses'); the cut-out textures (the "NZ" ones, extracted without alpha) keyed on
black (how the Special Edition cuts them out is not read).
"""
import hashlib
import math
import os
import random
import shutil
import struct
import sys

from . import chunks
from . import crazyquarter as cq
from . import crazycity as cc
from . import import_gltf as ig
from .custom import EDITS, blend, opaque_box, read_png, save_png, scaled

NAME = 'CrazyTown'
SRC = os.path.join('se', 'Textures', 'crazy town')  # under out
SIGN = os.path.join('Textures', 'gen_arche', 'arche_crazy.png')  # retail's unused gate sign, for the loading screen
DONOR = 'bar'
LEVELS = os.path.join('se', 'ttlv')  # under out: among the Special Edition's levels
TEXTURES = os.path.join('se', 'Imported', NAME)
VIGNETTE = os.path.join('se', 'Vignette', 'Level', NAME + '.png')
# write_level works here, beside ttlv, so the paths it reads relative to its folder (the donor's .ttlv / .ttai, Ed's
# textures in the donor's TEXS) resolve as they do from ttlv; the finished files then go to LEVELS (finish)
BUILD = '.crazytown-build'
# Where it was first made (2026-11-15, among the retail levels as an import): taken away (retire)
RETIRED = [os.path.join('ttlv', NAME + '.ttlv'), os.path.join('ttlv', NAME + '.ttai'),
           os.path.join('.custom-base', 'ttlv', NAME + '.ttlv'), os.path.join('.custom-base', 'ttlv', NAME + '.ttai'),
           os.path.join('Vignette', 'Level', NAME + '.png'), os.path.join('Imported', NAME)]
# Its files' stamp kind (chunks.KINDS), registered here: an entry in chunks.py would change that module's code, which
# every edition's stamp covers, and so make every converted level stale.
KIND = 'crazytown'
chunks.KINDS.setdefault(KIND, ((KIND,), ()))

ALPHA_TEST, TRANSPARENT, WRAP = ig.ALPHA_TEST, ig.TRANSPARENT, ig.WRAP_U | ig.WRAP_V
ROUND_CAMERA = 1 << 8  # TCX_FLAG_ROUND_CAMERA (core/include/tonic/custom/tcx.h)
KEYED = {'vg_arbregeoNZ', 'vg_buissongeoNZ', 'vg_buissongeo02NZ', 'rendu6_NZ', 'xx_coronneNZ', 'xx_cd_NZ'}

GRID = 2.5             # the height field's spacing
EXTENT = 182.0         # it covers [-EXTENT, EXTENT]^2 (past the old quarter and Crazy City's districts, crazycity.py)
SOLID_RADIUS = 105.0
CLIFF_SETBACK = 4.2    # the cliffs rise this far behind their upright foot (cliff_foot): no ground cell in front of it
                       # (2.5 x 2.5, so up to 3.6 across) reaches the rise   # ground beyond this is out of reach: drawn, not collided with
SUN = (lambda v: tuple(x / math.sqrt(sum(c * c for c in v)) for x in v))((-0.42, -0.62, 0.26))  # low: the dusk
# The dusk (2026-11-16, the user: the first light was "harsh and dodgy"): a violet sky's light from above, a dim warm
# sun low in the south-west wrapped round the faces (no hard edge between a lit wall and the next), the ground's
# shadow at the foot of everything, lanterns and windows in warm pools, the old quarter's covered alley in the dark;
# a violet haze (LEVL's fog) over the distance, the sky's dome left out of it.
SKY_UP, SKY_DOWN = (0.45, 0.42, 0.68), (0.24, 0.19, 0.29)
SUN_RGB = (0.92, 0.56, 0.34)
FOG = (22.0, 210.0, (0.30, 0.23, 0.38))  # near, far, colour (LEVL)
MAX_LIGHT = 1.9
WATER_Z = -0.35        # the river's surface; its bed is at -1 (Ed wades)
WATER_SCROLL = (0.04, 0.015)  # UV per second

TOWER_TOP = 33.0       # the tower's six drums (DRUM tall each); its stair climbs round them to the top
DRUM = 5.5
STEP_RISE = 0.40       # Ed walks up it; every tenth step is left out, a jump of 2 x STEP_RISE. Measured by a scratch
                       # climb (Ed's AI, 30 Hz): 0.4 climbs all 82 steps and the 8 jumps to the top in ~23 s; at 0.55
                       # he stood at the second step's riser, though the notes give his step-up as 1 (mechcheck 0.9)
STEP_ANGLE = math.radians(20)


def norm(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return tuple(x / l for x in v)


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def mul(a, k):
    return tuple(x * k for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def smoothstep(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3 - 2 * t)


class Lighting:
    """What lights a point (set by build): the land under it (its foot is darker), the lanterns [(position, rgb,
    reach)], the covered places [(x0, y0, x1, y1, z0, z1, ambient)] where the sun does not reach and the sky's light
    is that much."""

    def __init__(self):
        self.land, self.lamps, self.covered = None, [], []

    def at(self, n, p=None):
        h = 0.5 + 0.5 * n[2]
        amb = [lo + (hi - lo) * h for lo, hi in zip(SKY_DOWN, SKY_UP)]
        d = max(0.0, (dot(n, SUN) + 0.4) / 1.4) ** 1.5  # wrapped: faces turned a little away still get some
        sun, sky = 0.8, 1.0
        if p is not None:
            for x0, y0, x1, y1, z0, z1, a in self.covered:
                if x0 <= p[0] <= x1 and y0 <= p[1] <= y1 and z0 <= p[2] <= z1:
                    sun, sky = 0.0, a
                    break
            if self.land:
                floor = cc.floor_at(p[0], p[1])  # a district's own floor, or None: the land's
                above = p[2] - (floor if floor is not None else self.land.height(p[0], p[1]))
                foot = 0.6 + 0.4 * smoothstep(-0.5, 3.0, above)
                sky *= foot
                sun *= 0.75 + 0.25 * foot
        out = [a * sky + c * d * sun for a, c in zip(amb, SUN_RGB)]
        if p is not None:
            for q, rgb, reach in self.lamps:
                if abs(q[0] - p[0]) > reach or abs(q[1] - p[1]) > reach or abs(q[2] - p[2]) > reach:
                    continue
                v = sub(q, p)
                dist = math.sqrt(dot(v, v)) or 1e-6
                if dist >= reach:
                    continue
                f = (1.0 - dist / reach) ** 2 * (0.35 + 0.65 * max(0.0, dot(n, v) / dist))
                out = [o + c * f for o, c in zip(out, rgb)]
        return tuple(min(MAX_LIGHT, x) for x in out)


LIGHT = Lighting()


def light(n, p=None):
    """Baked light multiplier for a normal (and a point: its foot, the lanterns, a roof over it)."""
    return LIGHT.at(n, p)


# ---- the land ----

def river_y(x):
    """The river's middle line: it runs west to east across the south of the town."""
    return -28.0 + 5.0 * math.sin(x / 20.0 + 0.6)


def cliff_radius(a):
    return 80.0 + 5.0 * math.sin(3 * a + 0.5) + 3.0 * math.sin(7 * a + 2.0)


def hills(x, y):
    return 0.6 * math.sin(x / 11.0) * math.cos(y / 13.0) + 0.35 * math.sin((x + 2 * y) / 9.0) + 0.25 * math.cos((x - y) / 5.5)


class Land:
    """The ground's height: rolling hills, flattened pads (the square, the arrival, each building), the river's bed,
    the cliffs round it all and the high plateau behind them."""

    def __init__(self, pads):
        self.pads = pads  # (x, y, inner radius, outer radius, height or None = the hills' own at the middle)

    def height(self, x, y):
        h = hills(x, y)
        for px, py, r0, r1, z in self.pads:
            d = math.hypot(x - px, y - py)
            if d < r1:
                h += ((hills(px, py) if z is None else z) - h) * smoothstep(r1, r0, d)
        d = abs(y - river_y(x))
        h += (-1.0 - h) * smoothstep(6.0, 2.5, d)
        r, a = math.hypot(x, y), math.atan2(y, x)
        rim = cliff_radius(a)
        # the old quarter and Crazy City's districts: the cliffs give way round them, the land under them goes out of
        # sight
        district, out = cc.nearest(x, y)
        rise = smoothstep(rim + CLIFF_SETBACK, rim + CLIFF_SETBACK + 6.0, r) * smoothstep(1.5, 7.0, out)
        h += rise * (20.0 + 2.5 * math.sin(5 * a) + 1.5 * math.sin(x / 7.0) * math.cos(y / 9.0))
        if district is cq:
            # under the quarter, kept just under its lowest floor (no deeper: the grid's 2.5 cells would dig a ditch
            # along its walls, and a ledge at the arch's doorstep)
            return min(h, cq.ZA - 0.3) if out <= 0.0 else h
        if out <= 0.0:  # under a district: just under its floor there
            return min(h, district.floor_at(x, y) - 0.3)
        # past the cliff ring, a bank round it a little over its nearest floor, fading into the rock: its edge is a
        # slope up, not a drop into a trench between it and the rock
        bank = (1.0 - smoothstep(6.0, 9.0, out)) * smoothstep(rim, rim + 3.0, r)
        return h + (max(h, district.near_floor(x, y) + 1.0) - h) * bank

    def normal(self, x, y):
        e = 0.5
        dx = self.height(x + e, y) - self.height(x - e, y)
        dy = self.height(x, y + e) - self.height(x, y - e)
        return norm((-dx, -dy, 2 * e))


def polyline_distance(p, line):
    best = math.inf
    for a, b in zip(line, line[1:]):
        ab = sub(b, a)
        t = min(max(dot(sub(p, a), ab) / (dot(ab, ab) or 1), 0), 1)
        best = min(best, math.dist(p, add(a, mul(ab, t))))
    return best


# ---- the mesh builder ----

class Builder:
    def __init__(self, src, tex_dir):
        self.src, self.tex_dir = src, tex_dir
        self.groups, self.tris, self.copied = {}, [], {}

    def tex(self, name, flags=WRAP):
        """(texture path, flags) for a Crazy Town texture, copied into the level's own folder on first use (the
        cut-outs keyed on black)."""
        if name not in self.copied:
            src, dst = os.path.join(self.src, name + '.png'), os.path.join(self.tex_dir, name.replace(' ', '_') + '.png')
            w, h, px = read_png(src)
            if name in KEYED:
                px = bytearray(px)
                for i in range(0, len(px), 4):
                    if px[i] + px[i + 1] + px[i + 2] < 40:
                        px[i + 3] = 0
            save_png(dst, w, h, px)
            self.copied[name] = dst
        return self.copied[name], flags | (ALPHA_TEST if name in KEYED else 0)

    def tri(self, key, pts, uvs, normals=None, lit=None, solid=True):
        n = norm(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])))
        verts, indices = self.groups.setdefault(key, ([], []))
        first = len(verts)
        for i in range(3):
            nv = normals[i] if normals else n
            li = lit[i] if isinstance(lit, (list, tuple)) else lit  # one baked light, or one per corner
            verts.append((pts[i], nv, uvs[i], li if li is not None else ig.baked_light(light(nv, pts[i]))))
        indices += [first, first + 1, first + 2]
        if solid:
            self.tris.append(tuple(c for p in pts for c in p))

    def face(self, key, pts, size=(4.0, 4.0), solid=True, fit=False, lit=None):
        """A flat convex polygon, its points counter-clockwise seen from outside. Texture: planar, the top of the
        texture at the face's top edge; size = one tile's (width, height), or fit: one tile across the whole face."""
        n = norm(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])))
        if abs(n[2]) > 0.98:
            t = (1.0, 0.0, 0.0)
        else:
            t = norm(cross((0.0, 0.0, 1.0), n))
        b = cross(n, t)  # along the face, upwards
        us, vs = [dot(p, t) for p in pts], [dot(p, b) for p in pts]
        u0, v1 = min(us), max(vs)
        if fit:
            size = ((max(us) - u0) or 1.0, (v1 - min(vs)) or 1.0)
        uv = [((u - u0) / size[0], (v1 - v) / size[1]) for u, v in zip(us, vs)]
        for i in range(1, len(pts) - 1):
            self.tri(key, (pts[0], pts[i], pts[i + 1]), (uv[0], uv[i], uv[i + 1]), lit=lit, solid=solid)

    def prism(self, bottom, top, side, cap=None, base=None, size=(4.0, 4.0), fit=False, solid=True, sides=None):
        """Walls between two rings of points (counter-clockwise seen from above), a lid (cap) and a floor (base).
        sides: a texture per wall, else `side` for all."""
        n = len(bottom)
        for i in range(n):
            j = (i + 1) % n
            self.face((sides[i] if sides else side), [bottom[i], bottom[j], top[j], top[i]], size, solid, fit)
        if cap:
            self.face(cap, list(top), size, solid)
        if base:
            self.face(base, list(reversed(bottom)), size, solid)

    def box_collision(self, centre, half, z0, z1):
        """Collision only (nothing drawn): an upright box."""
        x, y = centre
        b = [(x - half, y - half, z0), (x + half, y - half, z0), (x + half, y + half, z0), (x - half, y + half, z0)]
        t = [(p[0], p[1], z1) for p in b]
        for i in range(4):
            j = (i + 1) % 4
            self.tris += [b[i] + b[j] + t[j], b[i] + t[j] + t[i]]


def ring(centre, radius, n, z, phase=0.0):
    return [(centre[0] + radius * math.cos(phase + 2 * math.pi * k / n), centre[1] + radius * math.sin(phase + 2 * math.pi * k / n), z)
            for k in range(n)]


def rect(centre, w, d, angle, z, scale=1.0, shift=(0.0, 0.0)):
    """The corners of a w x d rectangle turned by angle (its local -Y faces (sin a, -cos a), as the arches), CCW."""
    c, s = math.cos(angle), math.sin(angle)
    out = []
    for lx, ly in ((-w / 2, -d / 2), (w / 2, -d / 2), (w / 2, d / 2), (-w / 2, d / 2)):
        lx, ly = lx * scale, ly * scale
        out.append((centre[0] + shift[0] + c * lx - s * ly, centre[1] + shift[1] + s * lx + c * ly, z))
    return out


def facing(src, dst):
    """The angle that turns a rect's front (local -Y) from src towards dst."""
    dx, dy = dst[0] - src[0], dst[1] - src[1]
    return math.atan2(dx, -dy)


# ---- the town ----

def ground(b, land, paths):
    """The height field: grass, sand paths and banks, the paved square, rock where it is steep."""
    grass = [b.tex('hb_01'), b.tex('hb_03')]
    path, bed, square, rock, top = b.tex('hb_tergeo10'), b.tex('hb_sable'), b.tex('xx_pontfou01'), b.tex('rc_doc03'), b.tex('hb_02')
    n = int(2 * EXTENT / GRID)
    hs = [[land.height(-EXTENT + i * GRID, -EXTENT + j * GRID) for j in range(n + 1)] for i in range(n + 1)]
    ns = [[land.normal(-EXTENT + i * GRID, -EXTENT + j * GRID) for j in range(n + 1)] for i in range(n + 1)]
    for i in range(n):
        for j in range(n):
            x, y = -EXTENT + (i + 0.5) * GRID, -EXTENT + (j + 0.5) * GRID
            corners = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
            pts = [(-EXTENT + a * GRID, -EXTENT + c * GRID, hs[a][c]) for a, c in corners]
            nrm = [ns[a][c] for a, c in corners]
            steep = min(q[2] for q in nrm) < 0.72
            if steep:
                key = rock
            elif math.hypot(x, y) < 17.0:
                key = square
            elif min(p[2] for p in pts) < -0.45:
                key = bed
            elif any(polyline_distance((x, y), line) < 2.4 for line in paths):
                key = path
            elif max(p[2] for p in pts) > 12:
                key = top
            else:
                key = grass[int(3 + 2 * math.sin(x / 17.0) + 2 * math.cos(y / 13.0)) % 2]
            tile = 3.0 if key is square else 8.0
            if steep:
                f = norm((nrm[0][0] + nrm[2][0], nrm[0][1] + nrm[2][1], 0.0001))
                t = norm((-f[1], f[0], 0.0))
                uv = [(dot(p, t) / tile, -p[2] / tile) for p in pts]
            else:
                uv = [(p[0] / tile, -p[1] / tile) for p in pts]
            # the districts have their own floors; the land round them is solid as far as their banks reach
            solid = cc.outside(x, y) > 0.5 and (math.hypot(x, y) < SOLID_RADIUS or cc.new_outside(x, y) < 14.0)
            for a, c, d in ((0, 1, 2), (0, 2, 3)):
                b.tri(key, (pts[a], pts[c], pts[d]), (uv[a], uv[c], uv[d]), (nrm[a], nrm[c], nrm[d]), solid=solid)


def cliff_foot(b):
    """A vertical rock face round the foot of the cliffs. The height field's cliff leans back (its faces ~79 degrees)
    over a shallow crease, and Ed opening his wings in that crease was thrown up to 4 units in a frame (the flight's
    spheres pushed out of both faces; wander, 2026-11-15); against an upright wall, as the game's levels have, he is not.
    The cliffs rise CLIFF_SETBACK behind it, so the ground in front of it stays flat and it hides their first slope."""
    rock = b.tex('rc_doc03')
    n = 240
    pts = []
    for k in range(n):
        a = 2 * math.pi * k / n
        r = cliff_radius(a) + 0.6
        pts.append((r * math.cos(a), r * math.sin(a)))
    for k in range(n):
        p, q = pts[k], pts[(k + 1) % n]
        if cc.outside(0.5 * (p[0] + q[0]), 0.5 * (p[1] + q[1])) < 2.0:
            continue  # where the old quarter or a district goes through
        b.face(rock, [(p[0], p[1], -2.5), (q[0], q[1], -2.5), (q[0], q[1], 12.0), (p[0], p[1], 12.0)], (8.0, 8.0))


def river(b):
    """The river's surface (not solid: Ed wades its bed), scrolled by finish."""
    key = b.tex('o_fouTXY', WRAP | TRANSPARENT)
    lit = ig.baked_light((0.62, 0.56, 0.84), 200)  # the dusk's sky in it
    x = -90.0
    while x < 90.0:
        x1 = x + GRID
        quad = [(x, river_y(x) - 5.2, WATER_Z), (x1, river_y(x1) - 5.2, WATER_Z),
                (x1, river_y(x1) + 5.2, WATER_Z), (x, river_y(x) + 5.2, WATER_Z)]
        if all(math.hypot(p[0], p[1]) < cliff_radius(math.atan2(p[1], p[0])) + 1.5 for p in quad):
            b.face(key, quad, (6.0, 6.0), solid=False, lit=lit)
        x = x1


def sky(b):
    """The sunset: CIEL12 (dark red at the top, a yellow glow at the bottom's middle) round a dome drawn round the
    camera, the glow towards the sun; below the horizon the glow's colour carries on."""
    key = b.tex('CIEL12', ROUND_CAMERA)

    def tint(e):  # the dusk: the glow kept low down, the red above turning violet and dark towards the top
        t = smoothstep(0.0, math.radians(70), e)
        # a level's light, not SKY_ALPHA: an import draws in the port's lighting, which leaves a sky's untinted
        return ig.baked_light(tuple(lo + (hi - lo) * t for lo, hi in zip((0.72, 0.5, 0.66), (0.22, 0.2, 0.5))))
    radius, sun = 700.0, math.atan2(SUN[1], SUN[0])
    rows = [math.radians(e) for e in (-40, -5, 0, 8, 18, 30, 45, 62, 78, 90)]
    cols = 32
    for r in range(len(rows) - 1):
        for c in range(cols):
            pts, uvs, lits = [], [], []
            for dr, dc in ((0, 0), (0, 1), (1, 1), (1, 0)):
                e, a = rows[r + dr], sun - math.pi + 2 * math.pi * (c + dc) / cols
                pts.append((radius * math.cos(e) * math.cos(a), radius * math.cos(e) * math.sin(a), radius * math.sin(e)))
                uvs.append(((c + dc) / cols, min(0.97, max(0.03, 1.0 - max(e, 0.0) / (math.pi / 2)))))
                lits.append(tint(e))
            for t in ((0, 1, 2), (0, 2, 3)):
                b.tri(key, tuple(pts[k] for k in t), tuple(uvs[k] for k in t), lit=[lits[k] for k in t], solid=False)


def mountains(b):
    """The snowy peaks (rendu6_NZ) in a ring far behind the cliffs."""
    key = b.tex('rendu6_NZ', ig.WRAP_U)
    lit = ig.baked_light((0.5, 0.44, 0.68))  # in the dusk's shade, under its haze
    n, radius, z0, z1 = 12, 200.0, 16.0, 86.0
    for k in range(n):
        a0, a1 = 2 * math.pi * k / n + 0.2, 2 * math.pi * (k + 1) / n + 0.2
        p0, p1 = (radius * math.cos(a0), radius * math.sin(a0)), (radius * math.cos(a1), radius * math.sin(a1))
        pts = [(p0[0], p0[1], z0), (p1[0], p1[1], z0), (p1[0], p1[1], z1), (p0[0], p0[1], z1)]
        uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
        for t in ((0, 1, 2), (0, 2, 3)):
            b.tri(key, tuple(pts[i] for i in t), tuple(uvs[i] for i in t), lit=lit, solid=False)


def tower_centre(z):
    """The tower leans and wanders as it climbs."""
    return (1.4 * math.sin(z / 9.0), 1.1 * math.sin(z / 7.0 + 1.0))


def tower_radius(z):
    return 6.0 - 0.12 * min(int(z / DRUM), 5)


def tower(b):
    """The Crazy Tower: six crooked drums, each turned a little, of blue stone and striped walls with stained glass;
    a stair of planks round it to the top; a funnel for a roof on four posts, a crown round its rim."""
    walls = [b.tex(t) for t in ('mr_pierrbleu', 'mr_fou3', 'mr_pierrbleu', 'mr_fou1', 'mr_pierrocre', 'mr_fou5')]
    glass, door = b.tex('vr_vitraux1'), b.tex('bs_murplanche2')
    planks, plank_side = b.tex('bs_pon2nz'), b.tex('bs_pon1nz')
    stone_top = b.tex('pr_dalfou3')
    for k in range(6):
        z0, z1 = k * DRUM, (k + 1) * DRUM
        lo = -1.5 if k == 0 else z0
        r = tower_radius(z0 + 0.01)
        bottom = ring(tower_centre(z0), r, 8, lo, math.radians(6 * k))
        top = ring(tower_centre(z1), r, 8, z1, math.radians(6 * k))
        b.prism(bottom, top, walls[k], cap=stone_top if k == 5 else None, fit=True)
        if k % 2 == 1:  # stained glass on two faces, a hair outside the wall
            for f in (k, k + 4):
                a, c = bottom[f % 8], bottom[(f + 1) % 8]
                ta, tc = top[f % 8], top[(f + 1) % 8]
                n = norm(cross(sub(c, a), sub(ta, a)))

                def at(u, v):
                    lowp, highp = add(a, mul(sub(c, a), u)), add(ta, mul(sub(tc, ta), u))
                    return add(add(lowp, mul(sub(highp, lowp), v)), mul(n, 0.05))
                b.face(glass, [at(0.3, 0.3), at(0.7, 0.3), at(0.7, 0.78), at(0.3, 0.78)], fit=True, solid=False)
    # the door, at the foot, towards the arrival (south)
    bottom = ring(tower_centre(0), tower_radius(0), 8, 0.0, 0.0)
    a, c = bottom[5], bottom[6]  # the face looking south-ish (angles 225..270 degrees)
    n = norm(cross(sub(c, a), (0, 0, 1)))
    mid = mul(add(a, c), 0.5)
    e = norm(sub(c, a))
    z = 0.15
    pts = [add(add(mid, mul(e, -1.1)), mul(n, 0.05)), add(add(mid, mul(e, 1.1)), mul(n, 0.05))]
    b.face(door, [(pts[0][0], pts[0][1], z), (pts[1][0], pts[1][1], z), (pts[1][0], pts[1][1], z + 3.2),
                  (pts[0][0], pts[0][1], z + 3.2)], fit=True, solid=False)
    # the stair: from the south, anticlockwise, every tenth step left out (a jump)
    i, z_top = 0, STEP_RISE
    while z_top < TOWER_TOP - 0.01:
        if i % 10 != 9:
            angle = math.radians(-60) + i * STEP_ANGLE
            c = tower_centre(z_top)
            r_in, r_out = 0.86 * tower_radius(z_top), tower_radius(z_top) + 3.4
            span = STEP_ANGLE / 2 + math.radians(0.6)  # a hair of overlap with the next step
            # counter-clockwise seen from above: outer-start, outer-end, inner-end, inner-start
            top = [(c[0] + r * math.cos(a), c[1] + r * math.sin(a), z_top) for r, a in
                   ((r_out, angle - span), (r_out, angle + span), (r_in, angle + span), (r_in, angle - span))]
            # deep enough that its riser reaches past the step before's top (they overlap a little): a riser
            # hanging over that floor is an overhang Ed cannot step up (a scratch climb stopped at the second step)
            bottom = [(p[0], p[1], z_top - STEP_RISE - 0.45) for p in top]
            b.prism(bottom, top, plank_side, cap=planks, base=plank_side, size=(2.0, 2.0))
        i += 1
        z_top = STEP_RISE * (i + 1)
    # the roof: posts on the top, the funnel over them, its spout, the crown round the rim
    c = tower_centre(TOWER_TOP)
    for k in range(4):
        a = math.radians(45 + 90 * k)
        p = (c[0] + 4.0 * math.cos(a), c[1] + 4.0 * math.sin(a))
        b.prism(rect(p, 0.5, 0.5, a, TOWER_TOP), rect(p, 0.5, 0.5, a, TOWER_TOP + 4.6), plank_side, size=(1.0, 2.0))
    funnel = b.tex('xx_entonoir2')
    rim = ring(c, 7.0, 16, TOWER_TOP + 4.6)
    neck = ring((c[0] + 0.8, c[1] - 0.5), 1.1, 16, TOWER_TOP + 12.5)
    spout = ring((c[0] + 1.4, c[1] - 0.9), 0.8, 16, TOWER_TOP + 17.0)
    b.prism(rim, neck, funnel, size=(3.0, 3.0))
    b.prism(neck, spout, funnel, cap=funnel, size=(2.0, 2.0))
    b.face(b.tex('mt_mar2TXY'), list(reversed(rim)), (3.0, 3.0))  # its underside, seen from the top of the tower
    crown = b.tex('xx_coronneNZ', ig.WRAP_U)
    low, high = ring(c, 7.15, 16, TOWER_TOP + 4.3), ring(c, 7.6, 16, TOWER_TOP + 7.3)
    lit = ig.baked_light((0.95, 0.78, 0.62))
    for k in range(16):
        j = (k + 1) % 16
        pts = [low[k], low[j], high[j], high[k]]
        uvs = [(k / 4, 1), ((k + 1) / 4, 1), ((k + 1) / 4, 0), (k / 4, 0)]
        for t in ((0, 1, 2), (0, 2, 3)):
            b.tri(crown, tuple(pts[x] for x in t), tuple(uvs[x] for x in t), lit=lit, solid=False)


def house(b, land, centre, w, d, height, look, wall, twist=8.0, lean=(0.8, 0.4), top_scale=1.12, roof=5.0,
          roof_shift=(0.6, -0.4), door='bs_murplanche1'):
    """A crooked house: its walls twisted, leaning and wider at the top, a slate roof with its point off the middle,
    a plank door on its front (local -Y), which looks at `look`."""
    angle = facing(centre, look)
    z0 = land.height(*centre)
    base = rect(centre, w, d, angle, z0 - 1.2)
    top = rect(centre, w, d, angle + math.radians(twist), z0 + height, top_scale, lean)
    walls = b.tex(wall)
    b.prism(base, top, walls, cap=b.tex('bs_pon1nz'), fit=True)
    eave = rect(centre, w, d, angle + math.radians(twist), z0 + height, top_scale * 1.15, lean)
    apex = (centre[0] + lean[0] + roof_shift[0], centre[1] + lean[1] + roof_shift[1], z0 + height + roof)
    slate = b.tex('tt_ardoise')
    b.face(b.tex('bs_pon1nz'), list(reversed(eave)), (2.0, 2.0))
    for k in range(4):
        b.face(slate, [eave[k], eave[(k + 1) % 4], apex], (2.5, 2.5))
    # the door, on the front wall's plane, from the ground up
    a, c, ta = base[0], base[1], top[0]
    n = norm(cross(sub(c, a), sub(ta, a)))
    up = norm(sub(add(ta, top[1]), add(a, c)))
    mid = mul(add(a, c), 0.5)
    mid = add(mid, mul(up, (z0 - mid[2]) / up[2]))  # where the wall meets the ground
    e = norm(sub(c, a))
    corners = [(-0.95, 0.0), (0.95, 0.0), (0.95, 2.9), (-0.95, 2.9)]
    b.face(b.tex(door), [add(add(add(mid, mul(e, s)), mul(up, h / up[2])), mul(n, 0.06)) for s, h in corners],
           fit=True, solid=False)


def windmill(b, land, centre, look):
    z0 = land.height(*centre)
    stone, slate = b.tex('mr_pierrocre'), b.tex('tt_ardoise')
    bottom, top = ring(centre, 4.4, 12, z0 - 1.2), ring((centre[0] + 0.5, centre[1] + 0.3), 3.3, 12, z0 + 13.0)
    b.prism(bottom, top, stone, size=(5.0, 5.0))
    eave = ring((centre[0] + 0.5, centre[1] + 0.3), 4.1, 12, z0 + 13.0)
    b.face(b.tex('bs_pon1nz'), list(reversed(eave)), (2.0, 2.0))
    apex = (centre[0] + 1.6, centre[1] - 0.4, z0 + 19.5)
    for k in range(12):
        b.face(slate, [eave[k], eave[(k + 1) % 12], apex], (2.5, 2.5))
    # the hub (MR_moulin: a round window with a cross) and four sails, facing `look`
    f = norm((look[0] - centre[0], look[1] - centre[1], 0.0))
    side = (-f[1], f[0], 0.0)
    hub = (centre[0] + 0.5 + f[0] * 3.6, centre[1] + 0.3 + f[1] * 3.6, z0 + 10.5)
    disc = [add(hub, add(mul(side, 1.8 * math.cos(2 * math.pi * k / 16)), (0, 0, 1.8 * math.sin(2 * math.pi * k / 16))))
            for k in range(16)]
    b.face(b.tex('MR_moulin'), disc, fit=True, solid=False)
    sail = b.tex('bs_pon1nz')
    for k in range(4):
        a = math.radians(20 + 90 * k)
        along = add(mul(side, math.cos(a)), (0, 0, math.sin(a)))
        across = add(mul(side, -math.sin(a)), (0, 0, math.cos(a)))
        o = add(hub, mul(f, 0.3))
        pts = [add(add(o, mul(along, 1.6)), mul(across, -0.9)), add(add(o, mul(along, 9.0)), mul(across, -0.9)),
               add(add(o, mul(along, 9.0)), mul(across, 0.9)), add(add(o, mul(along, 1.6)), mul(across, 0.9))]
        b.face(sail, pts, (2.0, 2.0), solid=False)
    house_door(b, land, centre, f, 4.4)


def house_door(b, land, centre, f, radius):
    side = (-f[1], f[0], 0.0)
    z0 = land.height(centre[0] + f[0] * radius, centre[1] + f[1] * radius) - 0.1
    o = (centre[0] + f[0] * (radius - 0.05), centre[1] + f[1] * (radius - 0.05), z0)
    pts = [add(o, mul(side, -1.0)), add(o, mul(side, 1.0)), add(add(o, mul(side, 1.0)), (0, 0, 3.0)),
           add(add(o, mul(side, -1.0)), (0, 0, 3.0))]
    b.face(b.tex('bs_murplanche2'), pts, fit=True, solid=False)


def outhouse(b, land, centre, look):
    """The privy: plank walls, its door the heart-cut xx_wc, a slanted roof."""
    angle = facing(centre, look)
    z0 = land.height(*centre)
    base = rect(centre, 2.4, 2.4, angle, z0 - 0.6)
    top = rect(centre, 2.4, 2.4, angle + math.radians(6), z0 + 3.2, 1.05, (0.25, 0.1))
    planks, wc = b.tex('bs_murplanche1'), b.tex('xx_wc')
    b.prism(base, top, planks, sides=[wc, planks, planks, planks], fit=True)
    roof = [(p[0], p[1], p[2] + (0.9 if k in (2, 3) else 0.2)) for k, p in enumerate(rect(centre, 3.2, 3.2, angle + math.radians(6),
                                                                                       z0 + 3.2, 1.0, (0.25, 0.1)))]
    b.prism(roof, [(p[0], p[1], p[2] + 0.25) for p in roof], b.tex('bs_pon1nz'), cap=b.tex('pt_boiswc'), size=(2.0, 2.0))


def bathtub(b, land, centre, angle):
    z0 = land.height(*centre)
    tub = b.tex('xx_baignoire')
    outer = rect(centre, 3.4, 1.9, angle, z0 - 0.3)
    b.prism(outer, rect(centre, 3.4, 1.9, angle, z0 + 1.0), tub, fit=True)
    b.face(b.tex('o_fouTXY'), rect(centre, 3.1, 1.6, angle, z0 + 1.02), (2.0, 2.0))


def spiral_square(b, land, centre, radius=7.5):
    """Round the giant CD, a square paved in a spiral (the cut level's screenshot: grey stones, a brown spiral)."""
    key = cq.own(b, 'cq_spiral', lambda: cq.crazypaint.spiral_paving(cq.crazypaint.flagstones(5, base=(170, 168, 160))),
                 0)
    rings, n = 6, 40

    def at(i, k):
        r, a = radius * i / rings, 2 * math.pi * k / n
        x, y = centre[0] + r * math.cos(a), centre[1] + r * math.sin(a)
        return (x, y, land.height(x, y) + 0.06), ((x - centre[0]) / (2 * radius) + 0.5, (y - centre[1]) / (2 * radius) + 0.5)
    for i in range(rings):
        for k in range(n):
            (p0, t0), (p1, t1), (p2, t2), (p3, t3) = at(i, k), at(i + 1, k), at(i + 1, k + 1), at(i, k + 1)
            b.tri(key, (p0, p1, p2), (t0, t1, t2), solid=False)
            if i:
                b.tri(key, (p0, p2, p3), (t0, t2, t3), solid=False)


def cd(b, land, centre, look):
    """A giant CD stood on its edge in a stone block, leaning back, on its spiral square."""
    spiral_square(b, land, centre)
    f = norm((look[0] - centre[0], look[1] - centre[1], 0.0))
    side = (-f[1], f[0], 0.0)
    z0 = land.height(*centre)
    back = math.radians(12)
    up = add(mul((0, 0, 1), math.cos(back)), mul(f, -math.sin(back)))
    mid = add((centre[0], centre[1], z0), mul(up, 3.7))
    pts = [add(mid, add(mul(side, 4.0 * math.cos(2 * math.pi * k / 24)), mul(up, 4.0 * math.sin(2 * math.pi * k / 24))))
           for k in range(24)]
    b.face(b.tex('xx_cd_NZ', 0), pts, fit=True)
    b.face(b.tex('xx_cd_NZ', 0), list(reversed(pts)), fit=True)
    angle = facing(centre, look)
    b.prism(rect(centre, 3.2, 1.6, angle, z0 - 0.5), rect(centre, 3.2, 1.6, angle, z0 + 0.7), b.tex('pr_dalfou4'),
            cap=b.tex('pr_dalfou3'), size=(2.0, 2.0))


def sign(b, land, centre, look, picture, height=2.6):
    angle = facing(centre, look)
    z0 = land.height(*centre)
    post = b.tex('bs_pon1nz')
    b.prism(rect(centre, 0.28, 0.28, angle, z0 - 0.5), rect(centre, 0.28, 0.28, angle, z0 + height), post, size=(0.5, 2.0))
    board = rect((centre[0], centre[1]), 1.9, 0.14, angle, z0 + height - 0.4, 1.0)
    top = [(p[0], p[1], p[2] + 1.9) for p in board]
    pic = b.tex(picture, 0)
    b.prism(board, top, post, cap=post, base=post, sides=[pic, post, pic, post], fit=True)


def roadworks(b, land, centre, look):
    """A road to nowhere, closed for works: a striped barrier on two posts, a warning light, the works sign."""
    angle = facing(centre, look)
    z0 = land.height(*centre)
    c, s = math.cos(angle), math.sin(angle)
    post = b.tex('bs_pon1nz')
    for k in (-2.0, 2.0):
        p = (centre[0] + c * k, centre[1] + s * k)
        b.prism(rect(p, 0.3, 0.3, angle, z0 - 0.4), rect(p, 0.3, 0.3, angle, z0 + 1.6), post, size=(0.5, 2.0))
    bar = rect(centre, 5.2, 0.16, angle, z0 + 0.9)
    b.prism(bar, [(p[0], p[1], p[2] + 0.7) for p in bar], b.tex('xx_barietrav01'), cap=post, base=post, size=(1.4, 0.7))
    lamp = (centre[0] + c * 2.0, centre[1] + s * 2.0)
    b.prism(rect(lamp, 0.45, 0.45, angle, z0 + 1.6), rect(lamp, 0.45, 0.45, angle, z0 + 2.05), b.tex('xx_gyro', 0),
            cap=b.tex('xx_gyro', 0), fit=True, solid=False)
    sign(b, land, (centre[0] - c * 3.6 - s * 1.0, centre[1] - s * 3.6 + c * 1.0), look, 'xx_panotrav01', 2.2)


def bridge_planks(b, land, x):
    """A flat plank bridge across the river at x, on two pairs of posts."""
    y0, y1 = river_y(x) - 7.0, river_y(x) + 7.0
    z = max(land.height(x, y0), land.height(x, y1)) + 0.15
    deck = [(x - 1.7, y0, z - 0.4), (x + 1.7, y0, z - 0.4), (x + 1.7, y1, z - 0.4), (x - 1.7, y1, z - 0.4)]
    b.prism(deck, [(p[0], p[1], z) for p in deck], b.tex('bs_pon1nz'), cap=b.tex('bs_pon2nz'), base=b.tex('bs_pon1nz'),
            size=(3.4, 1.6))
    for dy in (-2.5, 2.5):
        for dx in (-1.5, 1.5):
            p = (x + dx, river_y(x) + dy)
            b.prism(rect(p, 0.35, 0.35, 0.0, -1.3), rect(p, 0.35, 0.35, 0.0, z - 0.4), b.tex('bs_pon1nz'), size=(0.5, 2.0))


def bridge_hump(b, land, x):
    """The crazy bridge: a humpback of pink and yellow cobbles (xx_pontfou01) with low stone parapets."""
    n, length, rise = 14, 17.0, 2.3
    y0 = river_y(x) - length / 2
    za, zb = land.height(x, y0), land.height(x, y0 + length)
    cobbles, stone = b.tex('xx_pontfou01'), b.tex('mr_pierrocre')

    def z_at(t):
        return za + (zb - za) * t + rise * math.sin(math.pi * t) + 0.1
    for k in range(n):
        t0, t1 = k / n, (k + 1) / n
        ya, yb = y0 + length * t0, y0 + length * t1
        top = [(x - 1.9, ya, z_at(t0)), (x + 1.9, ya, z_at(t0)), (x + 1.9, yb, z_at(t1)), (x - 1.9, yb, z_at(t1))]
        bottom = [(p[0], p[1], p[2] - 0.7) for p in top]
        b.prism(bottom, top, stone, cap=cobbles, base=stone, size=(2.0, 2.0))
        if 0 < k < n - 1:
            for side in (-1, 1):
                xa, xb = x + side * 1.9, x + side * 2.25
                low = [(xa, ya, z_at(t0) - 0.1), (xb, ya, z_at(t0) - 0.1), (xb, yb, z_at(t1) - 0.1), (xa, yb, z_at(t1) - 0.1)]
                if side < 0:
                    low = [low[1], low[0], low[3], low[2]]
                b.prism(low, [(p[0], p[1], p[2] + 0.75) for p in low], stone, cap=stone, size=(2.0, 2.0))


def plants(b, land, avoid, paths, rng):
    """Trees and bushes (crossed cut-out quads; a tree's trunk collides, nothing else does)."""
    tree, bushes = b.tex('vg_arbregeoNZ', 0), [b.tex('vg_buissongeoNZ', 0), b.tex('vg_buissongeo02NZ', 0)]
    placed = []

    def free(x, y, room):
        r = math.hypot(x, y)
        if r < 20 or r > cliff_radius(math.atan2(y, x)) - 4:
            return False
        if abs(y - river_y(x)) < 7.5 or any(polyline_distance((x, y), line) < 3.5 for line in paths):
            return False
        if any(math.dist((x, y), (ax, ay)) < ar + room for ax, ay, ar in avoid) or cc.outside(x, y) < room + 3:
            return False
        return all(math.dist((x, y), p) > room + 2 for p in placed)

    def crossed(key, x, y, w, h, turn):
        z = land.height(x, y) - 0.2
        # lit as the ground there, a little brighter towards the top (no normal of their own: cut-outs face both ways)
        lo, hi = (ig.baked_light(tuple(c * k for c in light((0.0, 0.0, 1.0), (x, y, z + dz))))
                  for k, dz in ((0.8, 0.3), (1.0, h)))
        lit = [lo, lo, hi, lo, hi, hi]
        for k in range(2):
            a = turn + k * math.pi / 2
            dx, dy = math.cos(a) * w / 2, math.sin(a) * w / 2
            pts = [(x - dx, y - dy, z), (x + dx, y + dy, z), (x + dx, y + dy, z + h), (x - dx, y - dy, z + h)]
            uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
            for j, t in enumerate(((0, 1, 2), (0, 2, 3))):
                b.tri(key, tuple(pts[i] for i in t), tuple(uvs[i] for i in t), lit=lit[3 * j:3 * j + 3], solid=False)
    for count, room, size in ((26, 3.5, None), (36, 1.5, None)):
        made, tries = 0, 0
        while made < count and tries < 4000:
            tries += 1
            a, r = rng.uniform(-math.pi, math.pi), rng.uniform(20, 78)
            x, y = r * math.cos(a), r * math.sin(a)
            if not free(x, y, room):
                continue
            placed.append((x, y))
            made += 1
            if room > 2:
                h = rng.uniform(6.0, 8.5)
                crossed(tree, x, y, h * 0.8, h, rng.uniform(0, math.pi))
                # up past the top: a trunk ending within reach would be a ledge Ed grabs (invisible, in mid-air)
                b.box_collision((x, y), 0.35, land.height(x, y) - 1, land.height(x, y) + h + 4.0)
            else:
                crossed(rng.choice(bushes), x, y, rng.uniform(2.2, 3.2), rng.uniform(1.4, 2.0), rng.uniform(0, math.pi))


def finish(src, dst, build_dir, level_dir):
    """The built .ttlv into the Special Edition's levels: its texture paths made relative to there, EDIT 1 (the
    Special Edition: the replays and the frontends' portals find its folder by it), the river's mesh scrolling
    (write_level writes no scroll: WATER_SCROLL, UV per second), the haze (LEVL's fog: FOG)."""
    c = chunks.read(src, 'TTLV')
    names = []
    o, (n,) = 4, struct.unpack_from('<I', c['TEXS'])
    for _ in range(n):
        size, = struct.unpack_from('<H', c['TEXS'], o)
        names.append(c['TEXS'][o + 2:o + 2 + size].decode())
        o += 2 + size
    names = [os.path.relpath(os.path.normpath(os.path.join(build_dir, t)), level_dir).replace(os.sep, '/') for t in names]
    # Crazy City's passers-by (crazycity.placements): records of the retail levels beside this one's build folder
    c['PRSO'] = cc.persos_chunk(os.path.join(os.path.dirname(os.path.abspath(build_dir)), 'ttlv'), level_dir, names)
    c['TEXS'] = struct.pack('<I', len(names)) + b''.join(struct.pack('<H', len(t.encode())) + t.encode() for t in names)
    mesh = bytearray(c['MESH'])
    count, = struct.unpack_from('<I', mesh)
    o = 4
    for _ in range(count):
        tex, _, _, _, frames, _ = struct.unpack_from('<iI2fHH', mesh, o)
        if 0 <= tex < len(names) and names[tex].endswith('/o_fouTXY.png'):
            struct.pack_into('<2f', mesh, o + 8, *WATER_SCROLL)
        o += 20 + 8 * frames
        nv, ni = struct.unpack_from('<II', mesh, o)
        o += 8 + 52 * nv + 4 * ni
    c['MESH'] = bytes(mesh)
    c['EDIT'] = struct.pack('<I', 1)
    c['LEVL'] = struct.pack('<I5f', 1, FOG[0], FOG[1], *FOG[2])  # the dusk's haze (the core: a level's fog)
    chunks.write(dst, 'TTLV', [(t, p) for t, p in c.items() if t != 'STMP'])


def retire(out):
    """Its files from where it was first made (RETIRED), when an earlier run left them."""
    for rel in RETIRED:
        path = os.path.join(out, rel)
        if os.path.isdir(path):
            shutil.rmtree(path)
        elif os.path.exists(path):
            os.remove(path)
        else:
            continue
        print(f'== Crazy Town: {rel} taken away (it is a Special Edition level now)')


def vignette(out, src):
    """The loading screen: the sunset, the far peaks along the bottom, retail's unused "Crazy Tower" sign over it."""
    w, h = 640, 480
    sky_img = read_png(os.path.join(src, 'CIEL12.png'))
    _, _, px = scaled(sky_img, w, h)
    img = (w, h, px)
    peaks = read_png(os.path.join(src, 'rendu6_NZ.png'))
    pw, ph, pp = peaks
    pp = bytearray(pp)
    for i in range(0, len(pp), 4):
        if pp[i] + pp[i + 1] + pp[i + 2] < 40:
            pp[i + 3] = 0
    for k in range(3):
        blend(img, (pw, ph, pp), (0, 0, pw, ph), (k * 256 - 64, h - 200), 1.6)
    sign_path = os.path.join(out, SIGN)
    if os.path.exists(sign_path):
        s = read_png(sign_path)
        box = opaque_box(s)
        k = 3.6
        blend(img, s, box, (int((w - (box[2] - box[0]) * k) / 2), 90), k)
    save_png(os.path.join(out, VIGNETTE), w, h, px)


def arrival():
    """The way back's arch (custom.py EDITS, pair 'crazy' in this level): (position, heading)."""
    e = next(e for e in EDITS if e.level == f'se/ttlv/{NAME}')
    return e.position, e.heading


def build(out):
    src = os.path.join(out, SRC)
    tex_dir = os.path.join(out, TEXTURES)
    os.makedirs(tex_dir, exist_ok=True)
    (ax, ay, _), heading = arrival()
    front = (math.sin(heading), -math.cos(heading))
    spawn_xy = (ax + front[0] * 5.5, ay + front[1] * 5.5)

    houses = [  # (centre, w, d, wall height, wall texture, twist, lean, top scale, roof, door)
        ((31.0, 7.0), 8.0, 7.0, 7.0, 'mr_maisonfou', 9.0, (0.9, -0.5), 1.15, 5.5, 'bs_murplanche2'),
        ((27.0, 29.0), 7.0, 7.5, 8.5, 'mr_fou1', -7.0, (-0.6, 0.9), 1.1, 6.0, 'bs_murplanche1'),
        ((5.0, 38.0), 7.5, 7.0, 6.5, 'mr_poisvert', 11.0, (1.0, 0.3), 1.2, 4.5, 'bs_murplanche1'),
        ((-21.0, 34.0), 8.5, 7.0, 7.5, 'mr_maisonfou', -10.0, (0.5, 1.0), 1.12, 6.5, 'bs_murplanche2'),
        ((-35.0, 11.0), 7.0, 8.0, 8.0, 'mr_fou4', 7.0, (-1.0, -0.4), 1.1, 5.0, 'bs_murplanche1'),
        ((-40.0, -11.0), 9.0, 7.0, 6.0, 'vache', -6.0, (0.7, 0.6), 1.18, 5.0, 'bs_murplanche2'),
        ((43.0, -8.0), 7.0, 7.0, 7.5, 'mr_pierrocre', 12.0, (-0.8, 0.5), 1.1, 6.0, 'bs_murplanche1'),
        ((-7.0, 57.0), 6.0, 6.0, 12.0, 'mr_fou2', -5.0, (1.6, 0.4), 1.2, 7.0, 'bs_murplanche2'),
        ((39.0, 45.0), 8.0, 7.0, 7.0, 'mr_fou5', 8.0, (0.4, -1.0), 1.12, 5.5, 'bs_murplanche1'),
    ]
    mill, privy, tub, disc = (-42.0, -50.0), (34.0, -52.0), (41.0, -41.0), (-17.0, 17.0)
    skull = (8.0, river_y(8.0) - 7.5)
    # the road east was closed for works; it leads to the old quarter now (crazyquarter.py), the works set aside
    works = (52.0, -9.5)
    pads = [(0.0, 0.0, 18.0, 22.0, 0.15), (ax, ay, 9.0, 14.0, 0.0), (mill[0], mill[1], 6.0, 9.0, None),
            (privy[0], privy[1], 2.5, 5.0, None), (tub[0], tub[1], 2.5, 5.0, None), (disc[0], disc[1], 8.0, 11.0, None),
            (cq.ARCH_X, -2.0, 4.5, 9.0, cq.ZA)] + cc.pads()
    pads += [(c[0], c[1], max(w, d) * 0.75, max(w, d) * 0.75 + 4.0, None) for c, w, d, *_ in houses]
    land = Land(pads)
    xr_w, xr_e = -24.0, 24.0
    paths = [
        [spawn_xy, (-12.0, -46.0), (xr_w, river_y(xr_w) - 8.5), (xr_w, river_y(xr_w) + 8.5), (-12.0, -13.0), (0.0, -10.0)],
        [spawn_xy, (12.0, -46.0), (xr_e, river_y(xr_e) - 9.5), (xr_e, river_y(xr_e) + 9.5), (12.0, -12.0), (0.0, -10.0)],
        [(17.0, 1.0), (40.0, 1.5), (60.0, 0.5), (70.0, -1.5), (cq.ARCH_X, 0.8)],
        [(-6.0, 16.0), (-7.0, 50.0)],
        [(12.0, -46.0), privy], [(-12.0, -46.0), mill],
    ] + cc.paths()

    # the lights before anything is lit: a lantern by each house's door and the tower's, lamps round the square and
    # along the road east, the old quarter's own
    lanterns, posts = [], [(15.5 * math.cos(a), 15.5 * math.sin(a)) for a in (0.6, 2.2, 3.8, 5.4)]
    posts += [(38.0, -1.8), (57.0, -2.6), (-14.0, -40.0), (14.0, -40.0)]
    for c, w, d, hgt, *_ in houses:
        f = norm((-c[0], -c[1], 0.0))
        sd = (-f[1], f[0])
        z = land.height(*c) + 2.9
        wall_pt = (c[0] + f[0] * d / 2 + sd[0] * 1.5, c[1] + f[1] * d / 2 + sd[1] * 1.5, z + 0.4)
        lanterns.append(((wall_pt[0] + f[0] * 0.9, wall_pt[1] + f[1] * 0.9, z), wall_pt))
    tc = tower_centre(0.0)
    for a in (math.radians(235), math.radians(260)):
        r = tower_radius(0.0) * math.cos(math.radians(22.5))
        wall_pt = (tc[0] + r * math.cos(a), tc[1] + r * math.sin(a), 3.7)
        lanterns.append(((tc[0] + (r + 0.8) * math.cos(a), tc[1] + (r + 0.8) * math.sin(a), 3.3), wall_pt))
    warm = cq.WARM
    LIGHT.land = land
    LIGHT.lamps = [(at, tuple(k * 1.1 for k in warm), 7.0) for at, _ in lanterns]
    LIGHT.lamps += [((x + 0.7, y, land.height(x, y) + 3.0), warm, 10.0) for x, y in posts]
    LIGHT.lamps += cq.lamps() + cc.lamps()
    LIGHT.covered = cq.covered()

    b = Builder(src, tex_dir)
    ground(b, land, paths)
    cliff_foot(b)
    river(b)
    sky(b)
    mountains(b)
    tower(b)
    for c, w, d, hgt, wall, twist, lean, top_scale, roof, door in houses:
        house(b, land, c, w, d, hgt, (0.0, 0.0), wall, twist, lean, top_scale, roof, (lean[0] * 0.5, lean[1] * 0.5), door)
    windmill(b, land, mill, spawn_xy)
    outhouse(b, land, privy, (12.0, -46.0))
    bathtub(b, land, tub, 0.6)
    cd(b, land, disc, (0.0, 0.0))
    sign(b, land, skull, (skull[0], skull[1] - 10.0), 'dc_panomort')
    roadworks(b, land, works, (0.0, 0.0))
    bridge_planks(b, land, xr_w)
    bridge_hump(b, land, xr_e)
    for at, wall_pt in lanterns:
        cq.lantern(b, at, wall_pt)
    for x, y in posts:
        cq.lamp_post(b, x, y, land.height(x, y))
    cq.build(b)
    cc.build(b, land)
    avoid = [(c[0], c[1], max(w, d)) for c, w, d, *_ in houses]
    avoid += [(mill[0], mill[1], 10.0), (privy[0], privy[1], 4.0), (tub[0], tub[1], 4.0), (disc[0], disc[1], 6.0),
              (ax, ay, 12.0), (skull[0], skull[1], 3.0), (works[0], works[1], 7.0)]
    plants(b, land, avoid, paths, random.Random(1999))

    spawn = (spawn_xy[0], spawn_xy[1], land.height(*spawn_xy) + 0.3)
    lo = [min(v[0][k] for g, (verts, _) in b.groups.items() if not g[1] & ROUND_CAMERA for v in verts) for k in range(3)]
    hi = [max(v[0][k] for g, (verts, _) in b.groups.items() if not g[1] & ROUND_CAMERA for v in verts) for k in range(3)]
    build_dir, level_dir = os.path.join(out, BUILD), os.path.join(out, LEVELS)
    os.makedirs(build_dir, exist_ok=True)
    os.makedirs(level_dir, exist_ok=True)
    # Ed faces along the arch's front, into the town (write_level: radians from +X)
    ig.write_level(NAME, b.groups, b.tris, spawn, donor=os.path.join('..', 'ttlv', DONOR),
                   heading=math.atan2(front[1], front[0]), textures=len(b.copied), bounds=(lo, hi), ttlv_dir=build_dir)
    finish(os.path.join(build_dir, NAME + '.ttlv'), os.path.join(level_dir, NAME + '.ttlv'), build_dir, level_dir)
    os.replace(os.path.join(build_dir, NAME + '.ttai'), os.path.join(level_dir, NAME + '.ttai'))
    shutil.rmtree(build_dir)
    vignette(out, src)


def record(out):
    """What CrazyTown.ttlv is made from: this code, the donor's stamp, the textures' names and sizes."""
    h = hashlib.sha256(chunks.kind_stamp(KIND).encode())
    h.update(str(chunks.file_stamp(os.path.join(out, 'ttlv', DONOR + '.ttlv'))).encode())
    src = os.path.join(out, SRC)
    for f in sorted(os.listdir(src)):
        h.update(f'{f} {os.path.getsize(os.path.join(src, f))}\n'.encode())
    return h.hexdigest()[:16]


def main(argv):
    out = argv[0] if argv else 'files/out'
    if not os.path.isdir(os.path.join(out, SRC)):
        print(f'== Crazy Town: needs the Special Edition\'s textures ({os.path.join(out, SRC)}) (optional, skip)')
        return
    if not os.path.exists(os.path.join(out, 'ttlv', DONOR + '.ttlv')):
        print(f'== Crazy Town: needs the retail levels (Ed comes from {DONOR}) (skip)')
        return
    retire(out)
    stamp = os.path.join(out, '.stamps', 'crazytown')
    want = record(out)
    level = os.path.join(out, LEVELS, NAME + '.ttlv')
    if (os.path.exists(level) and os.path.isdir(os.path.join(out, TEXTURES)) and os.path.exists(stamp)
            and open(stamp).read().strip() == want):
        if not os.path.exists(os.path.join(out, VIGNETTE)):
            vignette(out, os.path.join(out, SRC))
            print('== Crazy Town: current; its loading screen made again')
        else:
            print('== Crazy Town: current (skip)')
        return
    print('== Crazy Town')
    chunks.KIND = KIND
    build(out)
    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    with open(stamp, 'w') as f:
        f.write(want + '\n')


if __name__ == '__main__':
    # through the package's own module: crazyquarter imports it, and two copies would keep two lights
    from tools.ttre import crazytown
    crazytown.main(sys.argv[1:])

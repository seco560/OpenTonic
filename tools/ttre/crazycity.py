"""Crazy City (2026-11-22): Crazy Town's neighbourhoods beyond its cliffs (crazytown.py builds them into the level).

The user asked for "a bunch of neighbourhoods, almost as if it was Crazy City", with more flair. The town and its old
quarter (crazyquarter.py) stay as they were; four districts more lie past the cliff ring, each reached by a cut
through the rock from the town:

- the Strip (west): a cove with a neon promenade, a beach, a Ferris wheel, a lagoon, a pier and a lighthouse whose
  stair winds three times round it to a lamp room;
- the Heights (north): a stair up the cliff to three terraces: a gallery row with an open-air screen, a lane of villas
  with pools, the overlook with a radio mast, an observatory and the town's name in huge letters on the hill;
- Downtown (south, beside the arrival arch): a grid of crooked towers along asphalt streets, bath-tub cars parked at the
  kerbs, a plaza with a clock tower, a park, a building site to climb and a tower with a fire stair to its roof;
- the Junkyard (south-east): a scrapyard behind barbed wire, crate stacks to jump up, a crane holding a bath tub, junk
  piles of the town's own odd things.

All of it is ours: nothing of the cut level survives (docs/SE.md "Crazy Town"). Its textures are the Special
Edition's Crazy Town ones, pictures painted here (crazypaint), and some of the retail game's (out/Textures: Grogh's TV,
the fireworks, Transit's coloured tiles, Marmite's crates and portholes, Land's towers): the install needs nothing but
the game. A district is a union of rectangles (its outline): inside it the town's height field goes out of sight under
its own floors (crazytown.Land) and the cliffs give way; past the cliff ring the land round it rises to a bank over
its floor and then the rock, so its edges are slopes, not drops into a trench.
"""
import math
import os
import random
import shutil
import struct

from . import chunks
from . import crazypaint as cp
from . import crazyquarter as cq
from . import crazytown as ct
from . import import_gltf as ig

Z = 0.6  # where each district meets the town (the town's ground is about 0 there; a pad flattens it to this)
WARM = (1.0, 0.6, 0.28)
PINK, CYAN, VIOLET, LIME, AMBER, WHITE = (1.0, 0.25, 0.7), (0.25, 0.85, 1.0), (0.6, 0.3, 1.0), (0.5, 1.0, 0.35), \
    (1.0, 0.7, 0.2), (0.9, 0.95, 1.0)
GLOW = (1.9, 1.9, 1.9)


# ---- textures ----

def retail(b, rel, flags=None):
    """A texture of the retail game's (out/Textures/<rel>.png), copied into the level's folder: cut out when it has
    transparent pixels."""
    name = 'rt_' + rel.replace('/', '_')
    cut = b.__dict__.setdefault('retail_cut', {})
    if name not in b.copied:
        out = os.path.normpath(os.path.join(b.src, '..', '..', '..'))
        src = os.path.join(out, 'Textures', rel + '.png')
        dst = os.path.join(b.tex_dir, name + '.png')
        shutil.copyfile(src, dst)
        w, h, px = ct.read_png(dst)
        b.copied[name] = dst
        cut[name] = any(px[i] < 128 for i in range(3, len(px), 4))
    if flags is None:
        flags = ct.WRAP | (ig.ALPHA_TEST if cut[name] else 0)
    return b.copied[name], flags


def paint(b, name, picture, flags=None):
    """A picture painted here (crazypaint), into the level's folder."""
    return cq.own(b, 'cc_' + name, picture, ct.WRAP if flags is None else flags)


AWNINGS = [((240, 120, 170), (250, 245, 235)), ((70, 200, 210), (250, 245, 235)), ((250, 200, 60), (90, 50, 140))]


def awning_cloth(b, k):
    return paint(b, f'awning{k % 3}', lambda: cp.stripes(*AWNINGS[k % 3]))


def glowing(b, key, pts, rgb=GLOW):
    """A face lit full (signs, screens, windows at night): no light of the scene's."""
    b.face(key, pts, fit=True, solid=False, lit=ig.baked_light(rgb))


def upright(a, c, z0, z1, out=0.0):
    """The corners of an upright rectangle from a to c (x, y), z0 to z1, its face to the right of a -> c, pushed out
    along that face by `out`."""
    d = (c[0] - a[0], c[1] - a[1])
    n = math.hypot(*d) or 1.0
    o = (d[1] / n * out, -d[0] / n * out)
    return [(a[0] + o[0], a[1] + o[1], z0), (c[0] + o[0], c[1] + o[1], z0), (c[0] + o[0], c[1] + o[1], z1),
            (a[0] + o[0], a[1] + o[1], z1)]


def crossed(b, key, x, y, z, w, h, turn=0.0, solid_trunk=False):
    """A plant: two crossed cut-outs, lit as the ground there; solid_trunk: a trunk Ed bumps into, up past its top (a
    collision ending within his reach would be a ledge he grabs in mid-air)."""
    lo, hi = (ig.baked_light(tuple(c * k for c in ct.light((0.0, 0.0, 1.0), (x, y, z + dz)))) for k, dz in
              ((0.8, 0.3), (1.0, h)))
    for k in range(2):
        a = turn + k * math.pi / 2
        dx, dy = math.cos(a) * w / 2, math.sin(a) * w / 2
        pts = [(x - dx, y - dy, z), (x + dx, y + dy, z), (x + dx, y + dy, z + h), (x - dx, y - dy, z + h)]
        uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
        b.tri(key, (pts[0], pts[1], pts[2]), (uvs[0], uvs[1], uvs[2]), lit=[lo, lo, hi], solid=False)
        b.tri(key, (pts[0], pts[2], pts[3]), (uvs[0], uvs[2], uvs[3]), lit=[lo, hi, hi], solid=False)
    if solid_trunk:
        b.box_collision((x, y), 0.35, z - 1.0, z + h + 4.0)


def railing(b, key, a, c, z, height=1.0, thick=0.12):
    """A solid rail from a to c at floor height z (a low wall: Ed jumps over it, he does not walk off)."""
    d = (c[0] - a[0], c[1] - a[1])
    n = math.hypot(*d) or 1.0
    o = (-d[1] / n * thick / 2, d[0] / n * thick / 2)
    bottom = [(a[0] - o[0], a[1] - o[1], z), (c[0] - o[0], c[1] - o[1], z), (c[0] + o[0], c[1] + o[1], z),
              (a[0] + o[0], a[1] + o[1], z)]
    b.prism(bottom, [(p[0], p[1], z + height) for p in bottom], key, cap=key, size=(1.0, 1.0))


def spiral_stair(b, centre, half, z0, laps, start_side=3, rail=None, width=1.2):
    """A stair winding round a square tower (half its side), outside it: on each side 5 steps (RISE up, a tread of
    2 half / 5), at each corner a landing; laps x 20 steps from z0. Sides: 0 south (west to east), 1 east, 2 north,
    3 west, counter-clockwise from above; start_side: the side its first step is on (it starts from the corner before
    it). rail: a texture for the rail along its outer edge. Returns the top's height."""
    cx, cy = centre
    lo, hi = half, half + width
    z = z0
    corner = [(-1, -1), (1, -1), (1, 1), (-1, 1)]  # the corner each side starts from
    outward = [(0, -1), (1, 0), (0, 1), (-1, 0)]
    for k in range(4 * laps):
        s = (start_side + k) % 4
        sx, sy = corner[s]
        ex, ey = corner[(s + 1) % 4]
        along = ((ex - sx) / 2, (ey - sy) / 2)
        out = outward[s]
        tread = 2 * half / 5
        for i in range(5):
            z += cq.RISE
            t0, t1 = -half + tread * i, -half + tread * (i + 1) + 0.02
            p = [(cx + along[0] * t + out[0] * r, cy + along[1] * t + out[1] * r) for t, r in
                 ((t0, lo), (t1, lo), (t1, hi), (t0, hi))]
            cq.step(b, min(q[0] for q in p), min(q[1] for q in p), max(q[0] for q in p), max(q[1] for q in p), z)
            if rail:
                railing(b, rail, (cx + along[0] * t0 + out[0] * (hi - 0.06), cy + along[1] * t0 + out[1] * (hi - 0.06)),
                        (cx + along[0] * t1 + out[0] * (hi - 0.06), cy + along[1] * t1 + out[1] * (hi - 0.06)), z)
        # the landing at the corner this side ends in
        qx, qy = cx + ex * (lo + width / 2), cy + ey * (lo + width / 2)
        cq.step(b, qx - width / 2, qy - width / 2, qx + width / 2, qy + width / 2, z)
        if rail:
            for d in ((ex, 0), (0, ey)):
                mx, my = qx + d[0] * (width / 2 - 0.06), qy + d[1] * (width / 2 - 0.06)
                railing(b, rail, (mx - abs(d[1]) * width / 2, my - abs(d[0]) * width / 2),
                        (mx + abs(d[1]) * width / 2, my + abs(d[0]) * width / 2), z)
    return z


def roof_parapets(b, key, centre, half, top, start_side, laps, thick=0.2):
    """Parapets round a spiral-stair tower's roof, but on the side its stair comes up beside (the last one)."""
    cx, cy = centre
    last = (start_side + 4 * laps - 1) % 4
    corner = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    for s in range(4):
        if s != last:
            (ax, ay), (bx, by) = corner[s], corner[(s + 1) % 4]
            railing(b, key, (cx + ax * half, cy + ay * half), (cx + bx * half, cy + by * half), top, 1.0, thick)


def stairs_y(b, x0, x1, y0, z0, n, dy=1.0):
    """n steps from y0 along +y (dy 1) or -y (-1), the first one RISE over z0, TREAD deep. Returns (end y, top z)."""
    y, z = y0, z0
    for _ in range(n):
        z += cq.RISE
        y1 = y + dy * cq.TREAD
        cq.step(b, x0, min(y, y1) - (0.02 if dy < 0 else 0), x1, max(y, y1) + (0.02 if dy > 0 else 0), z)
        y = y1
    return y, z


def terrace(b, key, x0, y0, x1, y1, top, side=None, base=-1.5):
    """A raised floor: a block from under the land to `top`, its sides retaining walls."""
    cq.box(b, side or b.tex('mr_pierrocre'), x0, y0, x1, y1, base, top, cap=key, size=(3.0, 3.0))


def ramp(b, key, x0, x1, y0, y1, z0, z1, side=None):
    """A slope from (y0, z0) to (y1, z1) between x0 and x1, on a block."""
    if y1 < y0:
        y0, y1, z0, z1 = y1, y0, z1, z0
    bottom = [(x0, y0, -1.5), (x1, y0, -1.5), (x1, y1, -1.5), (x0, y1, -1.5)]
    top = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z1), (x0, y1, z1)]
    b.prism(bottom, top, side or b.tex('mr_pierrocre'), cap=key, size=(3.0, 3.0))


def ring_pts(centre, r, n, z, tilt=(0.0, 0.0)):
    """A ring's points, its plane tipped by tilt (dz per unit x, per unit y)."""
    return [(centre[0] + r * math.cos(2 * math.pi * k / n), centre[1] + r * math.sin(2 * math.pi * k / n),
             z + tilt[0] * r * math.cos(2 * math.pi * k / n) + tilt[1] * r * math.sin(2 * math.pi * k / n))
            for k in range(n)]


def sign_on(b, house, k, u, z, w, h, key, rgb=GLOW, out=0.12):
    """A flat sign on a cq.House's side k, centred at u along it (0..1), its bottom at z."""
    hw = w / 2 / house.width(k)
    glowing(b, key, house.face_quad(k, u - hw, u + hw, z, z + h, out), rgb)


def awning(b, key, house, k, u, z, w, depth=1.4):
    """A striped awning out from a house's side over its door: sloping down away from the wall."""
    hw = w / 2 / house.width(k)
    n = house.normal(k)
    a, c = house.face_point(k, u - hw, z, 0.05), house.face_point(k, u + hw, z, 0.05)
    a2 = (a[0] + n[0] * depth, a[1] + n[1] * depth, z - 0.6)
    c2 = (c[0] + n[0] * depth, c[1] + n[1] * depth, z - 0.6)
    for pts in ([a2, c2, c, a], [a, c, c2, a2]):  # both sides: seen from the street and from under it
        b.face(key, pts, fit=True, solid=False)


def bathtub_car(b, at, angle, colour_seed):
    """A bath tub on four wheels (the town has a tub in a field: here they drive): a tiled tub, black wheels, two lamps."""
    x, y, z = at
    c, s = math.cos(angle), math.sin(angle)

    def p(u, v, w):
        return (x + c * u - s * v, y + s * u + c * v, z + w)
    tub = b.tex('xx_baignoire')
    body = [p(-1.9, -0.9, 0.35), p(1.9, -0.9, 0.35), p(1.9, 0.9, 0.35), p(-1.9, 0.9, 0.35)]
    top = [p(-2.1, -1.05, 1.35), p(2.1, -1.05, 1.35), p(2.1, 1.05, 1.35), p(-2.1, 1.05, 1.35)]
    b.prism(body, top, tub, cap=paint(b, f'tubseat{colour_seed % 3}',
                                      lambda: cp.checker(*[((240, 110, 170), (250, 240, 230)), ((70, 200, 210),
                                                           (250, 240, 230)), ((250, 200, 60), (60, 60, 80))][colour_seed % 3])),
            size=(4.2, 1.0), fit=True)
    rubber = b.tex('pt_aciervf')
    for u in (-1.3, 1.3):
        for v in (-1.05, 1.05):
            wc = p(u, v, 0.45)
            ring = [(wc[0] + c * 0.45 * math.cos(t), wc[1] + s * 0.45 * math.cos(t), wc[2] + 0.45 * math.sin(t))
                    for t in (2 * math.pi * k / 8 for k in range(8))]
            side = [(q[0] - s * 0.12, q[1] + c * 0.12, q[2]) for q in ring]
            b.prism(ring, side, rubber, cap=rubber, base=rubber, size=(0.5, 0.5), solid=False)
    lamp = b.tex('X_Ampdc', 0)
    for v in (-0.6, 0.6):
        f = p(2.12, v, 1.0)
        glowing(b, lamp, [(f[0] - s * 0.2, f[1] + c * 0.2, f[2] - 0.2), (f[0] + s * 0.2, f[1] - c * 0.2, f[2] - 0.2),
                          (f[0] + s * 0.2, f[1] - c * 0.2, f[2] + 0.2), (f[0] - s * 0.2, f[1] + c * 0.2, f[2] + 0.2)])


def firework(b, key, at, size):
    """A burst over the roofs: a picture facing every way (three crossed squares), lit full."""
    x, y, z = at
    for k in range(3):
        a = k * math.pi / 3
        dx, dy = math.cos(a) * size / 2, math.sin(a) * size / 2
        glowing(b, key, [(x - dx, y - dy, z - size / 2), (x + dx, y + dy, z - size / 2), (x + dx, y + dy, z + size / 2),
                         (x - dx, y - dy, z + size / 2)])


# ---- a district ----

class District:
    """A neighbourhood: its outline (rects: x0, y0, x1, y1), its floor's height anywhere in it (floor_at), the pads and
    paths it asks of the town's land (the way to its gate), its lights, and build(b, land) for its meshes."""
    rects = ()
    pads = ()   # crazytown.Land pads: (x, y, inner radius, outer radius, height)
    paths = ()  # crazytown path polylines (the sand paths in the town's grass)

    def outside(self, x, y):
        best = math.inf
        for x0, y0, x1, y1 in self.rects:
            best = min(best, math.hypot(max(x0 - x, 0.0, x - x1), max(y0 - y, 0.0, y - y1)))
        return best

    def nearest_point(self, x, y):
        best, at = math.inf, (x, y)
        for x0, y0, x1, y1 in self.rects:
            q = (min(max(x, x0), x1), min(max(y, y0), y1))
            d = math.dist(q, (x, y))
            if d < best:
                best, at = d, q
        return at

    def floor_at(self, x, y):
        return Z

    def near_floor(self, x, y):
        """The floor at the outline's nearest point (the land's bank outside it rises over this)."""
        return self.floor_at(*self.nearest_point(x, y))

    def lamps(self):
        return []

    def build(self, b, land):
        pass


# ---- the Strip (west) ----

class Strip(District):
    """A cove through the west cliffs: the cut (GATE) opens onto a promenade (PROM) under a row of signs and fronts
    (FRONTS); south of it the beach (BEACH) slopes into the lagoon (LAGOON, wading deep), a pier runs out over it to a
    round head with the lighthouse."""
    GATE = (-90.0, -4.5, -70.0, 4.5)
    PROM = (-152.0, 4.5, -90.0, 14.0)
    FRONTS = (-152.0, 14.0, -90.0, 30.0)
    BEACH = (-152.0, -24.0, -90.0, 4.5)
    LAGOON = (-170.0, -78.0, -84.0, -24.0)
    rects = (GATE, PROM, FRONTS, BEACH, LAGOON)
    BED = -1.2
    PIER = (-121.0, -115.0)          # its deck's x
    PIER_Z = 1.4
    PIER_END = -57.0                 # where the deck meets the head
    HEAD, HEAD_R = (-118.0, -64.0), 7.5
    LIGHTHOUSE_HALF = 2.2
    WHEEL = (-104.0, -6.0)
    PIER_LAMPS = (-50.0, -38.0, -26.0, -14.0)
    pads = ((-67.0, 0.0, 4.0, 9.0, Z),)
    paths = ([(-17.0, 1.0), (-45.0, 1.0), (-68.0, 0.0)],)

    def beach_z(self, y):
        t = min(1.0, max(0.0, (self.BEACH[3] - y) / (self.BEACH[3] - self.BEACH[1])))
        return Z + (self.BED - Z) * t

    def on_pier(self, x, y):
        if self.PIER[0] <= x <= self.PIER[1] and self.PIER_END <= y <= 0.5:
            return True
        return math.dist((x, y), self.HEAD) <= self.HEAD_R

    def floor_at(self, x, y):
        if self.on_pier(x, y):
            return self.PIER_Z
        if self.PIER[0] <= x <= self.PIER[1] and 0.5 < y <= self.BEACH[3]:  # the pier's ramp
            return Z + (self.PIER_Z - Z) * (self.BEACH[3] - y) / (self.BEACH[3] - 0.5)
        if y < self.BEACH[1]:
            return self.BED
        if self.BEACH[0] <= x <= self.BEACH[2] and y < self.BEACH[3]:
            return self.beach_z(y)
        return Z  # the gate, the promenade, the fronts

    def lamps(self):
        out = []
        for k, x in enumerate(range(-146, -90, 12)):
            out.append(((x + 0.7, 13.0, Z + 3.0), WARM, 10.0))
            out.append(((x + 6.0, 15.0, Z + 4.5), (PINK, CYAN, VIOLET, LIME, AMBER)[k % 5], 9.0))
        out += [((self.PIER[1] + 0.2, y, self.PIER_Z + 3.0), WARM, 10.0) for y in self.PIER_LAMPS]
        hx, hy = self.HEAD
        out.append(((hx, hy, self.top + 1.4), (1.4, 1.2, 0.8), 34.0))  # the lighthouse's lamp, over the lagoon
        wx, wy = self.WHEEL
        out += [((wx - 4, wy + 3, 6.0), PINK, 12.0), ((wx + 4, wy - 3, 10.0), CYAN, 12.0)]
        out.append(((-80.0, 0.0, Z + 7.0), PINK, 11.0))  # the gate's sign
        return out

    @property
    def top(self):
        return self.PIER_Z + 3 * 20 * cq.RISE

    def build(self, b, land):
        self.gate(b)
        self.promenade(b)
        self.fronts(b)
        self.beach(b)
        self.lagoon(b)
        self.pier(b)
        self.lighthouse(b)
        self.wheel(b)
        self.sky(b)

    def gate(self, b):
        x0, y0, x1, y1 = self.GATE
        tiles = paint(b, 'pinktiles', lambda: cp.checker((240, 110, 170), (250, 240, 230)))
        terrace(b, tiles, x0, y0, x1, y1, Z, side=b.tex('rc_doc03'))
        rock = b.tex('rc_doc03')
        cq.wall(b, rock, (x0, y1 + 0.6), (x1, y1 + 0.6), -1.0, 22.0, tile=6.0, step=4.0)
        cq.wall(b, rock, (x1, y0 - 0.6), (x0, y0 - 0.6), -1.0, 22.0, tile=6.0, step=4.0)
        # an arch of two crooked pillars and a sign across them
        for y in (y0 + 0.4, y1 - 1.4):
            cq.box(b, b.tex('mr_fou3'), -79.5, y, -78.5, y + 1.0, Z, Z + 7.2, size=(1.0, 2.0))
        sign = paint(b, 'neon_strip', lambda: cp.neon('THE STRIP', (255, 60, 200), 512, 96))
        for xs, want in ((-78.4, 1.0), (-79.6, -1.0)):  # its two faces: towards the town and towards the cove
            pts = [(xs, y0 + 0.4, Z + 6.6), (xs, y1 - 0.4, Z + 6.6), (xs, y1 - 0.4, Z + 8.6), (xs, y0 + 0.4, Z + 8.6)]
            glowing(b, sign, pts if want > 0 else list(reversed(pts)))

    def promenade(self, b):
        x0, y0, x1, y1 = self.PROM
        tiles = paint(b, 'promtiles', lambda: cp.checker((70, 200, 210), (250, 240, 230), cells=2))
        terrace(b, tiles, x0, y0, x1, y1, Z)
        terrace(b, b.tex('mr_pierrocre'), x0, self.FRONTS[1], x1, self.FRONTS[3], Z)
        palm = b.tex('vg_arbregeoNZ', 0)
        for x in range(-146, -90, 12):
            cq.lamp_post(b, x, 13.0, Z)
            crossed(b, palm, x + 6.0, 5.8, Z, 5.0, 7.5, turn=x * 0.3, solid_trunk=True)
            cq.box(b, b.tex('bs_murplanche2'), x + 2.0, 12.2, x + 4.2, 12.8, Z, Z + 0.5, size=(1.0, 1.0))  # a bench

    def fronts(self, b):
        """The row facing the promenade: (x0, x1, top, wall, sign, glow colour, roof thing)."""
        rows = [
            (-152.0, -137.0, 28.0, 'mr_maisonfou', 'HOTEL', (255, 210, 80), 'tv'),
            (-137.0, -125.0, 11.0, 'mr_fou3', 'ARCADE', (255, 60, 200), 'wheel'),
            (-125.0, -116.0, 8.5, 'mr_poisvert', 'ICE CREAM', (90, 230, 255), 'cone'),
            (-116.0, -104.0, 9.5, 'mr_fou5', 'DINER', (255, 120, 60), 'cup'),
            (-104.0, -90.0, 12.0, 'mr_fou2', 'DISCO', (190, 90, 255), 'cd'),
        ]
        y0, y1 = self.FRONTS[1] + 0.6, self.FRONTS[3] - 1.0
        for k, (xa, xb, top, wall, word, glow, thing) in enumerate(rows):
            h = cq.House(b, (xa + 0.3, y0, xb - 0.3, y1), -1.0, Z + top, wall, 300 + k, roof=thing != 'tv',
                         upright=Z + 5.5)
            h.windows(b, 0, Z + 3.2, 8, 310 + k, skip={(0, 1)})
            h.door(b, 0, 0.5, Z, 'bs_murplanche2')
            awning(b, awning_cloth(b, k), h, 0, 0.5, Z + 3.2, 3.6)
            sign = paint(b, f'neon_{word.lower().replace(" ", "")}', lambda w=word, g=glow: cp.neon(w, g, 384, 96))
            sign_on(b, h, 0, 0.5, Z + 4.0, min(10.0, h.width(0) - 1.0), 2.4, sign)
            cx, cy = 0.5 * (xa + xb), 0.5 * (y0 + y1)
            roof = Z + top
            if thing == 'tv':  # the hotel's roof: a huge television showing Grogh
                cq.box(b, b.tex('pt_aciervf'), cx - 0.3, cy - 0.3, cx + 0.3, cy + 0.3, roof - 1.0, roof + 4.0)
                screen = retail(b, 'grog_tv/grog3', ct.WRAP)
                frame = b.tex('pt_aciervf')
                cq.box(b, frame, cx - 5.5, cy - 0.6, cx + 5.5, cy + 0.6, roof + 4.0, roof + 11.0, size=(2.0, 2.0),
                       solid=False)
                glowing(b, screen, upright((cx - 5.0, cy - 0.65), (cx + 5.0, cy - 0.65), roof + 4.5, roof + 10.5))
                vertical = paint(b, 'banner_hotel', lambda: cp.banner('HOTEL', (40, 20, 70), (255, 210, 80), 9))
                glowing(b, vertical, h.face_quad(0, 0.02, 0.16, Z + 9.0, Z + 24.0, 0.15))
            elif thing == 'cone':  # the ice cream parlour's giant cone, upside down on its roof as if dropped
                cq.cylinder(b, b.tex('bs_murplanche1'), (cx, cy), 0.2, roof + 4.0, roof + 9.0, 12, r1=2.2, solid=False)
                for i, (r, dz) in enumerate(((2.4, 9.0), (2.0, 10.4), (1.3, 11.5))):
                    cq.cylinder(b, paint(b, f'scoop{i}', lambda i=i: cp.checker(*[((250, 170, 200), (240, 150, 190)),
                                                                                 ((250, 240, 210), (240, 225, 190)),
                                                                                 ((120, 70, 40), (100, 60, 35))][i], 6)),
                                (cx, cy), r, roof + dz, roof + dz + 1.4, 14, cap=None, solid=False, r1=r * 0.85)
            elif thing == 'cd':
                b.face(b.tex('xx_cd_NZ', 0), upright((cx - 4.0, cy), (cx + 4.0, cy), roof + 5.0, roof + 13.0), fit=True,
                       solid=False)
                b.face(b.tex('xx_cd_NZ', 0), upright((cx + 4.0, cy), (cx - 4.0, cy), roof + 5.0, roof + 13.0), fit=True,
                       solid=False)
            elif thing == 'cup':  # a coffee cup the size of a shed, steaming
                cq.cylinder(b, paint(b, 'cupglaze', lambda: cp.checker((250, 250, 245), (240, 70, 90), 8)), (cx, cy),
                            2.0, roof + 4.0, roof + 7.0, 14, r1=2.6, solid=False)
                b.face(b.tex('brumeTXY', ig.TRANSPARENT | ct.WRAP), upright((cx - 2.0, cy), (cx + 2.0, cy), roof + 7.0,
                                                                          roof + 11.0), fit=True, solid=False)
            elif thing == 'wheel':  # the arcade's: a joystick
                cq.cylinder(b, b.tex('pt_aciervf'), (cx, cy), 0.35, roof + 3.5, roof + 9.0, 8, solid=False)
                knob = paint(b, 'redknob', lambda: cp.checker((230, 40, 50), (200, 30, 40), 2))
                cq.cylinder(b, knob, (cx, cy), 1.3, roof + 9.0, roof + 10.6, 12, cap=knob, solid=False, r1=0.9)

    def beach(self, b):
        """The sand, sloping from the promenade into the lagoon (a solid mesh), parasols and a sandcastle to climb."""
        sand = b.tex('hb_sable')
        x0, y0, x1, y1 = self.BEACH
        step = 2.5
        nx, ny = int((x1 - x0) / step), int((y1 - y0) / step)
        for i in range(nx):
            for j in range(ny):
                xa, xb = x0 + i * (x1 - x0) / nx, x0 + (i + 1) * (x1 - x0) / nx
                ya, yb = y0 + j * (y1 - y0) / ny, y0 + (j + 1) * (y1 - y0) / ny
                pts = [(xa, ya, self.beach_z(ya)), (xb, ya, self.beach_z(ya)), (xb, yb, self.beach_z(yb)),
                       (xa, yb, self.beach_z(yb))]
                uv = [(p[0] / 6.0, -p[1] / 6.0) for p in pts]
                b.tri(sand, (pts[0], pts[1], pts[2]), (uv[0], uv[1], uv[2]))
                b.tri(sand, (pts[0], pts[2], pts[3]), (uv[0], uv[2], uv[3]))
        pole = b.tex('pt_aciervf')
        rng = random.Random(77)
        for k, (x, y) in enumerate(((-146.0, -2.0), (-138.0, 0.5), (-129.0, -3.0), (-111.0, -1.0), (-95.0, -3.5))):
            z = self.beach_z(y)
            cq.cylinder(b, pole, (x, y), 0.08, z - 0.3, z + 2.6, 6)
            cloth = paint(b, f'parasol{k % 3}', lambda k=k: cp.stripes(*[((240, 70, 90), (250, 245, 235)),
                                                                        ((70, 150, 230), (250, 245, 235)),
                                                                        ((250, 200, 60), (240, 110, 170))][k % 3], count=8))
            rim = ring_pts((x, y), 1.8, 10, z + 2.2, (rng.uniform(-0.15, 0.15), rng.uniform(-0.15, 0.15)))
            for i in range(10):
                p, q = rim[i], rim[(i + 1) % 10]
                b.face(cloth, [p, q, (x, y, z + 2.8)], fit=True, solid=False)
                b.face(cloth, [(x, y, z + 2.8), q, p], fit=True, solid=False)
            cq.box(b, paint(b, f'towel{k % 2}', lambda k=k: cp.stripes(*[((240, 120, 170), (250, 245, 235)),
                                                                        ((90, 200, 120), (250, 245, 235))][k % 2], count=5)),
                   x + 0.6, y - 2.6, x + 1.6, y - 0.6, z - 0.2, z + 0.02, size=(1.0, 2.0), solid=False)
        # a sandcastle: three drums of sand, each one jump up, a flag on the keep
        cx, cy = -133.0, -12.0
        z = self.beach_z(cy)
        for r, h0, h1 in ((3.6, -0.6, 1.0), (2.4, 1.0, 2.1), (1.3, 2.1, 3.2)):
            cq.cylinder(b, sand, (cx, cy), r, z + h0, z + h1, 14, cap=sand, size=(2.0, 1.2))
            for k in range(7):
                a = 2 * math.pi * k / 7
                px, py = cx + (r - 0.25) * math.cos(a), cy + (r - 0.25) * math.sin(a)
                cq.box(b, sand, px - 0.25, py - 0.25, px + 0.25, py + 0.25, z + h1, z + h1 + 0.4, solid=False)
        cq.cylinder(b, pole, (cx, cy), 0.05, z + 3.2, z + 5.4, 5, solid=False)
        b.face(paint(b, 'flag', lambda: cp.banner('!', (240, 70, 90), (255, 240, 120), 3, 64, 64)),
               upright((cx, cy), (cx + 1.6, cy), z + 4.4, z + 5.4), fit=True, solid=False)

    def lagoon(self, b):
        x0, y0, x1, y1 = self.LAGOON
        cq.floor(b, b.tex('hb_sable'), x0, y0, x1, y1, self.BED, tile=6.0, step=6.0)
        water = b.tex('o_fouTXY', ct.WRAP | ig.TRANSPARENT)
        lit = ig.baked_light((0.62, 0.56, 0.84), 200)
        water_y = self.BEACH[3] - (Z - ct.WATER_Z) / (Z - self.BED) * (self.BEACH[3] - self.BEACH[1])
        for qx0, qy0, qx1, qy1 in ((x0, y0, x1, y1), (self.BEACH[0], y1, self.BEACH[2], water_y)):
            for i in range(int((qx1 - qx0) / 10.0) + 1):
                xa, xb = qx0 + i * 10.0, min(qx1, qx0 + (i + 1) * 10.0)
                if xb <= xa:
                    continue
                b.face(water, [(xa, qy0, ct.WATER_Z), (xb, qy0, ct.WATER_Z), (xb, qy1, ct.WATER_Z),
                               (xa, qy1, ct.WATER_Z)], (6.0, 6.0), solid=False, lit=lit)
        # a bath tub afloat, the town's runaway; stepping stones to it from the pier
        tub = b.tex('xx_baignoire')
        tx, ty = -140.0, -46.0
        b.prism([(tx - 3.2, ty - 1.4, self.BED), (tx + 3.2, ty - 1.4, self.BED), (tx + 3.2, ty + 1.4, self.BED),
                 (tx - 3.2, ty + 1.4, self.BED)],
                [(tx - 3.6, ty - 1.7, 0.6), (tx + 3.6, ty - 1.7, 0.6), (tx + 3.6, ty + 1.7, 0.6), (tx - 3.6, ty + 1.7, 0.6)],
                tub, cap=paint(b, 'tubfloor', lambda: cp.checker((250, 250, 245), (200, 230, 240), 6)), size=(7.2, 2.0),
                fit=True)
        for k, (x, y) in enumerate(((-124.5, -40.0), (-128.0, -43.5), (-131.5, -41.0), (-135.0, -44.5))):
            cq.cylinder(b, b.tex('rc_doc05'), (x, y), 1.1, self.BED, 0.35 + 0.15 * k, 9, cap=b.tex('rc_doc05'))
        # a drowned house: its roof above the water, crooked
        hx, hy = -158.0, -64.0
        apex = (hx + 1.2, hy - 0.8, 6.0)
        eave = ring_pts((hx, hy), 4.2, 4, 0.0, (0.12, -0.08))
        slate = b.tex('tt_ardoise')
        for k in range(4):
            b.face(slate, [eave[k], eave[(k + 1) % 4], apex], (2.5, 2.5))
        cq.cylinder(b, b.tex('mr_pierrbleu'), (hx - 1.5, hy + 1.0), 0.5, 2.0, 7.5, 4)

    def pier(self, b):
        x0, x1 = self.PIER
        deck = paint(b, 'pierplanks', lambda: cp.planks(61))
        wood = b.tex('bs_murplanche1')
        ramp(b, deck, x0, x1, self.BEACH[3], 0.5, Z, self.PIER_Z, side=wood)
        cq.box(b, wood, x0, self.PIER_END, x1, 0.5, self.PIER_Z - 0.35, self.PIER_Z, cap=deck, size=(2.0, 2.0))
        for y in range(int(self.PIER_END) + 2, 0, 6):
            for x in (x0 + 0.4, x1 - 0.4):
                cq.cylinder(b, wood, (x, y), 0.28, self.BED - 0.2, self.PIER_Z - 0.35, 6)
        railing(b, wood, (x1 - 0.06, self.PIER_END + 0.5), (x1 - 0.06, -1.0), self.PIER_Z)
        for ya, yb in ((self.PIER_END + 0.5, -44.0), (-37.0, -1.0)):  # the west one open by the stepping stones
            railing(b, wood, (x0 + 0.06, ya), (x0 + 0.06, yb), self.PIER_Z)
        hx, hy = self.HEAD
        cq.cylinder(b, wood, self.HEAD, self.HEAD_R, self.PIER_Z - 0.35, self.PIER_Z, 18, cap=deck)
        cq.cylinder(b, wood, self.HEAD, self.HEAD_R - 0.6, self.BED - 0.2, self.PIER_Z - 0.35, 18)
        rim = ct.ring(self.HEAD, self.HEAD_R - 0.06, 18, self.PIER_Z)
        for k in range(18):
            p, q = rim[k], rim[(k + 1) % 18]
            mid = (0.5 * (p[0] + q[0]), 0.5 * (p[1] + q[1]))
            if mid[1] > hy and abs(mid[0] - hx) < 4.0:
                continue  # the way in from the deck
            railing(b, wood, p, q, self.PIER_Z)
        for y in self.PIER_LAMPS:
            cq.lamp_post(b, x1 - 0.5, y, self.PIER_Z)

    def lighthouse(self, b):
        hx, hy = self.HEAD
        half = self.LIGHTHOUSE_HALF
        stripes = paint(b, 'lighthouse', lambda: cp.bands())
        cq.box(b, stripes, hx - half, hy - half, hx + half, hy + half, self.PIER_Z - 0.4, self.top,
               cap=b.tex('pr_dalfou3'), size=(2 * half, 6.0))
        top = spiral_stair(b, self.HEAD, half, self.PIER_Z, 3, start_side=3, rail=b.tex('pt_aciervf'))
        roof_parapets(b, b.tex('mr_pierrbleu'), self.HEAD, half, top, 3, 3)
        glass = retail(b, 'PharmMap/pilulle')
        cq.cylinder(b, glass, self.HEAD, 1.0, top, top + 2.6, 8, solid=True, size=(2.0, 2.6))
        cq.lantern(b, (hx, hy, top + 1.3))
        glow = b.tex('X_Ampdc', 0)
        firework(b, glow, (hx, hy, top + 1.3), 2.4)
        # its cap, tipped over like a hat
        cap = ring_pts(self.HEAD, 1.5, 8, top + 2.6, (0.25, -0.15))
        apex = (hx + 0.9, hy - 0.5, top + 5.2)
        for k in range(8):
            b.face(b.tex('tt_ardoise'), [cap[k], cap[(k + 1) % 8], apex], (1.5, 1.5), solid=False)
        for k in range(4):  # portholes up the tower
            z = self.PIER_Z + 4.0 + 5.5 * k
            glowing(b, retail(b, 'Marmite/mt_hublot02txy'), upright((hx - 0.8, hy - half - 0.05),
                                                                     (hx + 0.8, hy - half - 0.05), z, z + 1.6))

    def wheel(self, b):
        """A Ferris wheel on the beach, leaning (crazy): two rims, spokes, ten cabins, bulbs round it."""
        wx, wy = self.WHEEL
        z0 = self.beach_z(wy)
        axis_z, r = z0 + 12.5, 10.5
        yaw, lean = math.radians(18), math.radians(9)
        u = (math.cos(yaw), math.sin(yaw), 0.0)                       # across the wheel
        n = (-math.sin(yaw) * math.cos(lean), math.cos(yaw) * math.cos(lean), math.sin(lean))  # its axle
        up = ct.cross(n, u)
        steel = b.tex('pt_aciervf')
        for side in (-0.6, 0.6):  # the legs: an A on each side of the axle
            hub = ct.add((wx, wy, axis_z), ct.mul(n, side * 1.6))
            for f in (-1, 1):
                foot = (hub[0] + u[0] * f * 6.0 + n[0] * side * 2.0, hub[1] + u[1] * f * 6.0 + n[1] * side * 2.0, z0 - 0.5)
                bottom = ct.ring((foot[0], foot[1]), 0.35, 4, foot[2])
                top = ct.ring((hub[0], hub[1]), 0.25, 4, hub[2])
                b.prism(bottom, top, steel, size=(0.6, 2.0))
        seg = 32
        bulb = b.tex('X_Ampdc', 0)
        for side in (-0.45, 0.45):
            centre = ct.add((wx, wy, axis_z), ct.mul(n, side))
            pts = [ct.add(centre, ct.add(ct.mul(u, r * math.cos(2 * math.pi * k / seg)),
                                         ct.mul(up, r * math.sin(2 * math.pi * k / seg)))) for k in range(seg)]
            for k in range(seg):
                p, q = pts[k], pts[(k + 1) % seg]
                pi, qi = ct.add(p, ct.mul(ct.sub(centre, p), 0.04)), ct.add(q, ct.mul(ct.sub(centre, q), 0.04))
                b.face(steel, [p, q, qi, pi], (1.0, 1.0), solid=False)
                b.face(steel, [pi, qi, q, p], (1.0, 1.0), solid=False)
                if k % 2 == 0:
                    firework(b, bulb, p, 0.6)
            for k in range(0, seg, 4):  # spokes
                p = pts[k]
                b.face(steel, [centre, ct.add(centre, ct.mul(n, 0.08)), ct.add(p, ct.mul(n, 0.08)), p], (1.0, 1.0),
                       solid=False)
        for k in range(10):  # the cabins hang straight down from the rim
            a = 2 * math.pi * k / 10 + 0.3
            p = ct.add((wx, wy, axis_z), ct.add(ct.mul(u, r * math.cos(a)), ct.mul(up, r * math.sin(a))))
            cab = paint(b, f'cabin{k % 3}', lambda k=k: cp.checker(*[((240, 110, 170), (250, 240, 230)),
                                                                    ((70, 200, 210), (250, 240, 230)),
                                                                    ((250, 200, 60), (250, 240, 230))][k % 3], 2))
            cq.box(b, cab, p[0] - 0.9, p[1] - 0.9, p[0] + 0.9, p[1] + 0.9, p[2] - 2.2, p[2] - 0.6, cap=b.tex('tt_ardoise'),
                   solid=False, fit=True)

    def sky(self, b):
        """Fireworks over the cove (the retail game's, frozen mid-burst)."""
        for k, (rel, at, size) in enumerate((('feu_artifice/feu_art_red06_ad', (-130.0, -40.0, 42.0), 16.0),
                                             ('feu_artifice/feu_art_bleu06_ad', (-110.0, -55.0, 50.0), 20.0),
                                             ('feu_artifice/feu_art_verte07_ad', (-148.0, -20.0, 46.0), 14.0),
                                             ('feu_artifice/feu_art_orange07_ad', (-98.0, -30.0, 38.0), 12.0))):
            firework(b, retail(b, rel, ig.TRANSPARENT), at, size)


# ---- the Heights (north) ----

class Heights(District):
    """Up the north cliff: a stair (GATE) to the gallery row (T1), a ramp at its east end to the villas' lane (T2), a
    stair at that one's west end to the overlook (T3). Each terrace is a block, its sides retaining walls."""
    GATE = (3.0, 70.0, 13.0, 90.0)
    T1 = (-40.0, 90.0, 48.0, 116.0)
    T2 = (-48.0, 116.0, 48.0, 140.0)
    T3 = (-48.0, 140.0, 30.0, 160.0)
    rects = (GATE, T1, T2, T3)
    STAIR0 = 74.0                      # the gate's first step
    Z1, Z2, Z3 = Z + 6.0, Z + 12.0, Z + 18.0
    RAMP = (40.0, 48.0, 92.0, 116.0)
    STAIR2 = (-48.0, -40.0, 122.0)     # x0, x1, first step's y
    MAST = (12.0, 151.0)
    pads = ((8.0, 69.0, 4.0, 8.0, Z),)
    paths = ([(-7.0, 50.0), (2.0, 60.0), (8.0, 68.0)],)

    def floor_at(self, x, y):
        if y < self.T1[1]:
            t = (y - self.STAIR0) / cq.TREAD
            return Z + cq.RISE * min(15, max(0, math.floor(t) + 1)) if y > self.STAIR0 else Z
        rx0, rx1, ry0, ry1 = self.RAMP
        if y < self.T2[1]:
            if rx0 <= x <= rx1 and y > ry0:
                return self.Z1 + (self.Z2 - self.Z1) * (y - ry0) / (ry1 - ry0)
            return self.Z1
        sx0, sx1, sy = self.STAIR2
        if y < self.T3[1]:
            if sx0 <= x <= sx1 and y > sy:
                return self.Z2 + cq.RISE * min(15, math.floor((y - sy) / cq.TREAD) + 1)
            return self.Z2
        return self.Z3

    def lamps(self):
        out = [((5.0, 80.0, Z + 6.0), WARM, 9.0), ((11.0, 84.0, Z + 7.0), WARM, 9.0)]
        for x in range(-34, 46, 14):
            out.append(((x + 0.7, 112.0, self.Z1 + 3.0), WARM, 10.0))
            out.append(((x + 0.7, 118.5, self.Z2 + 3.0), WARM, 10.0))
        out += [((-20.0, 96.0, self.Z1 + 4.0), CYAN, 12.0), ((20.0, 132.0, self.Z2 + 2.0), (0.4, 0.8, 1.0), 10.0),
                ((self.MAST[0], self.MAST[1], self.Z3 + 40.0), (1.2, 0.2, 0.2), 14.0),
                ((-30.0, 150.0, self.Z3 + 3.0), VIOLET, 12.0), ((0.0, 144.0, self.Z3 + 3.0), WARM, 10.0)]
        return out

    def build(self, b, land):
        flags = cq.own(b, 'cq_flags', cp.flagstones)
        tiles = retail(b, 'Transit/dalcolor')
        x0, y0, x1, y1 = self.GATE
        terrace(b, flags, x0, y0, x1, self.STAIR0, Z, side=b.tex('rc_doc03'))
        end, top = stairs_y(b, x0, x1, self.STAIR0, Z, 15)
        terrace(b, flags, x0, end, x1, y1, top, side=b.tex('rc_doc03'))
        rock = b.tex('rc_doc03')
        cq.wall(b, rock, (x1 + 0.6, y1), (x1 + 0.6, y0), -1.0, 24.0, tile=6.0, step=4.0)
        cq.wall(b, rock, (x0 - 0.6, y0), (x0 - 0.6, y1), -1.0, 24.0, tile=6.0, step=4.0)
        for y in (78.0, 85.0):
            cq.lantern(b, (x0 + 0.6, y, Z + 6.0 + (y - 78) * 0.5), (x0 - 0.5, y, Z + 6.4 + (y - 78) * 0.5))
        sign = paint(b, 'neon_heights', lambda: cp.neon('THE HEIGHTS', (120, 220, 255), 512, 96))
        glowing(b, sign, upright((x0 + 0.2, 88.6), (x1 - 0.2, 88.6), self.Z1 + 4.2, self.Z1 + 6.0))
        for x in (x0 + 0.2, x1 - 1.0):
            cq.box(b, b.tex('mr_pierrbleu'), x, 88.0, x + 0.8, 89.2, self.Z1, self.Z1 + 6.0)
        self.gallery(b, tiles)
        self.villas(b, flags)
        self.overlook(b, land)

    def gallery(self, b, tiles):
        x0, y0, x1, y1 = self.T1
        terrace(b, tiles, x0, y0, x1, y1, self.Z1)
        rx0, rx1, ry0, ry1 = self.RAMP
        ramp(b, cq.own(b, 'cq_flags', cp.flagstones), rx0, rx1, ry0, ry1, self.Z1, self.Z2)
        # the retaining wall under the villas: a mural the length of it
        mural = paint(b, 'mural', lambda: cp.mural('CRAZY CITY'))
        b.face(mural, upright((-36.0, 115.9), (38.0, 115.9), self.Z1 + 0.3, self.Z2 - 0.4), fit=True, solid=False)
        # an open-air screen (Grogh's TV) and benches before it
        sx, sy = -22.0, 100.0
        frame = b.tex('pt_aciervf')
        cq.box(b, frame, sx - 7.5, sy - 0.4, sx + 7.5, sy + 0.4, self.Z1 + 1.5, self.Z1 + 10.0, size=(2.0, 2.0))
        glowing(b, retail(b, 'grog_tv/grog3', ct.WRAP), upright((sx + 7.0, sy + 0.45), (sx - 7.0, sy + 0.45),
                                                                  self.Z1 + 2.0, self.Z1 + 9.5))
        for k in range(2):
            cq.box(b, frame, sx - 7.0 + 13.6 * k, sy - 0.3, sx - 6.6 + 13.6 * k, sy + 0.3, self.Z1, self.Z1 + 1.5)
        for row in range(3):
            y = sy + 5.0 + row * 2.5
            cq.box(b, b.tex('bs_murplanche2'), sx - 5.0, y, sx + 5.0, y + 0.6, self.Z1, self.Z1 + 0.5, size=(1.0, 1.0))
        # a throne: the giant toilet on a plinth, the gallery's sculpture
        tx, ty = 14.0, 102.0
        cq.box(b, b.tex('mr_pierrbleu'), tx - 2.0, ty - 2.0, tx + 2.0, ty + 2.0, self.Z1, self.Z1 + 1.2,
               cap=b.tex('pr_dalfou9'))
        cq.cylinder(b, b.tex('xx_wc'), (tx, ty), 1.3, self.Z1 + 1.2, self.Z1 + 3.4, 12, cap=b.tex('xx_wc'), r1=1.6)
        cq.box(b, b.tex('xx_wc'), tx - 1.2, ty + 1.0, tx + 1.2, ty + 1.9, self.Z1 + 3.4, self.Z1 + 6.0)
        # boutiques along the south edge, a pharmacy window among them
        for k, xa in enumerate((-36.0, -6.0, 24.0)):
            h = cq.House(b, (xa, 90.6, xa + 9.0, 96.0), -1.0, self.Z1 + 8.0 + 2 * k, ('mr_fou4', 'mr_fou1', 'mr_poisvert')[k],
                         400 + k, upright=self.Z1 + 5.5)
            h.windows(b, 2, self.Z1 + 3.4, 2, 410 + k)
            h.door(b, 2, 0.3, self.Z1)
            glowing(b, retail(b, 'PharmMap/xx_vitrineeee_txy'), h.face_quad(2, 0.55, 0.85, self.Z1 + 0.4,
                                                                             self.Z1 + 2.8, 0.08), (1.4, 1.3, 1.1))
            awning(b, awning_cloth(b, k), h, 2, 0.7, self.Z1 + 3.2, 3.0)
        for x in range(-34, 46, 14):
            cq.lamp_post(b, x, 112.0, self.Z1)
        bush = b.tex('vg_buissongeoNZ', 0)
        for x in range(-30, 40, 10):
            crossed(b, bush, x + 3.0, 108.0, self.Z1, 2.4, 1.6, x * 0.7)

    def villas(self, b, flags):
        x0, y0, x1, y1 = self.T2
        terrace(b, b.tex('hb_01'), x0, y0, x1, y1, self.Z2)
        sx0, sx1, sy = self.STAIR2
        end, top = stairs_y(b, sx0, sx1, sy, self.Z2, 15)
        terrace(b, flags, sx0, end, sx1, self.T3[1], top)
        water = b.tex('o_fouTXY', ct.WRAP | ig.TRANSPARENT)
        tiles = retail(b, 'Transit/dalcolor2')
        for k, xa in enumerate((-34.0, -10.0, 14.0)):
            h = cq.House(b, (xa, 130.0, xa + 14.0, 139.4), -1.0, self.Z2 + 9.0 + 3 * (k % 2),
                         ('mr_maisonfou', 'mr_fou5', 'mr_fou2')[k], 420 + k, upright=self.Z2 + 5.5)
            h.windows(b, 0, self.Z2 + 0.4, 3, 430 + k)
            h.door(b, 0, 0.5, self.Z2, 'bs_murplanche2')
            # a pool on the lawn before it: a raised tiled basin, its water (scrolled as the river's: finish)
            px0, py0, px1, py1 = xa + 2.0, 121.0, xa + 12.0, 127.0
            for a, c in (((px0, py0), (px1, py0)), ((px1, py0), (px1, py1)), ((px1, py1), (px0, py1)),
                         ((px0, py1), (px0, py0))):
                railing(b, tiles, a, c, self.Z2, 0.7, 0.4)
            cq.floor(b, tiles, px0, py0, px1, py1, self.Z2 + 0.02, tile=2.0, step=5.0, solid=False)
            b.face(water, [(px0, py0, self.Z2 + 0.55), (px1, py0, self.Z2 + 0.55), (px1, py1, self.Z2 + 0.55),
                           (px0, py1, self.Z2 + 0.55)], (4.0, 4.0), solid=False, lit=ig.baked_light((0.7, 0.9, 1.2), 200))
            for j in range(3):  # a hedge of topiary along its side
                crossed(b, b.tex('vg_buissongeo02NZ', 0), xa + 1.5 + 5.5 * j, 129.0, self.Z2, 2.0, 2.2, j)
        for x in range(-34, 46, 14):
            cq.lamp_post(b, x, 118.5, self.Z2)
        # the parapet of the overlook over the lane
        stone = b.tex('mr_pierrbleu')
        railing(b, stone, (sx1, self.T3[1] + 0.1), (self.T3[2], self.T3[1] + 0.1), self.Z3, 1.0, 0.2)

    def overlook(self, b, land):
        x0, y0, x1, y1 = self.T3
        terrace(b, retail(b, 'Transit/dalcolor'), x0, y0, x1, y1, self.Z3)
        # the radio mast: four legs leaning in, rungs across, a red light on top
        mx, my = self.MAST
        steel = b.tex('pt_aciervf')
        height, foot, tip = 38.0, 2.4, 0.4
        legs = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
        for sx, sy in legs:
            bottom = ct.ring((mx + sx * foot, my + sy * foot), 0.18, 4, self.Z3)
            top = ct.ring((mx + sx * tip, my + sy * tip), 0.1, 4, self.Z3 + height)
            b.prism(bottom, top, steel, cap=steel, size=(0.5, 2.0))
        for k in range(1, 12):
            z = self.Z3 + height * k / 12
            r = foot + (tip - foot) * k / 12
            for i in range(4):
                (ax, ay), (cx, cy) = legs[i], legs[(i + 1) % 4]
                a, c = (mx + ax * r, my + ay * r), (mx + cx * r, my + cy * r)
                b.face(steel, upright(a, c, z, z + 0.12), (1.0, 1.0), solid=False)
                b.face(steel, upright(c, a, z, z + 0.12), (1.0, 1.0), solid=False)
        glow = b.tex('X_Ampdc', 0)
        firework(b, glow, (mx, my, self.Z3 + height + 0.6), 1.6)
        glowing(b, paint(b, 'neon_radio', lambda: cp.neon('CRAZY FM', (255, 80, 60), 384, 96)),
                upright((mx - 2.6, my - 2.7), (mx + 2.6, my - 2.7), self.Z3 + 3.0, self.Z3 + 5.0))
        # an observatory: a drum and a swirled dome, its slit glowing
        ox, oy = -32.0, 151.0
        cq.cylinder(b, b.tex('mr_pierrocre'), (ox, oy), 5.0, self.Z3, self.Z3 + 5.0, 16, cap=b.tex('pr_dalfou4'))
        dome = cq.own(b, 'cq_swirl', cp.swirl_dome)
        rings = [(5.2 * math.cos(math.pi / 2 * k / 5), 5.2 * math.sin(math.pi / 2 * k / 5)) for k in range(6)]
        for (r0, h0), (r1, h1) in zip(rings, rings[1:]):
            cq.cylinder(b, dome, (ox, oy), r0, self.Z3 + 5.0 + h0, self.Z3 + 5.0 + h1, 16, solid=False, r1=max(r1, 0.05))
        glowing(b, paint(b, 'slit', lambda: cp.solid((255, 220, 140))), upright((ox - 0.6, oy - 5.25),
                                                                               (ox + 0.6, oy - 5.25), self.Z3 + 5.4,
                                                                               self.Z3 + 9.8), (1.6, 1.4, 1.0))
        # a telescope pointing at the town, coin-operated
        cq.cylinder(b, steel, (-6.0, 142.0), 0.15, self.Z3, self.Z3 + 1.2, 6)
        cq.cylinder(b, b.tex('mr_poisvert'), (-6.0, 142.0), 0.3, self.Z3 + 1.2, self.Z3 + 1.9, 8, r1=0.4, solid=False)
        # a rocket, Transit's, standing on its fins for no reason
        rx, ry = 20.0, 154.0
        body = retail(b, 'Transit/fusee_txy')
        cq.cylinder(b, body, (rx, ry), 1.6, self.Z3, self.Z3 + 10.0, 12, size=(3.0, 3.0))
        cq.cylinder(b, body, (rx, ry), 1.6, self.Z3 + 10.0, self.Z3 + 14.0, 12, r1=0.05, size=(3.0, 3.0), solid=False)
        for k in range(3):
            a = 2 * math.pi * k / 3
            fx, fy = rx + 1.6 * math.cos(a), ry + 1.6 * math.sin(a)
            gx, gy = rx + 3.4 * math.cos(a), ry + 3.4 * math.sin(a)
            b.face(body, [(fx, fy, self.Z3), (gx, gy, self.Z3), (fx, fy, self.Z3 + 4.0)], fit=True, solid=False)
            b.face(body, [(fx, fy, self.Z3 + 4.0), (gx, gy, self.Z3), (fx, fy, self.Z3)], fit=True, solid=False)
        # the town's name on the hill behind, in letters each the size of a house
        y = y1 + 9.0
        z = land.height(-8.0, y) - 1.0
        letters = paint(b, 'hill_letters', lambda: cp.big_letters('CRAZY CITY'), ig.ALPHA_TEST)
        b.face(letters, upright((-50.0, y), (34.0, y), z, z + 15.0), fit=True, solid=False)


# ---- Downtown (south) ----

class Downtown(District):
    """Through the south cliff beside the arrival arch: a grid of streets (x at AVENUES, y at STREETS) between blocks
    of crooked towers; the plaza with the clock tower, a park, a building site and the fire-stair tower in four of
    them."""
    GATE = (12.0, -92.0, 22.0, -70.0)
    CITY = (-66.0, -170.0, 66.0, -92.0)
    rects = (GATE, CITY)
    AVENUES = (-46.0, -15.0, 17.0, 46.0)     # north-south streets (their middle; 17: on from the gate)
    STREETS = (-99.0, -123.0, -147.0)        # east-west (north of -99: a strip of tarmac under the cliff)
    ROAD = 7.0
    pads = ((17.0, -68.0, 4.0, 8.0, Z),)
    paths = ([(12.0, -46.0), (17.0, -66.0)],)

    def blocks(self):
        """The blocks between the streets: (x0, y0, x1, y1)."""
        xs = [self.CITY[0]] + [c for a in self.AVENUES for c in (a - self.ROAD / 2, a + self.ROAD / 2)] + [self.CITY[2]]
        ys = [self.CITY[1]] + [c for s in reversed(self.STREETS) for c in (s - self.ROAD / 2, s + self.ROAD / 2)] + \
            [self.CITY[3]]
        out = []
        for i in range(0, len(xs), 2):
            for j in range(0, len(ys), 2):
                if xs[i + 1] - xs[i] >= 8.0 and ys[j + 1] - ys[j] >= 8.0:
                    out.append((xs[i], ys[j], xs[i + 1], ys[j + 1]))
        return out

    def lamps(self):
        out = [((17.0, -80.0, Z + 7.0), VIOLET, 10.0)]
        for x in self.AVENUES:
            for y in range(-164, -94, 14):
                out.append(((x + self.ROAD / 2 + 0.2, y, Z + 3.0), WARM, 10.0))
        for y in self.STREETS:
            for x in range(-60, 64, 18):
                out.append(((x, y - self.ROAD / 2 - 0.2, Z + 3.0), WARM, 9.0))
        cx, cy = self.plaza_centre()
        out += [((cx - 6.0, cy, Z + 16.0), (1.3, 1.2, 0.9), 16.0), ((cx + 5.0, cy, Z + 2.0), CYAN, 9.0),
                ((cx - 1.0, cy - 6.0, Z + 2.0), PINK, 9.0)]
        return out

    def plaza_centre(self):
        b = self.special()['plaza']
        return 0.5 * (b[0] + b[2]), 0.5 * (b[1] + b[3])

    def special(self):
        """Which blocks are not towers: picked by their place in the grid."""
        blocks = self.blocks()

        def at(x, y):
            return next(k for k in blocks if k[0] <= x <= k[2] and k[1] <= y <= k[3])
        return {'plaza': at(0.0, -135.0), 'park': at(-30.0, -111.0), 'site': at(30.0, -135.0), 'fire': at(-30.0, -160.0)}

    def build(self, b, land):
        x0, y0, x1, y1 = self.CITY
        road = paint(b, 'asphalt', cp.asphalt)
        kerb = b.tex('pr_dalfou3')
        terrace(b, road, x0, y0, x1, y1, Z, side=b.tex('rc_doc03'))
        gx0, gy0, gx1, gy1 = self.GATE
        terrace(b, road, gx0, gy0, gx1, gy1, Z, side=b.tex('rc_doc03'))
        rock = b.tex('rc_doc03')
        cq.wall(b, rock, (gx0 - 0.6, gy0), (gx0 - 0.6, gy1), -1.0, 24.0, tile=6.0, step=4.0)
        cq.wall(b, rock, (gx1 + 0.6, gy1), (gx1 + 0.6, gy0), -1.0, 24.0, tile=6.0, step=4.0)
        sign = paint(b, 'neon_downtown', lambda: cp.neon('DOWNTOWN', (190, 90, 255), 512, 96))
        for x in (gx0 + 0.2, gx1 - 1.0):
            cq.box(b, b.tex('mr_pierrbleu'), x, -91.0, x + 0.8, -90.2, Z, Z + 7.0)
        glowing(b, sign, upright((gx0 + 0.2, -91.1), (gx1 - 0.2, -91.1), Z + 5.0, Z + 6.8))
        glowing(b, sign, upright((gx1 - 0.2, -90.1), (gx0 + 0.2, -90.1), Z + 5.0, Z + 6.8))
        lines = paint(b, 'roadline', lambda: cp.solid((240, 210, 60)))
        for x in self.AVENUES:  # dashed middle lines
            for y in range(int(y0) + 2, int(y1) - 2, 6):
                b.face(lines, [(x - 0.1, y, Z + 0.02), (x + 0.1, y, Z + 0.02), (x + 0.1, y + 3.0, Z + 0.02),
                               (x - 0.1, y + 3.0, Z + 0.02)], solid=False)
        for y in self.STREETS:
            for x in range(int(x0) + 2, int(x1) - 2, 6):
                b.face(lines, [(x, y - 0.1, Z + 0.02), (x + 3.0, y - 0.1, Z + 0.02), (x + 3.0, y + 0.1, Z + 0.02),
                               (x, y + 0.1, Z + 0.02)], solid=False)
        special = self.special()
        rng = random.Random(2026)
        walls = ['mr_fou1', 'mr_fou2', 'mr_fou3', 'mr_fou4', 'mr_fou5', 'mr_maisonfou', 'mr_pierrocre', 'mr_poisvert',
                 'xx_foutete02']
        retail_walls = ['Land/xx_uktour1ty', 'Land/mr_cite02_txy', 'Marmite/brikverte01txy', 'Sud0/mr_maison01ty']
        words = ['EAT SOUP', 'BUY NOW', 'CRAZY FM', 'NO PARKING', 'TOP HAT', 'DANCE', 'WOW', 'SALE', 'HOTDOGS',
                 'BINGO', 'TAXI', 'VOTE ED']
        glows = [(255, 60, 200), (90, 230, 255), (255, 210, 80), (190, 90, 255), (120, 255, 120), (255, 120, 60)]
        seed = 500
        for blk in self.blocks():
            bx0, by0, bx1, by1 = blk
            cq.box(b, kerb, bx0, by0, bx1, by1, Z, Z + 0.15, cap=kerb, size=(2.0, 2.0))  # the pavement, a kerb up
            kind = next((k for k, v in special.items() if v == blk), None)
            if kind == 'plaza':
                self.plaza(b, blk)
                continue
            if kind == 'park':
                self.park(b, blk)
                continue
            if kind == 'site':
                self.site(b, blk)
                continue
            if kind == 'fire':
                self.fire_tower(b, blk)
                continue
            # towers: the block split in two or three along its longer side, each inset from the pavement
            w, d = bx1 - bx0, by1 - by0
            n = 3 if max(w, d) > 22 else 2
            for k in range(n):  # 1.8 of pavement at the block's edges, 0.8 between two towers
                lo, hi = (1.8 if k == 0 else 0.4), (1.8 if k == n - 1 else 0.4)
                if w >= d:
                    base = (bx0 + w * k / n + lo, by0 + 1.8, bx0 + w * (k + 1) / n - hi, by1 - 1.8)
                else:
                    base = (bx0 + 1.8, by0 + d * k / n + lo, bx1 - 1.8, by0 + d * (k + 1) / n - hi)
                seed += 1
                tall = rng.uniform(12.0, 34.0) * (1.4 if rng.random() < 0.15 else 1.0)
                wall = walls[seed % len(walls)]
                h = cq.House(b, base, -1.0, Z + 0.15 + tall, wall, seed, roof=rng.random() < 0.45, upright=Z + 5.7)
                if rng.random() < 0.4:  # some faces of the retail game's walls instead: stone, brick, Land's towers
                    side = rng.randrange(4)
                    b.face(retail(b, retail_walls[seed % len(retail_walls)]), h.face_quad(side, 0.0, 1.0, Z + 0.2,
                                                                                         Z + 5.6, 0.03), (4.0, 4.0),
                           solid=False)
                for side in range(4):
                    h.windows(b, side, Z + 3.4, 12, seed * 4 + side)
                # its street face: a door, a shop window or a sign, sometimes an awning or a billboard
                side = rng.choice((0, 2))
                h.door(b, side, rng.uniform(0.25, 0.75), Z + 0.15, 'bs_murplanche2')
                if rng.random() < 0.6:
                    word = words[seed % len(words)]
                    key = paint(b, 'neon_' + word.lower().replace(' ', ''),
                                lambda w=word, g=glows[seed % len(glows)]: cp.neon(w, g, 384, 96))
                    sign_on(b, h, side, 0.5, Z + 3.4 + 3.0 * rng.randrange(2), min(7.0, h.width(side) - 1.0), 1.8, key)
                if rng.random() < 0.35 and tall > 18:
                    bill = paint(b, f'bill{seed % 4}', lambda s=seed: cp.poster(
                        [[('CRAZY', 30), ('CITY', 22)], [('EAT', 30), ('SOUP', 30)], [('VOTE', 28), ('ED', 34)],
                         [('BIG', 30), ('SALE!', 26)]][s % 4], seed=s))
                    k2 = (side + 1) % 4
                    b.face(bill, h.face_quad(k2, 0.15, 0.85, Z + tall * 0.55, Z + tall * 0.55 + 6.0, 0.15), fit=True,
                           solid=False)
                if rng.random() < 0.3:
                    awning(b, awning_cloth(b, seed), h, side, 0.5, Z + 3.2, 3.4)
        # bath-tub cars parked along the kerbs, street lamps, traffic lights at the crossings
        for x in self.AVENUES:
            for y in range(-164, -94, 14):
                cq.lamp_post(b, x + self.ROAD / 2 + 0.2, y, Z + 0.15)
                car = y + 4.0
                if rng.random() < 0.55 and all(abs(car - s) > self.ROAD / 2 + 2.6 for s in self.STREETS):
                    bathtub_car(b, (x - self.ROAD / 2 + 1.3, car, Z), math.pi / 2, int(y))
        for y in self.STREETS:
            for x in self.AVENUES:
                self.traffic_light(b, x + self.ROAD / 2 + 0.6, y + self.ROAD / 2 + 0.6)
        # an arrow sign (Marmite's) at the gate: this way to the town
        glowing(b, retail(b, 'Marmite/fleche'), upright((gx1 - 0.4, -94.0), (gx1 - 0.4, -96.5), Z + 2.0, Z + 4.0),
                (1.2, 1.2, 1.2))

    def traffic_light(self, b, x, y):
        steel = b.tex('pt_aciervf')
        cq.box(b, steel, x - 0.1, y - 0.1, x + 0.1, y + 0.1, Z, Z + 4.2, size=(0.5, 2.0))
        cq.box(b, steel, x - 0.35, y - 0.35, x + 0.35, y + 0.35, Z + 4.2, Z + 6.2, solid=False)
        for k, rgb in enumerate(((255, 50, 40), (255, 190, 40), (60, 230, 90))):
            disc = paint(b, f'light{k}', lambda c=rgb: cp.solid(c))
            z = Z + 5.8 - 0.6 * k
            for f in ((0, -1), (0, 1), (-1, 0), (1, 0)):  # all four ways: crazy lights
                o = (x + f[0] * 0.37, y + f[1] * 0.37)
                t = (-f[1] * 0.2, f[0] * 0.2)
                glowing(b, disc, [(o[0] - t[0], o[1] - t[1], z - 0.2), (o[0] + t[0], o[1] + t[1], z - 0.2),
                                  (o[0] + t[0], o[1] + t[1], z + 0.2), (o[0] - t[0], o[1] - t[1], z + 0.2)])

    def plaza(self, b, blk):
        x0, y0, x1, y1 = blk
        cq.floor(b, retail(b, 'Transit/dalcolor'), x0 + 0.5, y0 + 0.5, x1 - 0.5, y1 - 0.5, Z + 0.16, tile=3.0, step=4.0,
                 solid=False)
        cx, cy = 0.5 * (x0 + x1) - 6.0, 0.5 * (y0 + y1)
        # the clock tower: a crooked shaft, a clock on every side (each telling another wrong time), a slate spire
        h = cq.House(b, (cx - 2.5, cy - 2.5, cx + 2.5, cy + 2.5), Z, Z + 22.0, 'mr_pierrbleu', 600, roof=True,
                     upright=Z + 5.7)
        for k in range(4):
            face = paint(b, f'clock{k}', lambda k=k: cp.clock(hours=[7.3, 2.9, 11.6, 4.4][k],
                                                              minutes=[-0.15, 0.6, 0.33, -0.7][k]), ig.ALPHA_TEST)
            glowing(b, face, h.face_quad(k, 0.12, 0.88, Z + 16.0, Z + 20.0, 0.1), (1.5, 1.4, 1.1))
        # a fountain: a bath tub on a plinth pouring into a round basin
        fx, fy = cx + 11.0, cy
        tiles = retail(b, 'Transit/dalcolor2')
        for k in range(16):
            a0, a1 = 2 * math.pi * k / 16, 2 * math.pi * (k + 1) / 16
            railing(b, tiles, (fx + 4.0 * math.cos(a0), fy + 4.0 * math.sin(a0)),
                    (fx + 4.0 * math.cos(a1), fy + 4.0 * math.sin(a1)), Z + 0.15, 0.7, 0.4)
        b.face(b.tex('o_fouTXY', ct.WRAP | ig.TRANSPARENT), ring_pts((fx, fy), 3.8, 16, Z + 0.6), (4.0, 4.0),
               solid=False, lit=ig.baked_light((0.7, 0.9, 1.2), 200))
        cq.cylinder(b, b.tex('mr_pierrocre'), (fx, fy), 0.8, Z + 0.15, Z + 2.4, 8, cap=b.tex('pr_dalfou9'))
        b.prism(ring_pts((fx, fy), 1.0, 10, Z + 2.4), ring_pts((fx, fy), 1.6, 10, Z + 3.4, (0.2, 0.1)),
                b.tex('xx_baignoire'), cap=b.tex('o_fouTXY', ct.WRAP | ig.TRANSPARENT), solid=False, size=(2.0, 1.0))
        b.face(b.tex('brumeTXY', ct.WRAP | ig.TRANSPARENT), upright((fx + 1.2, fy - 0.3), (fx + 2.4, fy - 0.3),
                                                                    Z + 0.6, Z + 3.2), fit=True, solid=False)
        for x in (x0 + 2.0, x1 - 2.0):
            for y in (y0 + 2.0, y1 - 2.0):
                cq.lamp_post(b, x, y, Z + 0.15)

    def park(self, b, blk):
        x0, y0, x1, y1 = blk
        cq.floor(b, b.tex('hb_01'), x0 + 0.8, y0 + 0.8, x1 - 0.8, y1 - 0.8, Z + 0.17, tile=6.0, step=4.0, solid=False)
        rng = random.Random(64)
        tree = b.tex('vg_arbregeoNZ', 0)
        for k in range(7):
            x, y = rng.uniform(x0 + 3, x1 - 3), rng.uniform(y0 + 3, y1 - 3)
            crossed(b, tree, x, y, Z + 0.15, 5.0, rng.uniform(5.5, 7.5), rng.uniform(0, 3), solid_trunk=True)
        cx, cy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
        # a bandstand: a round floor a step up, posts, a striped roof
        cq.cylinder(b, b.tex('mr_pierrocre'), (cx, cy), 4.0, Z, Z + 0.55, 12, cap=b.tex('pr_dalfou4'))
        for k in range(6):
            a = 2 * math.pi * k / 6
            cq.cylinder(b, b.tex('pt_aciervf'), (cx + 3.6 * math.cos(a), cy + 3.6 * math.sin(a)), 0.12, Z + 0.55,
                        Z + 4.0, 6)
        roof = ring_pts((cx, cy), 4.6, 12, Z + 4.0)
        cloth = awning_cloth(b, 0)
        for k in range(12):
            b.face(cloth, [roof[k], roof[(k + 1) % 12], (cx, cy, Z + 6.0)], fit=True, solid=False)
            b.face(cloth, [(cx, cy, Z + 6.0), roof[(k + 1) % 12], roof[k]], fit=True, solid=False)

    def site(self, b, blk):
        """A building site: a concrete frame of two floors over the ground, a straight stair up the south edge to the
        first (under the second's floor: 4 of headroom) and one back along the north edge to the second (nothing over
        it), barriers round it, a crane holding a bath tub over it all."""
        x0, y0, x1, y1 = blk
        cq.floor(b, b.tex('hb_terregeo06'), x0 + 0.5, y0 + 0.5, x1 - 0.5, y1 - 0.5, Z + 0.16, tile=5.0, step=5.0,
                 solid=False)
        for x in range(int(x0) + 2, int(x1) - 2, 4):  # the barriers round it
            for y, flip in ((y0 + 0.4, 1), (y1 - 0.4, -1)):
                pts = upright((x, y), (x + 3.6, y), Z + 0.15, Z + 1.2)
                b.face(b.tex('xx_barietrav01', 0), pts if flip > 0 else list(reversed(pts)), fit=True)
        fx0, fy0, fx1, fy1 = x0 + 3.0, y0 + 3.0, x1 - 3.0, y1 - 3.0
        concrete, steel = b.tex('pr_dalfou9'), b.tex('pt_aciervf')
        ground, lift = Z + 0.15, 10 * cq.RISE
        well = 2.6  # the stairs' strips along the south and north edges
        for x in (fx0, fx1 - 0.5):
            for y in (fy0, fy1 - 0.5):
                cq.box(b, steel, x, y, x + 0.5, y + 0.5, ground, ground + 2 * lift + 1.0, size=(0.5, 2.0))
        # the first floor: all but the south strip, where its stair comes up
        cq.box(b, concrete, fx0, fy0 + well, fx1, fy1, ground + lift - 0.3, ground + lift, cap=concrete, size=(3.0, 3.0))
        # the second: all but the north strip, where its stair comes up
        cq.box(b, concrete, fx0, fy0, fx1, fy1 - well, ground + 2 * lift - 0.3, ground + 2 * lift, cap=concrete,
               size=(3.0, 3.0))
        x = fx0 + 0.5
        z = ground
        for _ in range(10):  # up the south strip, eastward
            z += cq.RISE
            cq.step(b, x, fy0 + 0.1, x + cq.TREAD + 0.02, fy0 + well - 0.05, z)
            x += cq.TREAD
        cq.step(b, x, fy0 + 0.1, x + 1.2, fy0 + well, z)  # its landing, beside the first floor
        x = fx1 - 0.5
        for _ in range(10):  # up the north strip, westward, from the first floor
            z += cq.RISE
            cq.step(b, x - cq.TREAD - 0.02, fy1 - well + 0.05, x, fy1 - 0.1, z)
            x -= cq.TREAD
        cq.step(b, x - 1.2, fy1 - well, x, fy1 - 0.1, z)
        for a, c in (((fx0, fy0), (fx1, fy0)), ((fx1, fy0), (fx1, fy1 - well)), ((fx0, fy1 - well), (fx0, fy0))):
            railing(b, steel, a, c, ground + 2 * lift, 1.0, 0.1)  # the top's edges but the stair's
        glowing(b, b.tex('xx_panotrav01', 0), upright((x0 + 2.0, y0 + 0.3), (x0 + 6.0, y0 + 0.3), Z + 1.3, Z + 3.3),
                (1.2, 1.2, 1.2))
        # the crane: a lattice mast and a jib, a bath tub hanging from its hook
        cx, cy = x1 - 1.6, y1 - 1.6
        for k in range(14):
            zz = Z + 2.0 * k
            cq.box(b, steel, cx - 0.9, cy - 0.9, cx + 0.9, cy + 0.9, zz, zz + 0.15, solid=False)
            for sx, sy in ((-0.9, -0.9), (0.9, -0.9), (0.9, 0.9), (-0.9, 0.9)):
                cq.box(b, steel, cx + sx - 0.08, cy + sy - 0.08, cx + sx + 0.08, cy + sy + 0.08, zz, zz + 2.0,
                       solid=k < 2)
        jz = Z + 28.0
        cq.box(b, paint(b, 'cranejib', lambda: cp.stripes((250, 200, 40), (40, 40, 50), count=8)), cx - 18.0, cy - 0.5,
               cx + 4.0, cy + 0.5, jz, jz + 1.2, solid=False, size=(4.0, 1.2))
        hx, hy = cx - 14.0, cy
        b.face(steel, upright((hx, hy), (hx + 0.06, hy), jz - 12.0, jz), (1.0, 1.0), solid=False)
        b.face(steel, upright((hx + 0.06, hy), (hx, hy), jz - 12.0, jz), (1.0, 1.0), solid=False)
        b.prism(ring_pts((hx, hy), 1.4, 8, jz - 15.0, (0.1, 0.25)), ring_pts((hx, hy), 2.0, 8, jz - 12.5, (0.1, 0.25)),
                b.tex('xx_baignoire'), solid=False, size=(3.0, 1.0))

    def fire_tower(self, b, blk):
        """The tallest of them, its fire stair winding up outside it to the roof: a helipad, a view of it all."""
        x0, y0, x1, y1 = blk
        cx, cy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
        half = 4.0
        wall = retail(b, 'Land/xx_uktour1ty')
        z0 = Z + 0.15
        top = z0 + 4 * 20 * cq.RISE
        cq.box(b, wall, cx - half, cy - half, cx + half, cy + half, -1.0, top, cap=paint(b, 'helipad', cp.helipad),
               size=(4.0, 4.0))
        spiral_stair(b, (cx, cy), half, z0, 4, start_side=0, rail=b.tex('pt_aciervf'))
        roof_parapets(b, b.tex('pt_aciervf'), (cx, cy), half, top, 0, 4, 0.12)
        for k in range(1, 11):  # rows of lit windows between the turns of the stair
            z = z0 + 3.0 * k
            for side in ((0, -1), (1, 0), (0, 1), (-1, 0)):
                o = (cx + side[0] * (half + 0.03), cy + side[1] * (half + 0.03))
                t = (-side[1], side[0])
                for u in (-2.0, 0.0, 2.0):
                    p = (o[0] + t[0] * u, o[1] + t[1] * u)
                    glowing(b, cq.own(b, 'cq_window1', lambda: cp.window(20), 0),
                            [(p[0] - t[0] * 0.5, p[1] - t[1] * 0.5, z), (p[0] + t[0] * 0.5, p[1] + t[1] * 0.5, z),
                             (p[0] + t[0] * 0.5, p[1] + t[1] * 0.5, z + 1.3),
                             (p[0] - t[0] * 0.5, p[1] - t[1] * 0.5, z + 1.3)], (1.7, 1.45, 1.05))
        glowing(b, paint(b, 'neon_top', lambda: cp.neon('TOP', (120, 255, 120), 256, 96)),
                upright((cx - 3.0, cy - half - 0.1), (cx + 3.0, cy - half - 0.1), top + 1.4, top + 3.6))
        cq.box(b, b.tex('pt_aciervf'), cx - 3.0, cy - half + 0.1, cx + 3.0, cy - half + 0.3, top + 1.0, top + 4.0,
               solid=False)


# ---- the Junkyard (south-east) ----

class Junkyard(District):
    """A scrapyard through the south-east cliff: barbed wire round it, junk piles of the town's odd things, crate
    stacks a jump apart up to a crane cab, Marmite's crates, a boat hull up on blocks."""
    GATE = (64.0, -20.0, 88.0, -10.0)
    YARD = (88.0, -78.0, 150.0, -14.0)
    rects = (GATE, YARD)
    CRATES = (118.0, -50.0)
    pads = ((63.0, -15.0, 3.5, 8.0, Z),)
    paths = ([(58.0, 0.0), (63.0, -14.0)],)

    def lamps(self):
        return [((96.0, -22.0, Z + 8.0), WHITE, 18.0), ((140.0, -22.0, Z + 8.0), WHITE, 18.0),
                ((96.0, -72.0, Z + 8.0), WHITE, 18.0), ((140.0, -72.0, Z + 8.0), WHITE, 18.0),
                ((76.0, -15.0, Z + 6.0), AMBER, 9.0), ((self.CRATES[0], self.CRATES[1], Z + 16.0), AMBER, 12.0)]

    def build(self, b, land):
        dirt = b.tex('hb_terregeo06')
        x0, y0, x1, y1 = self.YARD
        terrace(b, dirt, x0, y0, x1, y1, Z, side=b.tex('rc_doc03'))
        gx0, gy0, gx1, gy1 = self.GATE
        terrace(b, dirt, gx0, gy0, gx1, gy1, Z, side=b.tex('rc_doc03'))
        rock = b.tex('rc_doc03')
        cq.wall(b, rock, (gx0, gy1 + 0.6), (gx1, gy1 + 0.6), -1.0, 24.0, tile=6.0, step=4.0)
        cq.wall(b, rock, (gx1, gy0 - 0.6), (gx0, gy0 - 0.6), -1.0, 24.0, tile=6.0, step=4.0)
        # the gate: two crate pillars, the sign hung between them on chains
        for y in (gy0 + 0.3, gy1 - 1.8):
            cq.box(b, retail(b, 'Marmite/mart_caisse1txy'), 82.0, y, 83.5, y + 1.5, Z, Z + 7.5, size=(1.5, 1.5))
        sign = paint(b, 'neon_junk', lambda: cp.neon('JUNKYARD', (255, 190, 40), 512, 96))
        glowing(b, sign, [(82.8, gy0 + 1.8, Z + 5.4), (82.8, gy1 - 1.8, Z + 5.4), (82.8, gy1 - 1.8, Z + 7.0),
                          (82.8, gy0 + 1.8, Z + 7.0)])
        glowing(b, sign, [(82.7, gy1 - 1.8, Z + 5.4), (82.7, gy0 + 1.8, Z + 5.4), (82.7, gy0 + 1.8, Z + 7.0),
                          (82.7, gy1 - 1.8, Z + 7.0)])
        # barbed wire along the yard's edges (cut out, it does not hold Ed: the rock behind it does)
        wire = b.tex('xx_barbel01NZ', ig.ALPHA_TEST | ct.WRAP)
        for a, c in (((x0 + 0.5, y1 - 0.5), (x1 - 0.5, y1 - 0.5)), ((x1 - 0.5, y1 - 0.5), (x1 - 0.5, y0 + 0.5)),
                     ((x1 - 0.5, y0 + 0.5), (x0 + 0.5, y0 + 0.5)), ((x0 + 0.5, y0 + 0.5), (x0 + 0.5, gy0))):
            for pts in (upright(a, c, Z, Z + 3.0), list(reversed(upright(a, c, Z, Z + 3.0)))):
                b.face(wire, pts, (3.0, 3.0), solid=False)
        self.piles(b)
        self.crates(b)
        self.hull(b)
        for x, y in ((96.0, -22.0), (140.0, -22.0), (96.0, -72.0), (140.0, -72.0)):  # floodlights
            steel = b.tex('pt_aciervf')
            cq.box(b, steel, x - 0.15, y - 0.15, x + 0.15, y + 0.15, Z, Z + 8.0, size=(0.5, 2.0))
            glowing(b, retail(b, 'Marmite/mt_hublot02txy'), upright((x - 0.9, y - 0.3), (x + 0.9, y - 0.3), Z + 7.2,
                                                                     Z + 8.8))

    def piles(self, b):
        """Heaps of what the town throws out: colanders, funnels, discs, toilets, cameras, gyroscopes, tubs."""
        rng = random.Random(4242)
        stuff = ['xPassoirNZ', 'xx_entonoir1', 'xx_entonoir2', 'xx_cd_NZ', 'xx_wc', 'xx_camera', 'xx_gyro', 'xx_baignoire',
                 'xx_verboul', 'xx_verboul02']
        for px, py, n in ((100.0, -30.0, 14), (136.0, -32.0, 16), (100.0, -66.0, 12), (138.0, -66.0, 18)):
            for k in range(n):
                r = 4.5 * (1 - k / n)
                a = rng.uniform(0, 2 * math.pi)
                x, y = px + r * math.cos(a), py + r * math.sin(a)
                z = Z + 0.45 * k * (1 - r / 6)
                s = rng.uniform(0.8, 1.8)
                key = b.tex(stuff[rng.randrange(len(stuff))])
                turn = rng.uniform(0, math.pi)
                bottom = ct.ring((x, y), s, 5, z, turn)
                top = ct.ring((x + rng.uniform(-0.4, 0.4), y + rng.uniform(-0.4, 0.4)), s * rng.uniform(0.5, 1.1), 5,
                              z + s * rng.uniform(0.8, 1.6), turn + 0.4)
                b.prism(bottom, top, key, cap=key, size=(2 * s, s), fit=True, solid=k < n // 2)
            retail_crate = retail(b, 'Marmite/mart_caisse99txy')
            cq.box(b, retail_crate, px + 5.0, py - 1.0, px + 7.0, py + 1.0, Z, Z + 2.0, size=(2.0, 2.0))

    def crates(self, b):
        """A climb: crates stacked in a stair that winds up, each a jump over the last (Ed jumps ~2.8), to a crane cab
        on top with a lamp."""
        cx, cy = self.CRATES
        textures = [retail(b, 'Marmite/mart_caisse1txy'), retail(b, 'Marmite/mt_caisse01txy'),
                    retail(b, 'Marmite/mart_caisse2'), b.tex('bs_murplanche1')]
        z = Z
        for k in range(14):
            a = k * 0.62
            r = 6.0 - 0.18 * k
            x, y = cx + r * math.cos(a), cy + r * math.sin(a)
            top = Z + 1.3 * (k + 1)
            cq.box(b, textures[k % len(textures)], x - 1.4, y - 1.4, x + 1.4, y + 1.4, Z - 0.5, top, size=(2.8, 2.8),
                   fit=True)
            z = top
        # the cab on a last stack in the middle, its windows lit
        cq.box(b, retail(b, 'Marmite/brikverte01txy'), cx - 1.6, cy - 1.6, cx + 1.6, cy + 1.6, Z - 0.5, z + 0.6,
               cap=retail(b, 'Marmite/grille01txy'), size=(3.2, 3.2))
        cab = retail(b, 'Marmite/mr_metambre01ty')
        cq.box(b, cab, cx - 1.6, cy + 0.6, cx + 1.6, cy + 1.6, z + 0.6, z + 3.4, size=(3.2, 2.8), fit=True)
        glowing(b, cq.own(b, 'cq_window2', lambda: cp.window(21), 0),
                upright((cx - 1.2, cy + 0.55), (cx + 1.2, cy + 0.55), z + 1.4, z + 3.0), (1.7, 1.45, 1.05))
        cq.lantern(b, (cx, cy - 0.6, z + 3.0), (cx, cy + 0.55, z + 3.0))

    def hull(self, b):
        """A boat's hull up on blocks, too far from any water."""
        hx, hy = 128.0, -70.0
        wood = b.tex('bs_murplanche2')
        for x in (hx - 4.0, hx + 4.0):
            cq.box(b, b.tex('pr_dalfou9'), x - 0.6, hy - 1.0, x + 0.6, hy + 1.0, Z, Z + 1.2)
        keel = [(hx - 7.0, hy, Z + 1.2), (hx + 7.0, hy, Z + 1.2)]
        deck_l = [(hx - 8.0, hy - 2.6, Z + 3.4), (hx + 8.5, hy - 2.6, Z + 3.4)]
        deck_r = [(hx - 8.0, hy + 2.6, Z + 3.4), (hx + 8.5, hy + 2.6, Z + 3.4)]
        b.face(wood, [keel[0], keel[1], deck_l[1], deck_l[0]], (3.0, 3.0))
        b.face(wood, [keel[1], keel[0], deck_r[0], deck_r[1]], (3.0, 3.0))
        b.face(wood, [deck_l[0], deck_l[1], deck_r[1], deck_r[0]], (3.0, 3.0))
        b.face(wood, [keel[0], deck_l[0], deck_r[0]], (3.0, 3.0))
        b.face(wood, [keel[1], deck_r[1], (hx + 10.5, hy, Z + 3.8), deck_l[1]], (3.0, 3.0))
        glowing(b, retail(b, 'Marmite/mt_hublot03txy'), upright((hx - 3.0, hy - 2.0), (hx - 1.0, hy - 2.0), Z + 2.0,
                                                                 Z + 3.0), (1.0, 1.0, 1.0))


DISTRICTS = (Strip(), Heights(), Downtown(), Junkyard())


# ---- what crazytown asks of them all (the old quarter included) ----

def outside(x, y):
    """How far (x, y) is outside every district, the old quarter's included (0 inside one)."""
    return min([cq.outside(x, y)] + [d.outside(x, y) for d in DISTRICTS])


def new_outside(x, y):
    """... the new districts' only."""
    return min(d.outside(x, y) for d in DISTRICTS)


def nearest(x, y):
    """(the nearest district, crazyquarter for the old quarter, and how far outside it (x, y) is)."""
    best, out = cq, cq.outside(x, y)
    for d in DISTRICTS:
        o = d.outside(x, y)
        if o < out:
            best, out = d, o
    return best, out


def floor_at(x, y):
    """The floor's height at (x, y) inside a district, else None (the land's own)."""
    d, out = nearest(x, y)
    if out > 0.01:
        return None
    return cq.floor_at(x, y) if d is cq else d.floor_at(x, y)


def pads():
    return [p for d in DISTRICTS for p in d.pads]


def paths():
    return [p for d in DISTRICTS for p in d.paths]


def lamps():
    return [l for d in DISTRICTS for l in d.lamps()]


def build(b, land):
    for d in DISTRICTS:
        d.build(b, land)


# ---- who is about: characters borrowed from the retail levels ----

# placements(): the retail game's characters, standing about Crazy City playing their first animation over and over (the .ttlv PRSO
# records of the converted levels, copied: their geometry, their looping animation, their textures; no scripts, no
# collision: they are scenery). (donor level, instance name in its _cast.txt, where (x, y, floor z), turned by (radians
# about z, from how they stand in their own level)).
def placements():
    """Who stands where, made when asked: it uses crazytown's values, not there yet while this module loads."""
    return [
        ('pyr_fou', 'PAT_Fou', (14.0, 97.5, Heights.Z1), 0.4),                       # the madman by the throne
        ('pyr_fou', 'PAT_Fou', (-120.0, 9.0, Z), 2.6),                               # ... and his twin on the promenade
        ('bar', 'PAT_Barman_Astuce01', (-108.0, 9.5, Z), 3.1),                      # outside the diner
        ('cock01', 'PAT_FrigoPatineur01', (0.5, -129.5, Z + 0.16), 0.0),             # fridges skating in the plaza
        ('cock01', 'PAT_FrigoPatineur02', (-1.0, -141.0, Z + 0.16), 2.1),
        ('cock01', 'PAT_FrigoPatineur03', (11.5, -140.5, Z + 0.16), 4.2),
        ('Transit', 'HEG_Canard01', (-130.0, -32.0, ct.WATER_Z - 0.3), 0.3),         # ducks on the lagoon
        ('Transit', 'HEG_Canard02', (-126.0, -36.0, ct.WATER_Z - 0.3), 1.9),
        ('Transit', 'HEG_Canard03', (-136.0, -30.0, ct.WATER_Z - 0.3), 4.0),
        ('Transit', 'HEG_MoutonArgile01', (-143.0, -5.0, DISTRICTS[0].beach_z(-5.0)), 1.0),  # a clay sheep sunbathing
        ('sud0', 'HEG_Champi-Boxeur', (-34.0, -108.0, Z + 0.16), 0.8),               # boxing mushrooms in the park
        ('sud0', 'HEG_Champi-Boxeur2', (-24.0, -114.0, Z + 0.16), 3.6),
        ('sud0', 'HEG_Doc', (-31.0, 144.5, Heights.Z3), 3.0),                        # Doc at his observatory
        ('Land', 'LGE_Sluggy_1', (-20.0, 124.5, Heights.Z2), 0.5),                   # snails on the villas' lawns
        ('Land', 'LGE_Sluggy_2', (6.0, 125.5, Heights.Z2), 2.5),
        ('mammyon', 'PAT_Mammouth01', (110.0, -40.0, Z), 1.2),                       # a mammoth in the scrapyard
        ('death', 'SDO_SuzyDM', (-118.0, -58.5, Strip.PIER_Z), 3.1),                 # Suzy at the end of the pier
    ]


def _cast_prso(ttlv_dir, level, instance):
    """The PRSO index of a cast perso, from the level's listing (LEVEL_cast.txt: "... prso N ..."), or None."""
    path = os.path.join(ttlv_dir, level + '_cast.txt')
    if not os.path.exists(path):
        return None
    for line in open(path, encoding='latin-1'):
        words = line.split()
        if len(words) > 3 and words[2] == instance and 'prso' in words:
            k = int(words[words.index('prso') + 1])
            return k if k >= 0 else None
    return None


def _prso_entries(path):
    """A converted level's texture paths (relative to its folder) and PRSO records split into (world, head bytes:
    channels, samples, rate, parents; parts [(channel, object, (texture, flags, su, sv, frames), geometry bytes)];
    samples bytes)."""
    c = chunks.read(path, 'TTLV')
    t, o = c['TEXS'], 4
    textures = []
    for _ in range(struct.unpack_from('<I', t)[0]):
        n, = struct.unpack_from('<H', t, o)
        textures.append(t[o + 2:o + 2 + n].decode())
        o += 2 + n
    d, o = c['PRSO'], 4
    entries = []
    for _ in range(struct.unpack_from('<I', d)[0]):
        world = struct.unpack_from('<12f', d, o)
        n_ch, n_samples = struct.unpack_from('<2H', d, o + 48)
        head = d[o + 48:o + 56 + 2 * n_ch]
        o += 56 + 2 * n_ch
        n_parts, = struct.unpack_from('<I', d, o)
        o += 4
        parts = []
        for _ in range(n_parts):
            ch, obj = struct.unpack_from('<2H', d, o)
            tex, flags, su, sv, n_frames = struct.unpack_from('<iI2fH', d, o + 4)
            frames = [struct.unpack_from('<if', d, o + 24 + 8 * k) for k in range(n_frames)]
            o += 24 + 8 * n_frames
            nv, ni = struct.unpack_from('<II', d, o)
            size = 8 + 52 * nv + 4 * ni
            parts.append((ch, obj, (tex, flags, su, sv, frames), d[o:o + size]))
            o += size
        size = n_samples * n_ch * 50
        entries.append((world, head, parts, d[o:o + size]))
        o += size
    return textures, entries


def persos_chunk(ttlv_dir, level_dir, names):
    """Crazy City's PRSO chunk (crazytown.finish): each of placements() copied from its level in ttlv_dir, set where it
    stands here (its own scale kept); its textures added to names (paths relative to level_dir), the parts' textures
    renumbered to them. Those whose level or record is missing are left out (the city has them or not, nothing more)."""
    index = {n: k for k, n in enumerate(names)}
    cache, blobs = {}, []
    for level, instance, (x, y, z), turn in placements():
        k = _cast_prso(ttlv_dir, level, instance)
        path = os.path.join(ttlv_dir, level + '.ttlv')
        if k is None or not os.path.exists(path):
            continue
        if level not in cache:
            cache[level] = _prso_entries(path)
        textures, entries = cache[level]
        if k >= len(entries) or struct.unpack_from('<H', entries[k][1], 2)[0] == 0:
            continue  # no record, or nothing to play
        world, head, parts, samples = entries[k]
        c, s = math.cos(turn), math.sin(turn)
        m = list(world)
        for col in range(3):  # turned about z where it stands in its own level, then moved here
            a, b = m[col], m[4 + col]
            m[col], m[4 + col] = c * a - s * b, s * a + c * b
        m[3], m[7], m[11] = x, y, z

        def remap(t):
            if not 0 <= t < len(textures):
                return -1
            rel = os.path.relpath(os.path.normpath(os.path.join(ttlv_dir, textures[t])), level_dir).replace(os.sep, '/')
            if rel not in index:
                index[rel] = len(names)
                names.append(rel)
            return index[rel]
        body = b''
        for ch, obj, (tex, flags, su, sv, frames), geo in parts:
            body += struct.pack('<2H', ch, obj) + ig.surface(remap(tex), flags, (su, sv),
                                                             [(remap(t), sec) for t, sec in frames]) + geo
        blobs.append(struct.pack('<12f', *m) + head + struct.pack('<I', len(parts)) + body + samples)
    return struct.pack('<I', len(blobs)) + b''.join(blobs)

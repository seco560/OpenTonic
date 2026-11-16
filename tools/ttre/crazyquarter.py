"""Crazy Town's old quarter (2026-11-16, the user's concept layout and art; crazytown.py builds it into the level).

The user's sketch: a covered alley running west to east, a dead end at its west end (a house front and a door,
"Interior?"), steps half way along, a ROCK poster on its north wall, a green bottle-shaped sign hanging over it; at its
east end it turns north up more steps, under a big arch, into a sunny square of crooked houses with a fountain in the
middle, the teal mushroom kiosk selling the news and a striped stall. Three views were drawn: (1) from the alley's
west end (a lamp-lit house front, the bottle sign, a barrel, the tunnel going on into purple light), (2) from its east
end (the tunnel west, the big arch north and the fountain through it), (3) the square (leaning houses, curly
balconies, a red awning rolled into a spiral, a pink round sign). Ours, the user having asked to "go a bit crazy": the
door at the dead end opens into the tavern of the "Interior?" picture (a round barrel counter with a shield on it); an
upside-down house standing on its roof's point in the square; a stair up a house's front to a door in mid-air; a house
that looks at you. All of it is ours in shape and size: nothing of the cut level's own survives (docs/SE.md).

Where (Crazy Town's units, Ed's metre): the quarter lies east of the town, through the cliffs (the cliffs give way
round it: Land.quarter). The alley is x 70..109, y 3..11 (walls 0.6 thick), its west half on the town's level ZA, its
east half a flight up (ZB), the square another flight up (ZC). Its entrance is an arch in the alley's south wall at
x 76.5, where the town's east road (once closed for works) now leads.
"""
import math
import os
import random

from . import crazypaint
from . import crazytown as ct
from . import import_gltf as ig

ZA, ZB, ZC = 0.6, 2.6, 4.6         # the alley's west half, its east half, the square
RISE, TREAD = 0.4, 0.8             # the steps (Ed walks up 0.4: crazytown.STEP_RISE)
ALLEY = (70.0, 3.0, 109.0, 11.0)   # inside
WALL = 0.6
SPRING = ZB + 4.0                  # the vault springs here; a half circle over the alley's 8 width
VAULT_R = 4.0
BLOCK_TOP = SPRING + VAULT_R + 0.6  # the top of the alley's stone, where the houses over it stand
ARCH_X, ARCH_W = 76.5, 4.0         # the entrance, in the south wall
STAIRS1_X = 81.0                   # the alley's steps, eastward
BRANCH = (100.0, 109.0)            # the way north to the square
STAIRS2_Y = 12.0
TAVERN = (62.0, 3.6, 69.4, 10.4)   # inside; its door in the alley's west end
TAVERN_H = 4.6
SQUARE = (94.0, 19.0, 130.0, 38.0)
NOOK = (94.0, 38.0, 104.0, 43.0)
FOUNTAIN, KIOSK, STALL = (111.0, 28.0), (122.0, 26.5), (124.5, 21.5)
TOPSY = (99.5, 33.5)               # the upside-down house
WARM, DUSK = (1.0, 0.6, 0.28), (0.36, 0.28, 0.55)

# The houses round the square and the alley: (x0, y0, x1, y1 base; top over ZC; wall; seed). Their sides that face
# the square or the way up are its walls.
HOUSES = [
    (86.0, 11.6, 100.0, 19.0, 9.0, 'mr_pierrocre', 1),
    (109.0, 11.6, 118.0, 19.0, 10.0, 'xx_foutete02', 2),
    (118.0, 11.6, 131.0, 19.0, 13.0, 'mr_maisonfou', 3),
    (130.0, 19.0, 137.0, 28.0, 12.0, 'mr_pierrocre', 4),
    (130.0, 28.0, 138.0, 38.0, 15.0, 'xx_foutete02', 5),
    (121.0, 38.0, 131.0, 46.0, 11.0, 'mr_fou5', 6),
    (112.0, 38.0, 121.0, 46.0, 14.0, 'mr_pierrocre', 7),
    (104.0, 38.0, 112.0, 46.0, 10.0, 'mr_poisvert', 8),
    (94.0, 43.0, 104.0, 50.0, 12.0, 'mr_pierrocre', 9),
    (87.0, 31.0, 94.0, 43.0, 12.0, 'xx_foutete', 10),
    (87.0, 19.0, 94.0, 31.0, 10.0, 'mr_fou1', 11),
]
# what is solid ground for the quarter (the land under it is let down out of sight: Land.quarter)
OUTLINE = [(69.4, 2.4, 109.6, 11.6), (61.4, 3.0, 69.4, 11.0), (100.0, 11.6, 109.0, 19.0), SQUARE, NOOK] + \
    [h[:4] for h in HOUSES]


def outside(x, y):
    """How far (x, y) is outside the quarter's outline (0 inside)."""
    best = math.inf
    for x0, y0, x1, y1 in OUTLINE:
        dx = max(x0 - x, 0.0, x - x1)
        dy = max(y0 - y, 0.0, y - y1)
        best = min(best, math.hypot(dx, dy))
    return best


def floor_at(x, y):
    """The floor's height at (x, y) in the quarter (for the light: the foot of its walls is darker)."""
    if TAVERN[0] - 1 <= x <= ALLEY[2] + 1 and y <= ALLEY[3] + 0.6:
        return ZA + (ZB - ZA) * min(1.0, max(0.0, (x - STAIRS1_X) / (5 * TREAD)))
    if BRANCH[0] <= x <= BRANCH[1] and y <= SQUARE[1]:
        return ZB + (ZC - ZB) * min(1.0, max(0.0, (y - STAIRS2_Y) / (5 * TREAD)))
    return ZC


def lamps():
    """Its lanterns, for the light (crazytown.Lighting): (position, rgb, reach)."""
    w = WARM
    out = [((72.0, 10.3, ZA + 3.4), tuple(c * 1.2 for c in w), 7.0), ((78.5, 3.6, ZA + 3.6), w, 6.0),
           ((90.0, 10.3, ZB + 3.4), tuple(c * 1.1 for c in w), 7.0), ((98.0, 3.6, ZB + 3.4), w, 6.0),
           ((106.0, 3.6, ZB + 3.2), w, 5.5), ((104.5, 12.5, ZB + 2.5), DUSK, 8.0),
           ((74.0, 1.7, ZA + 3.4), w, 6.5), ((79.0, 1.7, ZA + 3.4), w, 6.5),
           ((65.0, 7.0, ZA + 3.6), tuple(c * 1.3 for c in w), 8.0), ((67.6, 4.6, ZA + 3.2), w, 5.0)]
    for p in POSTS:
        out.append(((p[0] + 0.7, p[1], ZC + 3.0), tuple(c * 1.1 for c in w), 10.0))
    out.append(((KIOSK[0] - 1.9, KIOSK[1], ZC + 1.5), (0.9, 0.7, 0.35), 4.0))
    return out


POSTS = [(97.5, 22.5), (126.5, 22.5), (127.0, 35.0), (101.5, 39.5)]


def covered():
    """Where the sun does not reach (crazytown.Lighting.covered): the alley, the tavern."""
    x0, y0, x1, y1 = ALLEY
    t = TAVERN
    return [(x0 - 0.05, y0 - 0.05, x1 + 0.05, y1 + 0.05, -5.0, BLOCK_TOP - 0.5, 0.3),
            (t[0] - 0.05, t[1] - 0.05, t[2] + 0.05, t[3] + 0.05, -5.0, ZA + TAVERN_H + 0.05, 0.22)]


# ---- building blocks ----

def own(b, name, picture, flags=ct.WRAP):
    """A texture painted here (crazypaint), into the level's own folder."""
    if name not in b.copied:
        path = os.path.join(b.tex_dir, name + '.png')
        ct.save_png(path, *picture())
        b.copied[name] = path
    return b.copied[name], flags


def quad(b, key, pts, want=None, size=(3.0, 3.0), solid=True, fit=False, lit=None):
    """A flat polygon; turned round when its face looks away from `want`."""
    if want is not None:
        n = ct.cross(ct.sub(pts[1], pts[0]), ct.sub(pts[2], pts[0]))
        if ct.dot(n, want) < 0:
            pts = list(reversed(pts))
    b.face(key, pts, size, solid=solid, fit=fit, lit=lit)


def floor(b, key, x0, y0, x1, y1, z, tile=4.0, step=1.6, solid=True):
    """A level floor in cells (for the light's pools), its texture laid in world space (no seams between cells)."""
    nx, ny = max(1, math.ceil((x1 - x0) / step)), max(1, math.ceil((y1 - y0) / step))
    for i in range(nx):
        for j in range(ny):
            xa, xb = x0 + (x1 - x0) * i / nx, x0 + (x1 - x0) * (i + 1) / nx
            ya, yb = y0 + (y1 - y0) * j / ny, y0 + (y1 - y0) * (j + 1) / ny
            pts = [(xa, ya, z), (xb, ya, z), (xb, yb, z), (xa, yb, z)]
            uv = [(p[0] / tile, -p[1] / tile) for p in pts]
            for t in ((0, 1, 2), (0, 2, 3)):
                b.tri(key, tuple(pts[k] for k in t), tuple(uv[k] for k in t), solid=solid)


def wall(b, key, a, c, z0, z1, tile=3.0, step=1.6, holes=(), solid=True):
    """An upright wall from a to c (x, y), z0 to z1, its face to the right of a -> c; in cells, its texture in world
    space. holes: (middle along it, half width, spring): an opening from the bottom, a half circle on top."""
    length = math.dist(a, c)
    d = ((c[0] - a[0]) / length, (c[1] - a[1]) / length)
    cuts = {0.0, length}
    s = step
    while s < length:
        cuts.add(s)
        s += step
    for m, hw, _ in holes:
        for k in range(17):
            cuts.add(min(length, max(0.0, m - hw + 2 * hw * k / 16)))
    cuts = sorted(cuts)

    def hole_at(s):
        for m, hw, spring in holes:
            if m - hw < s < m + hw:
                return m, hw, spring
        return None

    def top_of_hole(h, s):
        m, hw, spring = h
        return spring + math.sqrt(max(0.0, hw * hw - (s - m) ** 2))
    for s0, s1 in zip(cuts, cuts[1:]):
        h = hole_at(0.5 * (s0 + s1))
        lo0, lo1 = (top_of_hole(h, s0), top_of_hole(h, s1)) if h else (z0, z0)
        if min(lo0, lo1) >= z1:
            continue
        rows = max(1, math.ceil((z1 - min(lo0, lo1)) / step))
        for r in range(rows):
            za0, za1 = lo0 + (z1 - lo0) * r / rows, lo0 + (z1 - lo0) * (r + 1) / rows
            zb0, zb1 = lo1 + (z1 - lo1) * r / rows, lo1 + (z1 - lo1) * (r + 1) / rows
            p0 = (a[0] + d[0] * s0, a[1] + d[1] * s0)
            p1 = (a[0] + d[0] * s1, a[1] + d[1] * s1)
            pts = [(p0[0], p0[1], za0), (p1[0], p1[1], zb0), (p1[0], p1[1], zb1), (p0[0], p0[1], za1)]
            uv = [(s0 / tile, -za0 / tile), (s1 / tile, -zb0 / tile), (s1 / tile, -zb1 / tile), (s0 / tile, -za1 / tile)]
            for t in ((0, 1, 2), (0, 2, 3)):
                b.tri(key, tuple(pts[k] for k in t), tuple(uv[k] for k in t), solid=solid)


def lining(b, key, a, c, out, thick, hole, z0):
    """The sides and the curved top of an opening (wall(..., holes)) through a wall `thick` deep (out: towards its
    other face)."""
    length = math.dist(a, c)
    d = ((c[0] - a[0]) / length, (c[1] - a[1]) / length)
    m, hw, spring = hole

    def at(s, z, depth):
        return (a[0] + d[0] * s + out[0] * depth, a[1] + d[1] * s + out[1] * depth, z)
    for side in (-1, 1):  # the jambs, facing into the opening
        s = m + side * hw
        quad(b, key, [at(s, z0, 0), at(s, z0, thick), at(s, spring, thick), at(s, spring, 0)],
             (-side * d[0], -side * d[1], 0.0), (2.0, 2.0))
    n = 16
    for k in range(n):  # the curved top, facing down
        t0, t1 = math.pi * k / n, math.pi * (k + 1) / n
        s0, s1 = m - hw * math.cos(t0), m - hw * math.cos(t1)
        z_0, z_1 = spring + hw * math.sin(t0), spring + hw * math.sin(t1)
        mid = 0.5 * (t0 + t1)
        quad(b, key, [at(s0, z_0, 0), at(s1, z_1, 0), at(s1, z_1, thick), at(s0, z_0, thick)],
             (d[0] * math.cos(mid), d[1] * math.cos(mid), -math.sin(mid)), (2.0, 2.0))


def box(b, key, x0, y0, x1, y1, z0, z1, cap=None, size=(2.0, 2.0), solid=True, fit=False):
    bottom = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)]
    b.prism(bottom, [(p[0], p[1], z1) for p in bottom], key, cap=cap or key, size=size, solid=solid, fit=fit)


def cylinder(b, key, centre, r, z0, z1, n=12, cap=None, solid=True, size=(2.0, 2.0), r1=None):
    bottom = ct.ring(centre, r, n, z0)
    top = ct.ring(centre, r if r1 is None else r1, n, z1)
    b.prism(bottom, top, key, cap=cap, size=size, solid=solid)


def lantern(b, at, bracket=None):
    """A lantern: a glowing ball (crossed cut-outs, lit full) under a little iron cap; bracket: the wall point it
    hangs from."""
    glow = b.tex('X_Ampdc', 0)
    lit = ig.baked_light((1.9, 1.75, 1.3))
    x, y, z = at
    r = 0.32
    for k in range(3):
        a = k * math.pi / 3
        dx, dy = math.cos(a) * r, math.sin(a) * r
        pts = [(x - dx, y - dy, z - r), (x + dx, y + dy, z - r), (x + dx, y + dy, z + r), (x - dx, y - dy, z + r)]
        uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
        for t in ((0, 1, 2), (0, 2, 3)):
            b.tri(glow, tuple(pts[i] for i in t), tuple(uvs[i] for i in t), lit=lit, solid=False)
    iron = b.tex('pt_aciervf')
    cylinder(b, iron, (x, y), 0.38, z + r - 0.02, z + r + 0.22, 6, cap=iron, solid=False, r1=0.08)
    if bracket:
        bx, by, bz = bracket
        top = z + r + 0.22
        dx, dy = x - bx, y - by
        n = math.hypot(dx, dy) or 1.0
        sx, sy = -dy / n * 0.05, dx / n * 0.05
        b.prism([(bx - sx, by - sy, top - 0.05), (x - sx, y - sy, top - 0.05), (x + sx, y + sy, top - 0.05),
                 (bx + sx, by + sy, top - 0.05)],
                [(bx - sx, by - sy, top + 0.05), (x - sx, y - sy, top + 0.05), (x + sx, y + sy, top + 0.05),
                 (bx + sx, by + sy, top + 0.05)], iron, solid=False)


def lamp_post(b, x, y, z):
    """A street lamp: an iron pole, a curled arm, a lantern hanging from it (lamps(): its light)."""
    iron = b.tex('pt_aciervf')
    box(b, iron, x - 0.12, y - 0.12, x + 0.12, y + 0.12, z - 0.3, z + 3.7, size=(0.5, 2.0))
    cylinder(b, iron, (x, y), 0.35, z - 0.3, z + 0.35, 8, cap=iron)
    n = 6
    for k in range(n):  # the arm, curling out and down
        a0, a1 = math.pi * k / n, math.pi * (k + 1) / n
        p0 = (x + 0.35 * (1 - math.cos(a0)), z + 3.55 + 0.25 * math.sin(a0))
        p1 = (x + 0.35 * (1 - math.cos(a1)), z + 3.55 + 0.25 * math.sin(a1))
        quad(b, iron, [(p0[0], y - 0.05, p0[1]), (p1[0], y - 0.05, p1[1]), (p1[0], y - 0.05, p1[1] + 0.08),
                       (p0[0], y - 0.05, p0[1] + 0.08)], solid=False)
    lantern(b, (x + 0.7, y, z + 3.0))


class House:
    """A crooked house on an upright-walled base (x0, y0, x1, y1), from under the floors to `top`: upright up to
    `upright` (out of Ed's reach, jumping: a wall leaning over him is an overhang his collision pops him out of; the
    wander check found it on the way north), then its top turned and shifted, wider; a slate roof. face_point /
    face_quad put things on its sides (side k from corner k to k + 1, corners counter-clockwise from (x0, y0))."""

    def __init__(self, b, base, z0, top, wall_tex, seed, roof=True, solid=True, upright=None):
        rng = random.Random(seed)
        x0, y0, x1, y1 = base
        self.z0, self.top = (z0 if upright is None else upright), top
        cx, cy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
        self.bottom = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)]
        twist = math.radians(rng.uniform(-5, 5))
        lean = (rng.uniform(-0.9, 0.9), rng.uniform(-0.9, 0.9))
        grow = rng.uniform(1.03, 1.1)
        c, s = math.cos(twist), math.sin(twist)
        self.topring = [(cx + lean[0] + grow * (c * (p[0] - cx) - s * (p[1] - cy)),
                         cy + lean[1] + grow * (s * (p[0] - cx) + c * (p[1] - cy)), top) for p in self.bottom]
        if upright is not None:
            lower = [(p[0], p[1], z0) for p in self.bottom]
            self.bottom = [(p[0], p[1], upright) for p in self.bottom]
            b.prism(lower, self.bottom, b.tex(wall_tex), size=(5.0, 5.0), solid=solid)
        b.prism(self.bottom, self.topring, b.tex(wall_tex), cap=b.tex('bs_pon1nz'), size=(5.0, 5.0), solid=solid)
        if roof:
            eave = [(cx + lean[0] + 1.12 * (p[0] - cx - lean[0]), cy + lean[1] + 1.12 * (p[1] - cy - lean[1]), top)
                    for p in self.topring]
            apex = (cx + lean[0] * 1.6 + rng.uniform(-1, 1), cy + lean[1] * 1.6 + rng.uniform(-1, 1),
                    top + rng.uniform(3.5, 6.0))
            slate = b.tex('tt_ardoise')
            b.face(b.tex('bs_pon1nz'), list(reversed(eave)), (2.0, 2.0))
            for k in range(4):
                b.face(slate, [eave[k], eave[(k + 1) % 4], apex], (2.5, 2.5))
            if rng.random() < 0.7:  # a crooked chimney through the roof
                k = rng.randrange(4)
                p = ct.add(ct.mul(eave[k], 0.55), ct.mul(apex, 0.45))
                tip = (p[0] + rng.uniform(-0.7, 0.7), p[1] + rng.uniform(-0.7, 0.7), p[2] + rng.uniform(2.5, 4.0))
                bot = ct.ring((p[0], p[1]), 0.5, 4, p[2] - 1.5, math.radians(45))
                b.prism(bot, ct.ring((tip[0], tip[1]), 0.6, 4, tip[2], math.radians(45)), b.tex('mr_pierrbleu'),
                        cap=b.tex('rc_doc03'), size=(1.5, 1.5))

    def face_point(self, k, u, z, out=0.0):
        b0, b1 = self.bottom[k], self.bottom[(k + 1) % 4]
        t0, t1 = self.topring[k], self.topring[(k + 1) % 4]
        bot = ct.add(b0, ct.mul(ct.sub(b1, b0), u))
        top = ct.add(t0, ct.mul(ct.sub(t1, t0), u))
        t = max(0.0, (z - self.z0) / (self.top - self.z0))  # below an upright part: on its plane
        p = ct.add(bot, ct.mul(ct.sub(top, bot), t))
        return ct.add(p, ct.mul(self.normal(k), out))

    def normal(self, k):
        b0, b1 = self.bottom[k], self.bottom[(k + 1) % 4]
        return ct.norm(ct.cross(ct.sub(b1, b0), ct.sub(self.topring[k], b0)))

    def width(self, k):
        return math.dist(self.bottom[k], self.bottom[(k + 1) % 4])

    def face_quad(self, k, u0, u1, z0, z1, out=0.06):
        return [self.face_point(k, u0, z0, out), self.face_point(k, u1, z0, out), self.face_point(k, u1, z1, out),
                self.face_point(k, u0, z1, out)]

    def windows(self, b, k, floor_z, rows, seed, skip=()):
        """Lit windows on side k, a row per storey (3 apart) from floor_z + 1.3."""
        rng = random.Random(seed)
        w = self.width(k)
        cols = max(1, int(w / 2.6))
        lit = ig.baked_light((1.7, 1.45, 1.05))
        for r in range(rows):
            z = floor_z + 1.3 + 3.0 * r
            if z + 1.6 > self.top - 0.3:
                break
            for col in range(cols):
                if (r, col) in skip or rng.random() < 0.18:
                    continue
                u = (col + 0.5) / cols + rng.uniform(-0.04, 0.04)
                hw = 0.55 / w
                pic = own(b, f'cq_window{seed % 3}', lambda s=seed % 3: crazypaint.window(19 + s), 0)
                b.face(pic, self.face_quad(k, u - hw, u + hw, z, z + 1.5 + rng.uniform(-0.2, 0.3)), fit=True,
                       solid=False, lit=lit)

    def door(self, b, k, u, floor_z, tex='bs_murplanche1'):
        hw = 0.95 / self.width(k)
        b.face(b.tex(tex), self.face_quad(k, u - hw, u + hw, floor_z - 0.05, floor_z + 2.9), fit=True, solid=False)


# ---- the parts ----

def alley(b):
    x0, y0, x1, y1 = ALLEY
    stone, vault = b.tex('mr_pierrocre'), b.tex('mr_pierrocre')
    flags = own(b, 'cq_flags', crazypaint.flagstones)
    low = ZA - 3.0
    arch = (ARCH_X - x0, ARCH_W / 2, ZA + 2.6)
    # the inner walls
    wall(b, stone, (x1, y0), (x0, y0), low, SPRING, holes=[(x1 - ARCH_X, ARCH_W / 2, ZA + 2.6)])
    wall(b, stone, (x0, y1), (BRANCH[0], y1), low, SPRING)
    wall(b, stone, (x0, y0), (x0, y1), low, SPRING, holes=[(0.5 * (y1 - y0), 1.0, ZA + 2.2)])  # the tavern's door
    wall(b, stone, (x1, y1), (x1, y0), low, SPRING)
    # the outer walls (the quarter seen from the town)
    wall(b, stone, (x0 - WALL, y0 - WALL), (x1 + WALL, y0 - WALL), low, BLOCK_TOP,
         holes=[(ARCH_X - x0 + WALL, ARCH_W / 2, ZA + 2.6)])
    wall(b, stone, (BRANCH[0], y1 + WALL), (x0 - WALL, y1 + WALL), low, BLOCK_TOP)
    wall(b, stone, (x1 + WALL, y0 - WALL), (x1 + WALL, y1 + WALL), low, BLOCK_TOP)
    wall(b, stone, (x1 + WALL, y1 + WALL), (BRANCH[0], y1 + WALL), SPRING, BLOCK_TOP)  # over the way north
    wall(b, stone, (x0 - WALL, y1 + WALL), (x0 - WALL, y1), low, BLOCK_TOP)  # the west end, past the tavern
    wall(b, stone, (x0 - WALL, y0), (x0 - WALL, y0 - WALL), low, BLOCK_TOP)
    wall(b, stone, (x0 - WALL, TAVERN[3] + 0.6), (x0 - WALL, TAVERN[1] - 0.6), ZA + TAVERN_H, BLOCK_TOP)
    lining(b, stone, (x1, y0), (x0, y0), (0.0, -1.0), WALL, (x1 - ARCH_X, ARCH_W / 2, ZA + 2.6), ZA - 0.05)
    lining(b, stone, (x0, y0), (x0, y1), (-1.0, 0.0), WALL, (0.5 * (y1 - y0), 1.0, ZA + 2.2), ZA - 0.05)
    # the north wall's end at the way north, and its lintel
    quad(b, stone, [(BRANCH[0], y1, low), (BRANCH[0], y1 + WALL, low), (BRANCH[0], y1 + WALL, SPRING),
                    (BRANCH[0], y1, SPRING)], (1.0, 0.0, 0.0))
    quad(b, stone, [(BRANCH[0], y1, SPRING), (x1, y1, SPRING), (x1, y1 + WALL, SPRING), (BRANCH[0], y1 + WALL, SPRING)],
         (0.0, 0.0, -1.0))
    # the vault: a half circle over the alley, x0 to x1, and its two ends
    n, cy = 12, 0.5 * (y0 + y1)

    def arc(x, k):
        t = math.pi * k / n
        return (x, cy + VAULT_R * math.cos(t), SPRING + VAULT_R * math.sin(t))
    xs = [x0 + (x1 - x0) * i / 26 for i in range(27)]
    for xa, xb in zip(xs, xs[1:]):
        for k in range(n):
            pts = [arc(xa, k), arc(xb, k), arc(xb, k + 1), arc(xa, k + 1)]
            mid = math.pi * (k + 0.5) / n
            want = (0.0, -math.cos(mid), -math.sin(mid))
            uv = [(p[0] / 3.0, VAULT_R * math.pi * kk / n / 3.0) for p, kk in zip(pts, (k, k, k + 1, k + 1))]
            n_ = ct.cross(ct.sub(pts[1], pts[0]), ct.sub(pts[2], pts[0]))
            order = (0, 1, 2, 3) if ct.dot(n_, want) > 0 else (3, 2, 1, 0)
            pts, uv = [pts[i] for i in order], [uv[i] for i in order]
            for t in ((0, 1, 2), (0, 2, 3)):
                b.tri(vault, tuple(pts[i] for i in t), tuple(uv[i] for i in t))
    for x, want in ((x0, 1.0), (x1, -1.0)):
        centre = (x, cy, SPRING)
        for k in range(n):
            quad(b, vault, [centre, arc(x, k), arc(x, k + 1)], (want, 0.0, 0.0), (3.0, 3.0))
    # the floors and the steps
    floor(b, flags, x0, y0, STAIRS1_X, y1, ZA)
    floor(b, flags, STAIRS1_X + 5 * TREAD, y0, x1, y1, ZB)
    floor(b, flags, ARCH_X - ARCH_W / 2 - 1.0, y0 - WALL - 2.4, ARCH_X + ARCH_W / 2 + 1.0, y0 - WALL, ZA)  # its doorstep
    floor(b, flags, ARCH_X - ARCH_W / 2, y0 - WALL, ARCH_X + ARCH_W / 2, y0, ZA)  # through the arch
    for k in range(5):
        xa = STAIRS1_X + TREAD * k
        top = ZA + RISE * (k + 1)
        step(b, xa, y0, xa + TREAD + 0.02, y1, top)
    # the houses over it, from the town's side; lit windows on their fronts, glowing over the arch
    for i, (xa, xb, h, tex) in enumerate(((x0 - WALL, 80.0, 6.5, 'mr_maisonfou'), (80.0, 92.0, 8.0, 'mr_fou4'),
                                          (92.0, x1 + WALL, 5.5, 'mr_pierrocre'))):
        hs = House(b, (xa, y0 - WALL, xb, y1 + WALL), BLOCK_TOP, BLOCK_TOP + h, tex, 40 + i)
        hs.windows(b, 0, BLOCK_TOP - 0.6, 3, 41 + i)
        hs.windows(b, 2, BLOCK_TOP - 0.6, 2, 51 + i)
    lit = ig.baked_light((1.7, 1.45, 1.05))
    for x in (71.5, 82.5, 87.5):  # windows down the outer south wall, beside the arch
        pic = own(b, 'cq_window1', lambda: crazypaint.window(20), 0)
        b.face(pic, [(x - 0.6, y0 - WALL - 0.05, ZA + 4.2), (x + 0.6, y0 - WALL - 0.05, ZA + 4.2),
                     (x + 0.6, y0 - WALL - 0.05, ZA + 5.9), (x - 0.6, y0 - WALL - 0.05, ZA + 5.9)], fit=True,
               solid=False, lit=lit)
    # its lanterns: both sides of the arch outside, along the walls inside
    for x in (74.0, 79.0):
        lantern(b, (x, y0 - WALL - 0.7, ZA + 3.4), (x, y0 - WALL, ZA + 3.75))
    for x, y, z, wy in ((72.0, y1 - 0.7, ZA + 3.4, y1), (78.5, y0 + 0.6, ZA + 3.6, y0), (90.0, y1 - 0.7, ZB + 3.4, y1),
                        (98.0, y0 + 0.6, ZB + 3.4, y0), (106.0, y0 + 0.6, ZB + 3.2, y0)):
        lantern(b, (x, y, z), (x, wy, z + 0.35))
    # the dead end: a house front with its door (open: the tavern), a little roof over it, a bench, a barrel
    planks = b.tex('bs_pon1nz')
    roof = [(x0 + 0.02, y0 + 0.4, ZA + 4.4), (x0 + 1.4, y0 + 0.4, ZA + 3.7), (x0 + 1.4, y1 - 0.4, ZA + 3.7),
            (x0 + 0.02, y1 - 0.4, ZA + 4.4)]
    b.prism(roof, [(p[0], p[1], p[2] + 0.2) for p in roof], planks, cap=b.tex('tt_ardoise'), size=(2.0, 2.0),
            solid=False)
    pic = own(b, 'cq_window2', lambda: crazypaint.window(21), 0)
    for y in (y0 + 1.4, y1 - 1.4):
        b.face(pic, [(x0 + 0.05, y - 0.6, ZA + 1.2), (x0 + 0.05, y + 0.6, ZA + 1.2), (x0 + 0.05, y + 0.6, ZA + 2.8),
                     (x0 + 0.05, y - 0.6, ZA + 2.8)], fit=True, solid=False, lit=lit)
    seat = b.tex('pr_dalfou4')
    for x in (72.6, 74.8):
        box(b, seat, x - 0.3, y1 - 0.75, x + 0.3, y1 - 0.15, ZA - 0.1, ZA + 0.45)
    box(b, seat, 72.0, y1 - 0.85, 75.4, y1 - 0.05, ZA + 0.45, ZA + 0.62)
    barrel(b, (71.2, y0 + 0.9), ZA)
    barrel(b, (72.4, y0 + 0.75), ZA, 0.45)
    # the bottle sign over the alley, hanging from the north wall
    sign_x = 80.0
    iron = b.tex('pt_aciervf')
    b.prism([(sign_x - 0.05, y1 - 1.4, ZA + 4.55), (sign_x + 0.05, y1 - 1.4, ZA + 4.55), (sign_x + 0.05, y1, ZA + 4.55),
             (sign_x - 0.05, y1, ZA + 4.55)],
            [(sign_x - 0.05, y1 - 1.4, ZA + 4.65), (sign_x + 0.05, y1 - 1.4, ZA + 4.65), (sign_x + 0.05, y1, ZA + 4.65),
             (sign_x - 0.05, y1, ZA + 4.65)], iron, solid=False)
    bottle = own(b, 'cq_bottle', crazypaint.bottle_sign, ct.ALPHA_TEST)
    for xo, flip in ((-0.02, False), (0.02, True)):
        pts = [(sign_x + xo, y1 - 2.0, ZA + 2.4), (sign_x + xo, y1 - 0.6, ZA + 2.4), (sign_x + xo, y1 - 0.6, ZA + 4.5),
               (sign_x + xo, y1 - 2.0, ZA + 4.5)]
        quad(b, bottle, pts, (-1.0 if not flip else 1.0, 0.0, 0.0), fit=True, solid=False)
    # posters on the north wall: ROCK, the skull, ROCK again (concept, view 2)
    rock = own(b, 'cq_rock', lambda: crazypaint.banner('ROCK'), ct.ALPHA_TEST)
    rock2 = own(b, 'cq_rock2', lambda: crazypaint.banner('ROCK', (40, 110, 160), (255, 120, 190), 9), ct.ALPHA_TEST)
    for x, pic in ((91.6, rock), (94.2, b.tex('dc_panomort', 0)), (96.8, rock2)):
        y = y1 - 0.04
        b.face(pic, [(x - 0.75, y, ZB + 1.3), (x + 0.75, y, ZB + 1.3), (x + 0.75, y, ZB + 4.2), (x - 0.75, y, ZB + 4.2)],
               fit=True, solid=False)


def step(b, x0, y0, x1, y1, top):
    """A stone step: flagstones on top, its riser of the alley's stone; deep enough to reach under the one before
    (crazytown's tower: a riser hanging over a floor is an overhang Ed cannot step up)."""
    bottom = [(x0, y0, top - RISE - 0.45), (x1, y0, top - RISE - 0.45), (x1, y1, top - RISE - 0.45),
              (x0, y1, top - RISE - 0.45)]
    b.prism(bottom, [(p[0], p[1], top) for p in bottom], b.tex('mr_pierrocre'),
            cap=own(b, 'cq_flags', crazypaint.flagstones), size=(1.5, 1.5))


def barrel(b, at, z, r=0.55):
    staves = b.tex('bs_murplanche1')
    bands = [(0.0, r * 0.9), (0.55, r), (1.1, r * 0.9)]
    for (za, ra), (zb, rb) in zip(bands, bands[1:]):
        cylinder(b, staves, at, ra, z + za, z + zb, 10, r1=rb, size=(1.2, 1.2))
    b.face(b.tex('bs_pon1nz'), ct.ring(at, r * 0.9, 10, z + 1.1), (1.0, 1.0))


def way_north(b):
    """From the alley's east end up the steps, under the big arch (concept, view 2), to the square."""
    xa, xb = BRANCH
    flags = own(b, 'cq_flags', crazypaint.flagstones)
    floor(b, flags, xa, ALLEY[3], xb, STAIRS2_Y, ZB)
    for k in range(5):
        ya = STAIRS2_Y + TREAD * k
        step(b, xa, ya, xb, ya + TREAD + 0.02, ZB + RISE * (k + 1))
    floor(b, flags, xa, STAIRS2_Y + 5 * TREAD, xb, SQUARE[1], ZC)
    # the big arch: a wall across the way, open from side to side under a half circle, a face for its keystone
    stone = b.tex('mr_pierrbleu')
    y0, y1 = 17.0, 18.2
    hole = (0.5 * (xb - xa), 0.5 * (xb - xa) - 0.35, ZC + 3.2)
    top = ZC + 10.0
    wall(b, stone, (xb, y1), (xa, y1), ZC - 0.5, top, holes=[hole])  # its north face
    wall(b, stone, (xa, y0), (xb, y0), ZC - 0.5, top, holes=[hole])  # its south face, up the steps
    lining(b, stone, (xa, y0), (xb, y0), (0.0, 1.0), y1 - y0, hole, ZC - 0.05)
    quad(b, b.tex('bs_pon1nz'), [(xa, y0, top), (xb, y0, top), (xb, y1, top), (xa, y1, top)], (0.0, 0.0, 1.0))
    crown = hole[2] + hole[1]
    face = b.tex('xx_foutete', 0)  # it watches who comes up the steps
    mid = 0.5 * (xa + xb)
    b.face(face, [(mid - 0.9, y0 - 0.05, crown + 0.1), (mid + 0.9, y0 - 0.05, crown + 0.1),
                  (mid + 0.9, y0 - 0.05, crown + 1.9), (mid - 0.9, y0 - 0.05, crown + 1.9)], fit=True, solid=False)


def houses(b):
    """The square's houses (HOUSES) with their windows, doors, balconies, the awning and the signs."""
    built = []
    for x0, y0, x1, y1, h, tex, seed in HOUSES:
        built.append(House(b, (x0, y0, x1, y1), ZA - 3.0, ZC + h, tex, seed, upright=ZC + 5.5))
    # which side of each looks onto the square (or the way up), and what is on it
    rail = own(b, 'cq_rail', crazypaint.railing, ct.ALPHA_TEST)
    awning = own(b, 'cq_awning', crazypaint.spiral_awning)
    sides = {0: [2], 1: [3, 2], 2: [2], 3: [3], 4: [3], 5: [0], 6: [0], 7: [0, 3], 8: [0], 9: [1], 10: [1]}
    for i, hs in enumerate(built):
        for k in sides.get(i, []):
            hs.windows(b, k, ZC, 4, 100 + 7 * i + k)
            if (i, k) not in ((7, 3), (1, 3), (3, 3)):  # (3, 3): the stair to the door in mid-air is there
                hs.door(b, k, 0.5 + 0.18 * math.sin(i * 2.3), ZC, 'bs_murplanche2' if i % 2 else 'bs_murplanche1')
    # the side house's door at the way up (house 0's east side): it stands on the lower steps' level
    built[0].door(b, 1, 0.25, ZB)
    # balconies: a slab, its curly railing on three sides (concept, view 3)
    for i, k, u, z in ((4, 3, 0.5, ZC + 7.4), (6, 0, 0.45, ZC + 5.2), (2, 2, 0.3, ZC + 4.4)):
        hs = built[i]
        w = 3.0 / hs.width(k)
        a, c = hs.face_point(k, u - w / 2, z, 0.0), hs.face_point(k, u + w / 2, z, 0.0)
        n = hs.normal(k)
        a2, c2 = ct.add(a, ct.mul(n, 1.3)), ct.add(c, ct.mul(n, 1.3))
        slab = [a, c, c2, a2]
        b.prism([(p[0], p[1], p[2] - 0.25) for p in slab], slab, b.tex('pr_dalfou4'), cap=b.tex('pr_dalfou4'),
                size=(1.5, 1.5), solid=False)
        up = (0.0, 0.0, 1.0)
        for p, q in ((a2, c2), (a, a2), (c2, c)):
            for flip in (1, -1):  # both faces (a cut-out seen from the square and from the balcony)
                pts = [p, q, ct.add(q, up), ct.add(p, up)]
                quad(b, rail, pts if flip > 0 else list(reversed(pts)), None, fit=True, solid=False)
        hs.door(b, k, u, z, 'bs_murplanche2')
    # the big red awning rolled into a spiral (concept, view 3): over house 4's door
    hs = built[4]
    a, c = hs.face_point(3, 0.25, ZC + 3.6, 0.0), hs.face_point(3, 0.75, ZC + 3.6, 0.0)
    n = hs.normal(3)
    lo_a, lo_c = ct.add(ct.add(a, ct.mul(n, 2.2)), (0, 0, -0.9)), ct.add(ct.add(c, ct.mul(n, 2.2)), (0, 0, -0.9))
    for flip in (1, -1):
        pts = [lo_a, lo_c, c, a]
        quad(b, awning, pts if flip > 0 else list(reversed(pts)), None, fit=True, solid=False)
    # the roll at its end
    axis = ct.sub(lo_c, lo_a)
    for k in range(10):
        t0, t1 = 2 * math.pi * k / 10, 2 * math.pi * (k + 1) / 10
        off0 = ct.add(ct.mul(n, 0.3 * math.cos(t0)), (0, 0, 0.3 * math.sin(t0)))
        off1 = ct.add(ct.mul(n, 0.3 * math.cos(t1)), (0, 0, 0.3 * math.sin(t1)))
        pts = [ct.add(lo_a, off0), ct.add(lo_a, off1), ct.add(ct.add(lo_a, axis), off1), ct.add(ct.add(lo_a, axis), off0)]
        b.face(awning, pts, (1.0, 1.0), solid=False)
    # the pink round sign (house 5) and the house that looks at you (house 9: its face painted big)
    sign = own(b, 'cq_cafe', lambda: crazypaint.round_sign('CAFE'), ct.ALPHA_TEST)
    hs = built[5]
    b.face(sign, hs.face_quad(0, 0.62, 0.92, ZC + 3.4, ZC + 6.0, 0.08), fit=True, solid=False)
    hs = built[9]
    b.face(b.tex('xx_foutete', 0), hs.face_quad(1, 0.15, 0.85, ZC + 5.0, ZC + 10.5, 0.07), fit=True, solid=False)
    # the stair up house 3's front to a door in mid-air (climbable: a view over the square)
    hs = built[3]
    stone, flags = b.tex('mr_pierrocre'), own(b, 'cq_flags', crazypaint.flagstones)
    for k in range(11):
        u = 0.9 - 0.075 * k
        z = ZC + RISE * (k + 1)
        p0 = hs.face_point(3, u - 0.075, ZC, 0.0)
        p1 = hs.face_point(3, u, ZC, 0.0)
        n = hs.normal(3)
        q0, q1 = ct.add(p0, ct.mul(n, 1.3)), ct.add(p1, ct.mul(n, 1.3))
        top = [(p[0], p[1], z) for p in (p0, p1, q1, q0)]
        top = top if ct.cross(ct.sub(top[1], top[0]), ct.sub(top[2], top[0]))[2] > 0 else list(reversed(top))
        b.prism([(p[0], p[1], z - RISE - 0.45) for p in top], top, stone, cap=flags, size=(1.5, 1.5))
    hs.door(b, 3, 0.1, ZC + 4.4, 'bs_murplanche2')
    return built


def square(b):
    flags = own(b, 'cq_flags', crazypaint.flagstones)
    x0, y0, x1, y1 = SQUARE
    floor(b, flags, x0, y0, x1, y1, ZC)
    floor(b, flags, *NOOK[:2], *NOOK[2:], ZC)
    for x, y in POSTS:
        lamp_post(b, x, y, ZC)
    fountain(b)
    kiosk(b)
    stall(b)
    topsy(b)
    pots = [(95.2, 20.2), (110.6, 20.2), (128.8, 36.8), (95.2, 42.0), (103.0, 42.0)]
    bush = b.tex('vg_buissongeoNZ', 0)
    for i, (x, y) in enumerate(pots):
        cylinder(b, b.tex('xx_foutete02'), (x, y), 0.55, ZC - 0.05, ZC + 0.7, 10, r1=0.7, cap=b.tex('hb_terregeo06'))
        for k in range(2):
            a = k * math.pi / 2 + i
            dx, dy = math.cos(a) * 0.9, math.sin(a) * 0.9
            pts = [(x - dx, y - dy, ZC + 0.6), (x + dx, y + dy, ZC + 0.6), (x + dx, y + dy, ZC + 2.1),
                   (x - dx, y - dy, ZC + 2.1)]
            uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
            for t in ((0, 1, 2), (0, 2, 3)):
                b.tri(bush, tuple(pts[j] for j in t), tuple(uvs[j] for j in t), solid=False)


def fountain(b):
    """The square's fountain (concept, view 3): a round stone basin, its water scrolling, a column with a bowl and the
    funnel turned over on top, pouring."""
    x, y = FOUNTAIN
    stone, water = b.tex('mr_pierrocre'), b.tex('o_fouTXY', ct.WRAP | ct.TRANSPARENT)
    n = 20
    outer, inner = ct.ring((x, y), 3.0, n, ZC - 0.3), ct.ring((x, y), 2.5, n, ZC - 0.3)
    for k in range(n):  # the rim, a ring of blocks
        j = (k + 1) % n
        bottom = [outer[k], outer[j], inner[j], inner[k]]
        bottom = bottom if ct.cross(ct.sub(bottom[1], bottom[0]), ct.sub(bottom[2], bottom[0]))[2] > 0 else \
            list(reversed(bottom))
        b.prism(bottom, [(p[0], p[1], ZC + 0.8) for p in bottom], stone, cap=b.tex('pr_dalfou4'), size=(1.5, 1.5))
    b.face(b.tex('o_fouTXY'), ct.ring((x, y), 2.55, n, ZC + 0.05), (2.0, 2.0))  # the basin's floor, under water
    lit = ig.baked_light((0.75, 0.75, 1.0), 200)
    b.face(water, ct.ring((x, y), 2.55, n, ZC + 0.55), (2.0, 2.0), solid=False, lit=lit)
    cylinder(b, stone, (x, y), 0.35, ZC, ZC + 2.0, 8)
    bowl_lo, bowl_hi = ct.ring((x, y), 0.4, 12, ZC + 1.9), ct.ring((x, y), 1.2, 12, ZC + 2.4)
    b.prism(bowl_lo, bowl_hi, stone, cap=b.tex('o_fouTXY'), size=(1.0, 1.0), solid=False)  # an overhang: drawn only
    funnel = b.tex('xx_entonoir2')
    cylinder(b, funnel, (x, y), 0.7, ZC + 2.4, ZC + 3.4, 12, r1=0.15, solid=False)
    cylinder(b, funnel, (x, y), 0.15, ZC + 3.4, ZC + 4.1, 8, r1=0.12, cap=funnel, solid=False)
    for k in range(4):  # its spouts' water falling into the basin (scrolling, see-through)
        a = math.pi / 2 * k + 0.4
        p, q = (x + 1.15 * math.cos(a), y + 1.15 * math.sin(a)), (x + 1.9 * math.cos(a), y + 1.9 * math.sin(a))
        side = (-math.sin(a) * 0.2, math.cos(a) * 0.2)
        pts = [(q[0] - side[0], q[1] - side[1], ZC + 0.55), (q[0] + side[0], q[1] + side[1], ZC + 0.55),
               (p[0] + side[0], p[1] + side[1], ZC + 2.35), (p[0] - side[0], p[1] - side[1], ZC + 2.35)]
        for flip in (1, -1):
            quad(b, water, pts if flip > 0 else list(reversed(pts)), None, (1.0, 1.0), solid=False, lit=lit)


def kiosk(b):
    """The news kiosk (concept): a green iron drum with its lit window and bills, a teal dome with swirls, a spike, a
    red kite flying from it; a board out front: NEWS PRESS."""
    x, y = KIOSK
    green = own(b, 'cq_kiosk', crazypaint.kiosk_wall)
    dome = own(b, 'cq_swirl', crazypaint.swirl_dome)
    n = 12
    # turned so the window (the texture's middle) looks west, at the square
    bottom = ct.ring((x, y), 1.6, n, ZC - 0.1, math.pi)
    top = ct.ring((x, y), 1.6, n, ZC + 2.6, math.pi)
    for k in range(n):
        j = (k + 1) % n
        pts = [bottom[k], bottom[j], top[j], top[k]]
        uv = [((k % 3) / 3, 1), ((k % 3 + 1) / 3, 1), ((k % 3 + 1) / 3, 0), ((k % 3) / 3, 0)]
        for t in ((0, 1, 2), (0, 2, 3)):
            b.tri(green, tuple(pts[i] for i in t), tuple(uv[i] for i in t))
    rings = [(2.3, 2.6), (2.6, 3.1), (2.4, 3.6), (1.7, 4.2), (0.7, 4.7), (0.12, 5.4)]
    prev = ct.ring((x, y), 1.6, n, ZC + 2.6)
    for r, z in rings:
        cur = ct.ring((x, y), r, n, ZC + z)
        b.prism(prev, cur, dome, size=(2.0, 2.0), solid=False)
        prev = cur
    b.face(dome, list(reversed(ct.ring((x, y), 2.3, n, ZC + 2.6))), (2.0, 2.0), solid=False)  # under the brim
    kite = b.tex('xx_gyro', 0)
    tip = (x + 0.12, y, ZC + 5.4)
    b.face(kite, [tip, (x + 1.4, y + 0.3, ZC + 5.0), (x + 1.9, y + 0.1, ZC + 6.1), (x + 0.6, y - 0.1, ZC + 6.4)],
           fit=True, solid=False)
    news = own(b, 'cq_news', lambda: crazypaint.poster([('NEWS', 24), ('PRESS', 18)]), 0)
    bx, by = x - 3.2, y + 0.4
    for side in (-1, 1):  # an A-frame board
        pts = [(bx + side * 0.45, by - 0.6, ZC), (bx + side * 0.45, by + 0.6, ZC), (bx, by + 0.6, ZC + 1.4),
               (bx, by - 0.6, ZC + 1.4)]
        quad(b, news, pts, (side, 0.0, 0.25), fit=True, solid=False)


def stall(b):
    """A market stall: four posts, a table of planks with jars on it, a striped awning (pink and white)."""
    x, y = STALL
    post = b.tex('bs_pon1nz')
    for dx in (-1.5, 1.5):
        for dy in (-0.9, 0.9):
            box(b, post, x + dx - 0.08, y + dy - 0.08, x + dx + 0.08, y + dy + 0.08, ZC, ZC + (2.6 if dy < 0 else 2.3),
                size=(0.4, 1.5), solid=False)
    box(b, b.tex('bs_murplanche1'), x - 1.4, y - 0.8, x + 1.4, y + 0.8, ZC, ZC + 1.0, cap=b.tex('bs_pon2nz'))
    stripes = own(b, 'cq_stripes', crazypaint.stripes, ct.ALPHA_TEST)
    pts = [(x - 1.7, y - 1.4, ZC + 2.35), (x + 1.7, y - 1.4, ZC + 2.35), (x + 1.7, y + 1.1, ZC + 2.75),
           (x - 1.7, y + 1.1, ZC + 2.75)]
    for flip in (1, -1):
        quad(b, stripes, pts if flip > 0 else list(reversed(pts)), None, fit=True, solid=False)
    jar = b.tex('xx_verboul02')
    for k, dx in enumerate((-0.9, -0.3, 0.4, 1.0)):
        cylinder(b, jar, (x + dx, y - 0.2 + 0.2 * (k % 2)), 0.2, ZC + 1.0, ZC + 1.35 + 0.1 * k, 6, cap=jar, solid=False)


def topsy(b):
    """The upside-down house (ours): a little house standing on its roof's point, its walls up in the air, its door
    upside down, its chimney pointing at the ground."""
    x, y = TOPSY
    slate, walls = b.tex('tt_ardoise'), b.tex('mr_poisvert')
    w = 1.9
    z_eave = ZC + 2.6
    eave = [(x - w, y - w, z_eave), (x + w, y - w, z_eave), (x + w, y + w, z_eave), (x - w, y + w, z_eave)]
    apex = (x + 0.15, y - 0.1, ZC - 0.05)
    for k in range(4):  # the roof, upside down: its faces look out and down; drawn only, an upright box collides
        m = ct.mul(ct.add(eave[k], eave[(k + 1) % 4]), 0.5)
        quad(b, slate, [eave[k], eave[(k + 1) % 4], apex], (m[0] - x, m[1] - y, -1.0), (2.0, 2.0), solid=False)
    b.box_collision((x, y), w, ZC - 0.5, z_eave + 3.6)
    body = [(x - 1.5, y - 1.5, z_eave), (x + 1.5, y - 1.5, z_eave), (x + 1.5, y + 1.5, z_eave), (x - 1.5, y + 1.5, z_eave)]
    top = [(p[0] + 0.25, p[1] - 0.15, z_eave + 3.4) for p in body]
    b.prism(body, top, walls, cap=b.tex('hb_01'), size=(3.0, 3.0))
    # its door, upside down, on the side to the square's middle (south)
    door = b.tex('bs_murplanche2')
    zt, zb = z_eave + 3.3, z_eave + 0.6
    b.face(door, [(x - 0.6 + 0.2, y - 1.5 - 0.06 - 0.13, zt), (x + 0.6 + 0.2, y - 1.5 - 0.06 - 0.13, zt),
                  (x + 0.6 + 0.02, y - 1.5 - 0.06, zb), (x - 0.6 + 0.02, y - 1.5 - 0.06, zb)], fit=True, solid=False)
    pic = own(b, 'cq_window0', lambda: crazypaint.window(19), 0)
    lit = ig.baked_light((1.6, 1.4, 1.0))
    for dy in (-0.7, 0.7):
        b.face(pic, [(x + 1.5 + 0.13, y + dy - 0.45, z_eave + 2.4), (x + 1.5 + 0.13, y + dy + 0.45, z_eave + 2.4),
                     (x + 1.5 + 0.02, y + dy + 0.45, z_eave + 1.2), (x + 1.5 + 0.02, y + dy - 0.45, z_eave + 1.2)],
               fit=True, solid=False, lit=lit)
    # the chimney, hanging from the eaves towards the ground, and a bush growing on its floor, up in the air
    cylinder(b, b.tex('mr_pierrbleu'), (x - 1.0, y + 0.9), 0.35, ZC + 1.3, z_eave, 4, cap=b.tex('rc_doc03'))
    bush = b.tex('vg_buissongeo02NZ', 0)
    for k in range(2):
        a = k * math.pi / 2 + 0.3
        dx, dy = math.cos(a) * 1.2, math.sin(a) * 1.2
        zt0 = z_eave + 3.4
        pts = [(x - dx + 0.25, y - dy - 0.15, zt0), (x + dx + 0.25, y + dy - 0.15, zt0),
               (x + dx + 0.25, y + dy - 0.15, zt0 + 1.6), (x - dx + 0.25, y - dy - 0.15, zt0 + 1.6)]
        uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
        for t in ((0, 1, 2), (0, 2, 3)):
            b.tri(bush, tuple(pts[j] for j in t), tuple(uvs[j] for j in t), solid=False)


def tavern(b):
    """Through the dead end's door (the concept's "Interior?"): a low room of planks, a round barrel counter with a
    shield on its front, stools, barrels, shelves of bottles, lamps hanging from the beams."""
    x0, y0, x1, y1 = TAVERN
    z0, z1 = ZA, ZA + TAVERN_H
    boards = own(b, 'cq_planks', crazypaint.planks)
    walls = b.tex('bs_murplanche1')
    low = ZA - 3.0
    floor(b, boards, x0, y0, x1, y1, z0, tile=2.5)
    quad(b, boards, [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0.0, 0.0, -1.0), (2.5, 2.5))
    wall(b, walls, (x0, y0), (x0, y1), low, z1)        # west, facing east
    wall(b, walls, (x1, y0), (x0, y0), low, z1)        # south, facing north
    wall(b, walls, (x0, y1), (x1, y1), low, z1)        # north, facing south
    wall(b, walls, (x1, y1), (x1, y0), low, z1, holes=[(0.5 * (y1 - y0), 1.0, ZA + 2.2)])  # its door's side
    for x in (x0 + 2.0, x0 + 4.5, x0 + 6.6):  # beams
        box(b, b.tex('bs_pon1nz'), x - 0.15, y0, x + 0.15, y1, z1 - 0.35, z1 - 0.01, size=(1.0, 1.0), solid=False)
    # outside (seen from the town): its walls, lit windows, a slate roof against the alley's end, a chimney
    ox0, oy0, oy1, zt = x0 - 0.6, y0 - 0.6, y1 + 0.6, z1 + 0.6
    outer = b.tex('mr_fou3')
    wall(b, outer, (ox0, oy0), (x1, oy0), low, zt)
    wall(b, outer, (x1, oy1), (ox0, oy1), low, zt)
    wall(b, outer, (ox0, oy1), (ox0, oy0), low, zt)
    eave = [(ox0 - 0.5, oy0 - 0.5, zt), (x1, oy0 - 0.5, zt), (x1, oy1 + 0.5, zt), (ox0 - 0.5, oy1 + 0.5, zt)]
    apex = (0.5 * (ox0 + x1) + 0.6, 0.5 * (oy0 + oy1) - 0.3, zt + 3.3)
    b.face(b.tex('bs_pon1nz'), list(reversed(eave)), (2.0, 2.0))
    for k in range(4):
        b.face(b.tex('tt_ardoise'), [eave[k], eave[(k + 1) % 4], apex], (2.5, 2.5))
    cylinder(b, b.tex('mr_pierrbleu'), (ox0 + 1.5, oy1 - 1.2), 0.45, zt, zt + 3.4, 4, cap=b.tex('rc_doc03'))
    lit = ig.baked_light((1.7, 1.45, 1.05))
    pic = own(b, 'cq_window1', lambda: crazypaint.window(20), 0)
    for (px, py), want in (((63.0, oy0 - 0.05), (0, -1, 0)), ((67.5, oy0 - 0.05), (0, -1, 0)),
                           ((ox0 - 0.05, 7.0), (-1, 0, 0)), ((65.0, oy1 + 0.05), (0, 1, 0))):
        side = (-want[1] * 0.6, want[0] * 0.6)
        quad(b, pic, [(px - side[0], py - side[1], ZA + 1.3), (px + side[0], py + side[1], ZA + 1.3),
                      (px + side[0], py + side[1], ZA + 3.0), (px - side[0], py - side[1], ZA + 3.0)], want, fit=True,
             solid=False, lit=lit)
    # the counter: a half ring of staves, the shield on its front, a plank top
    cx, cy = x0 + 1.8, 0.5 * (y0 + y1)
    staves, top = b.tex('bs_murplanche2'), own(b, 'cq_planks2', lambda: crazypaint.planks(31, base=(150, 96, 52)))
    n = 10
    for k in range(n):
        a0, a1 = -math.pi / 2 + math.pi * k / n, -math.pi / 2 + math.pi * (k + 1) / n
        o0, o1 = (cx + 2.4 * math.cos(a0), cy + 2.4 * math.sin(a0)), (cx + 2.4 * math.cos(a1), cy + 2.4 * math.sin(a1))
        i0, i1 = (cx + 1.8 * math.cos(a0), cy + 1.8 * math.sin(a0)), (cx + 1.8 * math.cos(a1), cy + 1.8 * math.sin(a1))
        bottom = [(o0[0], o0[1], z0), (o1[0], o1[1], z0), (i1[0], i1[1], z0), (i0[0], i0[1], z0)]
        bottom = bottom if ct.cross(ct.sub(bottom[1], bottom[0]), ct.sub(bottom[2], bottom[0]))[2] > 0 else \
            list(reversed(bottom))
        b.prism(bottom, [(p[0], p[1], z0 + 1.25) for p in bottom], staves, cap=top, size=(1.2, 1.2))
    shield = own(b, 'cq_shield', crazypaint.shield, ct.ALPHA_TEST)
    sx = cx + 2.45
    b.face(shield, [(sx, cy - 0.7, z0 + 0.1), (sx, cy + 0.7, z0 + 0.1), (sx, cy + 0.7, z0 + 1.2), (sx, cy - 0.7, z0 + 1.2)],
           fit=True, solid=False)
    for a in (-1.1, -0.45, 0.45, 1.1):  # stools, the way to the counter's middle left free
        p = (cx + 3.2 * math.cos(a), cy + 3.2 * math.sin(a))
        cylinder(b, b.tex('bs_pon1nz'), p, 0.32, z0, z0 + 0.75, 8, cap=top)
    barrel(b, (x1 - 0.9, y0 + 0.8), z0)
    barrel(b, (x1 - 0.9, y1 - 0.8), z0)
    barrel(b, (x0 + 0.6, y0 + 0.6), z0, 0.45)
    # shelves of bottles on the west wall, behind the counter
    shelf = b.tex('bs_pon1nz')
    jars = [b.tex('xx_verboul'), b.tex('xx_verboul02'), b.tex('o_fouTXY')]
    for z in (z0 + 1.6, z0 + 2.5):
        box(b, shelf, x0, y0 + 1.0, x0 + 0.45, y1 - 1.0, z - 0.08, z, size=(1.0, 1.0), solid=False)
        for k in range(9):
            yy = y0 + 1.25 + (y1 - y0 - 2.5) * k / 8
            cylinder(b, jars[k % 3], (x0 + 0.22, yy), 0.12, z, z + 0.35 + 0.1 * (k % 2), 6, cap=jars[k % 3], solid=False)
    for at in ((cx + 0.2, cy, z1 - 0.9), (x1 - 1.8, y0 + 1.0, z1 - 1.2)):
        lantern(b, at, (at[0], at[1], z1))


def build(b):
    """All of it into the builder (crazytown.build sets the light first: lamps(), covered())."""
    alley(b)
    way_north(b)
    houses(b)
    square(b)
    tavern(b)

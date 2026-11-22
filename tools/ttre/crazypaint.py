"""Crazy Town's own pictures (2026-11-16): the textures its old quarter needs that the Special Edition's 112 do not
have, painted by code (the user asked that the install need nothing but the game: no concept art is read). Each is
(w, h, rgba bytes), straight alpha. Used by tools/ttre/crazyquarter.py.

The looks follow the concept art the user gave (a covered alley, a sunny square of crooked houses, a teal mushroom
kiosk selling the news, a red awning rolled into a spiral, curly iron balconies, a green bottle-shaped shop sign, ROCK
posters, a tavern behind a round barrel counter with a shield on it) and the cut level's screenshot (a spiral paved in
grey and brown round the giant CD)."""
import math
import random


def _px(w, h, fn):
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            r, g, b, a = fn(x, y)
            o = 4 * (y * w + x)
            out[o:o + 4] = bytes((max(0, min(255, int(r))), max(0, min(255, int(g))), max(0, min(255, int(b))),
                                  max(0, min(255, int(a)))))
    return w, h, bytes(out)


def _noise(seed, n=16):
    """Smooth value noise (u, v) -> 0..1, tiling over the unit square."""
    rng = random.Random(seed)
    grid = [[rng.random() for _ in range(n)] for _ in range(n)]

    def at(u, v):
        x, y = (u * n) % n, (v * n) % n
        i, j = int(x), int(y)
        fx, fy = x - i, y - j
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        a, b = grid[j % n][i % n], grid[j % n][(i + 1) % n]
        c, d = grid[(j + 1) % n][i % n], grid[(j + 1) % n][(i + 1) % n]
        top, bottom = a + (b - a) * fx, c + (d - c) * fx
        return top + (bottom - top) * fy
    return at


def _mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def _smooth(e0, e1, x):
    t = min(max((x - e0) / ((e1 - e0) or 1e-9), 0.0), 1.0)
    return t * t * (3 - 2 * t)


# ---- a hand-painted capital font: strokes on a 4 x 6 grid (y up) ----

STROKES = {
    'A': [[(0, 0), (2, 6), (4, 0)], [(0.9, 2.4), (3.1, 2.4)]],
    'B': [[(0, 0), (0, 6), (3, 6), (3.7, 5), (3, 3.2), (0, 3.2)], [(3, 3.2), (4, 1.6), (3, 0), (0, 0)]],
    'C': [[(4, 5.2), (3, 6), (1, 6), (0, 4.5), (0, 1.5), (1, 0), (3, 0), (4, 0.8)]],
    'D': [[(0, 0), (0, 6), (2.5, 6), (4, 4.2), (4, 1.8), (2.5, 0), (0, 0)]],
    'E': [[(4, 6), (0, 6), (0, 0), (4, 0)], [(0, 3.1), (3, 3.1)]],
    'F': [[(4, 6), (0, 6), (0, 0)], [(0, 3.1), (3, 3.1)]],
    'K': [[(0, 0), (0, 6)], [(4, 6), (0, 2.4)], [(1.3, 3.6), (4, 0)]],
    'N': [[(0, 0), (0, 6), (4, 0), (4, 6)]],
    'O': [[(2, 6), (0.3, 5), (0, 3), (0.3, 1), (2, 0), (3.7, 1), (4, 3), (3.7, 5), (2, 6)]],
    'P': [[(0, 0), (0, 6), (3, 6), (4, 4.8), (3, 3), (0, 3)]],
    'R': [[(0, 0), (0, 6), (3, 6), (4, 4.8), (3, 3), (0, 3)], [(1.8, 3), (4, 0)]],
    'S': [[(4, 5.3), (3, 6), (1, 6), (0, 4.8), (1, 3.3), (3, 2.7), (4, 1.2), (3, 0), (1, 0), (0, 0.7)]],
    'T': [[(0, 6), (4, 6)], [(2, 6), (2, 0)]],
    'U': [[(0, 6), (0, 1.2), (1, 0), (3, 0), (4, 1.2), (4, 6)]],
    'W': [[(0, 6), (0.9, 0), (2, 4), (3.1, 0), (4, 6)]],
    'Y': [[(0, 6), (2, 3), (4, 6)], [(2, 3), (2, 0)]],
    'Z': [[(0, 6), (4, 6), (0, 0), (4, 0)]],
    '!': [[(2, 6), (2, 1.8)], [(2, 0.4), (2, 0)]],
    # Crazy City's (2026-11-22): the rest its signs spell
    'G': [[(4, 5.2), (3, 6), (1, 6), (0, 4.5), (0, 1.5), (1, 0), (3, 0), (4, 1), (4, 2.8), (2.4, 2.8)]],
    'H': [[(0, 0), (0, 6)], [(4, 0), (4, 6)], [(0, 3.1), (4, 3.1)]],
    'I': [[(2, 0), (2, 6)], [(1, 6), (3, 6)], [(1, 0), (3, 0)]],
    'J': [[(1, 6), (4, 6)], [(3, 6), (3, 1.2), (2, 0), (1, 0), (0, 1)]],
    'L': [[(0, 6), (0, 0), (4, 0)]],
    'M': [[(0, 0), (0, 6), (2, 2.5), (4, 6), (4, 0)]],
    'Q': [[(2, 6), (0.3, 5), (0, 3), (0.3, 1), (2, 0), (3.7, 1), (4, 3), (3.7, 5), (2, 6)], [(2.4, 1.4), (4.2, -0.4)]],
    'V': [[(0, 6), (2, 0), (4, 6)]],
    'X': [[(0, 6), (4, 0)], [(0, 0), (4, 6)]],
}


def _segment_distance(px, py, a, b):
    abx, aby = b[0] - a[0], b[1] - a[1]
    t = max(0.0, min(1.0, ((px - a[0]) * abx + (py - a[1]) * aby) / ((abx * abx + aby * aby) or 1e-9)))
    return math.hypot(px - a[0] - abx * t, py - a[1] - aby * t)


def text_strokes(text, x, y, size, tilt=0.0, wobble=0.0, seed=1):
    """The segments of `text` (STROKES' letters) in picture pixels (y down): the letters `size` tall from the left end of
    their baseline at (x, y), each turned a little (tilt, radians, alternating) and jiggled (wobble, pixels)."""
    rng = random.Random(seed)
    segs = []
    pen = x
    k = size / 6.0
    for i, ch in enumerate(text):
        if ch == ' ':
            pen += 3.0 * k
            continue
        turn = tilt * (1 if i % 2 else -1) * (0.6 + 0.8 * rng.random())
        c, s = math.cos(turn), math.sin(turn)
        dy = rng.uniform(-wobble, wobble)
        for line in STROKES.get(ch, []):
            pts = []
            for gx, gy in line:
                lx, ly = (gx - 2) * k, (gy - 3) * k
                pts.append((pen + 2 * k + c * lx - s * ly, y - 3 * k - (s * lx + c * ly) + dy))
            segs += list(zip(pts, pts[1:]))
        pen += 5.2 * k
    return segs


def text_width(text, size):
    k = size / 6.0
    return sum(3.0 * k if ch == ' ' else 5.2 * k for ch in text) - 1.2 * k


def _stroke_cover(segs, px, py, width):
    """0..1: how much the strokes cover the pixel at (px, py) (soft edged)."""
    best = min((_segment_distance(px, py, a, b) for a, b in segs), default=1e9)
    return _smooth(width + 0.8, width - 0.4, best)


# ---- the pictures ----

def flagstones(seed=3, size=128, base=(196, 164, 112), cells=9):
    """Worn flagstones (the alley's and the square's floor): stones of a sandy colour, each its own shade, in dark
    joints; tiles both ways."""
    rng = random.Random(seed)
    pts = [(rng.random(), rng.random()) for _ in range(cells)]
    tone = [rng.uniform(0.82, 1.12) for _ in pts]
    hue = [rng.uniform(-14, 14) for _ in pts]
    n = _noise(seed + 1, 8)
    m = _noise(seed + 2, 32)

    def fn(x, y):
        u, v = (x + 0.5) / size, (y + 0.5) / size
        d = []
        for i, (a, b) in enumerate(pts):
            dx = min(abs(u - a), 1 - abs(u - a))
            dy = min(abs(v - b), 1 - abs(v - b))
            d.append((dx * dx + dy * dy, i))
        d.sort()
        edge = math.sqrt(d[1][0]) - math.sqrt(d[0][0])
        i = d[0][1]
        k = tone[i] * (0.86 + 0.22 * n(u, v)) * (0.92 + 0.12 * m(u, v))
        c = (base[0] * k + hue[i], base[1] * k + hue[i] * 0.6, base[2] * k)
        joint = _smooth(0.035, 0.008, edge)
        c = _mix(c, (70, 52, 44), joint * 0.85)
        return c + (255,)
    return _px(size, size, fn)


def spiral_paving(stones, size=512, turns=4.5):
    """The giant CD's square (the cut level's screenshot): grey flagstones with a brown spiral running in from the
    rim to the middle, a dark joint each side of it. stones: a flagstones() picture tiled under it."""
    sw, sh, sp = stones

    def fn(x, y):
        u, v = (x + 0.5) / size * 2 - 1, (y + 0.5) / size * 2 - 1
        r, a = math.hypot(u, v), math.atan2(v, u)
        k = 4 * (((y * 4) % sh) * sw + (x * 4) % sw)
        c = tuple(sp[k + j] * 1.0 for j in range(3))
        grey = sum(c) / 3.0
        c = _mix(c, (grey * 1.05, grey * 1.05, grey * 1.1), 0.75)  # the screenshot's stones are grey-white
        # the spiral: r = (a / 2pi + n) / turns; its band where the phase is in the middle half
        phase = (r * turns - a / (2 * math.pi)) % 1.0
        band = _smooth(0.12, 0.2, phase) * _smooth(0.48, 0.4, phase)
        c = _mix(c, (122 * grey / 170, 80 * grey / 170, 52 * grey / 170), band * 0.9)
        joint = max(_smooth(0.06, 0.1, phase) * _smooth(0.14, 0.1, phase), _smooth(0.44, 0.48, phase) * _smooth(0.52, 0.48, phase))
        c = _mix(c, (60, 48, 50), joint * 0.7)
        rim = _smooth(0.94, 0.97, r)
        c = _mix(c, (96, 70, 60), rim)
        return c + (255 if r < 1.0 else 0,)
    return _px(size, size, fn)


def banner(text, cloth=(110, 40, 150), ink=(255, 220, 70), seed=5, w=96, h=192):
    """A cloth poster (the alley's ROCK posters): the word written down it a letter under the other, a frayed
    bottom (cut out: alpha)."""
    rng = random.Random(seed)
    n = _noise(seed, 8)
    size = min(w * 0.62, h * 0.8 / max(1, len(text)) * 0.92)
    segs = []
    for i, ch in enumerate(text):
        cx = w / 2 - size * 2 / 6.0
        segs += text_strokes(ch, cx, h * 0.12 + size * (i + 1) * 1.08, size, 0.15, 2.0, seed + i)
    fray = [h * (0.93 + 0.05 * rng.random()) for _ in range(w)]

    def fn(x, y):
        if y > fray[x] or (y > h * 0.9 and (x // 6) % 2 and y > fray[x] - h * 0.03):
            return (0, 0, 0, 0)
        k = 0.85 + 0.25 * n(x / w, y / h)
        fold = 0.9 + 0.1 * math.sin(x / w * math.pi * 3)
        c = tuple(ch * k * fold for ch in cloth)
        cov = _stroke_cover(segs, x, y, size * 0.11)
        c = _mix(c, ink, cov)
        edge = _smooth(3, 0, min(x, w - 1 - x, y))
        c = _mix(c, tuple(ch * 0.5 for ch in cloth), edge)
        return c + (255,)
    return _px(w, h, fn)


def poster(lines, paper=(240, 232, 200), ink=(30, 30, 40), accent=(220, 40, 60), seed=7, w=128, h=128):
    """A newspaper bill (the kiosk's): big words, a red bar, columns of scribbled print."""
    rng = random.Random(seed)
    segs = []
    y = h * 0.26
    for i, (word, size) in enumerate(lines):
        width = text_width(word, size)
        segs += text_strokes(word, (w - width) / 2, y, size, 0.06, 1.0, seed + i)
        y += size * 1.35
    scribbles = [(rng.uniform(0.12, 0.42) if c == 0 else rng.uniform(0.58, 0.88), rng.uniform(0.66, 0.92))
                 for c in (0, 1) for _ in range(9)]

    def fn(x, y):
        u, v = x / w, y / h
        c = _mix(paper, (200, 190, 150), 0.3 * _noise(seed, 8)(u, v))
        if 0.04 < v < 0.1:
            c = accent
        for sx, sy in scribbles:
            if abs(v - sy) < 0.008 and abs(u - sx) < 0.13 and (int(u * w) // 3) % 4:
                c = _mix(c, ink, 0.55)
        c = _mix(c, ink, _stroke_cover(segs, x, y, 1.7))
        return c + (255,)
    return _px(w, h, fn)


def bottle_sign(w=64, h=128):
    """The alley's shop sign (concept, panel 1): a green bottle, cut out, a pink froth spilling from its neck, a
    keyhole-dark label."""
    def inside(u, v):  # v down
        if v < 0.3:  # the froth
            return 'froth' if math.hypot((u - 0.5) * 1.2, (v - 0.17) * 1.5) < 0.19 + 0.035 * math.sin(u * 30) else None
        if v > 0.94:
            return None
        if v < 0.46:
            half = 0.09
        elif v < 0.58:  # the shoulders
            half = 0.09 + 0.24 * _smooth(0.46, 0.58, v)
        else:  # the body, a little crooked, its bottom rounded
            half = 0.33 + 0.03 * math.sin(v * 11) - 0.25 * _smooth(0.86, 0.94, v) ** 2
        return 'glass' if abs(u - 0.5 - 0.03 * math.sin(v * 7)) < half else None

    def fn(x, y):
        u, v = (x + 0.5) / w, (y + 0.5) / h
        k = inside(u, v)
        if not k:
            # the pink outline round the glass
            near = any(inside(u + du, v + dv) for du, dv in ((0.04, 0), (-0.04, 0), (0, 0.025), (0, -0.025)))
            return (235, 110, 170, 255) if near else (0, 0, 0, 0)
        if k == 'froth':
            return (250, 200 + 30 * math.sin(u * 20), 225, 255)
        c = (40 + 80 * (1 - u), 160 + 50 * math.sin(v * 6), 70, 255)
        if 0.62 < v < 0.78 and abs(u - 0.5) < 0.12:  # the label, dark, a keyhole
            c = (30, 40, 30, 255)
        if abs(u - 0.38) < 0.03 and 0.45 < v < 0.9:  # the glint
            c = (200, 255, 210, 255)
        return c
    return _px(w, h, fn)


def kiosk_wall(w=128, h=128):
    """The kiosk's green iron (concept): dark green plates, rivets, a lit window with a counter, bills on it."""
    n = _noise(11, 8)

    def fn(x, y):
        u, v = x / w, y / h
        c = _mix((30, 90, 60), (50, 130, 80), n(u, v))
        if abs((u * 4) % 1.0 - 0.5) > 0.47:
            c = _mix(c, (20, 50, 35), 0.7)  # the plates' seams
        if ((u * 16) % 1.0 - 0.5) ** 2 + ((v * 16) % 1.0 - 0.5) ** 2 < 0.03 and v > 0.85:
            c = (150, 190, 150)  # rivets along the foot
        if 0.32 < u < 0.68 and 0.18 < v < 0.52:  # the window, lit from inside
            c = _mix((255, 220, 140), (230, 160, 80), (v - 0.18) / 0.34)
            if abs(u - 0.5) < 0.012:
                c = (40, 60, 40)
        if 0.3 < u < 0.7 and 0.52 < v < 0.57:
            c = (120, 80, 50)  # the counter
        if 0.05 < u < 0.25 and 0.25 < v < 0.75:  # a bill
            c = _mix((235, 225, 190), (210, 50, 60), 1.0 if v < 0.33 else 0.0)
            if v > 0.4 and int(v * 60) % 3 == 0 and 0.08 < u < 0.22:
                c = (60, 60, 70)
        return c + (255,)
    return _px(w, h, fn)


def swirl_dome(w=128, h=128):
    """The kiosk's teal dome (concept): curling swirls in a lighter teal."""
    n = _noise(13, 8)
    centres = [((i % 3) / 3 + 0.17 + (i // 3) * 0.08, (i // 3) / 2 + 0.25) for i in range(6)]

    def fn(x, y):
        u, v = x / w, y / h
        c = _mix((30, 120, 125), (50, 160, 160), n(u, v))
        for cx, cy in centres:
            dx = min(abs(u - cx), 1 - abs(u - cx)) * (1 if u > cx else -1)
            dy = v - cy
            r, a = math.hypot(dx, dy * 1.4), math.atan2(dy, dx)
            if r < 0.15:
                phase = (r * 28 - a) % (2 * math.pi)
                if phase < 0.9:
                    c = _mix(c, (130, 220, 210), 0.8 * (1 - r / 0.15))
        return c + (255,)
    return _px(w, h, fn)


def stripes(a=(240, 120, 170), b=(250, 245, 235), w=64, h=64, count=6):
    """An awning's stripes (the stall's), scalloped at the bottom (cut out)."""
    def fn(x, y):
        u, v = x / w, y / h
        if v > 0.86 + 0.12 * abs(math.sin(u * count * math.pi)):
            return (0, 0, 0, 0)
        c = a if int(u * count) % 2 else b
        return tuple(ch * (0.85 + 0.15 * (1 - v)) for ch in c) + (255,)
    return _px(w, h, fn)


def spiral_awning(w=128, h=64):
    """The big red awning rolled into a spiral at its end (concept, panel 3): red cloth, the spiral drawn darker."""
    n = _noise(17, 8)

    def fn(x, y):
        u, v = x / w, y / h
        c = _mix((170, 30, 40), (220, 60, 60), n(u, v))
        c = tuple(ch * (0.8 + 0.25 * math.sin(u * math.pi * 6) ** 2) for ch in c)  # its folds
        dx, dy = (u - 0.78) * 2, (v - 0.5)
        r, a = math.hypot(dx, dy), math.atan2(dy, dx)
        if r < 0.42 and ((r * 30 - a) % (2 * math.pi)) < 1.2:
            c = _mix(c, (90, 10, 20), 0.85)
        return c + (255,)
    return _px(w, h, fn)


def railing(w=128, h=64):
    """Curly wrought iron (the balconies, concept): a top and a bottom bar, curls between them; cut out."""
    def fn(x, y):
        u, v = x / w, y / h
        on = v < 0.08 or 0.9 < v < 0.98 or (u * 8) % 1.0 < 0.06
        cu, cv = (u * 4) % 1.0 - 0.5, (v - 0.5) * 1.6
        r, a = math.hypot(cu, cv), math.atan2(cv, cu)
        if 0.08 < r < 0.42 and ((r * 22 - a) % (2 * math.pi)) < 0.7:
            on = True
        return (30, 26, 34, 255) if on else (0, 0, 0, 0)
    return _px(w, h, fn)


def window(seed=19, w=64, h=64):
    """A lit window, a little crooked: warm panes in a dark frame, a cross of glazing bars, a curtain at one side."""
    rng = random.Random(seed)
    skew = rng.uniform(-0.08, 0.08)
    side = rng.choice((0, 1))

    def fn(x, y):
        u, v = x / w, y / h
        u2 = u + skew * (v - 0.5)
        if not (0.1 < u2 < 0.9 and 0.08 < v < 0.92):
            return (60, 40, 40, 255)
        c = _mix((255, 214, 120), (240, 150, 70), v)
        if abs(u2 - 0.5) < 0.035 or abs(v - 0.45) < 0.035:
            c = (70, 45, 40)
        if (u2 < 0.26) if side else (u2 > 0.74):
            c = _mix(c, (180, 60, 80), 0.6)
        return c + (255,)
    return _px(w, h, fn)


def round_sign(text='CAFE', w=128, h=128):
    """The square's pink round sign (concept, panel 3): a pink oval, a darker rim, the word across it."""
    size = 26
    segs = text_strokes(text, (w - text_width(text, size)) / 2, h / 2 + size / 2, size, 0.12, 1.5, 23)

    def fn(x, y):
        u, v = (x + 0.5) / w * 2 - 1, (y + 0.5) / h * 2 - 1
        r = math.hypot(u, v * 1.15)
        if r > 1.0:
            return (0, 0, 0, 0)
        c = (240, 120, 180) if r < 0.86 else (170, 50, 110)
        c = _mix(c, (255, 250, 230), _stroke_cover(segs, x, y, 2.4))
        return c + (255,)
    return _px(w, h, fn)


def shield(w=128, h=128):
    """The tavern counter's shield (the concept's interior): orange and black, a dark rim, rivets."""
    def fn(x, y):
        u, v = (x + 0.5) / w * 2 - 1, (y + 0.5) / h * 2 - 1
        inside = abs(u) < 0.85 * (1 - max(0.0, v - 0.1) ** 2 * 0.9) and v > -0.9
        if not inside:
            return (0, 0, 0, 0)
        c = (230, 140, 30)
        if abs(u) < 0.12 or abs(v + 0.1) < 0.1:
            c = (40, 25, 20)
        if abs(abs(u) - 0.45) < 0.08 and -0.5 < v < 0.4:
            c = (40, 25, 20)
        edge = abs(u) > 0.75 * (1 - max(0.0, v - 0.1) ** 2 * 0.9) or v < -0.8
        if edge:
            c = (90, 50, 30)
        return c + (255,)
    return _px(w, h, fn)


def planks(seed=29, w=64, h=64, base=(120, 74, 44)):
    """Dark boards (the tavern's floor and counter top)."""
    n = _noise(seed, 16)

    def fn(x, y):
        u, v = x / w, y / h
        row = int(v * 5)
        k = 0.8 + 0.3 * n(u * 0.3 + row * 0.37, v)
        c = tuple(ch * k for ch in base)
        if (v * 5) % 1.0 < 0.06:
            c = tuple(ch * 0.4 for ch in base)
        return c + (255,)
    return _px(w, h, fn)


# ---- Crazy City's (2026-11-22, tools/ttre/crazycity.py): its neighbourhoods' signs, roads and tiles ----

def _distance(segs, px, py):
    return min((_segment_distance(px, py, a, b) for a, b in segs), default=1e9)


def neon(text, glow=(255, 60, 200), w=256, h=64, seed=31):
    """A neon sign: the word in bent glass tubes, white hot in the middle, its colour round it, on a dark board with a
    tube along its edge."""
    size = min(h * 0.62, (w * 0.86) / max(1.0, text_width(text, 1.0)))
    segs = text_strokes(text, (w - text_width(text, size)) / 2, h / 2 + size / 2, size, 0.08, 1.0, seed)
    tube = size * 0.07

    def fn(x, y):
        d = _distance(segs, x, y)
        edge = min(x, y, w - 1 - x, h - 1 - y)
        c = (22, 12, 34)
        c = _mix(c, glow, 0.85 * _smooth(tube * 5, tube, d))
        c = _mix(c, glow, 0.9 * _smooth(4.0, 1.5, abs(edge - 3.0)))
        c = _mix(c, (255, 244, 250), _smooth(tube + 0.6, tube - 0.6, d))
        return c + (255,)
    return _px(w, h, fn)


def checker(a=(240, 110, 170), b=(70, 200, 210), cells=4, w=64, h=64, seed=37):
    """Glazed tiles in two colours with grout between, a little uneven."""
    n = _noise(seed, 8)

    def fn(x, y):
        u, v = x / w * cells, y / h * cells
        c = a if (int(u) + int(v)) % 2 else b
        c = tuple(ch * (0.88 + 0.18 * n(x / w, y / h)) for ch in c)
        if min(u % 1.0, v % 1.0) < 0.05:
            c = (60, 52, 58)
        return c + (255,)
    return _px(w, h, fn)


def asphalt(w=128, h=128, seed=41):
    """A road's tarmac: dark grey grit, patched here and there."""
    n, m = _noise(seed, 16), _noise(seed + 1, 4)
    rng = random.Random(seed)
    grit = [rng.random() for _ in range(w * h)]

    def fn(x, y):
        k = 52 + 26 * n(x / w, y / h) + 22 * (grit[y * w + x] - 0.5)
        if m(x / w, y / h) > 0.72:
            k *= 0.8
        return (k, k * 0.98, k * 1.04, 255)
    return _px(w, h, fn)


def solid(rgb, w=8, h=8):
    """One colour (road markings)."""
    return _px(w, h, lambda x, y: tuple(rgb) + (255,))


def bands(a=(225, 40, 50), b=(245, 240, 230), count=4, w=64, h=64):
    """Horizontal bands (the lighthouse's), a little worn."""
    n = _noise(43, 8)

    def fn(x, y):
        c = a if int(y / h * count) % 2 else b
        return tuple(ch * (0.85 + 0.2 * n(x / w, y / h)) for ch in c) + (255,)
    return _px(w, h, fn)


def mural(text='CRAZY CITY', w=512, h=128, seed=47):
    """A wall painting: rainbow waves and spirals, the words in fat outlined letters over them."""
    size = min(h * 0.55, w * 0.9 / max(1.0, text_width(text, 1.0)))
    segs = text_strokes(text, (w - text_width(text, size)) / 2, h / 2 + size / 2, size, 0.18, 4.0, seed)
    colours = [(240, 70, 90), (250, 170, 50), (250, 230, 80), (90, 200, 120), (70, 150, 230), (160, 90, 220)]

    def fn(x, y):
        u, v = x / w, y / h
        k = int((v * 6 + 0.6 * math.sin(u * 14 + v * 3)) % 6)
        c = colours[k]
        dx, dy = (u * 4) % 1.0 - 0.5, (v - 0.5) * 1.0
        r, a = math.hypot(dx, dy), math.atan2(dy, dx)
        if r < 0.45 and ((r * 40 - a) % (2 * math.pi)) < 1.0:
            c = _mix(c, (255, 255, 255), 0.5)
        d = _distance(segs, x, y)
        c = _mix(c, (20, 16, 30), _smooth(size * 0.2, size * 0.15, d))
        c = _mix(c, (255, 250, 235), _smooth(size * 0.13, size * 0.09, d))
        return c + (255,)
    return _px(w, h, fn)


def clock(w=128, h=128, hours=7.3, minutes=-0.15):
    """A clock face: cream, twelve ticks, a dark rim, its hands at a time no clock shows (the minute hand runs
    backwards)."""
    def fn(x, y):
        u, v = (x + 0.5) / w * 2 - 1, 1 - (y + 0.5) / h * 2
        r = math.hypot(u, v)
        if r > 1.0:
            return (0, 0, 0, 0)
        if r > 0.9:
            return (50, 34, 30, 255)
        c = (244, 232, 200)
        a = math.atan2(u, v) % (2 * math.pi)
        if r > 0.72 and (a * 12 / (2 * math.pi) + 0.03) % 1.0 < 0.06:
            c = (40, 30, 30)
        for ang, length, width in ((hours / 12 * 2 * math.pi, 0.48, 0.06), (minutes * 2 * math.pi, 0.72, 0.035)):
            px, py = math.sin(ang), math.cos(ang)
            t = u * px + v * py
            if 0 <= t <= length and abs(u * py - v * px) < width:
                c = (30, 24, 40)
        return c + (255,)
    return _px(w, h, fn)


def helipad(w=128, h=128):
    """A roof's landing pad: dark grey, a yellow ring and an H."""
    segs = text_strokes('H', w / 2 - 0.5 * text_width('H', 52), h / 2 + 26, 52)

    def fn(x, y):
        u, v = (x + 0.5) / w * 2 - 1, (y + 0.5) / h * 2 - 1
        r = math.hypot(u, v)
        c = (70, 72, 78)
        if 0.78 < r < 0.88:
            c = (240, 200, 40)
        return _mix(c, (245, 245, 240), _stroke_cover(segs, x, y, 4.5)) + (255,)
    return _px(w, h, fn)


def big_letters(text, w=1024, h=192, seed=53):
    """Huge letters standing on a hillside (cut out): white, shaded at their lower edge, each leaning its own way."""
    size = min(h * 0.8, w * 0.92 / max(1.0, text_width(text, 1.0)))
    segs = text_strokes(text, (w - text_width(text, size)) / 2, h * 0.9, size, 0.22, 6.0, seed)
    width = size * 0.11

    def fn(x, y):
        d = _distance(segs, x, y)
        if d > width:
            return (0, 0, 0, 0)
        k = 0.75 + 0.25 * (1 - y / h)
        return (250 * k, 246 * k, 236 * k, 255)
    return _px(w, h, fn)

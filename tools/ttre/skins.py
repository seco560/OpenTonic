"""Ed's skins (port, the user's request of 2026-11-16): other looks for Ed, made from his own textures by code.

The six looks follow a small picture the user gave (six Eds taken apart: head, bow tie, body, hands, feet); nothing of
that picture is read here, so the install needs only the game's own textures. Each skin is a set of Ed's textures
(Textures/ed2/hed_*, his bow ties) recoloured and painted over: the colours keep each texel's shading (its brightness
against its kind's mean), and the visors, masks, goggles and helmets are painted in the textures' own uv. Where Ed's
textures put things (read from his model in bar; tools only, nothing of it is kept):
  hed_front   the head's upper half (the eyes' height up to the top); u = 1 at the front's middle, 0 at the back
              (mirrored about the front), v = 0.1 just under the eyes, 1 at the top; the eyes stand at u > 0.75,
              v 0.25 - 0.67
  hed_bouche  the face under the eyes (and its mouths: hed_bouche_*, hed_moue, hed_grr*): v = 1 just under the eyes,
              0 at the chin; u = 1 at the front's middle, 0 at the cheeks
  hed_nuque   the back of the head's lower half
  hed_oeil_*  the eyes (one texture per lid: open, half shut, shut ...), top to bottom down the texture
  hed_crps_part2  the body: u = 1 at the chest's middle, 0 at the back's; cyan above, deep blue below
  hed_shoes_sem   the shoes: cyan uppers, deep blue soles; u = 1 at the toes
  hed_doigts, hed_doigts_dessous, hed_maimpaume  the hands
  hed_neu_pap05, accessoir/Noed_pap_jc_mic_anu   the bow tie (no alpha to hide it: the looks without one in the picture
              wear a collar or a scarf of their colours instead; port choice)

Output (port): <out>/Skins/<skin>/<the texture's path under Textures> (ed2/hed_front.png ...): a frontend loading a
level asks the core for a texture's skinned path (tc_skin_texture_path: ".../Textures/x" -> ".../Skins/<skin>/x") and
takes that file when there is one, so a skin replaces only what it has. The main menu's CUSTOMIZATION screen lists
SKINS and shows each turning (previews(), packed into menu.ttmn by menu.py).

    python3 -m tools.ttre.skins [out folder, default files/out]
"""
import colorsys
import math
import os
import random
import sys

from . import gf
from .export import read_png  # not custom.py's: menu.py imports this module, and its stamp would take in custom's imports

FOLDER = 'Skins'           # under out
BOW_TIES = ('accessoir/Noed_pap_jc_mic_anu',)

# (id, the menu's name): the order the menu steps through them; '' = Ed's own (no files)
SKINS = [('', 'ED'), ('visor', 'VISOR'), ('dapper', 'DAPPER'), ('fencer', 'FENCER'), ('mechanic', 'MECHANIC'),
         ('aviator', 'AVIATOR'), ('ninja', 'NINJA')]


# ---- a texture and its texels ----

class Tex:
    """An RGBA picture with float texels; (u, v) as the model's: u left to right, v bottom to top."""

    def __init__(self, w, h, px):
        self.w, self.h = w, h
        self.px = [[px[4 * i] / 255.0, px[4 * i + 1] / 255.0, px[4 * i + 2] / 255.0, px[4 * i + 3]] for i in range(w * h)]

    def rgba(self):
        out = bytearray()
        for r, g, b, a in self.px:
            out += bytes((min(255, max(0, int(r * 255 + 0.5))), min(255, max(0, int(g * 255 + 0.5))),
                          min(255, max(0, int(b * 255 + 0.5))), a))
        return out

    def uv(self, i):
        """The middle of texel i in (u, v)."""
        return ((i % self.w) + 0.5) / self.w, 1.0 - ((i // self.w) + 0.5) / self.h

    def texels(self):
        for i, p in enumerate(self.px):
            yield i, p, self.uv(i)


def rgb(h):
    """'#rrggbb' -> floats."""
    return tuple(int(h[k:k + 2], 16) / 255.0 for k in (1, 3, 5))


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def smooth(e0, e1, x):
    t = min(max((x - e0) / ((e1 - e0) or 1e-9), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def value(p):
    return max(p[0], p[1], p[2])


# ---- recolouring: each texel by its kind, keeping its shading ----

def kind(p):
    """'skin' (Ed's lilac), 'cloth' (his cyan and deep blue), 'white', 'warm' (the bow tie's reds and yellows) or None,
    and for cloth how far it is towards the deep blue (0 cyan .. 1 blue)."""
    h, s, v = colorsys.rgb_to_hsv(*p[:3])
    h *= 360
    if s < 0.22:
        return ('white', 0.0) if v > 0.62 else (None, 0.0)
    if 228 <= h <= 315:
        return 'skin', 0.0
    if 135 <= h < 228:
        return 'cloth', smooth(186, 206, h) * 0.7 + smooth(0.62, 0.3, v) * 0.3
    if h < 70 or h > 330:
        return 'warm', 0.0
    return None, 0.0


def recolour(t, skin=None, cloth=None, white=None, warm=None, keep=1.0):
    """Each texel of a kind to that kind's colour (cloth: a (light, dark) pair), times its brightness over the kind's
    mean in this texture (so the shading stays); keep: how much of that shading (1 all, 0 flat)."""
    targets = {'skin': skin, 'white': white, 'warm': warm}
    means = {}
    for _, p, _ in t.texels():
        k, _ = kind(p)
        if k:
            m = means.setdefault(k, [0.0, 0])
            m[0] += value(p)
            m[1] += 1
    for _, p, _ in t.texels():
        k, d = kind(p)
        if k == 'cloth' and cloth:
            c = mix(cloth[0], cloth[1], d)
        elif k and targets.get(k):
            c = targets[k]
        else:
            continue
        ref = means[k][0] / means[k][1] or 1.0
        # cloth: the two target tones carry most of the contrast already, so less of the texel's own
        f = 1.0 + (value(p) / ref - 1.0) * keep * (0.6 if k == 'cloth' else 1.0)
        p[0], p[1], p[2] = (min(1.0, x * f) for x in c)


def paint(t, where, colour, shade=0.35, alpha=1.0):
    """Paint over the texels where where(u, v) > 0 (its value: the cover, 0..1; soft edges), keeping `shade` of the
    texel's own light and dark. colour: rgb, or a function (u, v) -> rgb."""
    for _, p, (u, v) in t.texels():
        a = where(u, v) * alpha
        if a <= 0:
            continue
        c = colour(u, v) if callable(colour) else colour
        lum = 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2]
        f = 1.0 + (lum / 0.55 - 1.0) * shade
        q = tuple(min(1.0, x * f) for x in c)
        p[0], p[1], p[2] = mix(p[:3], q, min(1.0, a))


def band(v0, v1, soft=0.04, u0=-1.0, u1=2.0, usoft=0.04):
    """Cover between v0 and v1 (and u0 and u1), soft at the edges."""
    return lambda u, v: smooth(v0 - soft, v0 + soft, v) * smooth(v1 + soft, v1 - soft, v) * \
        smooth(u0 - usoft, u0 + usoft, u) * smooth(u1 + usoft, u1 - usoft, u)


def everywhere(u, v):
    return 1.0


def noise(seed, scale=1.0):
    """Value noise (u, v) -> 0..1, tiling in u."""
    rng = random.Random(seed)
    n = 16
    grid = [[rng.random() for _ in range(n)] for _ in range(n)]

    def at(u, v):
        x, y = (u * scale * n) % n, (v * scale * n) % n
        i, j = int(x), int(y)
        fx, fy = x - i, y - j
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        a, b = grid[j % n][i % n], grid[j % n][(i + 1) % n]
        c, d = grid[(j + 1) % n][i % n], grid[(j + 1) % n][(i + 1) % n]
        return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy
    return at


# ---- the materials painted on ----

def glossy(base, light=(1.0, 1.0, 1.0), slope=1.6, width=0.07, at=0.55):
    """A shiny plastic: the base colour, lighter towards the top, a diagonal glint."""
    def c(u, v):
        x = mix(base, (1.0, 1.0, 1.0), 0.25 * smooth(0.3, 1.0, v))
        streak = math.exp(-((v - at) - (u - 0.5) * slope) ** 2 / (2 * width * width))
        return mix(x, light, 0.85 * streak)
    return c


def leather(base, seed, seams=()):
    """Brown leather: mottled, darker where it creases, stitched seams at the given u."""
    n = noise(seed, 1.5)
    m = noise(seed + 1, 4.0)

    def c(u, v):
        k = 0.82 + 0.25 * n(u, v) + 0.1 * m(u, v)
        x = tuple(min(1.0, b * k) for b in base)
        for s in seams:
            d = abs(u - s)
            if d < 0.02:
                x = mix(x, tuple(b * 0.55 for b in base), 0.8)
            elif d < 0.045 and int(v * 40) % 2 == 0:
                x = mix(x, (0.93, 0.86, 0.7), 0.7)
        return x
    return c


def grille(light, dark, cells=14.0, wire=0.2):
    """A fencing mask's woven wire: crossing diagonals of light wire over the dark inside."""
    def c(u, v):
        a, b = (u + v) * cells, (u - v) * cells
        fa, fb = abs(a - round(a)), abs(b - round(b))
        on = min(fa, fb) < wire
        cross = fa < wire and fb < wire
        if not on:
            return dark
        return mix(light, (1.0, 1.0, 1.0), 0.25) if cross else light
    return c


def goggles(frame, glass, cx, cy, rx, ry):
    """Round goggles over the eye at (cx, cy): a metal rim and a glass with a glint."""
    def where(u, v):
        d = math.hypot((u - cx) / rx, (v - cy) / ry)
        return smooth(1.12, 1.0, d)

    def c(u, v):
        d = math.hypot((u - cx) / rx, (v - cy) / ry)
        if d > 0.78:
            return mix(frame, (1.0, 1.0, 1.0), 0.35 * smooth(0.0, 1.0, v - cy + 0.5))
        g = glossy(glass, slope=1.0, width=0.08, at=cy + 0.1)(u, v)
        return g
    return where, c


# ---- the skins ----

LILAC = rgb('#8f72e6')


def ed_textures(textures):
    """Ed's textures a skin may change: (path under Textures without .png) for those there are."""
    out = []
    d = os.path.join(textures, 'ed2')
    if os.path.isdir(d):
        for f in sorted(os.listdir(d)):
            if f.lower().startswith('hed_') and f.lower().endswith('.png') and 'super' not in f.lower():
                out.append('ed2/' + f[:-4])
    for b in BOW_TIES:
        if os.path.exists(os.path.join(textures, b + '.png')):
            out.append(b)
    return out


def part(name):
    """What a texture is on Ed (see the module's notes)."""
    n = name.split('/')[-1].lower()
    if name in BOW_TIES or n.startswith('hed_neu_pap'):
        return 'bow'
    if n.startswith('hed_oeil') or n.startswith('hed_paup'):
        return 'eye'
    if n == 'hed_pupille':
        return 'pupil'
    if n == 'hed_front':
        return 'front'
    if n == 'hed_nuque':
        return 'nape'
    if n.startswith('hed_bouche') or n in ('hed_moue', 'hed_grrarhg', 'hed_grrrrhh', 'hed_heu'):
        return 'face'
    if n.startswith('hed_crps'):
        return 'body'
    if n.startswith('hed_shoes'):
        return 'shoes'
    if n.startswith('hed_doigts') or n == 'hed_maimpaume':
        return 'hand'
    if n in ('hed_dentier_nz', 'hed_langue', 'hed_intern_bouch2'):
        return 'mouth'
    return 'other'


def skin_texture(skin, name, t):
    """Changes texture `name` (its Tex t) for `skin`; False when the skin leaves it as it is."""
    p = part(name)
    if p in ('other',):
        return False
    if skin == 'visor':
        sky, pink = rgb('#7ec8f2'), rgb('#f070c8')
        if p in ('front', 'nape', 'face'):
            recolour(t, skin=sky)
            if p == 'front':
                paint(t, band(0.22, 0.72, 0.05, 0.42, 2.0, 0.08), glossy(pink, at=0.47))
                paint(t, band(0.93, 1.1, 0.03), rgb('#8a64e0'))  # the purple tuft on top
        elif p in ('eye', 'pupil'):
            paint(t, everywhere, glossy(pink, slope=1.3, at=0.5), shade=0.25)
        elif p == 'body':
            recolour(t, cloth=(rgb('#6fd23c'), rgb('#2a5ad8')))
        elif p == 'hand':
            recolour(t, skin=rgb('#f4f4f8'), keep=0.8)
        elif p == 'shoes':
            recolour(t, cloth=(rgb('#7ad840'), rgb('#f2f2f2')))
        elif p == 'bow':
            recolour(t, warm=None, white=rgb('#ffd23a'))
            for _, q, _ in t.texels():  # the dots stay red, the rest goes orange
                h, s, v = colorsys.rgb_to_hsv(*q[:3])
                if s > 0.3 and (h * 360 < 70 or h * 360 > 330) and q[1] > 0.45:
                    q[0], q[1], q[2] = min(1.0, q[0]), q[1] * 0.62, q[2] * 0.3
        else:
            return False
    elif skin == 'dapper':
        brown = rgb('#7a5638')
        if p == 'front':
            paint(t, band(0.24, 0.7, 0.04, 0.35, 2.0, 0.06), leather(brown, 7), shade=0.4)
        elif p == 'eye':
            paint(t, everywhere, leather(rgb('#6e4c30'), 8), shade=0.5)
        elif p == 'pupil':
            paint(t, everywhere, rgb('#5a3c24'))
        elif p == 'body':
            recolour(t, cloth=(rgb('#e8402a'), rgb('#a8201a')))
            paint(t, lambda u, v: smooth(0.9, 0.93, u) * smooth(0.25, 0.35, v), rgb('#3a8ae0'), shade=0.3)  # the tie
        elif p == 'shoes':
            recolour(t, cloth=(rgb('#40c8e8'), rgb('#1f5ec8')))
        elif p == 'bow':
            for _, q, _ in t.texels():
                h, s, v = colorsys.rgb_to_hsv(*q[:3])
                if s < 0.3 and v > 0.6:
                    q[0], q[1], q[2] = 1.0, 0.62, 0.15
        else:
            return False
    elif skin == 'fencer':
        beige, dark = rgb('#b8a582'), rgb('#2a2622')
        if p in ('front', 'nape', 'face', 'eye', 'pupil', 'mouth'):
            lower = p in ('face', 'nape', 'mouth')
            wire = mix(beige, dark, 0.35) if lower else beige
            paint(t, everywhere, grille(wire, mix(dark, beige, 0.12 if lower else 0.22), 9.0 if p == 'front' else 7.0,
                                         0.16), shade=0.45)
            if p == 'front':  # its rim round the top
                paint(t, band(0.9, 1.1, 0.02), mix(beige, (1, 1, 1), 0.15), shade=0.4)
        elif p == 'body':
            recolour(t, cloth=(rgb('#9aa0a8'), rgb('#1e4c98')))
        elif p == 'shoes':
            recolour(t, cloth=(rgb('#585e66'), rgb('#1d58b0')))
        elif p == 'bow':
            paint(t, everywhere, rgb('#8c929a'), shade=0.5)
        else:
            return False
    elif skin == 'mechanic':
        if p == 'body':
            recolour(t, cloth=(rgb('#b0502c'), rgb('#6a2c18')))
            paint(t, lambda u, v: smooth(0.955, 0.975, u), rgb('#d8d0c0'), shade=0.5)  # the zip
            paint(t, lambda u, v: smooth(0.955, 0.975, u) * (1.0 if int(v * 48) % 2 else 0.0), rgb('#5a5048'))
        elif p == 'shoes':
            recolour(t, cloth=(rgb('#f07a30'), rgb('#62666c')))
        elif p == 'bow':
            paint(t, everywhere, rgb('#7a3a1e'), shade=0.5)
        elif p == 'eye':  # wide awake: no lid over the white
            return False
        else:
            return False
    elif skin == 'aviator':
        brown = rgb('#7c5232')
        cap = leather(brown, 21, seams=(0.5, 0.99))
        if p == 'front':
            paint(t, lambda u, v: max(smooth(0.66, 0.72, v), smooth(0.62, 0.55, u)), cap, shade=0.45)
            paint(t, band(0.66, 0.74, 0.02, 0.55, 2.0), rgb('#4a4e54'), shade=0.3)  # the goggles' strap
        elif p == 'nape':
            paint(t, everywhere, cap, shade=0.45)
        elif p == 'face':  # the cap's flaps down the cheeks
            paint(t, lambda u, v: smooth(0.2, 0.12, u) * smooth(0.2, 0.35, v), cap, shade=0.4)
        elif p == 'eye':
            paint(t, everywhere, glossy(rgb('#8c949c'), slope=1.2, width=0.06, at=0.62), shade=0.3)
            paint(t, band(0.0, 0.3, 0.06), rgb('#5a6068'), shade=0.3)
        elif p == 'pupil':
            paint(t, everywhere, rgb('#3a4048'))
        elif p == 'body':
            recolour(t, cloth=(rgb('#8a5a36'), rgb('#5a3a22')))
            paint(t, lambda u, v: smooth(0.86, 0.9, u), leather(rgb('#d8d0c4'), 22), shade=0.4)  # the scarf's end
        elif p == 'hand':
            recolour(t, skin=rgb('#7a5434'), keep=0.9)
            paint(t, lambda u, v: smooth(0.9, 0.97, v), LILAC, shade=0.4)  # the cuffs show his wrists
        elif p == 'shoes':
            recolour(t, cloth=(rgb('#8a5c38'), rgb('#4e3420')))
        elif p == 'bow':
            paint(t, everywhere, leather(rgb('#ece6dc'), 23), shade=0.5)  # a white scarf
        else:
            return False
    elif skin == 'ninja':
        green = rgb('#3cb43c')
        if p == 'front':
            paint(t, band(0.22, 0.66, 0.03), green, shade=0.45)
        elif p == 'eye':
            paint(t, band(-0.1, 0.62, 0.03), green, shade=0.4)
            paint(t, lambda u, v: band(0.3, 0.44, 0.04)(u, v), rgb('#141414'), shade=0.2)
        elif p == 'pupil':
            paint(t, everywhere, rgb('#141414'))
        elif p == 'body':
            recolour(t, cloth=(rgb('#4c6ae8'), rgb('#3a2aa8')))
        elif p == 'hand':
            recolour(t, skin=rgb('#4cc04a'), keep=0.9)
        elif p == 'shoes':
            recolour(t, cloth=(rgb('#52c844'), rgb('#2440d0')))
        elif p == 'bow':
            paint(t, everywhere, rgb('#2e8a30'), shade=0.5)
        else:
            return False
    else:
        return False
    return True


def skinned(textures, skin, name):
    """(w, h, rgba) of texture `name` (under Textures, no .png) in `skin`, or None when the skin leaves it."""
    if not skin:
        return None
    path = os.path.join(textures, name + '.png')
    if not os.path.exists(path):
        return None
    w, h, px = read_png(path)
    t = Tex(w, h, px)
    if not skin_texture(skin, name, t):
        return None
    return w, h, t.rgba()


def save_png(path, w, h, rgba):
    """Written only when it differs (the app's install copies what changed)."""
    data = gf.to_png(w, h, bytes(rgba))
    if not os.path.exists(path) or open(path, 'rb').read() != data:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)


def make(out):
    textures = os.path.join(out, 'Textures')
    names = ed_textures(textures)
    if not names:
        print('== Skins: no Ed textures (Textures/ed2) (skip)')
        return False
    root = os.path.join(out, FOLDER)
    made, wanted = 0, set()
    for skin, _ in SKINS:
        if not skin:
            continue
        for name in names:
            got = skinned(textures, skin, name)
            if got:
                path = os.path.join(root, skin, name + '.png')
                save_png(path, *got)
                wanted.add(os.path.normpath(path))
                made += 1
    for d, _, files in os.walk(root):  # what an earlier run made that no skin has now
        for f in files:
            if os.path.normpath(os.path.join(d, f)) not in wanted:
                os.remove(os.path.join(d, f))
    print(f'== Skins: {len(SKINS) - 1} skins, {made} textures in {root}')
    return True


# ---- the menu's previews: Ed turning, in each skin ----

PREVIEW_W, PREVIEW_H, PREVIEW_TURNS = 128, 192, 8
SUPER = 2  # samples per texel across


def ed_triangles(levels_dir, level_name='bar'):
    """Ed's triangles as the level holds him at its start: [(texture path under Textures, no .png, lower case),
    ((x, y, z), (u, v)) * 3]."""
    from . import level, sna
    data = level.LevelData(sna.Level(levels_dir, level_name))
    loc = data.player()
    out = []
    for key, geo, m in data.meshes(True, None):
        if not (isinstance(key, tuple) and getattr(key[0], 'perso', None) and key[0].perso.loc == loc):
            continue
        verts = [level.mat_apply(m, v) for v in geo.vertices]
        flip = level.mat_det(m) < 0
        for el in geo.elements:
            tex = el.material.texture.name if el.material and el.material.texture else None
            name = os.path.splitext(tex.replace('\\', '/'))[0].lower() if tex else None
            for tri, uvt in zip(el.triangles, el.uv_tris or [(0, 0, 0)] * len(el.triangles)):
                if flip:
                    tri, uvt = tri[::-1], uvt[::-1]
                uv = [el.uvs[i] if el.uvs else (0.0, 0.0) for i in uvt]
                out.append((name, tuple(zip((tuple(verts[i]) for i in tri), uv))))
    return out


def _texture_finder(textures):
    """Lower-case path under Textures (no .png) -> its real name (the converted files keep the archive's case)."""
    idx = {}
    for d, _, files in os.walk(textures):
        for f in files:
            if f.lower().endswith('.png'):
                rel = os.path.relpath(os.path.join(d, f), textures)[:-4].replace(os.sep, '/')
                idx[rel.lower()] = rel
    return idx


def render(tris, look, angle, w=PREVIEW_W, h=PREVIEW_H):
    """Ed seen from `angle` (radians round him from his front), into w x h RGBA with a clear background; look(name)
    gives a texture's (w, h, rgba) or None. Lit from the upper left, a soft rim from behind."""
    W, H = w * SUPER, h * SUPER
    pts = [p for _, t in tris for p, _ in t]
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    ctr = [(a + b) / 2 for a, b in zip(lo, hi)]
    eyes = [p for n, t in tris if n and n.endswith('hed_pupille') for p, _ in t]
    front = math.atan2(sum(p[1] for p in eyes) / len(eyes) - ctr[1], sum(p[0] for p in eyes) / len(eyes) - ctr[0]) \
        if eyes else 0.0
    a = front + angle
    f = (-math.cos(a), -math.sin(a), 0.0)  # looking at him
    side = (f[1], -f[0], 0.0)
    span = (hi[2] - lo[2]) * 1.06
    scale = H / span
    light = (lambda v: tuple(x / math.sqrt(sum(c * c for c in v)) for x in v))(
        (-0.45 * side[0] - 0.7 * f[0], -0.45 * side[1] - 0.7 * f[1], 0.75))
    zb = [math.inf] * (W * H)
    img = [None] * (W * H)
    cache = {}
    for name, tri in tris:
        P = []
        for p, _ in tri:
            d = [p[k] - ctr[k] for k in range(3)]
            P.append((W / 2 + scale * sum(d[k] * side[k] for k in range(3)), H / 2 - scale * d[2],
                      sum(d[k] * f[k] for k in range(3))))
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = P
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-9:
            continue
        A, B, C = [p for p, _ in tri]
        n = [(B[1] - A[1]) * (C[2] - A[2]) - (B[2] - A[2]) * (C[1] - A[1]),
             (B[2] - A[2]) * (C[0] - A[0]) - (B[0] - A[0]) * (C[2] - A[2]),
             (B[0] - A[0]) * (C[1] - A[1]) - (B[1] - A[1]) * (C[0] - A[0])]
        nl = math.sqrt(sum(x * x for x in n)) or 1.0
        n = [x / nl for x in n]
        if sum(n[k] * f[k] for k in range(3)) > 0:  # facing away (the model's faces are counter-clockwise outside)
            n = [-x for x in n]
        shade = 0.5 + 0.55 * max(0.0, sum(n[k] * light[k] for k in range(3))) + \
            0.2 * max(0.0, 1.0 - abs(sum(n[k] * f[k] for k in range(3)))) ** 2
        if name not in cache:
            cache[name] = look(name) if name else None
        tex = cache[name]
        uv = [t for _, t in tri]
        for py in range(max(0, int(min(y0, y1, y2))), min(H, int(max(y0, y1, y2)) + 1)):
            for px in range(max(0, int(min(x0, x1, x2))), min(W, int(max(x0, x1, x2)) + 1)):
                qx, qy = px + 0.5, py + 0.5
                w0 = ((x1 - qx) * (y2 - qy) - (x2 - qx) * (y1 - qy)) / area
                w1 = ((x2 - qx) * (y0 - qy) - (x0 - qx) * (y2 - qy)) / area
                w2 = 1 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                z = w0 * z0 + w1 * z1 + w2 * z2
                i = py * W + px
                if z >= zb[i]:
                    continue
                if tex:
                    tw, th, tp = tex
                    u = w0 * uv[0][0] + w1 * uv[1][0] + w2 * uv[2][0]
                    v = w0 * uv[0][1] + w1 * uv[1][1] + w2 * uv[2][1]
                    k = 4 * ((th - 1 - int(v * th) % th) * tw + int(u * tw) % tw)  # v = 0: the bottom row
                    col = tp[k:k + 3]
                else:
                    col = (200, 200, 200)
                zb[i] = z
                img[i] = tuple(min(255.0, c * shade) for c in col)
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            acc, cov = [0.0, 0.0, 0.0], 0
            for sy in range(SUPER):
                for sx in range(SUPER):
                    c = img[(y * SUPER + sy) * W + x * SUPER + sx]
                    if c:
                        cov += 1
                        for k in range(3):
                            acc[k] += c[k]
            if cov:
                o = 4 * (y * w + x)
                out[o:o + 4] = bytes((int(acc[0] / cov), int(acc[1] / cov), int(acc[2] / cov), 255 * cov // (SUPER * SUPER)))
    return out


def previews(textures, levels_dir, log=print):
    """[(skin id, name, [rgba PREVIEW_W x PREVIEW_H] x PREVIEW_TURNS)] for SKINS (Ed turning anticlockwise from his
    front), or [] without bar or Ed's textures."""
    if not os.path.isdir(os.path.join(textures, 'ed2')) or not os.path.isdir(os.path.join(levels_dir, 'bar')):
        log('   skins: no bar or Ed textures, no previews')
        return []
    tris = ed_triangles(levels_dir)
    if not tris:
        return []
    idx = _texture_finder(textures)
    out = []
    for skin, title in SKINS:
        def look(name, skin=skin):
            real = idx.get(name)
            if not real:
                return None
            return skinned(textures, skin, real) or read_png(os.path.join(textures, real + '.png'))
        out.append((skin, title, [render(tris, look, 2 * math.pi * k / PREVIEW_TURNS) for k in range(PREVIEW_TURNS)]))
    log(f'   skins: {len(out)} previews, {PREVIEW_TURNS} turns each')
    return out


if __name__ == '__main__':
    make(sys.argv[1] if len(sys.argv) > 1 else 'files/out')

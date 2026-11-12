"""The game's HUD data (docs/ENGINE.md "The HUD"): files/out/ttlv/hud.tthd, one file for all levels, read by the core's
tc_hud.c. Run as `python3 -m tools.ttre.hud files/out` (a step of tools/convert_files.sh).

The exe draws every HUD element as a "text": a string of game.dsb shown in one of the fonts of fix.sna (FON_*; the
drawer is FON_004cdd10). A font's glyph is a small object: a textured quad (FixFont, FixHitPoints, FontLips), a sprite,
or a 3D model (FontPC's extruded digits; FontFix3D's spade, bee and Doc's objects), drawn through a camera. Port: every
glyph is rendered here, once, front-on, into one atlas, so the game draws flat quads at any aspect ratio.

  game.dsb     the texts, in file order: "NewString" name, text, alignment; "NewStringLenght" name, u32 capacity,
               alignment (an empty buffer the game prints numbers into); "NewUpperString". A script's text node
               (type 30) holds the record's index over all three kinds (0x250 = TXTTOTALSKI).
  fix.sna      the fonts, a list (FON_fonfont_004cd410 walks it by name): +0 next, +4 prev, +0xC name, +0x10 depth
               test, +0x14 flags, +0x18 n, +0x1C the table of {u16 code, f32 baseline shift, ptr glyph}, +0x20 the
               spacing; a glyph {ptr object, f32 line step, f32 advance}. A script's font node (type 31) points at
               one: exported as its rank in the list (ai.py).
  fix.gpt      +0x5E0 the HUD's block (the exe's DAT_008bc820, 0x2CC bytes): the standard HUD's seventeen texts at
               +0x48; +0x8D8 a pointer per text to its record, whose +0 is the font it was saved with.

hud.tthd (container: chunks.py), chunks:
  ATLS  u16 width, u16 height, f32 panel uv[4] (a block of the text backdrop's colour, MotorTxt/LipSync.tga), rgba
  FONT  u16 n, n x { u8 name length, name, f32 spacing, f32 line step, u16 n_glyphs,
                     n_glyphs x { u16 code, u16 0, f32 line step, f32 advance, f32 baseline shift,
                                  f32 cell[4] (x0, z0, x1, z1 about the glyph's centre, font units; all 0: nothing drawn),
                                  f32 uv[4] (u0, v0, u1, v1: the cell's top-left and bottom-right) } }
  TEXT  u16 n, n x { u8 font (0xFF none), u8 0, u16 capacity, u16 length, bytes (the game's 8-bit codes) }
  STDH  u16 n (17), n x u16: the standard HUD's texts, in the block's order (STANDARD below)
  HEAD  f32 cell[4], f32 uv[4]: Ed's head beside the lives, as a glyph's cell and uv. The exe draws the HUD perso's
        (Ed's) channel 0x38 object, else his channel 3 (the block's u16 +0x46 / +0x44): his head as his animation has
        it that frame, turned 35 degrees about the vertical and scaled 0.73 (GAM_0041a720). Port: the head of his
        first state's first frame, drawn once.
"""
import math
import os
import struct
import sys

from . import chunks, level, sna, speech

LEVELS = 'files/sources/GAMEDATA/WORLD/LEVELS'
GAMEDATA = 'files/sources/GAMEDATA'
KINDS = (b'NewStringLenght\0', b'NewUpperString\0', b'NewString\0')
HUD_BLOCK = 0x5E0          # fix.gpt: the exe's DAT_008bc820
HUD_TEXTS = HUD_BLOCK + 0x48
TEXT_RECORDS = 0x8D8       # fix.gpt: a pointer per text (FON_004cd7c0)
STANDARD = ('life line 1', 'life line 2', 'spade', 'spade number', 'bee', 'bee number', 'tings', 'tings number',
            'lives', 'doc object', 'doc object number', 'thermometer', 'graduation', 'mercury', 'cheat used',
            'demo texts', 'logo')
# Which fonts are rendered, and which of their glyphs (None: all). FontPC is the 3D font of the counters: only what
# the counters print. The menus' fonts (FontEd, TitleEd, ExtraFont) are left out: no script shows a text in them.
FONTS = {'FixHitPoints': None, 'FixExtra': None, 'FixFont': None, 'FontFix3D': None, 'FontLips': None,
         'FontPC': ' 0123456789x:.'}
MESH_DENSITY = 128.0       # pixels per font unit for a 3D glyph (a 1080-line screen shows 154 per unit at depth 0)
MAX_DENSITY = 256.0
ATLAS_W = 1024
LIGHT = (-0.35, -0.8, 0.5)  # port: towards the viewer's upper left (the exe lights the texts with its own light, not read)


def texts(gamedata=GAMEDATA):
    """[(name, text bytes, capacity)] of game.dsb in file order."""
    d = bytes(sna.decrypt(open(f'{gamedata}/game.dsb', 'rb').read()))
    found = sorted((i, k) for k in KINDS for i in _find_all(d, k))
    out, last = [], -1
    for i, k in found:
        if i < last:  # "NewString" inside a longer keyword already taken
            continue
        o = i + len(k)
        n, = struct.unpack_from('<H', d, o)
        name = d[o + 2:o + 2 + n].split(b'\0')[0].decode('latin-1')
        o += 2 + n
        if k == KINDS[0]:
            cap, = struct.unpack_from('<I', d, o)
            out.append((name, b'', cap))
        else:
            n, = struct.unpack_from('<H', d, o)
            text = d[o + 2:o + 2 + n].split(b'\0')[0]
            out.append((name, text.upper() if k == KINDS[1] else text, len(text)))
        last = o
    return out


def _find_all(d, k):
    i = d.find(k)
    while i >= 0:
        # a record's keyword is a u16-counted string: "NewString\0" also ends "NewUpperString\0"
        if struct.unpack_from('<H', d, i - 2)[0] == len(k):
            yield i
        i = d.find(k, i + 1)


def _cstr(loc):
    return loc[0].data[loc[1]:loc[1] + 64].split(b'\0')[0].decode('latin-1')


def font_records(lv):
    """The fonts of fix.sna in list order: [(record location, name)]."""
    i = lv.fix.data.find(speech.FONT)
    rec = (lv.fix, i - 0x2C)
    out, cur = [], rec
    while cur and cur not in out:
        out.append(cur)
        cur = lv.ptr(cur, 0)
    cur = lv.ptr(rec, 4)
    while cur and cur not in out:
        out.insert(0, cur)
        cur = lv.ptr(cur, 4)
    return [(r, _cstr(lv.ptr(r, 0xC) or (r[0], r[1] + 0x2C))) for r in out]


def font_index(lv):
    """{font record location: its rank}: what a script's font node (type 31) is exported as."""
    if not hasattr(lv, '_hud_fonts'):
        lv._hud_fonts = {r: k for k, (r, _) in enumerate(font_records(lv))}
    return lv._hud_fonts


class Textures:
    def __init__(self):
        self.cache = {}

    def get(self, tex):
        if tex is None:
            return None
        if tex.name not in self.cache:
            try:
                self.cache[tex.name] = speech._texture(tex.name.replace('\\', '/'))
            except KeyError:
                self.cache[tex.name] = None
        return self.cache[tex.name]


def _sample(tex, u, v):
    """Bilinear, wrapped; straight-alpha floats 0..255. v runs up the picture (its rows are stored top first)."""
    w, h, px = tex
    x, y = u * w - 0.5, (1.0 - v) * h - 0.5
    x0, y0 = math.floor(x), math.floor(y)
    fx, fy = x - x0, y - y0
    out = [0.0, 0.0, 0.0, 0.0]
    for dy, wy in ((0, 1 - fy), (1, fy)):
        for dx, wx in ((0, 1 - fx), (1, fx)):
            k = wx * wy
            if k <= 0:
                continue
            o = 4 * (((y0 + dy) % h) * w + (x0 + dx) % w)
            a = px[o + 3] * k
            out[0] += px[o] * a
            out[1] += px[o + 1] * a
            out[2] += px[o + 2] * a
            out[3] += a
    if out[3] > 0:
        return out[0] / out[3], out[1] / out[3], out[2] / out[3], out[3]
    return 0.0, 0.0, 0.0, 0.0


def glyph_triangles(geo, textures):
    """A glyph object as triangles seen from the front: ([((x, z, depth), (u, v), shade) * 3, texture, rgb], flat).
    x right, z up; the viewer looks along +y."""
    n = math.sqrt(sum(c * c for c in LIGHT))
    light = tuple(c / n for c in LIGHT)
    flat = all(abs(v[1]) < 1e-4 for v in geo.vertices) and not geo.sprites or not geo.elements
    out = []
    for el in geo.elements:
        m = el.material
        tex = textures.get(m.texture) if m else None
        rgb = tuple(255.0 * c for c in (m.diffuse[:3] if m else (1, 1, 1)))
        for tri, uvt in zip(el.triangles, el.uv_tris or [None] * len(el.triangles)):
            corners = []
            p = [geo.vertices[i] for i in tri]
            e1 = [p[1][k] - p[0][k] for k in range(3)]
            e2 = [p[2][k] - p[0][k] for k in range(3)]
            fn = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
            fl = math.sqrt(sum(c * c for c in fn)) or 1.0
            for k, i in enumerate(tri):
                nv = geo.normals[i] if i < len(geo.normals) else tuple(c / fl for c in fn)
                if nv[1] > 0:  # facing away: the far side of a closed model, or a quad wound the other way
                    nv = tuple(-c for c in nv)
                shade = 1.0 if flat else 0.55 + 0.45 * max(0.0, sum(a * b for a, b in zip(nv, light)))
                uv = el.uvs[uvt[k]] if uvt and el.uvs else (0.0, 0.0)
                corners.append(((p[k][0], p[k][2], p[k][1]), uv, shade))
            out.append((corners, tex, rgb))
    for vi, hx, hy, m in geo.sprites:
        c = geo.vertices[vi]
        tex = textures.get(m.texture)
        # v runs up the picture (_sample), so the sprite's top edge is v = 1
        quad = [((c[0] - hx, c[2] + hy, c[1]), (0.0, 1.0), 1.0), ((c[0] + hx, c[2] + hy, c[1]), (1.0, 1.0), 1.0),
                ((c[0] + hx, c[2] - hy, c[1]), (1.0, 0.0), 1.0), ((c[0] - hx, c[2] - hy, c[1]), (0.0, 0.0), 1.0)]
        out.append(([quad[0], quad[1], quad[2]], tex, (255.0, 255.0, 255.0)))
        out.append(([quad[0], quad[2], quad[3]], tex, (255.0, 255.0, 255.0)))
    return out, flat


def density(tris, flat):
    """Pixels per font unit: a flat textured glyph at its texture's own resolution, a model at MESH_DENSITY."""
    if not flat:
        return MESH_DENSITY
    best = 0.0
    for corners, tex, _ in tris:
        if not tex:
            continue
        for a, b in ((0, 1), (1, 2), (2, 0)):
            dx = math.hypot(corners[a][0][0] - corners[b][0][0], corners[a][0][1] - corners[b][0][1])
            dt = math.hypot((corners[a][1][0] - corners[b][1][0]) * tex[0], (corners[a][1][1] - corners[b][1][1]) * tex[1])
            if dx > 1e-6:
                best = max(best, dt / dx)
    return min(MAX_DENSITY, best) if best > 0 else MESH_DENSITY


def render(tris, flat, ppu):
    """(left, top, right, bottom in font units, width, height, rgba bytes) of a glyph's triangles, or None."""
    if not tris:
        return None
    xs = [c[0][0] for t in tris for c in t[0]]
    zs = [c[0][1] for t in tris for c in t[0]]
    x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
    w, h = max(1, math.ceil((x1 - x0) * ppu - 0.05)), max(1, math.ceil((z1 - z0) * ppu - 0.05))
    ss = 1 if flat else 3
    W, H = w * ss, h * ss
    color = [None] * (W * H)
    depth = [1e30] * (W * H)
    step = 1.0 / (ppu * ss)
    for corners, tex, rgb in tris:
        (ax, az, ad), (bx, bz, bd), (cx, cz, cd) = (c[0] for c in corners)
        area = (bx - ax) * (cz - az) - (bz - az) * (cx - ax)
        if abs(area) < 1e-12:
            continue
        i0 = max(0, int((min(ax, bx, cx) - x0) / step) - 1)
        i1 = min(W - 1, int((max(ax, bx, cx) - x0) / step) + 1)
        j0 = max(0, int((z1 - max(az, bz, cz)) / step) - 1)
        j1 = min(H - 1, int((z1 - min(az, bz, cz)) / step) + 1)
        for j in range(j0, j1 + 1):
            pz = z1 - (j + 0.5) * step
            for i in range(i0, i1 + 1):
                px = x0 + (i + 0.5) * step
                wa = ((bx - px) * (cz - pz) - (bz - pz) * (cx - px)) / area
                wb = ((cx - px) * (az - pz) - (cz - pz) * (ax - px)) / area
                wc = 1.0 - wa - wb
                if wa < -1e-6 or wb < -1e-6 or wc < -1e-6:
                    continue
                d = wa * ad + wb * bd + wc * cd
                k = j * W + i
                if not flat and d >= depth[k]:
                    continue
                shade = wa * corners[0][2] + wb * corners[1][2] + wc * corners[2][2]
                if tex:
                    u = wa * corners[0][1][0] + wb * corners[1][1][0] + wc * corners[2][1][0]
                    v = wa * corners[0][1][1] + wb * corners[1][1][1] + wc * corners[2][1][1]
                    r, g, b, a = _sample(tex, u, v)
                else:
                    r, g, b, a = rgb[0], rgb[1], rgb[2], 255.0
                if not flat:
                    if a < 128.0:
                        continue
                    a = 255.0
                    depth[k] = d
                elif a <= 0.0:
                    continue
                color[k] = (min(255.0, r * shade), min(255.0, g * shade), min(255.0, b * shade), a)
    out = bytearray(w * h * 4)
    n = ss * ss
    for y in range(h):
        for x in range(w):
            r = g = b = a = 0.0
            for sy in range(ss):
                row = (y * ss + sy) * W + x * ss
                for sx in range(ss):
                    c = color[row + sx]
                    if c:
                        r += c[0] * c[3]
                        g += c[1] * c[3]
                        b += c[2] * c[3]
                        a += c[3]
            if a > 0:
                o = 4 * (y * w + x)
                out[o], out[o + 1], out[o + 2], out[o + 3] = int(r / a + 0.5), int(g / a + 0.5), int(b / a + 0.5), int(a / n + 0.5)
    if not any(out[3::4]):
        return None
    return (x0, z1, x0 + w / ppu, z1 - h / ppu, w, h, bytes(out))  # the cell: left, top, right, bottom


HEAD_CHANNEL = 3           # fix.gpt HUD block +0x44 (the exe's DAT_008bc864); +0x46 = 0x38 is tried first
HEAD_TURN = (0.819648, 0.572867)  # GAM_0041a720: the head's axes, cos and sin of 35 degrees about the vertical
HEAD_SCALE = 0.73


def head(data, textures):
    """Ed's head for the lives counter: (left, top, right, bottom, width, height, rgba) or None."""
    ed = data.perso_in_fix[0][0] if data.perso_in_fix else None
    perso = next((p for p in data.persos() if p.loc == ed), None)
    if not perso or not perso.anim:
        return None
    anim = perso.anim
    objs = anim.objects[0]
    for want in (0x38, HEAD_CHANNEL):
        for i, ch in enumerate(anim.chans):
            got = perso.objects.get(objs[i]) if ch[1] == want else None
            if got:
                geo, m = got
                c, s = HEAD_TURN

                def turn(v, scale):
                    x = (m[0] * v[0] + m[1] * v[1] + m[2] * v[2]) if scale else v[0]
                    y = (m[4] * v[0] + m[5] * v[1] + m[6] * v[2]) if scale else v[1]
                    z = (m[8] * v[0] + m[9] * v[1] + m[10] * v[2]) if scale else v[2]
                    k = HEAD_SCALE if scale else 1.0
                    return ((c * x - s * y) * k, (s * x + c * y) * k, z * k)
                turned = level.Geometric([turn(v, True) for v in geo.vertices], [turn(n, False) for n in geo.normals])
                turned.elements = geo.elements
                tris, flat = glyph_triangles(turned, textures)
                return render(tris, False, MESH_DENSITY)
    return None


class Atlas:
    """Shelves, tallest first; a clear pixel round every cell."""

    def __init__(self, width=ATLAS_W):
        self.width, self.cells = width, []

    def add(self, w, h, rgba):
        self.cells.append([w, h, rgba, 0, 0])
        return self.cells[-1]

    def pack(self):
        x = y = 1
        shelf = 0
        for c in sorted(self.cells, key=lambda c: -c[1]):
            if x + c[0] + 1 > self.width:
                x, y, shelf = 1, y + shelf + 1, 0
            c[3], c[4] = x, y
            x += c[0] + 1
            shelf = max(shelf, c[1])
        height = y + shelf + 1
        height += -height % 4
        px = bytearray(self.width * height * 4)
        for w, h, rgba, cx, cy in self.cells:
            for row in range(h):
                o = 4 * ((cy + row) * self.width + cx)
                px[o:o + 4 * w] = rgba[4 * row * w:4 * (row + 1) * w]
        return height, px

    def uv(self, c, height):
        return c[3] / self.width, c[4] / height, (c[3] + c[0]) / self.width, (c[4] + c[1]) / height


def build(levels_dir=LEVELS, gamedata=GAMEDATA, level_name=None, log=print):
    names = sorted((n for n in os.listdir(levels_dir) if os.path.isdir(os.path.join(levels_dir, n))), key=str.lower)
    lv = sna.Level(levels_dir, level_name or names[0])
    data = level.LevelData(lv)
    un = data.un
    table = texts(gamedata)
    g = lv.fix_gpt
    standard = struct.unpack_from(f'<{len(STANDARD)}I', g.data, HUD_TEXTS)
    if max(standard) >= len(table) or table[standard[0]][0] != 'FIX_LIFEPOINTSFIRSTLINE':
        raise ValueError(f'fix.gpt: the HUD block is not at {HUD_BLOCK:#x} (texts {standard[:3]})')
    records = font_records(lv)
    rank = {r: k for k, (r, _) in enumerate(records)}
    text_font = []
    for t in range(len(table)):
        rec = lv.ptr((g, TEXT_RECORDS + 4 * t))
        text_font.append(rank.get(lv.ptr(rec, 0) if rec else None, 0xFF))

    textures, atlas, fonts = Textures(), Atlas(), []
    for rec, name in records:
        n, = un('I', rec, 0x18)
        spacing, = un('f', rec, 0x20)
        tab = lv.ptr(rec, 0x1C)
        want = FONTS.get(name, '') if name in FONTS else ''
        glyphs, line_step = [], 0.0
        for k in range(n if tab else 0):
            e = lv.ptr(tab, 4 * k)
            code, = un('H', e)
            shift, = un('f', e, 4)
            gl = lv.ptr(e, 8)
            step, advance = un('2f', gl, 4)
            if code == 0x20:
                line_step = step
            cell = None
            obj = lv.ptr(gl, 0)
            if obj and (want is None or chr(code) in want):
                tris, flat = glyph_triangles(data.geometric(obj), textures)
                got = render(tris, flat, density(tris, flat))
                if got:
                    cell = (got[:4], atlas.add(*got[4:]))
            if cell or code == 0x20 or want is None or chr(code) in want:
                glyphs.append((code, step, advance, shift, cell))
        fonts.append((name, spacing, line_step, glyphs))
        log(f'   font {name}: {sum(1 for x in glyphs if x[4])} of {n} glyphs drawn')

    head_got = head(data, textures)
    head_cell = (head_got[:4], atlas.add(*head_got[4:])) if head_got else None
    log('   Ed\'s head: ' + ('drawn' if head_got else 'not found'))
    _, _, panel = speech._texture(speech.PANEL)
    panel_cell = atlas.add(8, 8, bytes(panel[:4]) * 64)
    height, pixels = atlas.pack()
    pu = atlas.uv(panel_cell, height)
    inset = (2 / atlas.width, 2 / height)
    atls = struct.pack('<HH4f', atlas.width, height, pu[0] + inset[0], pu[1] + inset[1], pu[2] - inset[0],
                       pu[3] - inset[1]) + bytes(pixels)
    font = struct.pack('<H', len(fonts))
    for name, spacing, line_step, glyphs in fonts:
        nb = name.encode('latin-1')[:255]
        font += struct.pack('<B', len(nb)) + nb + struct.pack('<ffH', spacing, line_step, len(glyphs))
        for code, step, advance, shift, cell in glyphs:
            box = cell[0] if cell else (0.0, 0.0, 0.0, 0.0)
            uv = atlas.uv(cell[1], height) if cell else (0.0, 0.0, 0.0, 0.0)
            # render's box is left, top, right, bottom: stored x0, z0 (bottom), x1, z1 (top)
            font += struct.pack('<HH3f4f4f', code, 0, step, advance, shift, box[0], box[3], box[2], box[1], *uv)
    text = struct.pack('<H', len(table))
    for (name, body, cap), f in zip(table, text_font):
        text += struct.pack('<BBHH', f, 0, min(cap, 0xFFFF), len(body)) + body
    stdh = struct.pack(f'<H{len(standard)}H', len(standard), *standard)
    out = [('ATLS', atls), ('FONT', font), ('TEXT', text), ('STDH', stdh)]
    if head_cell:
        box = head_cell[0]
        out.append(('HEAD', struct.pack('<4f4f', box[0], box[3], box[2], box[1], *atlas.uv(head_cell[1], height))))
    log(f'   atlas {atlas.width} x {height}, {len(atlas.cells)} cells; {len(table)} texts')
    return out, (atlas.width, height, pixels)


def main(out, preview=None):
    if not os.path.isdir(LEVELS):
        print('== HUD: no retail levels (skip)')
        return
    print('== HUD: fonts, texts and the atlas')
    body, (w, h, px) = build()
    chunks.write(os.path.join(out, 'ttlv', 'hud.tthd'), 'TTHD', body)
    if preview:
        from . import gf
        with open(preview, 'wb') as f:
            f.write(gf.to_png(w, h, bytes(px)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)

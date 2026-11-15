"""The main menu's data (docs/ENGINE.md "The menus"): files/out/ttlv/menu.ttmn, read by the core's tc_menu.c. Run as
`python3 -m tools.ttre.menu files/out` (a step of tools/convert_files.sh).

The exe's menus (module MNU) are binary records in fix.sna: fix.gpt +0x0C holds 100 menu pointers (GAM_Initeng_0040d700
reads them into DAT_0093e460; 53 are set, found by name with MNU_mnubase_0044ea50). A menu: +0 its name (inline), +0x34
u8 item count, +0x35 u8 the selection, +0x38 its items, +0x100 i32 background count, +0x104 the backgrounds. An item:
+0 a text index (types 0 and 1) or a record of them (0x2xx), +4 i32 type (0 a label, 1 a choice, 0x201/0x202 a name
field, 0x204/0x208 a choice with a value or arrows, 0x402 an animated perso), +9 selected, +0xA enabled, +0xB shown,
+0xC f32 x, y, z (the text's middle, font units; z away from the screen). The texts are game.dsb's, in their own fonts
(FON: the record per text at fix.gpt +0x8D8). Read for the main menu ("StartUp"): the title MNU_TITLEMAINMENU in TitleEd
at y 3; the choices in FontPC at y 1.0, 0.2, -0.6, -1.4, -2.2; two EdMenu persos at (+-8, -6.5, 5). See MNU_0044df60 (the
frame) and FUN_0044d760 (an item's drawing).

A background (+0x104 -> list of): +0 i32 kind (1 a vignette; 0 a texture over the screen), +8 the texture, +0xC f32 the
screen rect (x0, x1, y0, y1), +0x1C f32, +0x20 u8, +0x21 u8, +0x22 u8 flags (1: the corners' uv turn and zoom over time,
from the floats at +0x24 ... +0x60; MNU_0044df60 steps them each frame), passed on as read.

The texts' effects (fix.gpt +0x220, the exe's DAT_0051b8c0: four records of 0x24, for an item's states 2 (not selected),
1, 3 (selected), 4; FON_004cdbb0 / FUN_004cc100) are passed on as read.

This module imports hud.py (its renderer) but nothing imports it: editing it doesn't stale the levels (chunks.KINDS).

menu.ttmn (container: chunks.py), chunks:
  ATLS  u16 width, u16 height, rgba (straight alpha, top row first)
  FONT  as hud.tthd's FONT: u16 n, n x { u8 name length, name, f32 spacing, f32 line step, u16 n_glyphs, n_glyphs x {
        u16 code, u16 0, f32 line step, f32 advance, f32 baseline shift, f32 cell[4], f32 uv[4] } }
  NTXT  u16 n, n x { u8 name length, name, u8 font (FONT index, 0xFF none), u16 length, bytes }: game.dsb's MNU_ texts
  IMGS  u16 n, n x { u8 name length, name, u16 width, u16 height, rgba }: the backgrounds' textures
  LAYR  u16 n, n x { u8 image (IMGS index), u8 flags (+0x22), u8 +0x20, u8 +0x21, f32 rect[4] (+0xC), f32 +0x1C,
        f32 wobble[16] (+0x24 ... +0x60) }: the main menu's backgrounds, back to front
  EFCT  u16 n (4), n x 36 bytes: the texts' effects, as fix.gpt has them
"""
import os
import struct
import sys

from . import chunks, hud, level, sna, speech

LEVELS = hud.LEVELS
GAMEDATA = hud.GAMEDATA
MENUS = 0x0C               # fix.gpt: 100 menu pointers (DAT_0093e460)
EFFECTS = 0x220            # fix.gpt: the texts' effects (DAT_0051b8c0), 4 x 0x24
MAIN_MENU = 'StartUp'
FONTS = ('TitleEd', 'FontPC')  # the menus' title font and their choices' (FontEd: no menu text uses it)

chunks.KINDS.setdefault('menu', (('menu',), ()))


def _menus(lv):
    """{name: location} of the menus fix.gpt names."""
    g = lv.fix_gpt
    out = {}
    for k in range(100):
        m = lv.ptr((g, MENUS + 4 * k))
        if m:
            out[m[0].data[m[1]:m[1] + 0x34].split(b'\0')[0].decode('latin-1')] = m
    return out


def _font_glyphs(lv, data, rec, textures, atlas):
    un = data.un
    n, = un('I', rec, 0x18)
    spacing, = un('f', rec, 0x20)
    tab = lv.ptr(rec, 0x1C)
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
        if obj:
            tris, flat = hud.glyph_triangles(data.geometric(obj), textures)
            got = hud.render(tris, flat, hud.density(tris, flat))
            if got:
                cell = (got[:4], atlas.add(*got[4:]))
        glyphs.append((code, step, advance, shift, cell))
    return spacing, line_step, glyphs


def build(levels_dir=LEVELS, gamedata=GAMEDATA, log=print):
    names = sorted((n for n in os.listdir(levels_dir) if os.path.isdir(os.path.join(levels_dir, n))), key=str.lower)
    lv = sna.Level(levels_dir, names[0])
    data = level.LevelData(lv)
    un = data.un
    g = lv.fix_gpt
    menus = _menus(lv)
    if MAIN_MENU not in menus:
        raise ValueError(f'fix.gpt: no menu {MAIN_MENU} at {MENUS:#x} ({sorted(menus)[:5]})')

    records = dict((name, rec) for rec, name in hud.font_records(lv))
    textures, atlas, fonts = hud.Textures(), hud.Atlas(), []
    for name in FONTS:
        spacing, line_step, glyphs = _font_glyphs(lv, data, records[name], textures, atlas)
        fonts.append((name, spacing, line_step, glyphs))
        log(f'   font {name}: {sum(1 for x in glyphs if x[4])} of {len(glyphs)} glyphs drawn')
    height, pixels = atlas.pack()

    font = struct.pack('<H', len(fonts))
    for name, spacing, line_step, glyphs in fonts:
        nb = name.encode('latin-1')
        font += struct.pack('<B', len(nb)) + nb + struct.pack('<ffH', spacing, line_step, len(glyphs))
        for code, step, advance, shift, cell in glyphs:
            box = cell[0] if cell else (0.0, 0.0, 0.0, 0.0)
            uv = atlas.uv(cell[1], height) if cell else (0.0, 0.0, 0.0, 0.0)
            font += struct.pack('<HH3f4f4f', code, 0, step, advance, shift, box[0], box[3], box[2], box[1], *uv)

    # game.dsb's menu texts, by name, in the font each was saved with (when it is one of ours)
    rank = {r: k for k, (r, _) in enumerate(hud.font_records(lv))}
    by_rank = {k: n for k, (_, n) in enumerate(hud.font_records(lv))}
    table = hud.texts(gamedata)
    ntxt = []
    for t, (name, body, _cap) in enumerate(table):
        if not name.startswith('MNU_') or not body:
            continue
        rec = lv.ptr((g, hud.TEXT_RECORDS + 4 * t))
        fname = by_rank.get(rank.get(lv.ptr(rec, 0) if rec else None))
        f = FONTS.index(fname) if fname in FONTS else 0xFF
        nb = name.encode('latin-1')
        ntxt.append(struct.pack('<B', len(nb)) + nb + struct.pack('<BH', f, len(body)) + body)
    ntxt = struct.pack('<H', len(ntxt)) + b''.join(ntxt)

    # the main menu's backgrounds
    m = menus[MAIN_MENU]
    n, = un('i', m, 0x100)
    bl = lv.ptr(m, 0x104)
    images, layers = [], []
    for k in range(n if bl else 0):
        be = lv.ptr(bl, 4 * k)
        kind, = un('i', be, 0)
        tx = lv.ptr(be, 8)
        if kind != 0 or not tx:
            log(f'   background {k}: kind {kind} (not a texture: left out)')
            continue
        raw = tx[0].data[tx[1]:tx[1] + 0x200]
        i = raw.find(b'MENU\\')
        tname = raw[i:].split(b'\0')[0].decode('latin-1') if i >= 0 else None
        if not tname:
            log(f'   background {k}: no texture name (left out)')
            continue
        if tname not in [x[0] for x in images]:
            w, h, px = speech._texture(tname.replace('\\', '/'))
            images.append((tname, w, h, bytes(px)))
        img = [x[0] for x in images].index(tname)
        b20, b21, flags = un('3B', be, 0x20)
        layers.append(struct.pack('<4B4ff16f', img, flags, b20, b21, *un('4f', be, 0xC), un('f', be, 0x1C)[0],
                                  *un('16f', be, 0x24)))
        log(f'   background {k}: {tname} flags {flags:#x}')
    imgs = struct.pack('<H', len(images))
    for name, w, h, px in images:
        nb = name.encode('latin-1')
        imgs += struct.pack('<B', len(nb)) + nb + struct.pack('<HH', w, h) + px
    layr = struct.pack('<H', len(layers)) + b''.join(layers)
    efct = struct.pack('<H', 4) + g.data[EFFECTS:EFFECTS + 4 * 0x24]

    log(f'   atlas {atlas.width} x {height}, {len(atlas.cells)} cells; {len(images)} pictures, {len(layers)} layers')
    out = [('ATLS', struct.pack('<HH', atlas.width, height) + bytes(pixels)), ('FONT', font), ('NTXT', ntxt),
           ('IMGS', imgs), ('LAYR', layr), ('EFCT', efct)]
    return out, (atlas.width, height, pixels)


def main(out, preview=None):
    if not os.path.isdir(LEVELS):
        print('== Menu: no retail levels (skip)')
        return
    print('== Menu: fonts, texts and backgrounds')
    chunks.KIND = 'menu'
    body, (w, h, px) = build()
    chunks.write(os.path.join(out, 'ttlv', 'menu.ttmn'), 'TTMN', body)
    if preview:
        from . import gf
        with open(preview, 'wb') as f:
            f.write(gf.to_png(w, h, bytes(px)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)

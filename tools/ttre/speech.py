"""What the characters say (docs/ENGINE.md "Speech"): the .ttlv's SPCH chunk and the voices it names.

A level's speech is three files next to its snapshot, all encrypted as the snapshots are:
  <level>.lsy  { char name[255], u32 address } per script reference: the name of a dialogue ("suzy\\TXTDIA47.SYN^A3dLSData")
               and where in memory the reference to it lives: the param of a node of type 21, Action_SpeakAndWaitEnd's
               argument (SNA_snasyn_0045c910 writes the object's pointer there)
  <level>.bls  u32 n, n x the dialogue object (SNA_snasyn_0045ca20)
game.dsb holds the text, by name ("NewString" records); the engine's font FontLips sits in fix.sna.

SPCH: u32 n_dialogues, u32 n_glyphs, f32 glyph spacing, u16 atlas width, u16 atlas height, u8 panel rgba[4],
      u32 has arrival, f32 arrival[3] (a hint scene's: where its own Ed stands; the real one arrives there),
      atlas rgba (width x height x 4),
      n_glyphs x { u16 code, u16 0, f32 line step, f32 advance, f32 baseline shift, f32 half width, f32 half height,
                   f32 u0, v0, u1, v1 },
      n_dialogues x { u16 frames, u16 rate, u16 voice (core sound event, 0xFFFF none), u8 flags (1 text, 2 voice), u8 n_channels,
                      u8 channels[10], u16 text bytes, u16 n_events, u32 window[4] (top, bottom, left, right: percent),
                      frames x 3 phoneme bytes, the text (the game's 8-bit codes), n_events x { u32 frame, u32 state } }
Scripts name a dialogue by its 1-based index here.
"""
import glob
import os
import re
import struct

from . import cnt, gf, sna

LEVELS = 'files/sources/GAMEDATA/WORLD/LEVELS'
GAMEDATA = 'files/sources/GAMEDATA'
SOUND = 'files/sources/GAMEDATA/WORLD/SOUND'
VOICE_SOUND = 8192  # the core's events for the voices: VOICE_SOUND + rank (voices())
FONT = b'FontLips\0'
ATLAS = 'MotorTxt/typo2DCnz.tga'
PANEL = 'MotorTxt/LipSync.tga'


def _cstr(b):
    return b.split(b'\0')[0].decode('latin1')


def read_bls(path):
    """The dialogue objects of a .bls, as SNA_snasyn_0045ca20 reads them. A frame's phonemes are 3 u32 written over each
    other 1 byte apart: their low bytes are what stays."""
    d = bytes(sna.decrypt(open(path, 'rb').read()))
    o = 0

    def take(fmt):
        nonlocal o
        v, = struct.unpack_from(fmt, d, o)
        o += struct.calcsize(fmt)
        return v

    def name():
        nonlocal o
        o += 255
        return _cstr(d[o - 255:o])

    out = []
    for _ in range(take('<i')):
        e = {'name': name(), 'frames': take('<i'), 'rate': take('<i'), 'sound': take('<i'), 'lines': [], 'window': None}
        channels = [take('<B') for _ in range(10)]
        e['channels'] = channels[:take('<B')]
        n_lines, has_lines, n_events, has_events = take('<i'), take('<i'), take('<i'), take('<i')
        e['phonemes'] = bytes(take('<I') & 0xFF for _ in range(3 * e['frames']))
        e['text'], e['voice'] = take('<B'), take('<B')
        if has_lines:
            for _ in range(n_lines):
                take('<i'), take('<i')
                e['lines'].append(name())
                name()  # the font: FontLips in every dialogue
                o += 16
            e['window'] = [take('<i') for _ in range(4)]  # +0x30.. top, bottom, left, right (GAM_Basic_00410180)
            name()  # the backdrop: MOTORTXT\LIPSYNC.TGA in every dialogue
            o += 2 + 4 + 4 + 4
        e['events'] = [(take('<i'), take('<i')) for _ in range(n_events)] if has_events else []
        out.append(e)
    if o != len(d):
        raise ValueError(f'{path}: read {o} of {len(d)} bytes')
    return out


def read_lsy(path):
    """{address of the reference: dialogue name} of a .lsy."""
    d = bytes(sna.decrypt(open(path, 'rb').read()))
    return {struct.unpack_from('<I', d, i + 255)[0]: _cstr(d[i:i + 255]) for i in range(0, len(d) - 258, 259)}


def strings(gamedata=GAMEDATA):
    """{name: text bytes} of game.dsb's NewString records: u16 length-prefixed, NUL-terminated strings."""
    if not hasattr(strings, 'cache'):
        d = bytes(sna.decrypt(open(f'{gamedata}/game.dsb', 'rb').read()))
        out, i = {}, 0
        while (i := d.find(b'NewString\0', i)) >= 0:
            i += 10
            n, = struct.unpack_from('<H', d, i)
            key = d[i + 2:i + 2 + n].split(b'\0')[0].decode('latin1')
            i += 2 + n
            n, = struct.unpack_from('<H', d, i)
            out[key] = d[i + 2:i + 2 + n].split(b'\0')[0]
            i += 2 + n
        strings.cache = out
    return strings.cache


def _banks():
    """{bank number: path}: the voices are in English/ and among the levels' own banks."""
    from .sfx import read_bank
    if not hasattr(_banks, 'cache'):
        out = {}
        for p in glob.glob(f'{SOUND}/Bnk_*.bnm') + glob.glob(f'{SOUND}/English/Bnk_*.bnm'):
            try:
                out[int(os.path.basename(p)[4:-4])] = (p, *read_bank(p))
            except (ValueError, struct.error, OSError):
                pass
        _banks.cache = out
    return _banks.cache


def voice_of(e):
    """The dialogue's voice as (bank, resource index, file name), or None. The sound value's high half is the bank and
    the resource's file name carries the dialogue's number (TXTDIA139 = A_G0139.apm): in 96 of the 246 dialogues the low
    half no longer matches the bank's order, so the number decides; else the event whose target is the value."""
    bank = e['sound'] >> 16
    entry = _banks().get(bank)
    if not entry or not e['voice']:
        return None
    _, events, resources = entry
    digits = re.sub(r'\D', '', e['lines'][0]) if e['lines'] else ''
    for index, _, _, file in resources.values():
        m = re.match(r'A_\w\w(\d+)', file, re.I)
        if m and digits and int(m[1]) == int(digits):
            return bank, index, file
    for eid, _, target in events:
        if target == e['sound'] and eid in resources:
            return bank, resources[eid][0], resources[eid][3]
    return None


def voices(levels_dir=LEVELS):
    """{(bank, resource index): (the core's sound event, file name)}: every voice a level's dialogue names, by rank."""
    if not hasattr(voices, 'cache'):
        found = {}
        for path in glob.glob(f'{levels_dir}/*/*.bls'):
            for e in read_bls(path):
                v = voice_of(e)
                if v:
                    found[v[:2]] = v[2]
        voices.cache = {k: (VOICE_SOUND + i, found[k]) for i, k in enumerate(sorted(found))}
    return voices.cache


def _texture(name):
    """(width, height, rgba) of a texture in Textures.cnt."""
    c = cnt.Cnt(f'{GAMEDATA}/Textures.cnt')
    want = os.path.splitext(name.lower())[0]
    for e in c.entries:
        if os.path.splitext(e.path.replace('\\', '/').lower())[0] == want:
            return gf.decode(c.read(e))
    raise KeyError(name)


def font(lv):
    """FontLips in fix.sna: (spacing, glyphs). The record starts 0x2C before its name: +0x14 flags, +0x18 n, +0x1C the list of
    {u16 code, f32 baseline shift, ptr glyph}, +0x20 the spacing; a glyph is {ptr quad, f32 line step, f32 advance}."""
    from . import level
    i = lv.fix.data.find(FONT)
    rec = (lv.fix, i - 0x2C)
    n, = struct.unpack_from('<I', rec[0].data, rec[1] + 0x18)
    spacing, = struct.unpack_from('<f', rec[0].data, rec[1] + 0x20)
    table = lv.ptr(rec, 0x1C)
    data = level.LevelData(lv)
    glyphs = []
    for k in range(n):
        e = lv.ptr(table, 4 * k)
        code, = struct.unpack_from('<H', e[0].data, e[1])
        shift, = struct.unpack_from('<f', e[0].data, e[1] + 4)
        g = lv.ptr(e, 8)
        step, advance = struct.unpack_from('<2f', g[0].data, g[1] + 4)
        quad, hw, hh, uv = lv.ptr(g, 0), 0.0, 0.0, (0.0, 0.0, 0.0, 0.0)
        if quad:
            geo = data.geometric(quad)
            hw = max(abs(v[0]) for v in geo.vertices)
            hh = max(abs(v[2]) for v in geo.vertices)
            us, vs = [t[0] for t in geo.elements[0].uvs], [t[1] for t in geo.elements[0].uvs]
            uv = (min(us), min(vs), max(us), max(vs))
        glyphs.append((code, step, advance, shift, hw, hh, *uv))
    return spacing, glyphs


class Table:
    """A level's dialogues in .bls order and the script references to them."""

    def __init__(self, lv, levels_dir=LEVELS):
        self.lv = lv
        base = f'{levels_dir}/{lv.name}/{lv.name}'
        self.dialogues = read_bls(base + '.bls') if os.path.exists(base + '.bls') else []
        index = {e['name']: k + 1 for k, e in enumerate(self.dialogues)}
        self.refs = {a: index.get(n, 0) for a, n in read_lsy(base + '.lsy').items()} if self.dialogues else {}

    @property
    def arrival(self):
        """Where a hint scene's own Ed stands (docs/ENGINE.md "Walkable scenes"), None elsewhere."""
        from . import cast, level
        data = level.LevelData(self.lv)
        for p in data.persos():
            if data.perso_names(p.loc)[1] == cast.SCENE_ED:
                return p.world[3], p.world[7], p.world[11]
        return None

    def at(self, loc):
        """The 1-based dialogue of the type 21 node at loc, 0 none."""
        from .sfx import mem_address
        return self.refs.get(mem_address(loc), 0) if loc else 0

    def chunk(self):
        """The SPCH chunk, or None when the level has no dialogue."""
        if not self.dialogues:
            return None
        spacing, glyphs = font(self.lv)
        w, h, rgba = _texture(ATLAS)
        _, _, panel = _texture(PANEL)
        out = [struct.pack('<IIfHH4BI3f', len(self.dialogues), len(glyphs), spacing, w, h, *panel[:4],
                           1 if self.arrival else 0, *(self.arrival or (0, 0, 0))), bytes(rgba)]
        for g in glyphs:
            out.append(struct.pack('<HH9f', g[0], 0, *g[1:]))
        table, texts = voices(), strings()
        for e in self.dialogues:
            v = voice_of(e)
            voice = table[v[:2]][0] if v and v[:2] in table else 0xFFFF
            text = texts.get(e['lines'][0].upper(), b'') if e['lines'] and e['text'] else b''
            ch = (e['channels'] + [0] * 10)[:10]
            out.append(struct.pack('<HHHBB10BHH4I', e['frames'], e['rate'], voice,
                                   (1 if text else 0) | (2 if voice != 0xFFFF else 0), len(e['channels']), *ch, len(text),
                                   len(e['events']), *(e['window'] or (75, 95, 5, 95))))
            out.append(e['phonemes'])
            out.append(text)
            for f, s in e['events']:
                out.append(struct.pack('<II', f, s))
        return b''.join(out)

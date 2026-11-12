"""Tonic Trouble: Special Edition (the 1998 OEM bundle disc, build 8.5.2): its levels for Open Tonic (docs/SE.md).

An earlier build of the game with different levels under the retail names (carota, cavedoc, cock01, land, lecanyon,
marmite, north, outro, pyramide, sud0, totalski). Its data are the retail formats with small differences (sna.py,
gf.py, level.SeAnim); its AI numbers its primitives differently and its mechanics differ, so for now the levels are
played with the retail Ed: his record (EDPL) and .ttai come from the retail level of the same name (the donor; the
skier on Totalski), placed at the SE level's own start (its GPT perso-in-fix Ed matrix). The donor .ttai's
level-bound chunks (portals, proximity triggers, places, portal zones: retail coordinates) are emptied; its floor
sounds (FSND) don't match the SE collision, so the core drops them (LINK). The SE level persos are props (PRSO, their
initial animation), none runs its AI.

The disc: the raw image (Tt.bin, MODE1/2352, any folder under files/sources) or an .iso; its files are plain on it (no
tt1-4 archives as on the retail disc). extract() copies GAMEDATA and the game exe (MAID3DVR.EXE) to files/sources/se.

Usage (from the repo root; tools/convert_files.sh runs these when it finds the disc image):
  python3 -m tools.ttre se-extract [DISC]          (default: the first SE disc image found under files/sources)
  python3 -m tools.ttre se-levels                  (the SE level names, then the output name of each: LEVEL<TAB>NAME)
  python3 -m tools.ttre se-vignettes DIR           (DIR = files/out/se/Vignette/Level: pictures under the level names)
  python3 -m tools.ttre se-ttlv LEVEL OUT.ttlv     (+ OUT.ttai from the donor; textures from files/out/se/Textures)
"""
import glob
import os
import shutil
import struct

from . import chunks
from .sna import find_file

SOURCES = 'files/sources'
ROOT = 'files/sources/se'                   # the extracted disc
LEVELS = ROOT + '/GAMEDATA/WORLD/LEVELS'
OUT = 'files/out/se'                        # SE outputs: Textures, Vignette, ttlv (next to each other, as retail's)
RETAIL_LEVELS = 'files/sources/GAMEDATA/WORLD/LEVELS'
RETAIL_TTLV = 'files/out/ttlv'
KEEP = ('GAMEDATA/', 'MAID3DVR.EXE')        # what extract() copies


class Disc:
    """An ISO 9660 image, raw (2352-byte MODE1 sectors, user data at +16) or cooked (2048)."""

    def __init__(self, path):
        self.f = open(path, 'rb')
        head = self.f.read(16)
        size = os.path.getsize(path)
        self.raw = head[:12] == b'\0' + b'\xff' * 10 + b'\0' and size % 2352 == 0
        if self.sector(16)[1:6] != b'CD001':
            raise ValueError(f'{path}: not an ISO 9660 image')

    def sector(self, n, count=1):
        if not self.raw:
            self.f.seek(n * 2048)
            return self.f.read(2048 * count)
        out = bytearray()
        for k in range(count):
            self.f.seek((n + k) * 2352 + 16)
            out += self.f.read(2048)
        return bytes(out)

    def files(self):
        """[(path without leading slash, first sector, size)] of every file."""
        root = self.sector(16)[156:156 + 34]
        out = []

        def walk(lba, size, path, depth=0):
            data, i = self.sector(lba, (size + 2047) // 2048), 0
            while i < size:
                n = data[i]
                if n == 0:  # records don't cross sectors: the rest of this one is padding
                    i = (i // 2048 + 1) * 2048
                    continue
                rec, i = data[i:i + n], i + n
                lba2, size2 = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
                name = rec[33:33 + rec[32]]
                if name in (b'\0', b'\1'):
                    continue
                p = path + name.decode('latin-1').split(';')[0]
                if rec[25] & 2:
                    if depth < 16:
                        walk(lba2, size2, p + '/', depth + 1)
                else:
                    out.append((p, lba2, size2))
        walk(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0], '')
        return out

    def read(self, lba, size):
        return self.sector(lba, (size + 2047) // 2048)[:size]


def is_se_disc(path):
    """True for a Special Edition disc image: GAMEDATA/GAME.DSC (the text level script; retail: game.dsb) and levels."""
    try:
        names = {p.upper() for p, _, _ in Disc(path).files()}
    except (OSError, ValueError, struct.error, IndexError):
        return False
    return 'GAMEDATA/GAME.DSC' in names and 'GAMEDATA/WORLD/LEVELS/FIX.SNA' in names


def find_disc(sources=SOURCES):
    """The first SE disc image (.bin / .iso / .img) under files/sources, two folders deep at most, or None."""
    for pattern in ('*', '*/*', '*/*/*'):
        for p in sorted(glob.glob(os.path.join(sources, pattern))):
            if p.lower().endswith(('.bin', '.iso', '.img')) and os.path.getsize(p) > 100 << 20 and is_se_disc(p):
                return p
    return None


def extract(disc, out=ROOT):
    """Copy the disc's GAMEDATA and exe to `out` (via out.partial, swapped in when complete). Returns the file count."""
    d = Disc(disc)
    tmp = out + '.partial'
    shutil.rmtree(tmp, ignore_errors=True)
    n = 0
    for path, lba, size in d.files():
        if not path.upper().startswith(KEEP):
            continue
        dst = os.path.join(tmp, *path.split('/'))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'wb') as f:
            f.write(d.read(lba, size))
        n += 1
    shutil.rmtree(out, ignore_errors=True)
    os.replace(tmp, out)
    return n


def levels(levels_dir=LEVELS):
    """The SE level folders (names as on the disc, upper case)."""
    return sorted(e for e in os.listdir(levels_dir) if os.path.isdir(os.path.join(levels_dir, e))) \
        if os.path.isdir(levels_dir) else []


def donor(level, retail_levels=RETAIL_LEVELS):
    """The retail level of the same name (its folder's spelling as on disk, e.g. 'Totalski'), or None. Listed, not
    find_file: on a case-insensitive disk that returns the spelling asked for."""
    d = find_file(retail_levels)
    names = os.listdir(d) if os.path.isdir(d) else []
    return next((n for n in names if n.lower() == level.lower() and os.path.isdir(os.path.join(d, n))), None)


# The retail levels' folder spellings, for the SE levels' file names when the retail game is not there to ask.
RETAIL_SPELLING = {n.lower(): n for n in ('bar', 'BigBoss', 'carota', 'cavedoc', 'cock01', 'death', 'Land', 'LeCanyon',
                                          'mammyon', 'marmite', 'north', 'outro', 'pharmmap', 'pyr_fou', 'pyramide',
                                          'skymap', 'SpyCarota', 'spynorth', 'sud0', 'Totalski', 'Transit')}


def output_name(level):
    """The file name of an SE level's outputs: its donor's spelling (Totalski, LeCanyon, cock01...), else the retail
    spelling of that name (RETAIL_SPELLING), else its own in lower case. The core and the frontends key some things on
    the name (the ski start, Ed's level-name checks, the music table), which then work as on the retail level."""
    return donor(level) or RETAIL_SPELLING.get(level.lower(), level.lower())


def name_vignettes(level_dir, names, fallback_dir=None):
    """Copy the SE loading screens (Vignette.cnt Level/*.png: carota, cock, ski, sud, ...) to <name>.png for each level
    name (output_name) that has one: the same name, the name starting with it (cock -> cock01, sud -> sud0), or
    containing it (ski -> Totalski). Workaround: a level the disc has no picture for (outro: its Vignette.cnt holds
    none, and the exe picks them by level name, not the scripts) gets the retail one of that name from fallback_dir
    (files/out/Vignette/Level: retail Outro.png, Ed with the trophy). Returns {name: picture}."""
    pics = {os.path.splitext(f)[0].strip().lower(): f for f in sorted(os.listdir(level_dir)) if f.lower().endswith('.png')}
    retail = {os.path.splitext(f)[0].lower(): os.path.join(fallback_dir, f) for f in os.listdir(fallback_dir)
              if f.lower().endswith('.png')} if fallback_dir and os.path.isdir(fallback_dir) else {}
    out = {}
    for name in names:
        if name.lower() not in pics and name.lower() in retail and not any(
                k and (name.lower().startswith(k) or (len(k) >= 3 and k in name.lower())) for k in pics):
            shutil.copyfile(retail[name.lower()], os.path.join(level_dir, f'{name}.png'))
            out[name] = 'retail ' + os.path.basename(retail[name.lower()])
            continue
        n = name.lower()
        pick = pics.get(n) or next((f for k, f in pics.items() if k and n.startswith(k)), None) \
            or next((f for k, f in pics.items() if k and len(k) >= 3 and k in n), None)
        if pick and pick.lower() != f'{name}.png'.lower():  # (same name in another case: the same file on macOS)
            shutil.copyfile(os.path.join(level_dir, pick), os.path.join(level_dir, f'{name}.png'))
        if pick:
            out[name] = pick
    return out


def donor_cast(level):
    """(skier, CAST chunk or None) of the donor. skier: its player is the skier (retail Totalski / skymap), not the fix
    Ed. The CAST is a minimal one (cast.py's layout, no cast persos, no ghosts): main actor and main alias in the
    donor's perso numbering (its .ttai's perso nodes use it). The core keys the skier's things on that alias: the ski
    camera (tc_viewer: riding with main_alias != 0) and the fix Ed's counters read as the skier's (tc_cast)."""
    from . import level as lvl, sna
    name = donor(level)
    if not name:
        return False, None
    d = lvl.LevelData(sna.Level(RETAIL_LEVELS, name))
    fix_ed, player = (d.perso_in_fix[0][0] if d.perso_in_fix else None), d.player()
    if not player or player == fix_ed:
        return False, None
    ids = {p.loc: k + 1 for k, p in enumerate(d.persos())}
    return True, struct.pack('<4H', ids.get(player, 0), 0, 0, ids.get(fix_ed, 0)) + struct.pack('<4H', 0, 0, 0, 0)


# Workaround (port choice): SE ski snow is {1, 0, 0.001, 1, 1} (grip 0.001, rebound 0); retail's {1, 0.8, 0, 1, 1}. The
# core switches a level to sliding when a floor has grip <= 0 (tc_player_apply_floors) and the skier's physics was
# tuned on retail snow, so with the skier as the donor, floors under SNOW_GRIP get retail snow's values.
SNOW_GRIP = 0.01
RETAIL_SNOW = (0.0, 0.8)  # (grip, rebound), level.LevelData.floor_material
# Without the skier no SE floor may read as snow: grip <= 0 would put a whole walking level on the sliding profile.
MIN_GRIP = 0.001


def level_data(level, levels_dir=LEVELS, skier=False):
    """LevelData for an SE level with its floors adjusted for the retail Ed (SNOW_GRIP, MIN_GRIP)."""
    from . import level as lvl, sna

    class SeLevelData(lvl.LevelData):
        def floor_material(self, gm):
            f = super().floor_material(gm)
            if f is None:
                return f
            if skier and f[0] < SNOW_GRIP:
                return RETAIL_SNOW
            return (max(f[0], MIN_GRIP), f[1])
    return SeLevelData(sna.Level(levels_dir, level))


# ---- the donor's Ed in the .ttlv ----

def read_donor(path):
    """(texture paths, absolute; start; EDPL parts [(object, surface fields, geometry bytes)]; the EDPL bytes after
    the parts (animations); LITE records of the EDPL parts) of a retail .ttlv, or None without a player."""
    from .edpl import read_ed
    ed = read_ed(path)
    if ed is None:
        return None
    textures, start, parts, anims = ed
    base = os.path.dirname(os.path.abspath(path))
    textures = [os.path.normpath(os.path.join(base, t)) for t in textures]
    lite = chunks.read(path, 'TTLV')['LITE']
    n, = struct.unpack_from('<H', lite)
    o = 4 + 52 * n
    n, = struct.unpack_from('<H', lite, o)
    o += 4
    for _ in range(n):
        nl, ne = struct.unpack_from('<2H', lite, o)
        o += 4 + 2 * nl + 24 * ne
    n, = struct.unpack_from('<I', lite, o)
    o += 4
    recs = []
    for _ in range(n):
        runs, = struct.unpack_from('<I', lite, o + 24)
        recs.append(lite[o:o + 24] + struct.pack('<I', 0))  # no centres: the SE has no per-object light test (ttlv.lite)
        o += 28 + 20 * runs
    return textures, start, parts, anims, recs[len(recs) - len(parts):]


# ---- the donor's .ttai ----

EMPTY = {'TRIG': struct.pack('<HH', 0, 0), 'PORT': struct.pack('<HHH', 0, 0, 0), 'PLAC': struct.pack('<HH', 0, 0),
         'ZONE': struct.pack('<HH', 0, 0)}


def write_ttai(src, dst, extra=(), replace=()):
    """The donor's .ttai for an SE level: level-bound chunks emptied (EMPTY) or replaced (`replace` [(tag, bytes)]: the
    SE level's own, portal_chunks), the transition textures' paths re-rooted from the donor's folder to dst's, `extra`
    [(tag, bytes)] appended. Chunk order kept."""
    replace = dict(replace)
    raw = open(src, 'rb').read()
    order, o = [], 8
    while o + 8 <= len(raw):
        tag, size = raw[o:o + 4].decode('latin-1'), struct.unpack_from('<I', raw, o + 4)[0]
        order.append((tag, raw[o + 8:o + 8 + size]))
        o += 8 + size + (-size % 4)
    out = []
    for tag, body in order:
        if tag == 'STMP':
            continue
        if tag in replace:
            body = replace[tag]
        elif tag in EMPTY:
            body = EMPTY[tag]
        elif tag == 'TRAN':
            body = retarget_tran(body, os.path.dirname(os.path.abspath(src)), os.path.dirname(os.path.abspath(dst)))
        out.append((tag, body))
    chunks.write(dst, 'TTAI', out + list(extra))


def retarget_tran(body, src_dir, dst_dir):
    """TRAN (ai.py) with its texture paths made relative to dst_dir instead of src_dir."""
    out, o = bytearray(body[:8]), 8
    for _ in range(4):
        n, = struct.unpack_from('<H', body, o)
        out += body[o:o + 40]
        o += 40
        for _ in range(n):
            k, = struct.unpack_from('<H', body, o)
            p = body[o + 2:o + 2 + k].decode()
            o += 2 + k
            if p:
                p = os.path.relpath(os.path.normpath(os.path.join(src_dir, p)), dst_dir).replace(os.sep, '/')
            b = p.encode()
            out += struct.pack('<H', len(b)) + b
    return bytes(out + body[o:])


# ---- the SE's walk / ski switching (docs/SE.md "Sliding in the Special Edition") ----

SE_SKI = 52                     # SE Ed's ski behaviour (with reflex R24); his walking ones: SE_WALK
SE_WALK = (0, 3)
SE_MODE_DSG = 7                 # SE Ed's mode dsgvar: 11 skiing, < 3 walking (the triggers' guards)
WALK_DONOR = 'Land'             # the retail level whose Ed walks an SE level that switches (its .ttai: his AI)
MODE_WALK, MODE_SKI = 0, 1
TELEPORT_NONE, TELEPORT_TO, TELEPORT_BY = 0, 1, 2


def mode_zones(data):
    """The SE level's walk / ski switches: [dict(mode, require, once, teleport, vector, inverse, prims, name)]. A level
    perso that, with SE Ed in its ZDD zone k (InZone(Me, k, Ed)), forces SE Ed's ski behaviour (SE_SKI) or a walking
    one (SE_WALK) becomes a zone that switches the port's player between retail walking Ed and the retail skier:
    require = the mode its guard on Ed's dsgvar 7 asks for (Less 3: walking, Equal 11: skiing, none: any); once = its
    behaviour then moves on (ChangeBehaviour: the SE triggers that fire a single time); teleport = an
    ActivateObjectOnPosition of Ed in the same branch, to another perso's position (TELEPORT_TO) or by a vector from
    his own (TELEPORT_BY)."""
    from . import ai, cast
    lv = data.lv
    ed = data.perso_in_fix[0][0] if data.perso_in_fix else None
    if not ed:
        return []
    ed_ai = ai.AI(data, ed)
    ski = {loc for loc, nm in ed_ai.beh_names.items() if nm == f'B{SE_SKI}'}
    walk = {loc for loc, nm in ed_ai.beh_names.items() if nm in {f'B{k}' for k in SE_WALK}}
    persos = {p.loc: p for p in data.persos()}
    out = []
    for p in data.persos():
        if p.loc == ed or not lv.ptr(p.loc, 0xC):
            continue
        try:
            a = ai.AI(data, p.loc)
        except Exception:
            continue
        found = []

        def visit(nodes, guards):
            skip = set()
            for i, n in enumerate(nodes):
                if i in skip:
                    continue
                if n.type == 0 and n.param == 0 and i + 1 < len(nodes) and nodes[i + 1].type == 0 and nodes[i + 1].param == 1:
                    visit(nodes[i + 1].children, guards + n.children[:1])  # If cond / Then body (Else: not followed)
                    skip.add(i + 1)
                    continue
                if n.type == 4 and n.param == 237 and len(n.children) >= 2 and n.children[0].ref == ed:
                    target = n.children[1].ref
                    mode = MODE_SKI if target in ski else MODE_WALK if target in walk else None
                    if mode is not None:
                        found.append((mode, guards, nodes))
                visit(n.children, guards)
        for b in a.behaviours:
            for sc in b.scripts + ([b.schedule] if b.schedule else []):
                visit(sc, [])
        for mode, guards, branch in found:
            zone, require = None, 0
            for g in guards:
                c = g.children
                if g.type == 1 and g.param == 13 and len(c) == 3 and c[1].type == 12 and c[2].ref == ed:
                    zone = c[1].param
                if g.type == 1 and g.param in (3, 5) and len(c) == 2 and c[0].type == 2 and c[0].param == 12:
                    dot = c[0].children
                    if len(dot) == 2 and dot[0].ref == ed and dot[1].type == 11 and dot[1].param == SE_MODE_DSG:
                        require = MODE_WALK + 1 if g.param == 5 else MODE_SKI + 1
            prims = dict(cast.zones(data, p.loc, a)).get(zone) if zone else None
            if not prims:
                continue
            teleport, vector = TELEPORT_NONE, (0.0, 0.0, 0.0)
            for n in branch:
                c = n.children
                if n.type == 4 and n.param == 27 and len(c) == 2 and c[0].ref == ed:  # Proc_ActivateObjectOnPosition
                    v = c[1]
                    if v.type == 2 and v.param == 24 and len(v.children) == 2 and v.children[0].ref in persos:
                        w = persos[v.children[0].ref].world  # InPersoContext(P, GetPosition): P's position
                        teleport, vector = TELEPORT_TO, (w[3], w[7], w[11])
                    elif v.type == 2 and v.param == 16 and v.children and v.children[0].type == 16:  # VecAdd(VECTOR, ..)
                        comps = [struct.unpack('<f', struct.pack('<I', c.param & 0xFFFFFFFF))[0]
                                 for c in v.children[0].children if c.type == 13]  # its components: REAL nodes
                        if len(comps) == 3:
                            teleport, vector = TELEPORT_BY, tuple(comps)
            once = any(n.type == 4 and n.param == 239 for n in branch)  # ChangeBehaviour: the trigger moves on
            out.append(dict(mode=mode, require=require, once=once, teleport=teleport, vector=vector,
                            inverse=ai.mat_inverse(p.world), prims=prims, name=data.perso_names(p.loc)[2]))
    return out


def modz_chunk(zones):
    """MODZ (.ttai, core tc_ai.h tc_ai_mode_zone): u16 n, u16 0, n x { u8 mode (0 walk: the main model, 1 ski: ALTM),
    u8 require (0 any, 1 walking, 2 skiing), u8 once, u8 teleport (0 none, 1 to vector, 2 by vector), f32 vector[3],
    f32 inverse[12] (world -> the zone's axes, row-major 3x4), u16 n_prims, u16 0, n_prims x { u32 kind (7 sphere: a
    centre, b.x radius; 8 box: a min, b max), f32 a[3], f32 b[3] } }."""
    out = struct.pack('<HH', len(zones), 0)
    for z in zones:
        out += struct.pack('<4B3f12fHH', z['mode'], z['require'], int(z['once']), z['teleport'], *z['vector'],
                           *z['inverse'], len(z['prims']), 0)
        out += b''.join(struct.pack('<I3f3f', k, *a, *b) for k, a, b in z['prims'])
    return out


# ---- portals: the SE's, played the retail way (docs/SE.md "Portals") ----

def start_heading_deg(level, levels_dir=LEVELS):
    """The SE level's start facing as PROC_242 takes it (its perso rotation about Z = deg x pi / 180 - pi / 2): from
    its GPT perso-in-fix Ed matrix, without walking the level."""
    from . import sna
    lv = sna.Level(levels_dir, level)
    g = lv.gpt
    n, = struct.unpack_from('<I', g.data, 0)
    if not n:
        return 0
    f = struct.unpack_from('<I21f', g.data, 8)  # the Matrix (as LevelData.matrix: the second 3x3, stored as columns)
    s3 = f[13:22]
    return heading_deg((s3[0], s3[3], s3[6], 0, s3[1], s3[4], s3[7], 0, s3[2], s3[5], s3[8], 0))


def heading_deg(m):
    """A 3x4 world matrix's rotation about Z as PROC_242's degrees (0..360)."""
    import math
    return int(round(math.degrees(math.atan2(m[4], m[0]) + math.pi / 2))) % 360


# Port choice (user, 2026-11-05: "make him stop a bit in front, like he does with the Retail portals"): the SE arches'
# own zone (every arch: the gen_arche box, x +-1.25 across the opening, y +-1.2 through it) catches Ed already in the
# opening, so the retail walk-in (B68: to 4.5 in front of the arch, ChangeStateByDirection(dsg99, (0, -4.5, 0))) walked
# him back out first. Retail's hub arches capture within radius 4 (PORT, no zone) instead. The SE box keeps its width
# and height and reaches SE_ARCH_REACH along the arch's y on both sides: Ed is caught about that far out, walks on to
# 4.5 in front, stops and steps in.
SE_ARCH_REACH = 5.0


def arch_capture(prims, reach=SE_ARCH_REACH):
    """An SE arch's zone primitives (in its axes) deepened along y to +-reach: boxes (kind 8) only."""
    return [(k, (a[0], min(a[1], -reach), a[2]), (b[0], max(b[1], reach), b[2])) if k == 8 else (k, a, b)
            for k, a, b in prims]


def se_portals(data, levels_dir=LEVELS):
    """The SE level's portals as retail-style records: [(position, rows, perso id, zone prims, inverse, capture ops,
    fire ops, instance)], names in `names` (level and place names the ops index). An SE portal is a level perso that,
    with SE Ed in its ZDD zone (InZone(Me, k, Ed)), changes level: ChangeLevel(name) (retail PROC 7; sud0's hub arches
    pick it by their own dsg0) or ChangeMapAtPosition(name, place, camera place) (SE Proc_ChangeMapAtPosition, retail
    PROC 29). The SE does that at once. Port choice: they capture Ed the retail arches' way instead (ai.portals:
    dsg134 == 0, dsg99 = the arch, dsg100 = 0, dsg136 = the arrival heading, his walk-in B66 + R4: he walks up, steps
    in (state 228), the cover runs), then fire on Ed.dsg103 == 1 with the SE's destination. The first-time story
    branch of sud0's arches (World's dsg4) is left out."""
    from . import ai, cast
    lv = data.lv
    ed = data.perso_in_fix[0][0] if data.perso_in_fix else None
    names, out, heading_cache = {}, [], {}

    def sid(x):
        return names.setdefault(x.lower(), len(names))

    def cstr(ref):
        return ref[0].data[ref[1]:ref[1] + 256].split(b'\0')[0].decode('latin-1')

    def arrival_heading(dest, place):
        key = (dest.lower(), (place or '').lower())
        if key not in heading_cache:
            level_dir = next((e for e in levels(levels_dir) if e.lower() == dest.lower()), None)
            h = 0
            if level_dir and place:
                d2 = level_data(level_dir, levels_dir)
                q = next((x for x in d2.persos() if d2.perso_names(x.loc)[2].lower() == place.lower()), None)
                h = heading_deg(q.world) if q else start_heading_deg(level_dir, levels_dir)
            elif level_dir:
                h = start_heading_deg(level_dir, levels_dir)
            heading_cache[key] = h
        return heading_cache[key]

    for k, p in enumerate(data.persos()):
        if p.loc == ed or not lv.ptr(p.loc, 0xC):
            continue
        try:
            a = ai.AI(data, p.loc)
        except Exception:
            continue
        zone_k, actions = None, []

        def own_true(cond):
            """Equal(dsgN, v) on its own dsgvars: True / False; anything else: None."""
            c = cond.children
            if cond.type == 1 and cond.param == 3 and len(c) == 2 and c[0].type in (10, 11) and c[1].type == 12:
                v = ai.perso_dsg_value(data, p.loc, c[0].param)
                return v is not None and int(v) == struct.unpack('<i', struct.pack('<I', c[1].param))[0]
            return None

        def visit(nodes, ok):
            nonlocal zone_k
            skip = set()
            for i, n in enumerate(nodes):
                if i in skip:
                    continue
                c = n.children
                if n.type == 1 and n.param == 13 and len(c) == 3 and c[1].type == 12 and c[2].ref == ed:
                    zone_k = zone_k or c[1].param
                if n.type == 0 and n.param == 0 and i + 1 < len(nodes) and nodes[i + 1].type == 0 and nodes[i + 1].param == 1:
                    visit(c, ok)
                    t = own_true(c[0]) if c else None
                    visit(nodes[i + 1].children, ok and t is not False)
                    skip.add(i + 1)
                    continue
                if ok and n.type == 4 and n.param == 7 and c and c[0].type == 20 and c[0].ref:
                    actions.append((cstr(c[0].ref), None, None))
                elif ok and n.type == 4 and n.param == 29 and len(c) >= 3 and all(x.type == 20 and x.ref for x in c[:3]):
                    actions.append((cstr(c[0].ref), cstr(c[1].ref), cstr(c[2].ref)))
                visit(c, ok)
        for b in a.behaviours:
            for sc in b.scripts + ([b.schedule] if b.schedule else []):
                visit(sc, True)
        prims = dict(cast.zones(data, p.loc, a)).get(zone_k) if zone_k else None
        if not prims or len({x[0].lower() for x in actions}) != 1:
            continue  # not a portal, or its destination is not decided by its own dsgvars
        prims = arch_capture(prims)
        dest, place, cam = actions[0]
        if dest.lower() == lv.name.lower():
            continue  # a reload of this level (sud0's Doc barge), not a way out
        heading = arrival_heading(dest, place)
        cap = [(ai.POP_GUARD, 0, 134, 0, 0, (0, 0, 0)), (ai.POP_SET, 2, 99, 0, 0, (0, 0, 0)),
               (ai.POP_SET, 0, 100, 0, 0, (0, 0, 0)), (ai.POP_SET, 0, 136, heading, 0, (0, 0, 0)),
               (ai.POP_FORCE, 0, 66, 0, 0, (0, 0, 0)), (ai.POP_FORCE_REFLEX, 0, 4, 0, 0, (0, 0, 0))]
        fire = [(ai.POP_SET, 0, 103, 0, 0, (0, 0, 0))]
        if place:
            fire.append((ai.POP_LEVEL, 0, sid(cam), sid(dest), sid(place), (0, 0, 0)))
        else:
            fire.append((ai.POP_LEVEL, 0, 0xFFFF, sid(dest), 0xFFFFFFFF, (0, 0, 0)))
        w = p.world
        out.append(((w[3], w[7], w[11]), (w[0:3], w[4:7], w[8:11]), 0x8000 + k, prims, ai.mat_inverse(w), cap, fire,
                    data.perso_names(p.loc)[2], data.perso_sector(p.loc)))
    return out, names


def portal_chunks(data, portals, names):
    """PORT, ZONE and PLAC (ai.py's layouts) for se_portals' records and the SE level's own places."""
    from . import ai
    port = struct.pack('<HH', len(portals), 0)
    zone = struct.pack('<HH', len(portals), 0)
    for pos, rows, perso, prims, inv, cap, fire, _, sector in portals:
        port += struct.pack('<4f9fHBBHHiHH', *pos, 4.0, *[x for r in rows for x in r], perso, 0, 1, 103, 0, 1,
                            0xFFFF if sector is None else sector, 0)
        port += struct.pack('<HH', len(cap), len(fire))
        for op, kind, a, b, c, v in cap + fire:
            port += struct.pack('<BBHII3f', op, kind, a, b & 0xFFFFFFFF, c & 0xFFFFFFFF, *v)
        zone += struct.pack('<HH12f', len(prims), 0, *inv) + b''.join(struct.pack('<I3f3f', k, *x, *y) for k, x, y in prims)
    port += struct.pack('<H', len(names)) + b''.join(struct.pack('<H', len(n.encode())) + n.encode() for n in names)
    rows = ai.places(data)
    plac = struct.pack('<HH', len(rows), 0) + b''.join(
        struct.pack('<H', len(n.encode('latin-1'))) + n.encode('latin-1') + struct.pack('<4f', *pos, h) for n, pos, h in rows)
    return [('PORT', port), ('ZONE', zone), ('PLAC', plac)]


def export_level(level, out_ttlv, tex_root=OUT + '/Textures', levels_dir=LEVELS, retail_ttlv=RETAIL_TTLV):
    """Write OUT.ttlv (the SE level, the donor's Ed) and OUT.ttai (the donor's, emptied of level-bound chunks).
    Returns (ttlv.write's summary, donor name or None, the .ttai written or None)."""
    from . import ttlv
    name = donor(level)
    src = find_file(os.path.join(retail_ttlv, f'{name}.ttlv')) if name else None
    if src and not os.path.exists(src):
        src = None
    skier, cast = donor_cast(level) if src else (False, None)
    data = level_data(level, levels_dir, skier)
    r = ttlv.write(data, out_ttlv, tex_root=tex_root, ed_donor=src, skip_decals=skier, cast_body=cast)
    ai_out, ttai_path = None, os.path.splitext(out_ttlv)[0] + '.ttai'
    ttai = os.path.splitext(src)[0] + '.ttai' if src else None
    walk_src = find_file(os.path.join(retail_ttlv, f'{WALK_DONOR}.ttai'))
    zones = mode_zones(data) if skier else []
    gates, gate_names = se_portals(data, levels_dir)
    level_chunks = portal_chunks(data, gates, gate_names)
    if ttai and os.path.exists(ttai) and zones and os.path.exists(walk_src):
        # The SE switches between walking and skiing within the level: the main model walks (retail Ed), the skier
        # rides along as ALTM (a whole .ttai container), MODZ says where each takes over.
        ai_out = ttai_path
        write_ttai(ttai, ai_out + '.ski')
        alt = open(ai_out + '.ski', 'rb').read()
        os.remove(ai_out + '.ski')
        write_ttai(walk_src, ai_out, [('MODZ', modz_chunk(zones)), ('ALTM', alt)], level_chunks)
    elif ttai and os.path.exists(ttai):
        ai_out = ttai_path
        write_ttai(ttai, ai_out, (), level_chunks)
    elif os.path.exists(ttai_path):
        os.remove(ttai_path)  # an older one would not match this .ttlv
    return r, name if src else None, ai_out

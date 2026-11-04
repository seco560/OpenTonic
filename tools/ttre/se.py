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


def output_name(level):
    """The file name of an SE level's outputs: its donor's spelling (Totalski, LeCanyon, cock01...), else its own. The
    core and the frontends key some things on the name (the ski start, Ed's level-name checks, the music table), which
    then work as on the retail level."""
    return donor(level) or level


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
    the parts (animations); LITE records of the EDPL parts, raw) of a retail .ttlv, or None without a player."""
    from .import_gltf import read_ed
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
        recs.append(lite[o:o + 28 + 20 * runs])
        o += 28 + 20 * runs
    return textures, start, parts, anims, recs[len(recs) - len(parts):]


# ---- the donor's .ttai ----

EMPTY = {'TRIG': struct.pack('<HH', 0, 0), 'PORT': struct.pack('<HHH', 0, 0, 0), 'PLAC': struct.pack('<HH', 0, 0),
         'ZONE': struct.pack('<HH', 0, 0)}


def write_ttai(src, dst):
    """The donor's .ttai for an SE level: level-bound chunks emptied (EMPTY), the transition textures' paths re-rooted
    from the donor's folder to dst's. Chunk order kept."""
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
        if tag in EMPTY:
            body = EMPTY[tag]
        elif tag == 'TRAN':
            body = retarget_tran(body, os.path.dirname(os.path.abspath(src)), os.path.dirname(os.path.abspath(dst)))
        out.append((tag, body))
    chunks.write(dst, 'TTAI', out)


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
    if ttai and os.path.exists(ttai):
        ai_out = ttai_path
        write_ttai(ttai, ai_out)
    elif os.path.exists(ttai_path):
        os.remove(ttai_path)  # an older one would not match this .ttlv
    return r, name if src else None, ai_out

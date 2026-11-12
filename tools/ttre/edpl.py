"""Ed's record (the .ttlv EDPL chunk) read back from a converted level, for the levels that borrow him (the
Special Edition's, se.py; imports, import_gltf.py and ssx.py). Its own module so those importers' code is not part of
the retail levels' converter stamp (chunks.py)."""
import struct

from . import chunks


def read_ed(donor):
    """(texture paths relative to the donor's folder, Ed record split into (start, parts [(object, surface tuple,
    geometry bytes)], anim bytes)) of a .ttlv, or None when it has no player."""
    c = chunks.read(donor, 'TTLV')
    if c is None:
        raise SystemExit(f'{donor}: not a current .ttlv (re-run the conversion)')
    if 'EDPL' not in c:
        return None
    t, o = c['TEXS'], 4
    textures = []
    for _ in range(struct.unpack_from('<I', t)[0]):
        n, = struct.unpack_from('<H', t, o)
        textures.append(t[o + 2:o + 2 + n].decode())
        o += 2 + n
    d, o = c['EDPL'], 0
    start = struct.unpack_from('<12f', d, o)
    o += 48
    o += 4  # static perso
    n_parts, = struct.unpack_from('<I', d, o)
    o += 4
    parts = []
    for _ in range(n_parts):
        obj, = struct.unpack_from('<H', d, o)
        o += 4
        tex, flags, su, sv, n_frames = struct.unpack_from('<iI2fH', d, o)
        frames = [struct.unpack_from('<if', d, o + 20 + 8 * k) for k in range(n_frames)]
        o += 20 + 8 * n_frames
        nv, ni = struct.unpack_from('<II', d, o)
        size = 8 + 52 * nv + 4 * ni
        parts.append((obj, (tex, flags, su, sv, frames), d[o:o + size]))
        o += size
    return textures, start, parts, d[o:]

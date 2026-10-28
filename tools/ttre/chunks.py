"""The container both of the core's files use (.ttlv levels, .ttai AI; reader: core/src/tc_reader.h).

  char magic[4] ("TTLV" / "TTAI"), u32 CONTAINER
  then chunks to the end of the file: { char tag[4], u32 size, size bytes, zero padding to 4 bytes }

There are no format versions. These files are build outputs, always regenerated from the game data, so nothing ever
needs to read an old one. The tags are the schema:
- A reader skips tags it does not know, so a newer converter's output still loads.
- It refuses a file that lacks a chunk it needs, or whose chunk size differs from what it reads (TC_ERR_STALE: "re-run
  the conversion").
- New data goes in a new chunk. A chunk whose layout changes gets a new tag, so the old reader sees it as missing.
Each format lists its chunks in its own module docstring (ttlv.py, ai.py).

Every file starts with a STMP chunk: the converter's stamp (`stamp()`), a hash of the tools/ttre sources that wrote
it. tools/build_all.sh regenerates a level whose stamp differs from the current one, and the core keeps it for log
lines. CONTAINER (100) changes only if the framing above changes. The numbered formats before it used 1-12 at that
offset, so the core reports those files as stale too.
"""
import glob
import hashlib
import os
import struct
import sys

CONTAINER = 100


def stamp():
    """12 hex digits: a hash of every tools/ttre/*.py (the code that decides the files' contents)."""
    h = hashlib.sha256()
    for p in sorted(glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), '*.py'))):
        h.update(os.path.basename(p).encode() + b'\0' + open(p, 'rb').read())
    return h.hexdigest()[:12]


def chunk(tag, payload):
    assert len(tag) == 4
    return tag.encode() + struct.pack('<I', len(payload)) + bytes(payload) + b'\0' * (-len(payload) % 4)


def write(path, magic, chunks):
    """chunks: [(tag, payload bytes)] in file order; the STMP chunk is added first."""
    s = stamp().encode()
    with open(path, 'wb') as f:
        f.write(magic.encode() + struct.pack('<I', CONTAINER) + chunk('STMP', struct.pack('<H', len(s)) + s)
                + b''.join(chunk(t, p) for t, p in chunks))


def read(path_or_bytes, magic=None):
    """{tag: payload} of a container file (the first of a repeated tag counts, as in the core), or None when it is not one (an old numbered file)."""
    d = open(path_or_bytes, 'rb').read() if isinstance(path_or_bytes, str) else path_or_bytes
    if len(d) < 8 or (magic and d[:4] != magic.encode()) or struct.unpack_from('<I', d, 4)[0] != CONTAINER:
        return None
    out, o = {}, 8
    while o + 8 <= len(d):
        tag, size = d[o:o + 4].decode('latin-1'), struct.unpack_from('<I', d, o + 4)[0]
        if o + 8 + size > len(d):
            raise ValueError(f'chunk {tag!r} at {o} runs past the end')
        out.setdefault(tag, d[o + 8:o + 8 + size])
        o += 8 + size + (-size % 4)
    return out


def file_stamp(path):
    """The STMP of a container file, or None (missing, old format)."""
    try:
        c = read(path)
    except (OSError, ValueError):
        return None
    if not c or 'STMP' not in c:
        return None
    n, = struct.unpack_from('<H', c['STMP'])
    return c['STMP'][2:2 + n].decode()


if __name__ == '__main__':
    # python3 -m tools.ttre.chunks [FILE ...]: the current stamp, or each file's stamp and chunks
    if len(sys.argv) == 1:
        print(stamp())
    for p in sys.argv[1:]:
        c = read(p)
        print(p, 'not a container file (old format?)' if c is None else
              f'stamp {file_stamp(p)}' + (' (current)' if file_stamp(p) == stamp() else ' (stale)') + ': '
              + ', '.join(f'{t} {len(v)}' for t, v in c.items()))

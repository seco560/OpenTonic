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
it (comments and docstrings aside). tools/convert_files.sh regenerates a level whose stamp differs from the current one, and the core keeps it for log
lines. CONTAINER (100) changes only if the framing above changes. The numbered formats before it used 1-12 at that
offset, so the core reports those files as stale too.
"""
import hashlib
import io
import os
import re
import struct
import sys
import tokenize

CONTAINER = 100


def _imports(path):
    """The tools/ttre modules a module imports (`from . import a, b`, `from .a import x`, `from tools.ttre import a`)."""
    names = set()
    for m in re.finditer(r'^[ \t]*from[ \t]+(?:\.|tools\.ttre)(\.?\w*)[ \t]+import[ \t]+(?:\(([^)]*)\)|([\w \t,]+))',
                         open(path).read(), re.M):
        sub = m.group(1).lstrip('.')
        listed = re.sub(r'\bas\s+\w+', '', m.group(2) or m.group(3))
        names.update([sub] if sub else re.findall(r'\w+', listed))
    return names


def _code(path):
    """A module's source without comments, docstrings and blank lines, so editing those doesn't make every level stale.
    Only COMMENT tokens and bare string statements are cut from the text, so the result is the same on every Python 3."""
    src = open(path, 'rb').read()
    toks = [t for t in tokenize.tokenize(io.BytesIO(src).readline) if t.type not in (tokenize.NL, tokenize.ENCODING)]
    lines = src.decode('utf-8').splitlines(keepends=True)
    cut = []  # (start, end) positions to blank
    for i, t in enumerate(toks):
        if t.type == tokenize.COMMENT:
            cut.append((t.start, t.end))
        elif t.type == tokenize.STRING:
            prev = next((p for p in reversed(toks[:i]) if p.type != tokenize.COMMENT), None)
            after = next((n for n in toks[i + 1:] if n.type != tokenize.COMMENT), None)
            if (prev is None or prev.type in (tokenize.NEWLINE, tokenize.INDENT, tokenize.DEDENT)) and \
                    (after is None or after.type in (tokenize.NEWLINE, tokenize.ENDMARKER)):
                cut.append((t.start, t.end))
    for (sr, sc), (er, ec) in reversed(cut):
        if sr == er:
            lines[sr - 1] = lines[sr - 1][:sc] + lines[sr - 1][ec:]
        else:
            lines[sr - 1] = lines[sr - 1][:sc] + '\n'
            for r in range(sr, er - 1):
                lines[r] = '\n'
            lines[er - 1] = lines[er - 1][ec:]
    return ''.join(l.rstrip() + '\n' for l in lines if l.strip())


def stamp(modules=('__init__', '__main__'), files=()):
    """12 hex digits: a hash of the code that decides a build output's contents, without comments, docstrings and blank
    lines. By default the levels': the package, the converter's entry point (__main__.py) and every tools/ttre module
    they import, directly or not (so import_gltf.py, which doesn't feed them, doesn't count). `modules` picks other
    tools/ttre roots, `files` adds scripts (their '#' comment lines don't count): tools/convert_files.sh keeps these
    for its other steps in files/out/.stamps."""
    here = os.path.dirname(os.path.abspath(__file__))
    seen, todo = set(), list(modules)
    while todo:
        m = todo.pop()
        p = os.path.join(here, m + '.py')
        if m in seen or not os.path.exists(p):
            continue
        seen.add(m)
        todo.extend(_imports(p))
    h = hashlib.sha256()
    for m in sorted(seen):
        h.update(m.encode() + b'\0' + _code(os.path.join(here, m + '.py')).encode())
    for f in files:
        text = ''.join(l for l in open(f) if l.strip() and not l.lstrip().startswith('#'))
        h.update(os.path.basename(f).encode() + b'\0' + text.encode())
    return h.hexdigest()[:12]


def chunk(tag, payload):
    assert len(tag) == 4
    return tag.encode() + struct.pack('<I', len(payload)) + bytes(payload) + b'\0' * (-len(payload) % 4)


def write(path, magic, chunks):
    """chunks: [(tag, payload bytes)] in file order; the STMP chunk is added first. Written next to `path` and renamed
    over it, so an interrupted run never leaves a cut-off file carrying a current stamp."""
    s = stamp().encode()
    tmp = path + '.partial'
    with open(tmp, 'wb') as f:
        f.write(magic.encode() + struct.pack('<I', CONTAINER) + chunk('STMP', struct.pack('<H', len(s)) + s)
                + b''.join(chunk(t, p) for t, p in chunks))
    os.replace(tmp, path)


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
    # python3 -m tools.ttre.chunks --inputs NAME ...: the stamp of other outputs' code (tools/ttre module names, and
    #   paths of scripts: anything with a '/')
    if sys.argv[1:2] == ['--inputs']:
        names = sys.argv[2:]
        print(stamp([n for n in names if '/' not in n], [n for n in names if '/' in n]))
        sys.exit()
    if len(sys.argv) == 1:
        print(stamp())
    for p in sys.argv[1:]:
        c = read(p)
        print(p, 'not a container file (old format?)' if c is None else
              f'stamp {file_stamp(p)}' + (' (current)' if file_stamp(p) == stamp() else ' (stale)') + ': '
              + ', '.join(f'{t} {len(v)}' for t, v in c.items()))

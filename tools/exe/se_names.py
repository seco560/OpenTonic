"""The Special Edition exe's AI primitive names, mapped onto TonicTrouble.exe's (retail) numbering (docs/SE.md "AI
primitive names").

    tools/bin/oracle-venv/bin/python -m tools.exe.se_names [out.tsv]

The SE's game exe (MAID3DVR.EXE, build 8.5.2, 1998; files/sources/se, tools/ttre/se.py) still holds its AI primitive
tables with their script names and exports 2593 engine functions by name (fn_eGetNbCond, szFindConditionScriptNameFromId,
...). Retail stripped both. Its tables (entry = char name[], handler, parameter types):
  KW    0x572930   9 x 28 (name 16, handler +16)        COND  0x56c158  98 x 56 (name 44, handler +44, types +48)
  FUNC  0x56dc68 121 x 56                               PROC  0x56f6e0 230 x 56
  META  0x572a30  42 x 56 (name 40, start +40, cont +44) OP    0x56d8e8  28 x 32 (name 28, handler +28)
  FIELD 0x56d6c8  15 x 32 (name 20, type +20, get +24, set +28)
Retail's are the same lists, grown (tools/exe/tables.tsv: COND 111, FUNC 165, PROC 294, KW 10, META 43; OP and FIELD
unchanged), handlers only.

Mapping: per kind, the two lists are aligned in order (Needleman-Wunsch: insertions on either side), scoring a pair
by how alike the code each entry runs is. Many entries share one handler that switches on the entry's own index, so
an entry's code is its traced path: the handler is run under Unicorn with a node whose index is the entry's (calls
skipped, returning 0; unmapped memory reads zero), and the path's prefix shared by every index of that handler is cut.
Instructions are compared as objdump text with addresses masked (difflib ratio). Confidence per pair:
  code        similarity >= 0.85
  code+order  0.5 .. 0.85
  order       below 0.5, kept by the alignment between better pairs
  position    a run of SE-only entries paired with an equally long run of retail-only ones between the same anchors
Rows with no partner: retail-only (added after the SE) or SE-only (removed). Checked against the names this project
gave retail primitives by reading their code (tools/exe/ai_names.tsv): they agree wherever both exist (ButtonHeld =
Cond_PressedBut, InZone = Cond_CollidePersoZDDWithPerso, ForceBehaviour = Proc_ChangeComport, ChangeState =
Proc_ChangeAction, ...).

Output (default tools/exe/se_ai_map.tsv): kind, retail index, SE index, SE name, SE parameter types, similarity,
confidence, retail name (ai_names.tsv). Needs: the Unicorn venv (tools/exe/oracle.py), objdump, the unpacked retail exe
(docs/EXE.md) and the extracted SE (files/sources/se/MAID3DVR.EXE).
"""
import difflib
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SE_EXE = os.path.join(ROOT, 'files/sources/se/MAID3DVR.EXE')
RT_EXE = os.path.expanduser('~/Projects/RETonicTrouble/Source Files/Installed Files Linux/TonicTrouble.exe')
OUT = os.path.join(ROOT, 'tools/exe/se_ai_map.tsv')
# kind: (base, count, stride, name length, handler offsets, types offset or None, handler arguments)
SE_TABLES = {'KW': (0x572930, 9, 28, 16, (16,), 20, 3), 'COND': (0x56c158, 98, 56, 44, (44,), 48, 3),
             'FUNC': (0x56dc68, 121, 56, 44, (44,), 48, 3), 'PROC': (0x56f6e0, 230, 56, 44, (44,), 48, 2),
             'META': (0x572a30, 42, 56, 40, (40, 44), 48, 2), 'OP': (0x56d8e8, 28, 32, 28, (28,), None, 3),
             'FIELD': (0x56d6c8, 15, 32, 20, (24,), None, 2)}
KINDS = ('KW', 'OP', 'FIELD', 'COND', 'FUNC', 'PROC', 'META')
GAP = -0.15
STACK, STOP, DATA = 0x7F000000, 0x0FFFF000, 0x10000000


class Image:
    """A PE file's sections, by virtual address."""

    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        e = struct.unpack_from('<I', d, 0x3C)[0]
        n, opt = struct.unpack_from('<H', d, e + 6)[0], struct.unpack_from('<H', d, e + 20)[0]
        self.base = struct.unpack_from('<I', d, e + 24 + 28)[0]
        self.secs = []
        for i in range(n):
            vs, va, rs, ro = struct.unpack_from('<4I', d, e + 24 + opt + 40 * i + 8)
            self.secs.append((self.base + va, vs, ro, rs))

    def bytes(self, va, n):
        for v, vs, ro, rs in self.secs:
            if v <= va < v + rs:
                return self.d[ro + va - v:ro + va - v + n]
        raise ValueError(f'{va:#x} not in the file')


class Tracer:
    """Runs a handler under Unicorn and records the addresses it executes (calls skipped with eax = 0, unmapped reads
    get zero pages). Handler(me, node after its own, result): the node before holds the entry's index."""

    def __init__(self, path):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED
        raw = open(path, 'rb').read()
        e = struct.unpack_from('<I', raw, 0x3C)[0]
        n, opt = struct.unpack_from('<H', raw, e + 6)[0], struct.unpack_from('<H', raw, e + 20)[0]
        base, size = struct.unpack_from('<I', raw, e + 24 + 28)[0], struct.unpack_from('<I', raw, e + 24 + 56)[0]
        size = (size + 0xFFF) & ~0xFFF
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, size)
        uc.mem_write(base, raw[:0x1000])
        for k in range(n):
            vs, va, rs, rp = struct.unpack_from('<4I', raw, e + 24 + opt + 40 * k + 8)
            uc.mem_write(base + va, raw[rp:rp + min(rs, vs or rs)])
        self.base, self.image = base, bytes(uc.mem_read(base, size))
        uc.mem_map(STACK, 0x100000)
        uc.mem_map(DATA, 0x100000)
        uc.mem_map(STOP, 0x1000)
        uc.mem_write(STOP, b'\xf4' * 0x1000)
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)
        uc.hook_add(UC_HOOK_CODE, self._code)

    def _unmapped(self, uc, access, addr, size, value, _):
        from unicorn import UC_PROT_ALL
        try:
            uc.mem_map(addr & ~0xFFF, 0x1000, UC_PROT_ALL)
            self.extra.append(addr & ~0xFFF)
            return True
        except Exception:
            return False

    def _code(self, uc, addr, size, _):
        from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_EAX
        if addr >= STOP or len(self.trace) >= self.limit:
            uc.emu_stop()
            return
        self.trace.append(addr)
        b = bytes(uc.mem_read(addr, min(size, 2)))
        if b[0] == 0xE8 or (b[0] == 0xFF and len(b) > 1 and (b[1] >> 3) & 7 == 2):  # a call: skipped, returns 0
            uc.reg_write(UC_X86_REG_EIP, addr + size)
            uc.reg_write(UC_X86_REG_EAX, 0)

    def run(self, fn, index, nargs, limit=400):
        from unicorn.x86_const import UC_X86_REG_ESP
        uc = self.uc
        self.trace, self.limit, self.extra = [], limit, []
        uc.mem_write(self.base, self.image)
        uc.mem_write(DATA, b'\0' * 0x100000)
        node = DATA + 0x1000
        uc.mem_write(node, struct.pack('<IBBBB', index, 0, 0, 1, 3))
        args = [DATA + 0x8000, node + 8, DATA + 0x9000][:nargs]
        frame = struct.pack('<I', STOP) + b''.join(struct.pack('<I', a) for a in args)
        sp = STACK + 0x80000 - len(frame)
        uc.mem_write(sp, frame)
        uc.reg_write(UC_X86_REG_ESP, sp)
        try:
            uc.emu_start(fn, STOP, count=limit * 4)
        except Exception:
            pass
        for p in self.extra:
            try:
                uc.mem_unmap(p, 0x1000)
            except Exception:
                pass
        return list(self.trace)


def disassembly(path, start=None, stop=None):
    """address -> (mnemonic, operands with 5+ digit hex numbers masked), from objdump."""
    cmd = ['objdump', '-D' if start else '-d', '--no-show-raw-insn'] + \
        ([f'--start-address={start:#x}', f'--stop-address={stop:#x}'] if start else []) + [path]
    out = {}
    for line in subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.splitlines():
        m = re.match(r'\s+([0-9a-f]+):\s+(\S+)\s*(.*)', line)
        if m:
            ops = re.sub(r'0x[0-9a-f]{5,}', 'A', m.group(3).split('<')[0].split('#')[0].strip())
            out[int(m.group(1), 16)] = m.group(2) + ' ' + ops
    return out


def se_entries(img, kind):
    base, n, stride, nl, hoffs, toff, _ = SE_TABLES[kind]
    rows = []
    for i in range(n):
        raw = img.bytes(base + stride * i, stride)
        types = raw[toff:toff + 4].split(b'\0')[0].decode('latin-1') if toff is not None else ''
        rows.append((raw[:nl].split(b'\0')[0].decode('latin-1'), struct.unpack_from('<I', raw, hoffs[0])[0], types))
    return rows


def retail_entries(img, kind):
    for line in open(os.path.join(ROOT, 'tools/exe/tables.tsv')):
        f = line.rstrip('\n').split('\t')
        if f[0] == kind:
            base, n, stride, off = int(f[1], 16), int(f[2]), int(f[3]), int(f[4])
            return [struct.unpack('<I', img.bytes(base + stride * i + off, 4))[0] for i in range(n)]
    raise KeyError(kind)


def fingerprints(tracer, dis, handlers, nargs):
    """Per entry: the instructions its index runs, minus the prefix shared by all entries of its handler."""
    traces = [tracer.run(h, i, nargs) for i, h in enumerate(handlers)]
    groups = {}
    for i, h in enumerate(handlers):
        groups.setdefault(h, []).append(i)
    out = []
    for i, h in enumerate(handlers):
        t, cut = traces[i], 0
        if len(groups[h]) > 1:
            same = [traces[k] for k in groups[h]]
            while cut < len(t) and all(len(o) > cut and o[cut] == t[cut] for o in same):
                cut += 1
        out.append([dis.get(a, '?') for a in t[cut:]] or [dis.get(a, '?') for a in t])
    return out


def align(fs, fr):
    """Needleman-Wunsch over two fingerprint lists: [(SE index or None, retail index or None, similarity)]."""
    n, m, sim = len(fs), len(fr), {}

    def s(i, j):
        if (i, j) not in sim:
            sim[(i, j)] = difflib.SequenceMatcher(None, fs[i], fr[j], autojunk=False).ratio() if fs[i] and fr[j] else 0.0
        return sim[(i, j)]
    D = [[0.0] * (m + 1) for _ in range(n + 1)]
    B = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        D[i][0], B[i][0] = D[i - 1][0] + GAP, 1
    for j in range(1, m + 1):
        D[0][j], B[0][j] = D[0][j - 1] + GAP, 2
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            D[i][j], B[i][j] = max((D[i - 1][j - 1] + s(i - 1, j - 1) - 0.5, 0), (D[i - 1][j] + GAP, 1),
                                   (D[i][j - 1] + GAP, 2))
    i, j, out = n, m, []
    while i or j:
        if B[i][j] == 0:
            out.append((i - 1, j - 1, s(i - 1, j - 1)))
            i, j = i - 1, j - 1
        elif B[i][j] == 1:
            out.append((i - 1, None, None))
            i -= 1
        else:
            out.append((None, j - 1, None))
            j -= 1
    out.reverse()
    # equally long runs of SE-only and retail-only entries between the same two pairs: paired in order (position)
    k, fixed = 0, []
    while k < len(out):
        if out[k][0] is not None and out[k][1] is not None:
            fixed.append(out[k])
            k += 1
            continue
        e = k
        while e < len(out) and (out[e][0] is None or out[e][1] is None):
            e += 1
        se_only = [r[0] for r in out[k:e] if r[0] is not None]
        rt_only = [r[1] for r in out[k:e] if r[1] is not None]
        if se_only and len(se_only) == len(rt_only):
            fixed += [(a, b, 'position') for a, b in zip(se_only, rt_only)]
        else:
            fixed += out[k:e]
        k = e
    return fixed


def confidence(sim):
    if sim == 'position':
        return 'position'
    return 'code' if sim >= 0.85 else 'code+order' if sim >= 0.5 else 'order'


def known_names():
    names = {}
    for line in open(os.path.join(ROOT, 'tools/exe/ai_names.tsv'), encoding='utf-8'):
        if line.strip() and not line.startswith('#'):
            f = line.rstrip('\n').split('\t')
            names[(f[0], int(f[1]))] = f[2]
    return names


def main(argv):
    out_path = argv[0] if argv else OUT
    for p in (SE_EXE, RT_EXE):
        if not os.path.exists(p):
            sys.exit(f'missing {p} (docs/SE.md, docs/EXE.md)')
    se_img, rt_img = Image(SE_EXE), Image(RT_EXE)
    se_tr, rt_tr = Tracer(SE_EXE), Tracer(RT_EXE)
    se_dis = disassembly(SE_EXE)
    rt_dis = disassembly(RT_EXE, 0x401000, 0x4D2000)  # the dump's code section (named .data, see docs/EXE.md)
    known = known_names()
    rows, summary = [], []
    for kind in KINDS:
        se_rows, rt_handlers = se_entries(se_img, kind), retail_entries(rt_img, kind)
        nargs = SE_TABLES[kind][6]
        pairs = align(fingerprints(se_tr, se_dis, [r[1] for r in se_rows], nargs),
                      fingerprints(rt_tr, rt_dis, rt_handlers, nargs))
        counts = {}
        for a, b, sim in pairs:
            conf = 'se-only' if b is None else 'retail-only' if a is None else confidence(sim)
            counts[conf] = counts.get(conf, 0) + 1
            name, types = (se_rows[a][0], se_rows[a][2]) if a is not None else ('', '')
            rows.append((kind, '' if b is None else str(b), '' if a is None else str(a), name, types,
                         f'{sim:.2f}' if isinstance(sim, float) else '', conf,
                         known.get((kind, b), '') if b is not None else ''))
        summary.append(f'{kind}: SE {len(se_rows)}, retail {len(rt_handlers)}: ' +
                       ', '.join(f'{v} {k}' for k, v in sorted(counts.items())))
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('# Generated by tools/exe/se_names.py (docs/SE.md "AI primitive names"): the Special Edition exe\'s AI\n'
                '# primitive names on the retail numbering. confidence: code / code+order / order / position (see the\n'
                '# module), retail-only (added after the SE), se-only (gone from retail).\n')
        f.write('kind\tretail\tse\tse_name\tse_types\tsimilarity\tconfidence\tretail_name\n')
        f.writelines('\t'.join(r) + '\n' for r in rows)
    print('\n'.join(summary))
    print(f'wrote {out_path}')


if __name__ == '__main__':
    main(sys.argv[1:])

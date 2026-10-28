"""Oracle tests: the exe's own functions, run under an x86 emulator, against the core's ports (docs/DECOMP.md).

    tools/bin/oracle-venv/bin/python -m tools.exe.oracle [--exe PATH] [--cases N] [--seed S] [--show K]
        [--fpu single|extended] [TEST ...]

Needs Unicorn (`python3 -m venv tools/bin/oracle-venv && tools/bin/oracle-venv/bin/pip install unicorn`) and the
unpacked exe (docs/EXE.md: sha256 fea1140e..., the installed TonicTrouble.exe; the disc's packed one cannot run).
The functions tested are pure float math: no OS calls, no globals. So mapping the image, pushing the arguments and
running to a return address is enough. The core is built as a shared library (cc, the core's flags) and called with
ctypes on the same inputs.

The exe computes on the x87; Direct3D (the game's renderer DLL) normally sets its precision control to 24 bits for
the whole game, so the default here is single precision (--fpu extended: the x87's power-on 64-bit mantissa). The
port is built with -ffp-contract=off, as the core (no fused multiply-adds: the x87 has none). Values are compared with a tolerance,
and an outcome that differs only where the exe's own numbers sit on a decision boundary is counted apart ("edge
cases") from a real disagreement.

Tests:
  sphere_tri   FUN_00465b60 (tc_mec_sphere_tri): a moving sphere against a triangle: face / edge / corner, t,
               normal, point
  point_tri    FUN_0046ff70 (tc_mec_point_tri): a moving point (Ed's hand point) against a triangle: face only
"""
import ctypes
import hashlib
import math
import os
import random
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.expanduser('~/Projects/RETonicTrouble/Source Files/Installed Files Linux/TonicTrouble.exe')
EXE_SHA256 = 'fea1140ebe1b9a7ea1cbcfdc8d857693eadcc4177b634a243f829dd2ec30107d'
STACK, STACK_SIZE = 0x7F000000, 0x100000
DATA, DATA_SIZE = 0x10000000, 0x10000
STOP = 0x0FFFF000  # return address: a page of hlt


class Exe:
    """The dump mapped at its image base, ready to call functions."""

    def __init__(self, path, fpu_single=True):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
        raw = open(path, 'rb').read()
        if hashlib.sha256(raw).hexdigest() != EXE_SHA256:
            raise SystemExit(f'{path}: not the unpacked dump (sha256 {EXE_SHA256[:8]}...), see docs/EXE.md')
        pe = struct.unpack_from('<I', raw, 0x3C)[0]
        n_sections, opt_size = struct.unpack_from('<H', raw, pe + 6)[0], struct.unpack_from('<H', raw, pe + 20)[0]
        base, image_size = struct.unpack_from('<I', raw, pe + 24 + 28)[0], struct.unpack_from('<I', raw, pe + 24 + 56)[0]
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, (image_size + 0xFFF) & ~0xFFF)
        uc.mem_write(base, raw[:0x1000])
        for k in range(n_sections):
            o = pe + 24 + opt_size + 40 * k
            vsize, va, rsize, rptr = struct.unpack_from('<4I', raw, o + 8)
            uc.mem_write(base + va, raw[rptr:rptr + min(rsize, vsize or rsize)])
        uc.mem_map(STACK, STACK_SIZE)
        uc.mem_map(DATA, DATA_SIZE)
        uc.mem_map(STOP, 0x1000)
        uc.mem_write(STOP, b'\xF4' * 0x1000)
        self.fpcw = 0x007F if fpu_single else 0x037F

    def reset_fpu(self):
        from unicorn.x86_const import UC_X86_REG_FPCW
        self.uc.reg_write(UC_X86_REG_FPCW, self.fpcw)

    def call(self, addr, args):
        """Push the u32 args (cdecl order), run addr until it returns; EAX."""
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        sp = STACK + STACK_SIZE - 0x100
        for a in reversed(args):
            sp -= 4
            self.uc.mem_write(sp, struct.pack('<I', a & 0xFFFFFFFF))
        sp -= 4
        self.uc.mem_write(sp, struct.pack('<I', STOP))
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.reset_fpu()
        self.uc.emu_start(addr, STOP, count=2_000_000)
        return self.uc.reg_read(UC_X86_REG_EAX)

    def put(self, offset, fmt, *values):
        self.uc.mem_write(DATA + offset, struct.pack(fmt, *values))
        return DATA + offset

    def get(self, offset, fmt):
        return struct.unpack(fmt, self.uc.mem_read(DATA + offset, struct.calcsize(fmt)))


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def build_core():
    """The core as a shared library with its Makefile's flags."""
    out = os.path.join(tempfile.gettempdir(), f'toniccore_oracle_{os.getpid()}.so')
    src = [os.path.join(ROOT, 'core/src', f) for f in sorted(os.listdir(os.path.join(ROOT, 'core/src'))) if f.endswith('.c')]
    subprocess.check_call(['cc', '-O2', '-std=c11', '-ffp-contract=off', '-shared', '-fPIC', '-I' + os.path.join(ROOT, 'core/include'),
                           *src, '-o', out, '-lm'])
    return ctypes.CDLL(out)


class Vec3(ctypes.Structure):
    _fields_ = [('x', ctypes.c_float), ('y', ctypes.c_float), ('z', ctypes.c_float)]


def v3(p):
    return Vec3(*p)


# ---- sphere_tri: FUN_00465b60 ----

def sphere_tri_case(rng):
    """A random triangle and a sphere moving near it; a mix aimed at the face, the edges, the corners and grazes."""
    def rnd(s=2.0):
        return [rng.uniform(-s, s) for _ in range(3)]
    a, b, c = rnd(), rnd(), rnd()
    if rng.random() < 0.3:  # floors and walls like the levels': axis-aligned planes
        axis = rng.randrange(3)
        for v in (a, b, c):
            v[axis] = 0.0
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
    ln = math.sqrt(sum(x * x for x in n)) or 1.0
    n = [x / ln for x in n]
    r = rng.choice([0.25, 0.3, 0.375, 0.53, rng.uniform(0.05, 1.5)])
    mode = rng.random()
    target = [rng.uniform(0, 1) for _ in range(3)]
    s = sum(target)
    on_tri = [(a[k] * target[0] + b[k] * target[1] + c[k] * target[2]) / s for k in range(3)]
    if mode < 0.4:  # towards a point on (or near) the triangle from in front
        away = rng.uniform(0.0, 3.0)
        p0 = [on_tri[k] + n[k] * (r + away) + rng.uniform(-0.5, 0.5) for k in range(3)]
        d = [on_tri[k] - p0[k] + rng.uniform(-1, 1) for k in range(3)]
    elif mode < 0.6:  # towards an edge or a corner
        e = rng.choice([(a, b), (a, c), (b, c), (a, a), (b, b), (c, c)])
        w = rng.random()
        q = [e[0][k] * (1 - w) + e[1][k] * w for k in range(3)]
        p0 = [q[k] + rng.uniform(-2, 2) for k in range(3)]
        d = [(q[k] - p0[k]) * rng.uniform(0.5, 1.5) for k in range(3)]
    elif mode < 0.7:  # touching the face, then moving into it or along it
        p0 = [on_tri[k] + n[k] * r for k in range(3)]
        d = [rng.uniform(-1, 1) for _ in range(3)]
    elif mode < 0.75:  # moving exactly along an edge
        p0 = [a[k] + n[k] * r * 0.5 for k in range(3)]
        d = [u[k] * rng.uniform(-1, 1) for k in range(3)]
    else:  # anywhere
        p0, d = rnd(3), rnd(3)
    return [[f32(x) for x in p] for p in (p0, d, n, a, b, c)] + [f32(r)]


def sphere_tri_exe(exe, case):
    p0, d, n, a, b, c, r = case
    P0 = exe.put(0x000, '<3f', *p0)
    P2 = exe.put(0x010, '<3f', 0, 0, 0)  # param_2: unused by the function
    D = exe.put(0x020, '<3f', *d)
    N, A, B, C = (exe.put(0x030 + 0x10 * k, '<3f', *v) for k, v in enumerate((n, a, b, c)))
    TRI = exe.put(0x080, '<4I', N, A, B, C)
    KIND = exe.put(0x090, '<I', 0xFFFF)
    T = exe.put(0x094, '<f', -1)
    NORMAL = exe.put(0x0A0, '<3f', 0, 0, 0)
    POINT = exe.put(0x0B0, '<3f', 0, 0, 0)
    ret = exe.call(0x00465B60, [P0, P2, D, struct.unpack('<I', struct.pack('<f', r))[0], TRI, KIND, T, NORMAL, POINT])
    return (ret & 0xFFFF, exe.get(0x090, '<h')[0], exe.get(0x094, '<f')[0], exe.get(0x0A0, '<3f'),
            exe.get(0x0B0, '<3f'))


def sphere_tri_port(lib, case):
    p0, d, n, a, b, c, r = case
    kind, t = ctypes.c_int(-1), ctypes.c_float(-1)
    normal, point = Vec3(), Vec3()
    ret = lib.tc_mec_sphere_tri(v3(p0), v3(d), ctypes.c_float(r), v3(n), v3(a), v3(b), v3(c), ctypes.byref(kind),
                                ctypes.byref(t), ctypes.byref(normal), ctypes.byref(point))
    return ret, kind.value, t.value, (normal.x, normal.y, normal.z), (point.x, point.y, point.z)


def close(u, v, tol):
    return all(abs(x - y) <= tol * (1 + abs(x)) for x, y in zip(u, v))


def sphere_tri_compare(case, want, got):
    """'ok', 'edge' (the exe sits on a decision boundary: t at 0 or 1, a face hit at the triangle's border, a corner
    or edge root at the touching distance) or 'bad'."""
    (wr, wk, wt, wn, wp), (gr, gk, gt, gn, gp) = want, got
    if wr == 0 and gr == 0:
        return 'ok'
    if wr and gr and wr == gr and wk == gk and close((wt,), (gt,), 1e-4) and close(wn, gn, 1e-3) and close(wp, gp, 1e-3):
        return 'ok'
    t = wt if wr else gt
    if abs(t) < 1e-4 or abs(t - 1) < 1e-4:
        return 'edge'
    if wr and gr and wk == gk and abs(wt - gt) < 1e-3:
        return 'edge'
    # a face hit lost to an edge or corner (or back): the contact point lies on the triangle's border
    p0, d, n, a, b, c, r = case
    for (rr, kk, tt, nn, pp) in (want, got):
        if rr and kk == 1:
            for e0, e1 in ((a, b), (a, c), (b, c)):
                ev = [e1[k] - e0[k] for k in range(3)]
                ee = sum(x * x for x in ev) or 1
                s = sum((pp[k] - e0[k]) * ev[k] for k in range(3)) / ee
                q = [e0[k] + ev[k] * min(max(s, 0), 1) for k in range(3)]
                if math.dist(q, pp) < 1e-3:
                    return 'edge'
    return 'bad'


# ---- point_tri: FUN_0046ff70 (a moving point against one triangle, with the exe's own box rejection) ----

def point_tri_case(rng):
    p0, d, n, a, b, c, r = sphere_tri_case(rng)
    if rng.random() < 0.5:  # aim the point itself at the face
        k = [rng.uniform(0, 1) for _ in range(3)]
        q = [(a[i] * k[0] + b[i] * k[1] + c[i] * k[2]) / sum(k) for i in range(3)]
        p0 = [f32(q[i] + n[i] * rng.uniform(0, 2) + rng.uniform(-0.3, 0.3)) for i in range(3)]
        d = [f32((q[i] - p0[i]) * rng.uniform(0.5, 2)) for i in range(3)]
    return [p0, d, n, a, b, c, r]


def point_tri_exe(exe, case):
    """FUN_0046ff70 with a context (the 0x50D840 layout) built by hand: an identity obstacle matrix (FUN_0047afd0), the
    obstacle object (+4 vertices), its triangle element (+8 indices, +0x10 normals), triangle 0, p0 at +0x23C, d at
    +0x254. Its contact lands in the best-hit globals (0x50E140 t, +4 point, +0x10 normal, 0x50E170 found)."""
    p0, d, n, a, b, c, r = case
    CTX, MAT = DATA + 0x1000, DATA + 0x2000
    exe.uc.mem_write(CTX, bytes(0x900))
    exe.call(0x0047AFD0, [MAT, 0x80])
    VERTS = exe.put(0x3000, '<9f', *a, *b, *c)
    IDX = exe.put(0x3040, '<3h', 0, 1, 2)
    NORM = exe.put(0x3050, '<3f', *n)
    OBJ = exe.put(0x3060, '<4I', 3, VERTS, 0, 0)
    EL = exe.put(0x3080, '<5I', 0, 1, IDX, 0, NORM)
    exe.uc.mem_write(CTX + 0x0C, struct.pack('<2I', OBJ, MAT))
    exe.uc.mem_write(CTX + 0x200, struct.pack('<I', EL))
    exe.uc.mem_write(CTX + 0x23C, struct.pack('<3f', *p0))
    exe.uc.mem_write(CTX + 0x254, struct.pack('<3f', *d))
    exe.uc.mem_write(0x50E140, struct.pack('<f', 2.0))
    exe.uc.mem_write(0x50E170, b'\0')
    exe.call(0x0046FF70, [CTX])
    if exe.uc.mem_read(0x50E170, 1)[0] == 0:
        return (0, 0, 0.0, (0, 0, 0), (0, 0, 0))
    t, = struct.unpack('<f', exe.uc.mem_read(0x50E140, 4))
    return (1, 1, t, struct.unpack('<3f', exe.uc.mem_read(0x50E150, 12)), struct.unpack('<3f', exe.uc.mem_read(0x50E144, 12)))


def point_tri_port(lib, case):
    p0, d, n, a, b, c, r = case
    t, point = ctypes.c_float(-1), Vec3()
    ret = lib.tc_mec_point_tri(v3(p0), v3(d), v3(n), v3(a), v3(b), v3(c), ctypes.byref(t), ctypes.byref(point))
    return (ret, 1 if ret else 0, t.value if ret else 0.0, tuple(n) if ret else (0, 0, 0),
            (point.x, point.y, point.z) if ret else (0, 0, 0))


TESTS = {'sphere_tri': (sphere_tri_case, sphere_tri_exe, sphere_tri_port, sphere_tri_compare),
         'point_tri': (point_tri_case, point_tri_exe, point_tri_port, sphere_tri_compare)}


def main(argv):
    opts = {'--exe': EXE, '--cases': '20000', '--seed': '1', '--show': '5', '--fpu': 'single'}
    names = []
    i = 0
    while i < len(argv):
        if argv[i] in opts:
            opts[argv[i]] = argv[i + 1]
            i += 2
        else:
            names.append(argv[i])
            i += 1
    exe, lib = Exe(opts['--exe'], opts['--fpu'] == 'single'), build_core()
    failed = 0
    for name in names or list(TESTS):
        gen, run_exe, run_port, compare = TESTS[name]
        rng = random.Random(int(opts['--seed']))
        counts, outcomes, shown = {'ok': 0, 'edge': 0, 'bad': 0}, {}, 0
        for _ in range(int(opts['--cases'])):
            case = gen(rng)
            want, got = run_exe(exe, case), run_port(lib, case)
            verdict = compare(case, want, got)
            counts[verdict] += 1
            key = 'none' if not want[0] else {0: 'corner', 1: 'face', 2: 'edge'}.get(want[1], want[1])
            outcomes[key] = outcomes.get(key, 0) + 1
            if verdict == 'bad' and shown < int(opts['--show']):
                shown += 1
                print(f'  {name} mismatch: case {case}\n    exe  {want}\n    port {got}')
        total = sum(counts.values())
        print(f'{name}: {counts["ok"]} agree, {counts["edge"]} differ only on a boundary, {counts["bad"]} disagree '
              f'of {total}; exe outcomes {outcomes}')
        failed += counts['bad'] > 0
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

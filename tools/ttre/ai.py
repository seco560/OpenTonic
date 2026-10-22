"""Tonic Trouble AI scripts: read a perso's behaviours and print them as indented trees.

Layout (see docs/NOTES.md "AI scripts"): perso +0xC brain -> +0 mind -> +0 AI model = {ptr normal behaviour list,
ptr reflex list, ptr dsgVar defs, ...}; list = {ptr array, u32 n}; behaviour = 12 bytes {ptr scripts (ptr[n] -> nodes),
ptr schedule (null, or ptr -> {ptr nodes}), u32 n_scripts}; script = 8-byte nodes {u32 param, u8, u8, u8 depth, u8 type} in
pre-order (a node's children follow it with depth + 1); depth 0 ends the script.

Primitive names are placeholders (COND_12, PROC_200, ...) until the exe's tables are decoded; tools/exe/ai_names.tsv
(optional, "<kind><TAB><index><TAB><name>") overrides them.
"""
import collections
import os
import struct
from dataclasses import dataclass, field

NODE_TYPES = {
    0: 'KEYWORD', 1: 'COND', 2: 'OP', 3: 'FUNC', 4: 'PROC', 5: 'META', 9: 'FIELD', 11: 'DSGVAR', 12: 'INT',
    13: 'REAL', 14: 'BUTTON', 16: 'VECTOR', 17: 'MASK', 20: 'STRING', 24: 'PERSO', 25: 'REF25', 30: 'T30',
    31: 'REF31', 32: 'BEHAVIOUR', 33: 'CHANNEL', 34: 'T34', 37: 'REF37',
}
KINDS = {0: 'KW', 1: 'COND', 2: 'OP', 3: 'FUNC', 4: 'PROC', 5: 'META', 9: 'FIELD'}  # indexed primitives


@dataclass
class Node:
    type: int
    param: int
    depth: int
    ref: tuple = None  # resolved pointer target (sna, offset), if the param is a relocated pointer
    children: list = field(default_factory=list)


@dataclass
class Behaviour:
    index: int
    reflex: bool
    scripts: list            # [Node roots per script]
    schedule: list = None    # roots of the schedule script, or None


def load_names(path='tools/exe/ai_names.tsv'):
    names = {}
    if os.path.exists(path):
        for line in open(path, encoding='utf-8'):
            if line.strip() and not line.startswith('#'):
                kind, idx, name = line.rstrip('\n').split('\t')[:3]
                names[(kind, int(idx))] = name
    return names


class AI:
    def __init__(self, data, perso):
        """data: level.LevelData; perso: the perso's location (e.g. data.perso_in_fix[0][0] for Ed)."""
        self.d, self.lv, self.perso = data, data.lv, perso
        lv = self.lv
        brain = lv.ptr(perso, 0xC)
        mind = lv.ptr(brain) if brain else None
        self.model = lv.ptr(mind) if mind else None
        p3d = lv.ptr(perso)
        fam = lv.ptr(p3d, 0x14) if p3d else None
        self.states = data.family_states(fam) if fam else []
        self.state_index = {s: i for i, s in enumerate(self.states)}
        self.behaviours = []
        for off, reflex in ((0, False), (4, True)):
            lst = lv.ptr(self.model, off) if self.model else None
            arr = lv.ptr(lst) if lst else None
            n = data.un('I', lst, 4)[0] if lst else 0
            for i in range(n if arr else 0):
                sc, sched = lv.ptr(arr, 12 * i), lv.ptr(arr, 12 * i + 4)
                ns = data.un('I', arr, 12 * i + 8)[0]
                scripts = [self.script(lv.ptr(sc, 4 * k)) for k in range(ns if sc else 0)]
                sched = lv.ptr(sched) if sched else None  # schedule: ptr -> {ptr nodes}
                self.behaviours.append(Behaviour(i, reflex, scripts, self.script(sched) if sched else None))

    def script(self, loc):
        """Script location -> list of root Nodes (tree built from depths)."""
        if loc is None:
            return []
        roots, stack, o = [], [], 0
        while o < 0x40000:
            param, _, _, depth, typ = self.d.un('IBBBB', loc, o)
            if depth == 0:
                break
            n = Node(typ, param, depth, self.lv.ptr(loc, o))
            while stack and stack[-1].depth >= depth:
                stack.pop()
            (stack[-1].children if stack else roots).append(n)
            stack.append(n)
            o += 8
        return roots

    def nodes(self):
        """Every node of every script (normal, reflex, schedules)."""
        def walk(ns):
            for n in ns:
                yield n
                yield from walk(n.children)
        for b in self.behaviours:
            for s in b.scripts + ([b.schedule] if b.schedule else []):
                yield from walk(s)


def label(ai, n, names):
    kind = KINDS.get(n.type)
    if kind:
        return names.get((kind, n.param), f'{kind}_{n.param}')
    if n.type == 11:
        return f'dsg{n.param}'
    if n.type == 12:
        v = struct.unpack('<i', struct.pack('<I', n.param))[0]
        return str(v)
    if n.type == 13:
        return '%gf' % struct.unpack('<f', struct.pack('<I', n.param))[0]
    if n.ref is not None:
        if n.ref in ai.state_index:
            return f'state#{ai.state_index[n.ref]}'
        return f'{NODE_TYPES.get(n.type, "T%d" % n.type)}@{n.ref[1]:#x}'
    return f'{NODE_TYPES.get(n.type, "T%d" % n.type)}({n.param:#x})'


def format_tree(ai, roots, names, indent='    '):
    out = []

    def rec(n, d):
        args = n.children
        if n.type in KINDS and n.type != 0 and args:
            # a primitive with arguments: print simple arguments inline, nested calls on their own lines
            if all(not a.children for a in args):
                out.append(indent * d + f'{label(ai, n, names)}({", ".join(label(ai, a, names) for a in args)})')
                return
        out.append(indent * d + label(ai, n, names))
        for a in args:
            rec(a, d + 1)
    for r in roots:
        rec(r, 0)
    return out


def dump(ai, path, names=None):
    names = names if names is not None else load_names()
    with open(path, 'w', encoding='utf-8') as f:
        for b in ai.behaviours:
            f.write(f'=== {"reflex" if b.reflex else "behaviour"} {b.index}: {len(b.scripts)} scripts\n')
            if b.schedule:
                f.write('  [schedule]\n')
                f.write('\n'.join('  ' + l for l in format_tree(ai, b.schedule, names)) + '\n')
            for k, s in enumerate(b.scripts):
                f.write(f'  [script {k}]\n')
                f.write('\n'.join('  ' + l for l in format_tree(ai, s, names)) + '\n')


# ---- .ttai export (reader: core/src/tc_ai.c) ----
"""
.ttai version 1: one perso's AI (Ed's), with every pointer resolved to an index. Little-endian.
  "TTAI", u32 version
  u32 n_nodes, n_nodes x { u32 param, u8 type, u8 depth, u16 0 }
      Scripts are runs of nodes in the engine's order, each closed by an end node (type 8, depth 0).
      Resolved params: 14 button -> button index; 15 constant vector -> vector index; 20 string -> string index;
      24 perso -> 0 = this perso, k + 1 = level perso k (LevelData.persos()); 25 state -> state index;
      32 behaviour -> normal index, or 0x10000 | reflex index. Unresolvable pointers -> 0xFFFFFFFF.
  u32 n_scripts, n_scripts x u32 first node
  u16 n_normal, u16 n_reflex, (n_normal + n_reflex) x { i32 schedule script (-1 none), u16 first script, u16 n }
  u16 initial normal behaviour, u16 initial reflex (0xFFFF none)
  dsgvar block = { u16 n_dsgvars, u16 0, u32 buffer size, n x { u32 offset, u8 type (engine dsgvar type 0-16),
      u8 x3 }, buffer bytes (initial values), padded to 4 }: this perso's
  u16 n_states, u16 initial state, n x { i16 next (-1 none), u16 n_frames, u8 speed (frames/s, 0 = 30),
      u8 flags (State +0x3C: 1 = only listed transitions allowed), u16 n_transitions, u16 n_prohibited,
      u8 repeats (State +0x3A: extra plays before the next state), u8 0, u16 anim (first state with the same
      animation: the .ttlv's Ed animation "s<anim>"), u16 0, f32 ground (units per animation frame Ed moves forward
      while the animation plays on the ground: level.forward_stride of looping states, 0 = in place),
      n_transitions x { u16 target, u16 via }, n_prohibited x u16 state }
  u16 n_buttons, u16 0, n x { u16 inputs (mask of TC_AI_IN_*), u8 analog (0 none, 1 stick angle, 2 stick
      magnitude, 3 stick x, 4 stick y), u8 0, i16 lo, i16 hi (stick range, -100..100, that counts as held) }
  u16 n_strings, n x { u16 len, bytes }
  u16 n_vectors, n x f32[3]
  u16 n_persos, u16 0, n x { u16 id (as in perso nodes), u8 role (1 = perso-in-fix 1, the camera; 2 = perso-in-fix 2;
      0 other), u8 0, dsgvar block }: the other persos the scripts reference (Dot(perso, dsgvar) reads and writes
      their dsgvars; their own AI does not run)
"""
import re

TTAI_VERSION = 1
NODE_END = 8
# Virtual inputs (bit numbers), named after the game's input entries (fix: "<Name>_Key" / "<Name>_Pad").
INPUTS = ['left', 'right', 'up', 'down', 'speedup', 'jump', 'lateral', 'action', 'shoot', 'camera']
KEY_INPUT = {0xcb: 0, 0xcd: 1, 0xc8: 2, 0xd0: 3, 0x2a: 4, 0x2c: 5, 0x38: 6, 0x1d: 7, 0x39: 8, 0x52: 9}  # DIK codes
PAD_INPUT = {16: 4, 10: 5, 13: 6, 11: 7, 12: 8, 14: 9}  # joystick buttons
DIR_INPUT = {0: 2, 1: 3, 2: 0, 3: 1}  # joystick hat directions: up, down, left, right


class Buttons:
    """Input entries (0x1C: u32 n_items, ptr items, ptr, u32, i32 counter, f32 analog, u32) and their item lists,
    a prefix formula of 8-byte items {u32 op, u32 0}: 0xF001 AND, 0xF002 OR, 0xF00B NOT (each followed by its
    operands), 0xF00A {entry address} reference, 0xF006 {DIK code} key held, 0xF004 key hit, 0xF01A {joy, button},
    0xF00E {joy, direction}, 0xF010 {joy, axis, lo, hi} axis in range, 0xF011..13 {joy, axes x3?, lo, hi} analog
    stick (angle, magnitude, ?); operands are {value, 0xFF0000}; a bare operand where an op is expected is an entry reference too.
    References hold the raw (unrelocated) address."""

    def __init__(self, data, loc):
        self.d, self.loc = data, loc
        # raw address -> location: the entry's items pointer is stored both raw and relocated
        raw = data.un('I', loc, 4)[0]
        self.delta = raw - data.lv.ptr(loc, 4)[1]

    def items(self, off):
        d, sn = self.d, self.loc[0]
        n = d.un('I', (sn, off))[0]
        p = d.lv.ptr((sn, off), 4)
        return [d.un('2I', p, 8 * i) for i in range(n)] if p and n < 256 else []

    def leaves(self, off, depth=0):
        """(inputs mask, analog kind, lo, hi) of the entry at offset off."""
        it = self.items(off)
        acc = [0, 0, -100, 100]

        def s16(x):
            return x - 0x10000 if x >= 0x8000 else x

        def walk(i, neg):
            op = it[i][0]
            if it[i][1] == 0xFF0000:  # bare operand {entry address, 0xFF0000}: a reference (seen under NOT)
                if not neg and op and depth < 6:
                    acc[0] |= self.leaves(op - self.delta, depth + 1)[0]
                return i + 1
            if op in (0xF001, 0xF002):
                return walk(walk(i + 1, neg), neg)
            if op == 0xF00B:
                return walk(i + 1, True)
            if op == 0xF00A:
                if not neg and depth < 6:
                    m, kind, lo, hi = self.leaves(it[i + 1][0] - self.delta, depth + 1)
                    acc[0] |= m
                    if kind and not acc[1]:
                        acc[1:] = [kind, lo, hi]
                return i + 2
            if op in (0xF006, 0xF004):
                if not neg and it[i + 1][0] in KEY_INPUT:
                    acc[0] |= 1 << KEY_INPUT[it[i + 1][0]]
                return i + 2
            if op == 0xF01A:
                if not neg and it[i + 2][0] in PAD_INPUT:
                    acc[0] |= 1 << PAD_INPUT[it[i + 2][0]]
                return i + 3
            if op == 0xF00E:
                if not neg and it[i + 2][0] in DIR_INPUT:
                    acc[0] |= 1 << DIR_INPUT[it[i + 2][0]]
                return i + 3
            if op == 0xF010:
                axis, lo, hi = it[i + 2][0], s16(it[i + 3][0]), s16(it[i + 4][0])
                if not neg and not acc[1]:
                    acc[1:] = [3 if axis == 4 else 4, lo, hi]
                return i + 5
            if op in (0xF011, 0xF012, 0xF013):
                if not neg and not acc[1]:
                    acc[1:] = [1 if op == 0xF011 else 2, s16(it[i + 4][0]), s16(it[i + 5][0])]
                return i + 6
            raise ValueError(f'input item {op:#x}')
        if it:
            walk(0, False)
        return tuple(acc)


def export(ai, path):
    """Write the perso's AI as .ttai (format above). Returns a summary dict."""
    d, lv = ai.d, ai.lv
    beh_index = {}
    lists = []
    for off, flag in ((0, 0), (4, 0x10000)):
        lst = lv.ptr(ai.model, off)
        arr = lv.ptr(lst) if lst else None
        n = d.un('I', lst, 4)[0] if lst else 0
        lists.append(n if arr else 0)
        for i in range(n if arr else 0):
            beh_index[(arr[0], arr[1] + 12 * i)] = flag | i
    persos = {p.loc: k + 1 for k, p in enumerate(d.persos())}
    persos[ai.perso] = 0
    buttons, strings, vectors = {}, {}, {}
    btn = None
    unresolved = collections.Counter()

    def cstr(loc):
        b = bytes(d.un('256B', loc)) if loc[1] + 256 <= len(loc[0].data) else loc[0].data[loc[1]:]
        return b.split(b'\0')[0]

    def resolve(n):
        nonlocal btn
        t, ref = n.type, n.ref
        if t == 14 and ref:
            if btn is None:
                btn = Buttons(d, ref)
            return buttons.setdefault(ref, len(buttons))
        if t == 15 and ref:
            return vectors.setdefault(ref, len(vectors))
        if t == 20 and ref:
            return strings.setdefault(ref, len(strings))
        if t == 24 and ref in persos:
            return persos[ref]
        if t == 25 and ref in ai.state_index:
            return ai.state_index[ref]
        if t == 32 and ref in beh_index:
            return beh_index[ref]
        if ref is not None or t in (14, 15, 20, 24, 25, 32):
            unresolved[t] += 1
            return 0xFFFFFFFF
        return n.param

    nodes, scripts, listing = [], [], []
    names = load_names()

    def add_script(roots, title):
        scripts.append(len(nodes))
        listing.append(f'--- script {len(scripts) - 1}: {title}')

        def rec(n):
            listing.append(f'{len(nodes):6d} {"  " * (n.depth - 1)}{label(ai, n, names)}')
            nodes.append((resolve(n), n.type, n.depth))
            for c in n.children:
                rec(c)
        for r in roots:
            rec(r)
        nodes.append((0, NODE_END, 0))
        return len(scripts) - 1

    behs = []
    for b in ai.behaviours:
        name = f'{"R" if b.reflex else "B"}{b.index}'
        sched = add_script(b.schedule, name + ' schedule') if b.schedule else -1
        first = len(scripts)
        for k, s in enumerate(b.scripts):
            add_script(s, f'{name} rule {k}')
        behs.append((sched, first, len(b.scripts)))

    mind = lv.ptr(lv.ptr(ai.perso, 0xC))
    initial = []
    for off in (4, 8):
        intel = lv.ptr(mind, off)
        cur = lv.ptr(intel, 8) if intel else None
        initial.append(beh_index.get(cur, 0xFFFF) & 0xFFFF)

    def dsg_block(perso):
        # mind +0xC dsgMem {ptr -> ptr def, ptr initial buffer, ptr current buffer}; def {ptr default buffer,
        # ptr infos (16 B {u32 offset, u32 type, ...}), u32 buffer size, u32 count}
        brain = lv.ptr(perso, 0xC)
        m = lv.ptr(brain) if brain else None
        dm = lv.ptr(m, 0xC) if m else None
        dsg, buf = [], b''
        if dm:
            dfn = lv.ptr(lv.ptr(dm))
            infos, (size, count) = lv.ptr(dfn, 4), d.un('2I', dfn, 8)
            src = lv.ptr(dm, 4) or lv.ptr(dfn)
            dsg = [d.un('2I', infos, 16 * i) for i in range(count)]
            buf = bytes(d.un(f'{size}B', src))
        out = struct.pack('<HHI', len(dsg), 0, len(buf))
        for off, typ in dsg:
            out += struct.pack('<IB3x', off, typ)
        return out + buf + b'\0' * (-len(buf) % 4), len(dsg)
    own_dsg, n_dsg = dsg_block(ai.perso)

    # states (family list order = state index)
    from .level import forward_stride
    keys = d.state_anim_keys(ai.states)
    strides = {k: forward_stride(d.read_a3d(lv.ptr(lv.ptr(ai.states[k], 0xC), 0x28))) for k in set(keys) - {-1}}
    st_rows = []
    for s in ai.states:
        nxt = lv.ptr(s, 0x28)
        ar = lv.ptr(s, 0xC)
        n_frames = d.un('H', ar, 4)[0] if ar else 0
        speed = d.un('B', s, 0x3B)[0] or (d.un('B', ar, 6)[0] if ar else 0)
        flags, repeats = d.un('B', s, 0x3C)[0], d.un('B', s, 0x3A)[0]
        trans, prohib = [], []
        e = lv.ptr(s, 0x10)
        while e and len(trans) < 512:
            tgt, via = lv.ptr(e, 0xC), lv.ptr(e, 0x10)
            if tgt in ai.state_index and via in ai.state_index:
                trans.append((ai.state_index[tgt], ai.state_index[via]))
            e = lv.ptr(e)
        e = lv.ptr(s, 0x1C)
        while e and len(prohib) < 512:
            tgt = lv.ptr(e, 0xC)
            if tgt in ai.state_index:
                prohib.append(ai.state_index[tgt])
            e = lv.ptr(e)
        k, me = keys[len(st_rows)], len(st_rows)
        ground = strides.get(k, 0.0) if ai.state_index.get(nxt, -1) == me else 0.0  # looping cycles only
        st_rows.append((ai.state_index.get(nxt, -1), n_frames, speed, flags, trans, prohib, repeats, k & 0xFFFF, ground))
    p3d = lv.ptr(ai.perso)
    init_state = ai.state_index.get(lv.ptr(p3d, 0), 0) if p3d else 0

    out = bytearray(b'TTAI' + struct.pack('<I', TTAI_VERSION))
    out += struct.pack('<I', len(nodes))
    for param, typ, depth in nodes:
        out += struct.pack('<IBBH', param & 0xFFFFFFFF, typ, depth, 0)
    out += struct.pack('<I', len(scripts)) + b''.join(struct.pack('<I', s) for s in scripts)
    out += struct.pack('<HH', *lists)
    for sched, first, n in behs:
        out += struct.pack('<iHH', sched, first, n)
    out += struct.pack('<HH', *initial)
    out += own_dsg
    out += struct.pack('<HH', len(st_rows), init_state)
    for nxt, nf, speed, flags, trans, prohib, repeats, anim, ground in st_rows:
        out += struct.pack('<hHBBHHBBHHf', nxt, nf, speed, flags, len(trans), len(prohib), repeats, 0, anim, 0, ground)
        out += b''.join(struct.pack('<HH', *t) for t in trans) + b''.join(struct.pack('<H', p) for p in prohib)
    out += struct.pack('<HH', len(buttons), 0)
    button_rows = []
    for ref in buttons:
        mask, kind, lo, hi = btn.leaves(ref[1])
        button_rows.append((ref[1], mask, kind, lo, hi))
        out += struct.pack('<HBBhh', mask, kind, 0, lo, hi)
    out += struct.pack('<H', len(strings))
    for ref in strings:
        s = cstr(ref)
        out += struct.pack('<H', len(s)) + s
    out += struct.pack('<H', len(vectors))
    for ref in vectors:
        out += struct.pack('<3f', *d.un('3f', ref))
    used = sorted({p for p, t, _ in nodes if t == 24 and p not in (0, 0xFFFFFFFF)})
    by_id = {k: loc for loc, k in persos.items()}
    fix = [x[0] for x in d.perso_in_fix]
    out += struct.pack('<HH', len(used), 0)
    for k in used:
        role = fix.index(by_id[k]) if by_id[k] in fix[1:3] else 0
        out += struct.pack('<HBB', k, role, 0) + dsg_block(by_id[k])[0]
    with open(path, 'wb') as f:
        f.write(out)
    with open(os.path.splitext(path)[0] + '_ai.txt', 'w', encoding='utf-8') as f:
        f.write('\n'.join(listing) + '\n')
    return {'nodes': len(nodes), 'scripts': len(scripts), 'behaviours': lists, 'initial': initial,
            'dsgvars': n_dsg, 'persos': used, 'states': len(st_rows), 'initial_state': init_state,
            'transitions': sum(len(r[4]) for r in st_rows),
            'moving': [i for i, r in enumerate(st_rows) if r[8] > 0], 'buttons': button_rows,
            'strings': [cstr(r) for r in strings], 'unresolved': dict(unresolved), 'bytes': len(out)}

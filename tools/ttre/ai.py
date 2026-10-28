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
import math
import struct
import zlib
from dataclasses import dataclass, field

from . import chunks

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
        self.beh_names = {}  # behaviour location -> 'B<k>' / 'R<k>' (listing labels)
        self.buttons = None  # Buttons, built from the first button node the listing meets
        for off, reflex in ((0, False), (4, True)):
            lst = lv.ptr(self.model, off) if self.model else None
            arr = lv.ptr(lst) if lst else None
            n = data.un('I', lst, 4)[0] if lst else 0
            for i in range(n if arr else 0):
                self.beh_names[(arr[0], arr[1] + 12 * i)] = f'{"R" if reflex else "B"}{i}'
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
        if n.type == 32 and n.ref in ai.beh_names:
            return ai.beh_names[n.ref]
        if n.type == 20:
            raw = n.ref[0].data[n.ref[1]:n.ref[1] + 64].split(b'\0')[0]
            return repr(raw.decode('latin-1'))
        if n.type == 14:
            if ai.buttons is None:
                ai.buttons = Buttons(ai.d, n.ref)
            mask, kind, lo, hi = ai.buttons.leaves(n.ref[1])
            keys = '|'.join(k for i, k in enumerate(INPUTS) if mask >> i & 1)
            analog = f'{["", "angle", "magnitude", "x", "y"][kind]}{lo}..{hi}' if kind else ''
            return f'btn({" ".join(x for x in (keys, analog) if x) or hex(n.ref[1])})'
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
.ttai: one perso's AI (the player's), with every pointer resolved to an index, in the chunk container of chunks.py.
Little-endian. Every chunk is required; the per-state tables hold one row per STAT state, in order.
  SCPT  u32 n_nodes, n_nodes x { u32 param, u8 type, u8 depth, u16 0 }
          Scripts are runs of nodes in the engine's order, each closed by an end node (type 8, depth 0).
          Resolved params: 14 button -> button index; 15 constant vector -> vector index; 20 string -> string index;
          24 perso -> 0 = this perso, k + 1 = level perso k (LevelData.persos()); 25 state -> state index;
          32 behaviour -> normal index, or 0x10000 | reflex index. Unresolvable pointers -> 0xFFFFFFFF.
        u32 n_scripts, n_scripts x u32 first node
        u16 n_normal, u16 n_reflex, (n_normal + n_reflex) x { i32 schedule script (-1 none), u16 first script, u16 n }
        u16 initial normal behaviour, u16 initial reflex (0xFFFF none)
        dsgvar block (this perso's) = { u16 n_dsgvars, u16 0, u32 buffer size, n x { u32 offset, u8 type (engine
          dsgvar type 0-16), u8 x3 }, buffer bytes (initial values), padded to 4 }
  STAT  u16 n_states, u16 initial state, n x { i16 next (-1 none), u16 n_frames, u8 speed (frames/s, 0 = 30),
          u8 flags (State +0x3C: 1 = only listed transitions allowed), u16 n_transitions, u16 n_prohibited,
          u8 repeats (State +0x3A: extra plays before the next state), u8 0, u16 anim (first state with the same
          animation: the .ttlv's Ed animation "s<anim>"), u16 card (the state's mechanics ID card, State +0x30, as
          an index into CARD; 0xFFFF none), f32 ground (units per animation frame Ed moves forward while the animation
          plays on the ground: level.forward_stride of looping states, 0 = in place), f32 drop (how far the state's
          pose reaches below the perso's origin, >= 0, first animation frame; the skier's riding states 0.63),
          n_transitions x { u16 target, u16 via }, n_prohibited x u16 state }
  BTNS  u16 n, u16 0, n x { u16 inputs (mask of TC_AI_IN_*), u8 analog (0 none, 1 stick angle, 2 stick magnitude,
          3 stick x, 4 stick y), u8 0, i16 lo, i16 hi (stick range, -100..100, that counts as held) }
  STRS  u16 n, n x { u16 len, bytes }
  VECS  u16 n, n x f32[3]
  PRSO  u16 n, u16 0, n x { u16 id (as in perso nodes), u8 role (1 = perso-in-fix 1, the camera; 2 = perso-in-fix 2;
          0 other), u8 0, dsgvar block }: the other persos the scripts reference (Dot(perso, dsgvar) reads and writes
          their dsgvars; their own AI does not run)
  CARD  u16 n, u16 0, n x { u32 type (0-12), u32 n_words, n_words x u32 (the card after its type word, raw: mostly
          f32, some bytes/ints; sizes from MEC_hidcard_004b7d04) }
  BODY  root motion and collision spheres: u32 n_rows, n_rows x f32[3] (each A3D frame's own vector, units per
          animation frame in the perso's axes); per state { u32 first row, u16 n_rows, u16 sphere set (0xFFFF none) };
          u16 n_sets, u16 0, n_sets x { u16 n, u16 0, n x { f32 centre[3], f32 radius } } (ZDM, ai.zdm_sets)
  ZDMF  the BODY parts' collide flags (their GameMaterial's collide material identifier, u16 at +2; FUN_00420610 /
          FUN_0043bf70: 0x10 no collision, 8 floors only, 1 may step up) and kinds: u16 n_sets, then per set u16 n,
          n x { u16 flags, u16 collide element type (7 sphere, 5 point: radius 0 in BODY), f32 rebound, f32 grip
          (its own mechanics material: the responses multiply them with the obstacle's) } (ai.zone_parts)
  TRIG  u16 n, u16 0, n x { f32 position[3], f32 radius, u16 behaviour, u16 guard dsgvar, u32 sleep ms }
          (ai.proximity_triggers)
  PORT  portals (docs/PORTALS.md): u16 n, u16 0, n x { f32 position[3], f32 radius, f32 rows[9] (its 3x3, row-major
          as stored), u16 perso (as perso nodes), u8 fire (0: when Ed's dsgvar fire_dsg == fire_value, 1: when the
          screen is covered, 2: never, capture only: the player's scripts do the rest), u8 0, u16 fire_dsg, u16 0,
          i32 fire_value, u16 n_capture, u16 n_fire, (n_capture + n_fire) x op { u8 op, u8 kind, u16 a, u32 b, u32 c,
          f32 v[3] } } (ops: ai.POP_*, see portals());
        u16 n_names, n x { u16 len, bytes }: level and place names the ops refer to (lowercase)
  PLAC  u16 n, u16 0, n x { u16 len, name bytes (lowercase), f32 position[3], f32 heading }: every level perso by
          instance name, for PROC_29 arrivals into this level (other levels' scripts name them)
  TRAN  screen transitions: u16 cover threshold, u16 cover range, u16 reveal threshold, u16 reveal range (random r in
          [0, range]: r > threshold -> effect B), 4 x { u16 n_variants, u16 0, f32 fields[9] (effect +0x10..+0x30),
          n_variants x { u16 len, texture path relative to the .ttai } } for cover A, cover B, reveal A, reveal B
  SNDS  sounds (tools/ttre/sfx.py, docs/NOTES.md "Sound events"): u32 n_rows, n_rows x { u16 frame, u16 event (global
          sound event = Bnk_0 event index), u8 first, u8 period, u16 0 }: the type-0 events of the states' animations;
          per state { u32 first row, u16 n_rows, u16 0 }. Sound event nodes (type 34, PlaySound3D's argument) hold the
          event index too. first / period = the event's counter (event +0xB / +0xC, FUN_00436560 / FUN_004366c0): it
          starts at first when the state starts; reaching the frame, 0 fires the event and reloads period, else it
          counts down, 0xFF stays. 0xFF first: fired only when a script changes the state (FUN_00436600).
  MECE  the animations' mechanics requests (event type 1; FUN_004366c0 -> FUN_00436910 pushes {mode = value, the
          state's card} when the frame is reached; Ed's jump, state 43, pushes mode 7 at frame 0: the take-off):
          u32 n_rows, n_rows x { u16 frame, u16 mode, u8 first, u8 period, u16 0 } (counters as SNDS); per state
          { u32 first row, u16 n_rows, u16 0 }
  WATR  water (water_planes, docs/NOTES.md "Swimming"): u16 n, u16 0, n x { f32 z, f32 gravity, f32 density, f32 drag
          coefficient (the environment below the surface), u16 n_boxes, u16 0, n_boxes x { f32 min[3], f32 max[3] }
          (the bounding boxes of the sectors that list the surface) }
  WPTS  water sample points (water_points): u16 n_sets, u16 0, n_sets x { u16 n, u16 0, n x { u32 kind 1-6,
          f32 point[3] (the perso's axes) } }; per state u16 set (0xFFFF none)
  ZONE  the portals' zones (portal_zones, docs/NOTES.md "ZDx zones"): u16 n (= PORT's n), u16 0, per portal
          u16 n_prims, u16 0, then if n_prims: f32 inverse[12] (the perso's world matrix inverted, row-major 3x4) and
          n_prims x { u32 kind (7 sphere: a = centre, b.x = radius; 8 box: a = min, b = max), f32 a[3], f32 b[3] } in
          the perso's axes: the capture is "the player's origin inside" (COND_13) instead of the radius
  FSND  the floors' sound types (docs/NOTES.md "Footsteps"): u32 n (= the .ttlv's collision triangles), n x u8
          (GameMaterial +8 of the triangle's material, 0xFF none)
  DRYB  the dry sectors' boxes (dry_boxes; none in a level without water): u16 n, u16 0, n x { f32 min[3],
          f32 max[3] }: a point inside one, smaller than every water box holding the point, is dry
  LINK  u32 n_triangles, u32 CRC-32 of the .ttlv's COLL chunk: FSND indexes those triangles, so the core ignores it
          when the level's collision differs (an imported level using a donor's .ttai)
"""
import re

# ID card sizes in bytes by type (constructor MEC_hidcard_004b7d04), including the type word
CARD_SIZES = {0: 0x40, 1: 0x48, 2: 0x50, 3: 0x10, 4: 0x40, 5: 0x50, 6: 0x40, 7: 0x28, 8: 8, 9: 0x1C, 10: 0x14,
              11: 0x34, 12: 0x18}
NODE_END = 8
TEX_ROOT = 'files/out/Textures'  # the effect textures of the screen transitions
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


def pose_drops(d, perso, keys):
    """{animation key: how far its first frame reaches below the perso's origin (>= 0)} for the player's state
    animations (LevelData.ed: the player's objects)."""
    from .level import mat_mul
    ed = d.ed(state_anims=True) if perso == d.player() else None
    if not ed:
        return {}
    p, _, anims = ed
    out = {}
    for name, a, _ in anims:
        if not (name.startswith('s') and name[1:].isdigit()):
            continue
        low = 0.0
        for o, m in a.pose(0):
            if o in p.objects:
                geo, scale = p.objects[o]
                t = mat_mul(m, scale)
                for v in geo.vertices:
                    low = min(low, t[8] * v[0] + t[9] * v[1] + t[10] * v[2] + t[11])
        out[int(name[1:])] = -low
    return out


def root_motion(d, states, keys):
    """{animation key: [(x, y, z), ...] per frame}: the A3D frames' own vector (frame +2 -> vectors), the
    displacement the perso makes per animation frame in its local axes (Ed's front = -Y). Walk (state 1)
    (0, -0.08, 0), run (10) (0, -0.2, 0), backward walk (2) +0.08; take-off, landing, idle: none. Times the
    animation's frame rate it gives the speeds hand-tuned earlier (walk 0.08 x 30 x 139 % = 3.3 u/s, run 0.2 x 45 =
    9 u/s), so this is the ground speed of states whose AI sets no move direction. The frame quaternion next to it
    is the identity for every state of Ed's (not exported). Frames without a vector (index out of range) are 0."""
    lv, out = d.lv, {}
    for k in set(keys) - {-1}:
        a = d.read_a3d(lv.ptr(lv.ptr(states[k], 0xC), 0x28))
        rows = [tuple(a.vec[f[1]]) if f[1] < len(a.vec) else (0.0, 0.0, 0.0) for f in a.frames]
        if any(any(c != 0 for c in r) for r in rows):
            out[k] = rows
    return out


def zdd_zone(d, loc, k):
    """A perso's ZDD zone k (0-based; collide set +0, the exe's list 0) as [(7, centre, (r, 0, 0)) | (8, min, max)] in
    the perso's axes, or None (no such zone, or one with other element types: triangle zones are not exported)."""
    lv, un = d.lv, d.un
    cs = lv.ptr(loc, 0x14)
    head = lv.ptr(cs) if cs else None
    e = lv.ptr(head) if head else None
    for _ in range(k):
        e = lv.ptr(e) if e else None
    g = lv.ptr(e, 0xC) if e else None
    if not g:
        return None
    verts, types, els, prims = lv.ptr(g, 4), lv.ptr(g, 0x18), lv.ptr(g, 0x1C), []
    for i in range(un('I', g, 0x14)[0]):
        t, el = un('H', types, 2 * i)[0], lv.ptr(els, 4 * i)
        n, entries = un('I', el)[0], lv.ptr(el, 4)
        if t == 7:
            for j in range(n):
                prims.append((7, un('3f', verts, 12 * un('H', entries, 12 * j)[0]), (un('f', entries, 12 * j + 4)[0], 0, 0)))
        elif t == 8:
            for j in range(n):
                lo, hi = un('2H', entries, 8 * j)
                prims.append((8, un('3f', verts, 12 * lo), un('3f', verts, 12 * hi)))
        else:
            return None
    return prims or None


def portal_zones(ai, gates):
    """Per portal (as portals() returns them): (inverse world 3x4, ZDD zone primitives) when its perso's scripts test
    the player with COND_13(Me, k, player) (GAM_ZoneHolds_00420280: the player's origin inside ZDD zone k, 1-based),
    else None. Totalski's holes: a 110 x 130 slab 4..12 below the opening, not the radius-8 stand-in."""
    d = ai.d
    persos = d.persos()
    players = {ai.perso} | ({d.perso_in_fix[0][0]} if d.perso_in_fix else set())
    found = {}

    def walk(nodes):
        for n in nodes:
            c = n.children
            if (n.type == 1 and n.param == 13 and len(c) == 3 and c[0].type == 0 and c[0].param == 4 and c[1].type == 12
                    and ((c[2].type == 24 and c[2].ref in players) or (c[2].type == 0 and c[2].param == 5))):
                yield c[1].param
            yield from walk(c)

    out = []
    for g in gates:
        p = persos[g[3] - 1]
        if p.loc not in found:
            ks = set()
            try:
                other = AI(d, p.loc)
                for b in other.behaviours:
                    for sc in b.scripts + ([b.schedule] if b.schedule else []):
                        ks.update(walk(sc))
            except Exception:
                pass
            found[p.loc] = min(ks) if len(ks) == 1 else None
        k = found[p.loc]
        prims = zdd_zone(d, p.loc, k - 1) if k else None
        out.append((mat_inverse(p.world), prims) if prims else None)
    return out


def mat_inverse(m):
    """Row-major 3x4 (x' = M x + t) -> its inverse, row-major 3x4."""
    a, b, c, tx, d_, e, f, ty, g, h, i, tz = m
    det = a * (e * i - f * h) - b * (d_ * i - f * g) + c * (d_ * h - e * g)
    r = [(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
         (f * g - d_ * i) / det, (a * i - c * g) / det, (c * d_ - a * f) / det,
         (d_ * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d_) / det]
    t = [-(r[3 * k] * tx + r[3 * k + 1] * ty + r[3 * k + 2] * tz) for k in range(3)]
    return (r[0], r[1], r[2], t[0], r[3], r[4], r[5], t[1], r[6], r[7], r[8], t[2])


def water_points(d, states):
    """Per state its water sample points (State +0x34: a list of {next, .., +0xC point (the perso's axes), +0x18 kind
    1-6, +0x1C value}; GAM_00437900 sets each value to the water flag of the environment there, the AI's zone
    conditions read them): (distinct point sets [[(kind, (x, y, z))]], per-state set index or 0xFFFF). Ed: standing
    z 2.0 / 1.7 / 0.8 / 0.55 / 0.35 / 0.1 (head to feet), swimming 1.4 .. -0.2."""
    lv, un = d.lv, d.un
    sets, index = {}, []
    for st in states:
        head = lv.ptr(st, 0x34)
        e, pts = (lv.ptr(head) if head else None), []
        while e and len(pts) < 8:
            pts.append((un('I', e, 0x18)[0], un('3f', e, 0xC)))
            e = lv.ptr(e)
        index.append(sets.setdefault(tuple(pts), len(sets)) if pts else 0xFFFF)
    return [list(p) for p in sets], index


def sector_box(d, so):
    """The bounding box (min xyz, max xyz) of a sector's static geometry (empty: min > max)."""
    lo, hi = [1e9] * 3, [-1e9] * 3
    for o in d.walk(so):
        if o.kind != 'ipo' or not o.lods:
            continue
        m = o.world
        for v in o.lods[0].vertices:
            w = [m[4 * i] * v[0] + m[4 * i + 1] * v[1] + m[4 * i + 2] * v[2] + m[4 * i + 3] for i in range(3)]
            lo = [min(a, b) for a, b in zip(lo, w)]
            hi = [max(a, b) for a, b in zip(hi, w)]
    return lo, hi


def dry_boxes(d):
    """The boxes of the sectors that list no water surface and reach beyond the water boxes they overlap (v11). The exe
    looks the environment up in the perso's own sector; the port has no sectors: a point in such a box smaller than the
    water boxes holding it stays dry (sud0: the way into Doc's cave, under the river's surface height)."""
    lv, un, out = d.lv, d.un, []
    water = [b for _, _, boxes in water_planes(d) for b in boxes]
    inside = lambda a, b: all(b[0][k] - 1 <= a[0][k] and a[1][k] <= b[1][k] + 1 for k in range(3))
    for so in d.walk():
        if so.kind != 'sector':
            continue
        data = lv.ptr(so.loc, 4)
        if data and lv.ptr(data, 0x10) and un('I', data, 0x18)[0]:
            continue
        lo, hi = sector_box(d, so)
        box = (tuple(lo), tuple(hi))
        # a sector wholly inside a water box is a piece of that water's area (sud0: rocks in the river), not a
        # neighbour overlapping its edge: kept wet (the C check under the visible water lost 11 of 63 otherwise)
        if lo[0] <= hi[0] and not any(inside(box, w) for w in water):
            out.append(box)
    return out


def water_planes(d):
    """The level's water surfaces as the mechanics see them (docs/NOTES.md "Swimming"): [(z, (gravity, density, drag),
    [(min xyz, max xyz) per sector])]. Sector data +4 lists the sector's mechanical environments, +0x10 its surfaces; the
    env lookup FUN_00451b90 takes environment k + 1 below the k-th surface (in list order), whose height is its +0x5C
    matrix's z (FUN_0045d880). MecEnv: +0 g, +0x10 drag coefficient, +0x14 density. The exe asks the perso's sector;
    the port has no sectors, so each surface carries the bounding boxes of the static geometry of the sectors listing it
    (sud0: the river 15 sectors, the falls' foot, the top pool ...)."""
    lv, un = d.lv, d.un
    planes = {}  # surface -> [z, env, boxes]
    for so in d.walk():
        if so.kind != 'sector':
            continue
        data = lv.ptr(so.loc, 4)
        if not data:
            continue
        envs, node = [], lv.ptr(data, 4)
        while node and len(envs) < 16:
            e = lv.ptr(node)
            envs.append(lv.ptr(e) if e else None)
            node = lv.ptr(node, 4)
        srf, n, k = lv.ptr(data, 0x10), un('I', data, 0x18)[0], 0
        while srf and k < min(n, 16):
            sp = lv.ptr(srf)
            env = envs[k + 1] if k + 1 < len(envs) else None
            if sp and env:
                lo, hi = sector_box(d, so)
                p = planes.setdefault(sp, [un('I21f', (sp[0], sp[1] + 0x5C))[3], None, []])
                g, drag, density = un('f', env)[0], un('f', env, 0x10)[0], un('f', env, 0x14)[0]
                p[1] = p[1] or (g, density, drag)
                if lo[0] <= hi[0]:
                    p[2].append((tuple(lo), tuple(hi)))
            srf = lv.ptr(srf, 4)
            k += 1
    return [(z, env, boxes) for z, env, boxes in planes.values() if boxes]


def collide_flags(d, gm):
    """A GameMaterial's collide flags: its collide material (+0xC) u16 at +2 (FUN_0042f770; the same field as
    level.collide_id), 0 without one. On Ed's ZDM spheres (bar): 1 the feet and lower body (may step up), 2 / 4 the
    body and head, 0x11 the spheres below the feet that never collide (FUN_00420610 leaves 0x10 out)."""
    cm = d.lv.ptr(gm, 0xC) if gm else None
    return d.un('H', cm, 2)[0] if cm else 0


def own_material(d, gm):
    """A GameMaterial's mechanics material (+4) -> (rebound [1], grip [2]); no material: (1, 1), the exe's defaults
    (DAT_004dc9c4..d4). Ed's body spheres: rebound 0.85, grip 0; his feet none; some lower-body spheres grip 0.02."""
    mm = d.lv.ptr(gm, 4) if gm else None
    return tuple(d.un('2f', mm, 4)) if mm else (1.0, 1.0)


def zone_parts(d, g):
    """A ZDx zone's GeometricObjectCollide -> its mechanics parts [(cx, cy, cz, r, flags, element type, rebound, grip)]
    (own_material): each sphere of
    its type 7 elements (entries inline at element +8: {u16 centre vertex, u16, f32 radius, ptr game material}), then
    its points: element type 5 (Points), whose vertices FUN_00470b40 sweeps as moving points, radius 0, flags from the
    vertex's material (object +0xC, one GameMaterial per vertex; FUN_004702d0). Ed's zone 9 is a point 1.3 ahead and 2.0
    up with flag 8: it lands on ledge tops and GroundMaterialIs(8) hangs him (docs/DECOMP.md "Ledge hang")."""
    lv, un = d.lv, d.un
    if not g:
        return []
    nv, ne = un('I', g)[0], un('I', g, 0x14)[0]
    types = un(f'{ne}H', lv.ptr(g, 0x18)) if 0 < ne < 64 and lv.ptr(g, 0x18) else ()
    verts, vmats = lv.ptr(g, 4), lv.ptr(g, 0xC)
    parts, in_spheres = [], set()
    for k, t in enumerate(types):
        el = lv.ptr(lv.ptr(g, 0x1C), 4 * k)
        if t == 7 and el:
            for j in range(min(un('I', el)[0], 16)):
                c, = un('H', el, 8 + 12 * j)
                if c < nv:
                    in_spheres.add(c)
                    gm = lv.ptr(el, 8 + 12 * j + 8)
                    parts.append(un('3f', verts, 12 * c) + un('f', el, 8 + 12 * j + 4) + (collide_flags(d, gm), 7) +
                                 own_material(d, gm))
    if 5 in types:
        for v in range(min(nv, 16)):
            if v not in in_spheres:
                gm = lv.ptr(vmats, 4 * v) if vmats else None
                parts.append(un('3f', verts, 12 * v) + (0.0, collide_flags(d, gm), 5) + own_material(d, gm))
    return parts


def zdm_sets(d, perso, n_states):
    """Ed's mechanics collision body per state: (list of distinct sets of parts [(cx, cy, cz, r, flags, element type
    7 sphere / 5 point), ...] (zone_parts), per-state set index or 0xFFFF). Perso +0x14 = collide set {ptr ZDD list, ptr ZDE list, ptr ZDM list, ptr ZDR list (0 for Ed),
    then the activation lists of the same four}. A zone list = head {ptr first, ptr last, u32 n} of entries
    {ptr next, ptr prev, ptr head, ptr GeometricObjectCollide}; an activation list has one entry per state
    (+0x10 = state index, +0xC = head of {.., +0xC = zone index} entries). Zones are mostly one sphere (element type
    7: vertex = centre, radius at element +0xC). Ed standing: r 0.25 at (0, 0, 0.25) (touches the origin), 0.375 at
    (0, -0.1, 0.75), 0.53 at (0, -0.15, 1.28), 0.3 at (0, -0.3, 1.95); his model is 2.17 tall. Zone 9 (element
    type 5) is his hand point (zone_parts). A collide set with byte +0x36 set (the skier) has no ZDM: see below."""
    lv, un = d.lv, d.un
    cs = lv.ptr(perso, 0x14) if perso else None
    if cs and un('B', cs, 0x36)[0]:
        # Collide set byte +0x36 set (GAM_00431720; the skier): the mechanics sweep one sphere centred on the
        # perso's origin (FUN_0043d480 -> FUN_004717d0) of radius +0x30 x 0.5 (DAT_004d2928; skier 1.3 -> 0.65),
        # whatever the state. His riding poses reach 0.61-0.65 below the origin: the board rides on that sphere.
        r = un('f', cs, 0x30)[0] * 0.5
        return ([[(0.0, 0.0, 0.0, r, 0, 7, 1.0, 1.0)]], [0] * n_states) if r > 0 else ([], [0xFFFF] * n_states)
    if not cs or not lv.ptr(cs, 8) or not lv.ptr(cs, 0x18):
        return [], [0xFFFF] * n_states
    zones, e = [], lv.ptr(lv.ptr(cs, 8))
    while e and len(zones) < 256:
        zones.append(zone_parts(d, lv.ptr(e, 0xC)))
        e = lv.ptr(e)
    sets, per_state, e = {}, [0xFFFF] * n_states, lv.ptr(lv.ptr(cs, 0x18))
    while e:
        st, act = un('I', e, 0x10)[0], lv.ptr(e, 0xC)
        z, f = [], lv.ptr(act) if act else None
        while f and len(z) < 32:
            i = un('I', f, 0xC)[0]
            if i < len(zones):
                z.extend(zones[i])
            f = lv.ptr(f)
        if st < n_states and z:
            per_state[st] = sets.setdefault(tuple(z), len(sets))
        e = lv.ptr(e)
    return [list(s) for s in sets], per_state


def perso_dsg_value(d, perso, k):
    """Current value of dsgvar k of a perso (float or int), or None."""
    lv, un = d.lv, d.un
    mind = lv.ptr(lv.ptr(perso, 0xC)) if lv.ptr(perso, 0xC) else None
    dm = lv.ptr(mind, 0xC) if mind else None
    dfn = lv.ptr(lv.ptr(dm)) if dm and lv.ptr(dm) else None
    if not dfn:
        return None
    infos, (size, count) = lv.ptr(dfn, 4), un('2I', dfn, 8)
    cur = lv.ptr(dm, 8) or lv.ptr(dm, 4) or lv.ptr(dfn)
    if k >= count or not infos or not cur:
        return None
    off, typ = un('2I', infos, 16 * k)
    return un('f', cur, off)[0] if typ == 7 else un('i', cur, off)[0]


def proximity_triggers(ai):
    """Level persos whose starting schedule is the obstacle pattern (Totalski's trees, cacti and standing icicles):
        Proc9(1); PROC_18/19/20(Me, radius)          -- the perso's AI only runs while the main actor is within
                                                        radius (std game +0x2C = radius^2; GAM_0042b520 compares it
                                                        with GAM_0042a680's squared distance between the origins)
        If Equal(Dot(KW_5, dsg<g>), 0)               -- KW_5 = the main actor; dsg<g> its "just crashed" flag
        Then PROC_237(KW_5, <behaviour>)             -- force the main actor's normal behaviour (the skier's B9)
        META_29(<ms>)                                -- wait (the If is skipped while the flag is set, the wait not)
    The main actor is this AI's perso (the player). Returns [(position, radius, behaviour index, guard dsgvar or
    0xFFFF, sleep ms, (family, instance))]. Running other persos' AI in general is not done yet; this extracts the
    one effect these props have on the player."""
    d, lv = ai.d, ai.lv
    normal = {ref: int(name[1:]) for ref, name in ai.beh_names.items() if name.startswith('B')}
    out = []
    for p in d.persos():
        if p.loc == ai.perso or not lv.ptr(p.loc, 0xC):
            continue
        try:
            other = AI(d, p.loc)
        except Exception:
            continue
        mind = lv.ptr(lv.ptr(p.loc, 0xC))
        intel = lv.ptr(mind, 4) if mind else None
        cur = lv.ptr(intel, 8) if intel else None
        name = other.beh_names.get(cur)
        if not name or not name.startswith('B'):
            continue
        sched = other.behaviours[int(name[1:])].schedule
        radius = beh = guard = sleep = None
        def kw(n, k):
            return n.type == 0 and n.param == k
        def walk(ns):
            nonlocal radius, beh, guard, sleep
            for n in ns:
                c = n.children
                if n.type == 4 and n.param in (18, 19, 20) and len(c) >= 2 and kw(c[0], 4):
                    v = c[1]
                    if v.type in (10, 11):
                        radius = perso_dsg_value(d, p.loc, v.param)
                    elif v.type == 12:
                        radius = struct.unpack('<i', struct.pack('<I', v.param))[0]
                    elif v.type == 13:
                        radius = struct.unpack('<f', struct.pack('<I', v.param))[0]
                if n.type == 1 and n.param == 3 and len(c) == 2 and c[0].type == 2 and c[0].param == 0xC and \
                        len(c[0].children) == 2 and kw(c[0].children[0], 5) and c[0].children[1].type in (10, 11) and \
                        c[1].type == 12 and c[1].param == 0:
                    guard = c[0].children[1].param
                if n.type == 4 and n.param == 237 and len(c) == 2 and kw(c[0], 5) and c[1].type == 32:
                    beh = normal.get(c[1].ref)
                if n.type == 5 and n.param == 29 and beh is not None and sleep is None and c and c[0].type == 12:
                    sleep = c[0].param
                walk(c)
        walk(sched or [])
        if radius and radius > 0 and beh is not None:
            pos = tuple(p.world[3::4][:3])
            out.append((pos, float(radius), beh, 0xFFFF if guard is None else guard, sleep or 0,
                        d.perso_names(p.loc)[:3:2]))
    return out


def perso_dsg(d, perso, k):
    """Typed value of a perso's dsgvar k (its current buffer): ('int', v), ('float', v), ('vec', (x, y, z)) for vectors
    and waypoints (type 8: {u32, f32 position[3], f32 radius, ...}), ('perso', loc), or None. Widths as tc_ai.c
    dsg_width: types 0-2 one byte (0/1 signed), 3-4 two (3 signed), 7 f32, 12 vec3, the rest 4."""
    lv, un = d.lv, d.un
    mind = lv.ptr(lv.ptr(perso, 0xC)) if lv.ptr(perso, 0xC) else None
    dm = lv.ptr(mind, 0xC) if mind else None
    dfn = lv.ptr(lv.ptr(dm)) if dm and lv.ptr(dm) else None
    if not dfn:
        return None
    infos, (size, count) = lv.ptr(dfn, 4), un('2I', dfn, 8)
    cur = lv.ptr(dm, 8) or lv.ptr(dm, 4) or lv.ptr(dfn)
    if k >= count or not infos or not cur:
        return None
    off, typ = un('2I', infos, 16 * k)
    if typ in (0, 1):
        return ('int', un('b', cur, off)[0])
    if typ == 2:
        return ('int', un('B', cur, off)[0])
    if typ in (3, 4):
        return ('int', un('h' if typ == 3 else 'H', cur, off)[0])
    if typ == 7:
        return ('float', un('f', cur, off)[0])
    if typ == 12:
        return ('vec', un('3f', cur, off))
    if typ == 8:
        wp = lv.ptr(cur, off)
        return ('vec', un('3f', wp, 4)) if wp else None
    if typ == 10:
        return ('perso', lv.ptr(cur, off))
    return ('int', un('i', cur, off)[0])


# Portal ops (reader: core/src/tc_ai.c, TC_AI_POP_*). Fixed 24-byte records { u8 op, u8 kind, u16 a, u32 b, u32 c,
# f32 v[3] }.
POP_GUARD, POP_SET, POP_FORCE, POP_FORCE_REFLEX, POP_COVER, POP_WAIT, POP_TELEPORT, POP_CAMERA, POP_LEVEL = range(1, 10)


def portals(ai, strings):
    """Level persos that move the player elsewhere: the arches (gen_arche: blue in-level teleports, red level changes)
    and the zone teleports (genplainenord / gencanyon ...). They never move Ed themselves; their scripts do, in order:
      capture (Ed within the perso's activation radius, PROC_18(Me, r), or an explicit Less(FUNC_4(Ed), dsg) test;
               optionally only while Equal(Dot(Ed, dsg<k>), v)): writes Ed's dsgvars (dsg99 = Me, dsg136 = arrival
               heading, ...), forces his normal / reflex behaviour (PROC_237 / PROC_238: the walk into the arch B66,
               or the wait B15), maybe starts the cover transition (GameAction2);
      fire     (If Equal(Dot(Ed, dsg103), 1) - Ed's B106 sets it once the cover is done -, or If GameState63):
               more dsgvar writes and forcing, waits (META_29), then PROC_8 (teleport Ed and the camera to two
               positions), ChangeLevel(name) or PROC_29(name, n, place, camera place).
    Each perso's scripts are evaluated statically against its own dsgvars (the If chains on them pick one branch; an
    undecidable condition takes the branch with the capture, which opens the collectible gates) and flattened into
    op lists. Returns [(position, radius, 3x3 rows, perso id, fire kind, fire dsg, fire value, capture ops, fire ops,
    (family, instance))]; strings: dict name -> index, extended here. Persos whose effect does not fit are skipped
    (GenTransitionSubMap's zone test COND_13, Doc's cutscenes, the game-over screen ...)."""
    d, lv = ai.d, ai.lv
    fix_ed = d.perso_in_fix[0][0] if d.perso_in_fix else None
    if ai.perso != fix_ed:
        return ski_portals(ai, strings)
    persos = {p.loc: k + 1 for k, p in enumerate(d.persos())}
    normal = {ref: int(n[1:]) for ref, n in ai.beh_names.items() if n.startswith('B')}
    reflex = {ref: int(n[1:]) for ref, n in ai.beh_names.items() if n.startswith('R')}

    def cstr(loc):
        b = loc[0].data[loc[1]:loc[1] + 256]
        return b.split(b'\0')[0]

    def sid(s):
        return strings.setdefault(s.lower(), len(strings))

    def is_ed(n):
        return n.type == 24 and n.ref == fix_ed

    def ed_dsg(n):
        """Dot(Ed, dsg<k>) -> k, else None."""
        c = n.children
        if n.type == 2 and n.param == 12 and len(c) == 2 and is_ed(c[0]) and c[1].type in (10, 11):
            return c[1].param
        return None

    out = []
    for p in d.persos():
        if p.loc == ai.perso or not lv.ptr(p.loc, 0xC):
            continue
        try:
            other = AI(d, p.loc)
        except Exception:
            continue
        pos = tuple(p.world[3::4][:3])
        env = {}

        def value(n):
            """Static value of an expression: ('int'|'float', v), ('vec', xyz), ('self',), or None."""
            t, c = n.type, n.children
            if t == 12:
                return ('int', struct.unpack('<i', struct.pack('<I', n.param))[0])
            if t == 13:
                return ('float', struct.unpack('<f', struct.pack('<I', n.param))[0])
            if t in (10, 11):
                return env.get(n.param) or perso_dsg(d, p.loc, n.param)
            if t == 0 and n.param == 4:
                return ('self',)
            if t == 3 and n.param == 0 and not c:
                return ('vec', pos)
            if t == 3 and n.param == 3 and c:
                v = value(c[0])
                return v if v and v[0] == 'vec' else ('vec', pos) if v == ('self',) else None
            return None

        def decide(cond):
            """True / False for a condition on this perso's own dsgvars, None when it depends on anything else."""
            c = cond.children
            if cond.type == 1 and cond.param == 3 and len(c) == 2 and ed_dsg(c[0]) is None:
                a, b = value(c[0]), value(c[1])
                if a and b and a[0] in ('int', 'float') and b[0] in ('int', 'float'):
                    return a[1] == b[1]
            return None

        def contains(stmts, pred):
            return any(pred(n) or contains(n.children, pred) for n in stmts)

        def is_capture(n):
            return n.type == 4 and n.param == 237 and len(n.children) == 2 and is_ed(n.children[0])

        def is_action(n):
            return n.type == 4 and n.param in (7, 8, 29)

        def flatten(stmts):
            """Ops of a statement list; static Ifs resolved."""
            ops, i = [], 0
            while i < len(stmts):
                n, c = stmts[i], stmts[i].children
                if n.type == 0 and n.param == 0:  # If cond / Then ... / Else ...
                    then = stmts[i + 1] if i + 1 < len(stmts) and stmts[i + 1].type == 0 and stmts[i + 1].param == 1 else None
                    other_b = stmts[i + 2] if then and i + 2 < len(stmts) and stmts[i + 2].type == 0 and stmts[i + 2].param == 2 else None
                    ok = decide(c[0]) if c else None
                    if ok is None:  # undecidable: the branch with the capture or the action (collectible gates)
                        ok = not (other_b and contains(other_b.children, lambda x: is_capture(x) or is_action(x)))
                    branch = then if ok else other_b
                    if branch:
                        ops += flatten(branch.children)
                    i += 1 + (then is not None) + (other_b is not None)
                    continue
                if n.type == 2 and n.param == 11 and len(c) == 2:  # Assign
                    k = ed_dsg(c[0])
                    v = value(c[1])
                    if k is not None and v:
                        if v[0] == 'int':
                            ops.append((POP_SET, 0, k, v[1] & 0xFFFFFFFF, 0, (0, 0, 0)))
                        elif v[0] == 'float':
                            ops.append((POP_SET, 1, k, 0, 0, (v[1], 0, 0)))
                        elif v[0] == 'self':
                            ops.append((POP_SET, 2, k, 0, 0, (0, 0, 0)))
                    elif c[0].type in (10, 11) and v:
                        env[c[0].param] = v
                elif n.type == 4 and n.param in (237, 238) and len(c) == 2 and is_ed(c[0]):
                    b = (normal if n.param == 237 else reflex).get(c[1].ref)
                    if b is not None:
                        ops.append((POP_FORCE if n.param == 237 else POP_FORCE_REFLEX, 0, b, 0, 0, (0, 0, 0)))
                elif n.type == 4 and n.param == 2:
                    ops.append((POP_COVER, 0, 0, 0, 0, (0, 0, 0)))
                elif n.type == 5 and n.param == 29 and c and c[0].type == 12:
                    ops.append((POP_WAIT, 0, 0, c[0].param, 0, (0, 0, 0)))
                elif n.type == 4 and n.param == 8 and len(c) >= 4 and is_ed(c[0]):
                    a, b = value(c[1]), value(c[3])
                    if a and a[0] == 'vec':
                        ops.append((POP_TELEPORT, 0, 0, 0, 0, a[1]))
                    if b and b[0] == 'vec':
                        ops.append((POP_CAMERA, 0, 0, 0, 0, b[1]))
                elif n.type == 4 and n.param == 7 and c and c[0].type == 20 and c[0].ref:
                    ops.append((POP_LEVEL, 0, 0xFFFF, sid(cstr(c[0].ref)), 0xFFFFFFFF, (0, 0, 0)))
                elif n.type == 4 and n.param == 29 and len(c) >= 4 and all(x.type == 20 and x.ref for x in (c[0], c[2], c[3])):
                    ops.append((POP_LEVEL, 0, sid(cstr(c[3].ref)), sid(cstr(c[0].ref)), sid(cstr(c[2].ref)), (0, 0, 0)))
                i += 1
            return ops

        def blocks():
            """(guard condition or None, statements) for every top-level statement list and If-Then of the scripts."""
            for b in other.behaviours:
                for s in b.scripts + ([b.schedule] if b.schedule else []):
                    def rec(stmts, cond):
                        yield cond, stmts
                        for k, n in enumerate(stmts):
                            if n.type == 0 and n.param == 0 and k + 1 < len(stmts) and stmts[k + 1].param == 1 and n.children:
                                yield from rec(stmts[k + 1].children, n.children[0])
                                if k + 2 < len(stmts) and stmts[k + 2].type == 0 and stmts[k + 2].param == 2:
                                    yield from rec(stmts[k + 2].children, None)  # Else: unguarded
                    yield from rec(s, None)

        capture = fire = None
        radius = None
        for cond, stmts in blocks():
            if capture is None and any(is_capture(n) for n in stmts):
                capture = (cond, stmts)
            if fire is None and cond is not None and contains(stmts, is_action):
                k = ed_dsg(cond.children[0]) if cond.type == 1 and cond.param == 3 and cond.children else None
                if k is not None and value(cond.children[1]) and value(cond.children[1])[0] == 'int':
                    fire = (0, k, value(cond.children[1])[1], stmts)
                elif cond.type == 1 and cond.param == 63:
                    fire = (1, 0, 0, stmts)
            for n in stmts:
                if n.type == 4 and n.param in (18, 19, 20) and len(n.children) >= 2 and n.children[0].type == 0 \
                        and n.children[0].param == 4:
                    r = value(n.children[1])
                    if r and r[0] in ('int', 'float') and r[1] > 0:
                        radius = r[1] if radius is None else min(radius, r[1])
        if capture is None or fire is None:
            continue
        guard, cap_ops = capture[0], []
        if guard is not None and guard.type == 1 and guard.param == 5 and len(guard.children) == 2:
            a, b = guard.children  # Less(FUNC_4(Ed), dsg): within a distance of Ed
            if a.type == 3 and a.param == 4 and a.children and is_ed(a.children[0]):
                r = value(b)
                radius = r[1] if r and r[0] in ('int', 'float') else radius
        elif guard is not None and guard.type == 1 and guard.param == 3 and guard.children:
            k = ed_dsg(guard.children[0])
            v = value(guard.children[1])
            if k is not None and v and v[0] == 'int':
                cap_ops.append((POP_GUARD, 0, k, v[1] & 0xFFFFFFFF, 0, (0, 0, 0)))
            elif decide(guard) is False:
                continue
        cap_ops += flatten(capture[1])
        fire_ops = flatten(fire[3])
        if not radius or not any(op[0] in (POP_TELEPORT, POP_LEVEL) for op in fire_ops):
            continue
        # A portal sends Ed into the walk-in (B66, which ends in the cover) or starts the cover itself. Not portals:
        # GenTransitionSubMap's zone test (COND_13), the fall-off-the-map ChangeMapDebut/Fin, canyon's retry warp.
        walk_in = normal.get(next((r for r, n in ai.beh_names.items() if n == 'B66'), None))
        if not any(op[0] == POP_COVER or (op[0] == POP_FORCE and op[2] == walk_in) for op in cap_ops):
            continue
        rows = (p.world[0:3], p.world[4:7], p.world[8:11])
        out.append((pos, float(radius), rows, persos[p.loc], fire[0], fire[1], fire[2], cap_ops, fire_ops,
                    d.perso_names(p.loc)[:3:2]))
    return out


def ski_portals(ai, strings):
    """Totalski and skymap, played as the skier: a stand-in for the game's finish (there the arches capture the skier by a
    zone test and his own scripts walk him in, B7, and change level, B22, after the finish markers' cutscene-like
    sequence). The holes (SYM_GenTrou) work as in the game, their zone test (COND_13) exported as their ZDD box
    (portal_zones; the radius 8 is only the fallback): they set the skier's respawn point and fall flag, his own scripts do the rest. The arch named *Retry teleports the skier back to the start of the run (cover, reveal); the others
    lead to sud0 at the place the skier's B22 names for this level. Same records as portals()."""
    d, lv = ai.d, ai.lv
    persos = {p.loc: k + 1 for k, p in enumerate(d.persos())}
    arrival = {'totalski': ('heg_positionedtotalski', 'heg_positioncamtotalski'),
               'skymap': ('nbd_positionedrevientskymap', 'nbd_positioncamrevientskymap')}.get(lv.name.lower())
    start = d.fix_matrices.get(ai.perso) or next((p.world for p in d.persos() if p.loc == ai.perso), None)
    if not arrival or start is None:
        return []
    import math
    sid = lambda s: strings.setdefault(s.encode(), len(strings))
    out = []
    for p in d.persos():
        fam, model, inst = d.perso_names(p.loc)
        if model == 'SYM_GenTrou':
            # A hole (its zone test, COND_13): the skier's respawn point := its own dsg0, dsg22 := 0, dsg29 := 1 (fell in);
            # his reflexes R1 / R3 then cover the screen and B10 respawns him there (SetPositionFrom(KW_5, dsg30)).
            # Also dsg31 := own dsg1 (a second point next to the respawn one); dsg32 := own dsg2 (a perso) is not
            # expressible as a portal op. The capture is the hole's ZDD zone (portal_zones), the radius only a fallback.
            spot, look = perso_dsg(d, p.loc, 0), perso_dsg(d, p.loc, 1)
            if spot and spot[0] == 'vec':
                rows = (p.world[0:3], p.world[4:7], p.world[8:11])
                ops = [(POP_SET, 3, 30, 0, 0, tuple(spot[1])), (POP_SET, 0, 22, 0, 0, (0, 0, 0))]
                if look and look[0] == 'vec':
                    ops.append((POP_SET, 3, 31, 0, 0, tuple(look[1])))
                ops.append((POP_SET, 0, 29, 1, 0, (0, 0, 0)))
                out.append((tuple(p.world[3::4][:3]), 8.0, rows, persos[p.loc], 2, 0, 0, ops, [], (fam, inst)))
            continue
        if model in ('SYM_GenHurryUp', 'SYM_Gen_Ressort'):
            # Within its activation radius (PROC_18(Me, r)) the prop writes the skier's dsgvars, then sleeps (META_29):
            #   speed arrow (HurryUp, r = own dsg0): dsg0 := 0, dsg22 := 1, dsg24 := own dsg1, dsg27 := own dsg2,
            #     dsg46 := its facing direction;
            #   spring star (Ressort, r = own dsg3): dsg41 := 0, dsg0 := own dsg4, dsg26 := 1,
            #     dsg27 := (own dsg0, dsg1, dsg2), dsg46 := 0, dsg41 := 1.
            # His own scripts do the rest (the boost, the jump).
            def num(k):
                v = perso_dsg(d, p.loc, k)
                return v[1] if v and v[0] in ('int', 'float') else 0
            m = p.world
            if model == 'SYM_GenHurryUp':
                f = (-m[1], -m[5], -m[9])  # FacingDirection: where its local -Y points
                l = math.sqrt(sum(x * x for x in f)) or 1
                spot = perso_dsg(d, p.loc, 2)
                ops = [(POP_SET, 0, 0, 0, 0, (0, 0, 0)), (POP_SET, 0, 22, 1, 0, (0, 0, 0)),
                       (POP_SET, 0, 24, int(num(1)) & 0xFFFFFFFF, 0, (0, 0, 0)),
                       (POP_SET, 3, 27, 0, 0, tuple(spot[1]) if spot and spot[0] == 'vec' else (0, 0, 0)),
                       (POP_SET, 3, 46, 0, 0, tuple(x / l for x in f))]
                radius = num(0)
            else:
                ops = [(POP_SET, 0, 41, 0, 0, (0, 0, 0)), (POP_SET, 0, 0, int(num(4)) & 0xFFFFFFFF, 0, (0, 0, 0)),
                       (POP_SET, 0, 26, 1, 0, (0, 0, 0)), (POP_SET, 3, 27, 0, 0, (float(num(0)), float(num(1)), float(num(2)))),
                       (POP_SET, 3, 46, 0, 0, (0, 0, 0)), (POP_SET, 0, 41, 1, 0, (0, 0, 0))]
                radius = num(3)
            if radius > 0:
                rows = (m[0:3], m[4:7], m[8:11])
                out.append((tuple(m[3::4][:3]), float(radius), rows, persos[p.loc], 2, 0, 0, ops, [], (fam, inst)))
            continue
        if model not in ('SYM_Arche_Ski', 'NBD_ArcheSpePourSkiEtSky'):
            continue
        if inst.lower().endswith('retry'):
            heading = math.atan2(-start[1], start[5])
            fire = [(POP_TELEPORT, 1, 0, 0, struct.unpack('<I', struct.pack('<f', heading))[0], tuple(start[3::4][:3]))]
        else:
            fire = [(POP_LEVEL, 0, sid(arrival[1]), sid('sud0'), sid(arrival[0]), (0, 0, 0))]
        rows = (p.world[0:3], p.world[4:7], p.world[8:11])
        out.append((tuple(p.world[3::4][:3]), 6.0, rows, persos[p.loc], 1, 0, 0,
                    [(POP_COVER, 0, 0, 0, 0, (0, 0, 0))], fire, (fam, inst)))
    return out


def places(d):
    """Every level perso by instance name, for PROC_29 arrivals ('heg_positionedland' = where Ed appears, '..cam..'
    the camera): [(lowercase name, position, heading)], heading as tc_heading_of (where local -Y points)."""
    import math
    out = []
    for p in d.persos():
        name = d.perso_names(p.loc)[2]
        if name:
            m = p.world
            out.append((name.lower(), tuple(m[3::4][:3]), math.atan2(-m[1], m[5])))
    return out


def transition_config(d):
    """The screen transitions (docs/PORTALS.md): the 0x30-byte block FUN_004cb350 reads from fix.gpt at 0x12a8 into
    DAT_0051b040: {u32 flags, RND channel, kind, threshold, range, ptr effect A, ptr effect B} for the cover, then
    {kind, threshold, range, effect A, effect B} for the reveal. Effect = {ptr variant list, ?, u32 count, ptr sprite,
    f32 +0x10..+0x30 (A: start, end, current, speed; B: radius start, end, current, speed, current speed, acceleration,
    angle start, current angle, spin)}; variant = {ptr next, ?, ?, ptr texture file name}.
    Returns ((cover threshold, range), (reveal threshold, range), [cover A, cover B, reveal A, reveal B] as
    (fields[9], [texture names]))."""
    lv, g = d.lv, d.lv.fix_gpt
    base = 0x12A8
    un = lambda o: struct.unpack_from('<I', g.data, base + o)[0]
    effects = []
    for off in (0x14, 0x18, 0x28, 0x2C):
        e = lv.ptr((g, base + off))
        if e is None:
            raise ValueError('fix.gpt transition block not found at 0x12a8')
        n = d.un('I', e, 8)[0]
        names, node = [], lv.ptr(e, 0)
        for _ in range(n):
            names.append(node[0].data[lv.ptr(node, 0xC)[1]:].split(b'\0')[0].decode('latin-1').replace('\\', '/'))
            node = lv.ptr(node, 0)
        effects.append((d.un('9f', e, 0x10), names))
    return (un(0xC), un(0x10)), (un(0x20), un(0x24)), effects


def started_by_others(ai):
    """(normal, reflex) behaviour locations another perso puts this perso in when it takes over from the fix Ed, or
    None each: a schedule that deactivates the fix Ed (PROC_6) and sets this perso's behaviours with PROC_237 /
    PROC_238 (set the normal / reflex behaviour of a perso; the last call wins). Totalski's start trigger (perso
    0x60ae7, its first schedule) and skymap's (0x6bc43, B2) send the skier to its riding start this way; the
    skier's own initial behaviour B0 is empty."""
    d, found = ai.d, [None, None]
    fix_ed = d.perso_in_fix[0][0] if d.perso_in_fix else None
    if ai.perso == fix_ed:
        return found
    for p in d.persos():
        if p.loc == ai.perso:
            continue
        for b in AI(d, p.loc).behaviours:
            calls = [n for n in b.schedule or [] if n.type == 4 and len(n.children) >= 1]
            if not any(n.param == 6 and n.children[0].ref == fix_ed for n in calls):
                continue
            for n in calls:
                if n.param in (237, 238) and len(n.children) == 2 and n.children[0].ref == ai.perso:
                    found[n.param - 237] = n.children[1].ref
    return found


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
        if t == 34:  # a sound event: snapshot pointer -> global event index
            from .sfx import event_index
            k = event_index(n.param)
            return k if k is not None else 0xFFFFFFFF
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
    for k, ref in enumerate(started_by_others(ai)):
        if ref in beh_index:
            initial[k] = beh_index[ref] & 0xFFFF

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
    st_rows, cards = [], {}
    drops = pose_drops(d, ai.perso, keys)
    roots = root_motion(d, ai.states, keys)
    sphere_sets, state_sets = zdm_sets(d, ai.perso, len(ai.states))
    for s in ai.states:
        c = lv.ptr(s, 0x30)
        card = cards.setdefault(c, len(cards)) if c and d.un('I', c)[0] in CARD_SIZES else 0xFFFF
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
        st_rows.append((ai.state_index.get(nxt, -1), n_frames, speed, flags, trans, prohib, repeats, k & 0xFFFF, ground, card, drops.get(k, 0.0)))
    p3d = lv.ptr(ai.perso)
    init_state = ai.state_index.get(lv.ptr(p3d, 0), 0) if p3d else 0

    out, parts = bytearray(), []

    def cut(tag):  # the bytes since the last cut become chunk `tag`
        parts.append((tag, bytes(out)))
        out.clear()
    out += struct.pack('<I', len(nodes))
    for param, typ, depth in nodes:
        out += struct.pack('<IBBH', param & 0xFFFFFFFF, typ, depth, 0)
    out += struct.pack('<I', len(scripts)) + b''.join(struct.pack('<I', s) for s in scripts)
    out += struct.pack('<HH', *lists)
    for sched, first, n in behs:
        out += struct.pack('<iHH', sched, first, n)
    out += struct.pack('<HH', *initial)
    out += own_dsg
    cut('SCPT')
    out += struct.pack('<HH', len(st_rows), init_state)
    for nxt, nf, speed, flags, trans, prohib, repeats, anim, ground, card, drop in st_rows:
        out += struct.pack('<hHBBHHBBHHff', nxt, nf, speed, flags, len(trans), len(prohib), repeats, 0, anim, card, ground,
                           drop)
        out += b''.join(struct.pack('<HH', *t) for t in trans) + b''.join(struct.pack('<H', p) for p in prohib)
    cut('STAT')
    out += struct.pack('<HH', len(buttons), 0)
    button_rows = []
    for ref in buttons:
        mask, kind, lo, hi = btn.leaves(ref[1])
        button_rows.append((ref[1], mask, kind, lo, hi))
        out += struct.pack('<HBBhh', mask, kind, 0, lo, hi)
    cut('BTNS')
    out += struct.pack('<H', len(strings))
    for ref in strings:
        s = cstr(ref)
        out += struct.pack('<H', len(s)) + s
    cut('STRS')
    out += struct.pack('<H', len(vectors))
    for ref in vectors:
        out += struct.pack('<3f', *d.un('3f', ref))
    cut('VECS')
    used = sorted({p for p, t, _ in nodes if t == 24 and p not in (0, 0xFFFFFFFF)})
    by_id = {k: loc for loc, k in persos.items()}
    fix = [x[0] for x in d.perso_in_fix]
    out += struct.pack('<HH', len(used), 0)
    for k in used:
        role = fix.index(by_id[k]) if by_id[k] in fix[1:3] else 0
        out += struct.pack('<HBB', k, role, 0) + dsg_block(by_id[k])[0]
    cut('PRSO')
    out += struct.pack('<HH', len(cards), 0)
    for c in cards:
        t = d.un('I', c)[0]
        n = CARD_SIZES[t] // 4 - 1
        out += struct.pack('<II', t, n) + struct.pack(f'<{n}I', *d.un(f'{n}I', c, 4))
    cut('CARD')
    root_rows, root_index = [], []
    for k in keys:
        rows = roots.get(k, [])
        root_index.append((len(root_rows), len(rows)))
        root_rows += rows
    out += struct.pack('<I', len(root_rows)) + b''.join(struct.pack('<3f', *r) for r in root_rows)
    for (first, n), zs in zip(root_index, state_sets):
        out += struct.pack('<IHH', first, n, zs)
    out += struct.pack('<HH', len(sphere_sets), 0)
    for zs in sphere_sets:
        out += struct.pack('<HH', len(zs), 0) + b''.join(struct.pack('<4f', *z[:4]) for z in zs)
    cut('BODY')
    out += struct.pack('<H', len(sphere_sets)) + b''.join(
        struct.pack('<H', len(zs)) + b''.join(struct.pack('<2H2f', *z[4:8]) for z in zs) for zs in sphere_sets)
    cut('ZDMF')
    triggers = proximity_triggers(ai)
    out += struct.pack('<HH', len(triggers), 0)
    for pos, radius, beh, guard, sleep_ms, _ in triggers:
        out += struct.pack('<4fHHI', *pos, radius, beh, guard, sleep_ms)
    cut('TRIG')
    names = {}
    gates = portals(ai, names)
    out += struct.pack('<HH', len(gates), 0)
    for pos, radius, rows, perso, fire, fire_dsg, fire_value, cap, fire_ops, _ in gates:
        out += struct.pack('<4f9fHBBHHi', *pos, radius, *[x for r in rows for x in r], perso, fire, 0, fire_dsg, 0,
                           struct.unpack('<i', struct.pack('<I', fire_value & 0xFFFFFFFF))[0])
        out += struct.pack('<HH', len(cap), len(fire_ops))
        for op, kind, a, b, c, v in cap + fire_ops:
            out += struct.pack('<BBHII3f', op, kind, a, b & 0xFFFFFFFF, c & 0xFFFFFFFF, *v)
    place_rows = places(d)
    out += struct.pack('<H', len(names))
    for n in names:
        out += struct.pack('<H', len(n)) + n
    cut('PORT')
    out += struct.pack('<HH', len(place_rows), 0)
    for n, pos, h in place_rows:
        b = n.encode('latin-1')
        out += struct.pack('<H', len(b)) + b + struct.pack('<4f', *pos, h)
    cut('PLAC')
    from . import export as tex_export
    tex_idx = tex_export.texture_index(TEX_ROOT)
    (cov_t, cov_r), (rev_t, rev_r), effects = transition_config(d)
    out += struct.pack('<4H', cov_t, cov_r, rev_t, rev_r)
    missing_fx = []
    for fields, variants in effects:
        out += struct.pack('<HH9f', len(variants), 0, *fields)
        for v in variants:
            png = tex_idx.get(os.path.splitext(v)[0].lower())
            if png is None:
                missing_fx.append(v)
            rel = os.path.relpath(png, os.path.dirname(os.path.abspath(path))).replace(os.sep, '/') if png else ''
            out += struct.pack('<H', len(rel)) + rel.encode()
    cut('TRAN')
    from .sfx import event_index
    sound_rows, sound_index, mech_rows, mech_index = [], [], [], []
    for st in ai.states:
        ar = lv.ptr(st, 0xC)
        ev = lv.ptr(ar, 8) if ar else None
        n_ev = d.un('I', ar, 0xC)[0] if ev else 0
        rows, mech = [], []
        for k in range(n_ev if n_ev < 256 else 0):
            typ, value, frame = d.un('3I', ev, 20 * k)  # anim event (20 bytes): {type, value, u16 frame, ..}
            first, period = d.un('2B', ev, 20 * k + 0xB)  # its counter: start value, reload (0xFF: never again)
            e = event_index(value) if typ == 0 and value else None
            if e is not None and (frame & 0xFFFF) <= d.un('H', ar, 4)[0]:  # within the animation (some lists hold garbage)
                rows.append((frame & 0xFFFF, e, first, period))
            if typ == 1 and value < 0x100 and (frame & 0xFFFF) <= d.un('H', ar, 4)[0]:  # FUN_00436910 case 1
                mech.append((frame & 0xFFFF, value, first, period))
        sound_index.append((len(sound_rows), len(rows)))
        sound_rows += sorted(rows)
        mech_index.append((len(mech_rows), len(mech)))
        mech_rows += sorted(mech)
    out += struct.pack('<I', len(sound_rows)) + b''.join(struct.pack('<2H2BH', *r, 0) for r in sound_rows)
    out += b''.join(struct.pack('<IHH', first, n, 0) for first, n in sound_index)
    cut('SNDS')
    out += struct.pack('<I', len(mech_rows)) + b''.join(struct.pack('<2H2BH', *r, 0) for r in mech_rows)
    out += b''.join(struct.pack('<IHH', first, n, 0) for first, n in mech_index)
    cut('MECE')
    water = water_planes(d)
    out += struct.pack('<HH', len(water), 0)
    for z, env, boxes in water:
        out += struct.pack('<4fHH', z, *env, len(boxes), 0) + b''.join(struct.pack('<6f', *lo, *hi) for lo, hi in boxes)
    cut('WATR')
    point_sets, point_index = water_points(d, ai.states)
    out += struct.pack('<HH', len(point_sets), 0)
    for pts in point_sets:
        out += struct.pack('<HH', len(pts), 0) + b''.join(struct.pack('<I3f', k, *v) for k, v in pts)
    out += b''.join(struct.pack('<H', i) for i in point_index)
    cut('WPTS')
    zones = portal_zones(ai, gates)
    out += struct.pack('<HH', len(zones), 0)
    for z in zones:
        if not z:
            out += struct.pack('<HH', 0, 0)
            continue
        inv, prims = z
        out += struct.pack('<HH12f', len(prims), 0, *inv)
        out += b''.join(struct.pack('<I3f3f', k, *a, *b) for k, a, b in prims)
    cut('ZONE')
    fix_ed = d.perso_in_fix[0][0] if d.perso_in_fix else None
    tris, _, sounds = d.collision(skip_decals=d.player() != fix_ed, sounds=True)  # as ttlv.py writes the triangles
    out += struct.pack('<I', len(sounds)) + bytes(x & 0xFF for x in sounds)
    cut('FSND')
    dry = dry_boxes(d) if water else []
    out += struct.pack('<HH', len(dry), 0) + b''.join(struct.pack('<6f', *lo, *hi) for lo, hi in dry)
    cut('DRYB')
    coll = struct.pack('<I', len(tris)) + b''.join(struct.pack('<9f', *a, *b, *c) for a, b, c in tris)
    out += struct.pack('<II', len(tris), zlib.crc32(coll))  # the .ttlv's COLL chunk, byte for byte
    cut('LINK')
    chunks.write(path, 'TTAI', parts)
    with open(os.path.splitext(path)[0] + '_ai.txt', 'w', encoding='utf-8') as f:
        f.write('\n'.join(listing) + '\n')
    return {'nodes': len(nodes), 'scripts': len(scripts), 'behaviours': lists, 'initial': initial,
            'dsgvars': n_dsg, 'persos': used, 'states': len(st_rows), 'initial_state': init_state,
            'transitions': sum(len(r[4]) for r in st_rows),
            'moving': [i for i, r in enumerate(st_rows) if r[8] > 0], 'buttons': button_rows,
            'strings': [cstr(r) for r in strings], 'cards': len(cards), 'root_states': sum(1 for _, n in root_index if n),
            'sphere_sets': len(sphere_sets), 'triggers': len(triggers), 'portals': gates, 'portal_names': list(names),
            'places': len(place_rows), 'water': len(water), 'zones': sum(1 for z in zones if z), 'missing_effects': missing_fx, 'unresolved': dict(unresolved), 'bytes': os.path.getsize(path)}

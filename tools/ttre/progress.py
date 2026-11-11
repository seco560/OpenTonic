"""The progress survey: which characters' scripts test, give and take each power, and read and write each save value,
over every retail level (docs/ENGINE.md "Game progress"). The debug menu shows it next to each flag (core tc_debug.h).

Run as `python3 -m tools.ttre.progress files/out`: writes files/out/ttlv/progress.ttpg for the core and progress.tsv, the
same rows as text, beside it.

What counts as a use: a script node of one of the primitives below whose first argument is a constant (the mask, or the
slot). A use with a computed argument is filed under key -1 (the inventory reads save values by an object's slot). The
rows say a script holds the call, not that it ever runs.

  COND 90   Cond_TestPower(mask)      tests      FUNC 93   SaveValue(slot)          reads
  PROC 200  the powers := mask        sets       PROC 143  IncSaveValue(slot)       adds
  PROC 201  Proc_SubPower(mask)       takes      PROC 144  SetSaveValue(slot, v)    sets
  PROC 202  Proc_AddPower(mask)       gives

A perso that comes with the fix (Ed, the camera, the cheat-code reader ...) is in every level: its rows carry the level
"*" once, from the first level read.

progress.ttpg (container: chunks.py; reader: core/src/tc_debug.c), chunk USES:
  u32 n, then n x { u8 kind (0 power, 1 save value), u8 use (USE_* below), i16 key (mask or slot; -1 computed),
                    u16 calls, u16 instances, string level, string model, string instance (the first one's name) }
  string = u16 length + bytes (latin-1).
"""
import os
import struct
import sys

from . import ai, chunks, level, sna

LEVELS = 'files/sources/GAMEDATA/WORLD/LEVELS'
USE_TESTS, USE_GIVES, USE_TAKES, USE_SETS, USE_READS, USE_ADDS = range(6)
USE_NAMES = ('tests', 'gives', 'takes', 'sets', 'reads', 'adds')
POWER, VALUE = 0, 1
PRIMITIVES = {(1, 90): (POWER, USE_TESTS), (4, 200): (POWER, USE_SETS), (4, 201): (POWER, USE_TAKES),
              (4, 202): (POWER, USE_GIVES), (3, 93): (VALUE, USE_READS), (4, 143): (VALUE, USE_ADDS),
              (4, 144): (VALUE, USE_SETS)}
CONSTANTS = (12, 17)  # INT, MASK


def level_names(levels=LEVELS):
    return sorted((n for n in os.listdir(levels) if os.path.isdir(os.path.join(levels, n))), key=str.lower)


def survey(names, levels=LEVELS):
    """{(level, kind, key, use, model): [calls, instances, first instance name]} over the levels."""
    rows, fix_done = {}, set()
    for name in names:
        d = level.LevelData(sna.Level(levels, name))
        fix = [x[0] for x in d.perso_in_fix if x and x[0]]
        seen = set()
        for loc in fix + [p.loc for p in d.persos()] + [t.loc for t in d.spawnables()]:
            if loc is None or loc in seen:
                continue
            seen.add(loc)
            try:
                a = ai.AI(d, loc)
            except Exception:
                continue
            _, model, inst = d.perso_names(loc)
            where = '*' if loc in fix else name
            if where == '*' and (model, inst) in fix_done:
                continue
            found = set()
            for n in a.nodes():
                u = PRIMITIVES.get((n.type, n.param))
                if not u:
                    continue
                c = n.children[0] if n.children else None
                key = c.param if c is not None and c.type in CONSTANTS and c.param < 0x8000 else -1
                k = (where, u[0], key, u[1], model)
                r = rows.setdefault(k, [0, 0, inst])
                r[0] += 1
                if k not in found:
                    found.add(k)
                    r[1] += 1
            if where == '*':
                fix_done.add((model, inst))
    return rows


def write(rows, out_dir):
    def s(t):
        b = t.encode('latin-1', 'replace')[:255]
        return struct.pack('<H', len(b)) + b
    order = sorted(rows.items(), key=lambda kv: (kv[0][1], kv[0][2], kv[0][3], kv[0][0].lower(), kv[0][4].lower()))
    body = struct.pack('<I', len(order))
    for (where, kind, key, use, model), (calls, insts, inst) in order:
        body += struct.pack('<BBhHH', kind, use, key, min(calls, 0xFFFF), min(insts, 0xFFFF)) + s(where) + s(model) + s(inst)
    chunks.write(os.path.join(out_dir, 'progress.ttpg'), 'TTPG', [('USES', body)])
    with open(os.path.join(out_dir, 'progress.tsv'), 'w') as f:
        f.write('kind\tkey\tuse\tlevel\tmodel\tfirst instance\tinstances\tcalls\n')
        for (where, kind, key, use, model), (calls, insts, inst) in order:
            shown = 'computed' if key < 0 else f'{key:#x}' if kind == POWER else str(key)
            f.write(f"{('power', 'value')[kind]}\t{shown}\t{USE_NAMES[use]}\t{where}\t{model}\t{inst}\t{insts}\t{calls}\n")
    return len(order)


def main(out):
    if not os.path.isdir(LEVELS):
        print('== progress survey: no retail levels (skip)')
        return
    names = level_names()
    n = write(survey(names), os.path.join(out, 'ttlv'))
    print(f'== progress survey: {n} rows over {len(names)} levels')


if __name__ == '__main__':
    main(sys.argv[1])

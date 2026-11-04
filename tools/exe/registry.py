"""The registry of TonicTrouble.exe's functions and AI primitives: what is known, what the port implements, what is a
stub. Writes docs/registry.tsv (one row per function of the full dump, then one per named AI primitive).

    python3 -m tools.exe.registry [out.tsv]

Status, in order of trust (the `basis` column says which one decided):
  hand        tools/exe/registry_status.tsv: implemented / partial / stub / known / unknown, with a note
  ai          the AI interpreter (core/src/tc_ai.c): a primitive with its own `case` is implemented, one in the explicit
              no-op list is a stub, one that falls to `default: unknown(...)` is missing (aicheck's "skipped primitives")
  core-cite   core/ names the function in a comment: implemented? (cited as ported), or known (cited as "not ported",
              "not read", "open" on the same line). A guess: check the cited line before relying on it
  doc-cite    only docs/*.md name it: known (read)
  name        it has a descriptive name (names.tsv or the typing passes) but nobody cites it: named
  none        unknown
Columns: addr, name, module, status, basis, size, port (core file:line, first 3), docs (doc files), note.
"""
import json
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INDEX = os.path.join(ROOT, 'files/out/_decomp_artifacts/all/index.json')
HAND = os.path.join(ROOT, 'tools/exe/registry_status.tsv')
NAMES = os.path.join(ROOT, 'tools/exe/names.tsv')
AI_NAMES = os.path.join(ROOT, 'tools/exe/ai_names.tsv')
TC_AI = os.path.join(ROOT, 'core/src/tc_ai.c')
OUT = os.path.join(ROOT, 'docs/registry.tsv')

# FUN_004a3130, GAM_PlayAnim_Update_00415c00, MEC_SweepSphere_0043d480, AI_0048eab0, ... and bare 0x4a3130 / 0x004A3130
CITE = re.compile(r'\b(?:[A-Za-z][A-Za-z0-9]*_)+(00[45][0-9a-fA-F]{5})\b|\b0x(00)?([45][0-9a-fA-F]{5})\b')
NEGATIVE = re.compile(r"not ported|isn't ported|not read|not modelled|not modeled|\bopen\b|todo|not traced|not decoded",
                      re.I)
AUTO_NAME = re.compile(r'^(?:[A-Z]+_)?(?:FUN_|LAB_|thunk_)?[0-9a-f]{8}$|^[A-Z]+_[0-9a-f]{8}$')


def tsv(path):
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if line and not line.startswith('#'):
                yield line.split('\t')


def citations(paths, functions):
    """addr -> [(file, line, text)] for lines naming a function of the dump."""
    out = defaultdict(list)
    for top in paths:
        for dirpath, _, files in os.walk(os.path.join(ROOT, top)):
            for fn in sorted(files):
                if not fn.endswith(('.c', '.h', '.md', '.py')):
                    continue
                path = os.path.join(dirpath, fn)
                rel = os.path.relpath(path, ROOT)
                with open(path, encoding='utf-8', errors='replace') as f:
                    for n, text in enumerate(f, 1):
                        for m in CITE.finditer(text):
                            a = (m.group(1) or ('00' + m.group(3))).lower()
                            if a in functions:
                                out[a].append((rel, n, text))
    return out


def ai_cases():
    """kind -> (implemented indices, stub indices), from tc_ai.c's dispatch functions."""
    src = open(TC_AI, encoding='utf-8').read()
    bodies = {'COND': 'condition', 'FUNC': 'function', 'OP': 'operation', 'PROC': 'procedure', 'META': 'meta_action'}
    result = {}
    for kind, fn in bodies.items():
        m = re.search(r'\nstatic [a-z_0-9 ]+\b' + fn + r'\(run \*r, uint32_t i\) \{\n(.*?)\n\}\n', src, re.S)
        if not m:
            continue
        body = m.group(1)
        first = re.search(r'\n( +)case ', body)
        if not first:
            continue
        indent = first.group(1)
        impl, stub = set(), set()
        # the statement after a run of case labels: a no-op list says so in its comment
        for block in re.finditer(r'((?:^' + indent + r'case [^\n]*\n)+)', body, re.M):
            labels = block.group(1)
            nums = {int(x) for x in re.findall(r'case (\d+):', labels)}
            rest = body[block.end():block.end() + 200]
            tail = labels.splitlines()[-1] + '\n' + rest.split('\n', 1)[0]
            if re.search(r'no-ops? for now|nothing to do', tail):
                stub |= nums
            else:
                impl |= nums
        result[kind] = (impl - stub, stub)
    return result


def main(out):
    index = json.load(open(INDEX))['functions']
    functions = {f['addr'].lower(): f for f in index}
    names = {a.lower(): r[0] for a, *r in tsv(NAMES) if r}
    hand = {}
    for row in tsv(HAND):
        row += [''] * (4 - len(row))
        hand[row[0].lower()] = row[1:4]
    core = citations(['core/src', 'core/include'], functions)
    docs = citations(['docs'], functions)
    rows = []
    counts = defaultdict(int)
    for a in sorted(functions):
        f = functions[a]
        name = names.get(a) or f['name']
        module = name.split('_')[0] if '_' in name and not name.startswith('FUN_') else ''
        port = ', '.join(f'{p}:{n}' for p, n, _ in core.get(a, [])[:3])
        docfiles = ', '.join(sorted({os.path.basename(p) for p, _, _ in docs.get(a, [])}))
        note = ''
        if a in hand:
            hname, status, note = hand[a]
            name = hname or name
            basis = 'hand'
        elif a in core:
            lines = core[a]
            positive = [l for l in lines if not NEGATIVE.search(l[2])]
            status, basis = ('implemented?' if positive else 'known'), 'core-cite'
        elif a in docs:
            status, basis = 'known', 'doc-cite'
        elif not AUTO_NAME.match(name):
            status, basis = 'named', 'name'
        else:
            status, basis = 'unknown', 'none'
        counts[status] += 1
        rows.append([a, name, module, status, basis, str(f.get('size', '')), port, docfiles, note])

    # AI primitives (named in ai_names.tsv, or handled by tc_ai.c)
    cases = ai_cases()
    ai_rows = {}
    for row in tsv(AI_NAMES):
        row += [''] * (5 - len(row))
        kind, idx, pname, impl_addr, note = row[:5]
        if not idx.isdigit():
            continue
        ai_rows[(kind, int(idx))] = (pname, impl_addr, note)
    for kind, (impl, stub) in cases.items():
        for i in impl | stub:
            ai_rows.setdefault((kind, i), ('', '', ''))
    ai_counts = defaultdict(int)
    for (kind, i) in sorted(ai_rows, key=lambda k: (k[0], k[1])):
        pname, impl_addr, note = ai_rows[(kind, i)]
        impl, stub = cases.get(kind, (set(), set()))
        if kind in cases:
            status = 'implemented' if i in impl else 'stub' if i in stub else 'missing'
            basis = 'ai'
        else:
            status, basis = 'unknown', 'none'
        ai_counts[status] += 1
        rows.append([f'{kind} {i}', pname, 'AI', status, basis, '', 'core/src/tc_ai.c' if basis == 'ai' and status != 'missing'
                     else '', '', (impl_addr + ' ' + note).strip()])

    with open(out, 'w', encoding='utf-8') as f:
        f.write('# Generated by python3 -m tools.exe.registry (do not edit: hand statuses go in tools/exe/registry_status.tsv).\n')
        f.write('# status: implemented / partial / stub / known / unknown (hand); implemented? / known (from citations);\n')
        f.write('#   named; AI primitives: implemented / stub / missing (tc_ai.c). basis: hand, ai, core-cite, doc-cite, name, none.\n')
        f.write('addr\tname\tmodule\tstatus\tbasis\tsize\tport\tdocs\tnote\n')
        for r in rows:
            f.write('\t'.join(c.replace('\t', ' ').replace('\n', ' ') for c in r) + '\n')
    print(f'wrote {os.path.relpath(out, ROOT)}: {len(functions)} functions ' +
          ', '.join(f'{k} {v}' for k, v in sorted(counts.items())) + f'; {len(ai_rows)} AI primitives ' +
          ', '.join(f'{k} {v}' for k, v in sorted(ai_counts.items())))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else OUT)

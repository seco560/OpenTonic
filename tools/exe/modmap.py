"""Assign TonicTrouble.exe functions to original source files / engine modules.

Evidence, strongest first:
  direct    the function references an assert/__FILE__ string such as X:\\Cpa\\Tempgrp\\GAM\\Engine.c
  export    the function is exported by name (the SND_* sound API)
  between   unlabeled functions lying between two functions of the same file
            (MSVC links each .obj contiguously, so a gap bounded by one file belongs to it)

Usage: python3 -m tools.exe.modmap out/exe/index.json out/exe/exports.txt out/exe/modmap.json
"""
import json
import re
import sys
from collections import Counter, defaultdict

SRC = re.compile(r'([A-Za-z0-9_]+)\.(c|cpp)$', re.I)

# Bare file names (no Tempgrp path) -> engine module, by OpenSpace naming conventions.
FILE_MODULE = [
    (r'^(snd|sound3d|sndthrd|sndseq|snderr|sndmem)', 'SND'),
    (r'^(geo|element)', 'GEO'),
    (r'^(hidcard|hmec)', 'MEC'),
    (r'^(ino|mousedi)', 'IPT'),
    (r'^(cpammg|cpaerror|cpastr|cpagmt|gmtvfunc)', 'CPA'),
    (r'^(tmp)', 'TMP'),
    (r'^timer', 'TMR'),
    (r'^drawpo', 'PO'),
    (r'^vignette', 'VIG'),
    (r'^inventor', 'GAM'),
    (r'^sectload', 'SCT'),
    (r'^(ai_|strintel|eng_stli|mth_|keyword)', 'AI'),
    (r'^engmain', 'MAIN'),
]


def source_of(s):
    """Normalise a path-like string to (module, file) or None."""
    m = SRC.search(s.strip('?@|L>'))
    if not m or re.search(r'\s', s.strip()):  # messages like "error in ... Demos.c" are not paths
        return None
    path = s.replace('/', '\\')
    parts = [p for p in path.split('\\') if p and p not in ('.', '..')]
    fname = parts[-1]
    for pat, module in FILE_MODULE:
        if re.match(pat, fname, re.I):
            return module, fname
    if 'Tempgrp' in parts or 'TempGrp' in parts or 'tempgrp' in [p.lower() for p in parts]:
        i = [p.lower() for p in parts].index('tempgrp')
        module = parts[i + 1].upper()
    elif '_' in fname[:5]:
        module = fname.split('_')[0].upper()   # IPT_Main.c, SCR_Mem.c, FIL_GF.c
    elif len(parts) > 1:
        module = parts[-2].upper()             # .\AIBase\AI_Proto.c, .\P5\Sectload.c
    else:
        module = '?'
    return module, fname


def build(index, exports):
    funcs = sorted((f for f in index['functions'] if not f['external']), key=lambda f: int(f['addr'], 16))
    label = {}
    for f in funcs:
        hits = Counter(src for s in f['strings'] if (src := source_of(s)))
        if hits:
            (module, fname), _ = hits.most_common(1)[0]
            label[f['addr']] = dict(module=module, file=fname, how='direct', candidates=len(hits))
    for addr, name in exports:
        a = f'{addr:08x}'
        if a not in label:
            label[a] = dict(module='SND', file=None, how='export')
    for i, f in enumerate(funcs):
        if f['addr'] in label:
            continue
        prev = next((label[g['addr']] for g in reversed(funcs[:i]) if g['addr'] in label), None)
        nxt = next((label[g['addr']] for g in funcs[i + 1:] if g['addr'] in label), None)
        if prev and nxt and prev['module'] == nxt['module']:
            same_file = prev['file'] and prev['file'] == nxt['file']
            label[f['addr']] = dict(module=prev['module'], file=prev['file'] if same_file else None, how='between')
    return funcs, label


def main(index_path, exports_path, out_path):
    index = json.load(open(index_path))
    exports = [(int(a, 16), n) for a, n in (l.split() for l in open(exports_path) if l.strip())]
    funcs, label = build(index, exports)
    by_mod = defaultdict(Counter)
    for f in funcs:
        l = label.get(f['addr'])
        by_mod[l['module'] if l else '(unknown)'][l['how'] if l else '-'] += 1
    json.dump({f['addr']: label.get(f['addr']) for f in funcs}, open(out_path, 'w'), indent=0)
    total = len(funcs)
    known = sum(1 for f in funcs if f['addr'] in label)
    print(f'{known}/{total} functions assigned ({100 * known // total}%)')
    for mod, c in sorted(by_mod.items(), key=lambda kv: -sum(kv[1].values())):
        print(f'  {mod:12s} {sum(c.values()):5d}  ' + ' '.join(f'{k}={v}' for k, v in c.items()))


if __name__ == '__main__':
    main(*sys.argv[1:4])

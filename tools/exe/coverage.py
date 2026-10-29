"""How much of TonicTrouble.exe has been read (docs/DECOMP.md).

    python3 -m tools.exe.coverage            # per-module table + the mechanics closure
    python3 -m tools.exe.coverage --unread 40 # also the largest unread functions on the mechanics path

Three tiers per function:
  documented  its address appears in docs/*.md, core/, signatures.tsv, names.tsv or ai_names.tsv (read and used)
  dumped      only a raw/typed decompile exists (files/out/_decomp_artifacts/decomp, typed/, col/, and all/ once the
              full dump is made: tools/ghidra/DumpAll.java, then every function is at least dumped): not yet read
  unseen      neither
The mechanics closure = everything statically reachable from the per-frame driver GAM_0042b520 plus the 13 handler
pairs of the 0x8C2240 table (indirect calls, so added by hand). Static closure over-approximates (allocator, sound
and GEO helpers come along) and misses the collision library's own function-pointer dispatch (FUN_00464440).
"""
import collections, glob, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART = os.path.join(ROOT, 'files/out/_decomp_artifacts')
HANDLERS = ('4aa6af 4ae392 4abbb8 4af8f0 4ab91d 4b00bf 4b2900 4b3810 4b0f2a 4ac0da 4aea7c 4aca45 4b11ea 4acb49 '
            '4b17d1 4ac62b 4af04e 4ad386 4b239c 4adfc3 4b252e 4adb4d 4b2793 4a9d20 4a7459').split()


def text(paths):
    out = []
    for p in paths:
        try:
            out.append(open(p, errors='ignore').read())
        except OSError:
            pass
    return '\n'.join(out)


def main(argv):
    unread = int(argv[argv.index('--unread') + 1]) if '--unread' in argv else 0
    funcs = {f['addr'].lower(): f for f in json.load(open(os.path.join(ART, 'exe/index.json')))['functions']
             if not f['external'] and not f['thunk'] and not f['addr'].startswith('EXT')}
    modmap = json.load(open(os.path.join(ART, 'exe/modmap.json')))

    def addrs(s):
        found = set()
        for m in re.finditer(r'(?:0x|_|\b)([0-9a-fA-F]{6,8})\b', s):
            a = m.group(1).lower().rjust(8, '0')
            if a in funcs:
                found.add(a)
        return found

    documented = addrs(text(glob.glob(os.path.join(ROOT, 'docs/*.md')) + glob.glob(os.path.join(ROOT, 'core/src/*'))
                            + glob.glob(os.path.join(ROOT, 'core/include/tonic/*.h'))
                            + [os.path.join(ROOT, 'tools/exe', n) for n in
                               ('signatures.tsv', 'names.tsv', 'ai_names.tsv')]))
    dumped = {os.path.basename(p)[:8] for p in glob.glob(os.path.join(ART, 'typed/**/*.c'), recursive=True)
              + glob.glob(os.path.join(ART, 'col/*.c')) + glob.glob(os.path.join(ART, 'all/c/*.c'))}
    for p in glob.glob(os.path.join(ART, 'decomp/*.txt')):
        dumped |= {m.group(1) for m in re.finditer(r'\w*?([0-9a-f]{8})\s*\(', open(p, errors='ignore').read())
                   if m.group(1) in funcs}

    def tier(a):
        return 'documented' if a in documented else 'dumped' if a in dumped else 'unseen'

    def module(a):
        return (modmap.get(a) or {}).get('module') or '?'

    def table(title, keyed):
        print(f'\n{title}\n{"":12}{"funcs":>6}{"KB":>7}{"documented":>13}{"dumped":>10}{"unseen":>10}   (KB)')
        tot = collections.Counter()
        for key, c in sorted(keyed.items(), key=lambda kv: -kv[1]['b']):
            tot.update(c)
            print(f'{key:12}{c["n"]:6}{c["b"]/1024:7.1f}{c["documented"]/1024:13.1f}{c["dumped"]/1024:10.1f}'
                  f'{c["unseen"]/1024:10.1f}')
        print(f'{"TOTAL":12}{tot["n"]:6}{tot["b"]/1024:7.1f}{tot["documented"]/1024:13.1f}{tot["dumped"]/1024:10.1f}'
              f'{tot["unseen"]/1024:10.1f}')

    def count(addresses, key):
        keyed = collections.defaultdict(collections.Counter)
        for a in addresses:
            c = keyed[key(a)]
            c['n'] += 1
            c['b'] += funcs[a]['size']
            c[tier(a)] += funcs[a]['size']
        return keyed

    table('Whole exe, by module (modmap; ? = no assert strings)', count(funcs, module))

    mech, stack = set(), ['0042b520'] + ['00' + h for h in HANDLERS]
    while stack:
        a = stack.pop()
        if a in mech or a not in funcs:
            continue
        mech.add(a)
        stack += [c.lower() for c in funcs[a]['callees']]

    def region(a):
        n = int(a, 16)
        if 0x4a7000 <= n < 0x4b9300: return 'DNM 4a7-4b9'
        if 0x4b9300 <= n < 0x4c1300: return 'MEC 4b9-4c1'
        if 0x4c1300 <= n < 0x4cb000: return 'MTH 4c1-4cb'
        if 0x45fd70 <= n < 0x472000: return 'COL 460-472'
        if 0x47a000 <= n < 0x480000: return 'MTX 47a-480'
        if 0x43b000 <= n < 0x43e000: return 'SWP 43b-43e'
        return module(a)

    table('Mechanics closure, by region', count(mech, region))
    if unread:
        print('\nLargest not-documented functions on the mechanics path:')
        for a in sorted((a for a in mech if a not in documented), key=lambda a: -funcs[a]['size'])[:unread]:
            print(f'  {a}  {funcs[a]["size"]:5}  {tier(a):7}  {region(a)}')


if __name__ == '__main__':
    main(sys.argv[1:])

"""Queries over the full dump of TonicTrouble.exe (docs/EXE.md "Full dump"; made by tools/ghidra/DumpAll.java).

    python3 -m tools.exe.xref fn X                # summary: module, size, signature, callers, callees, globals, fields
    python3 -m tools.exe.xref show X              # the decompile
    python3 -m tools.exe.xref callers X [-d N]    # who calls X (N levels up, default 1)
    python3 -m tools.exe.xref callees X [-d N]    # what X calls
    python3 -m tools.exe.xref path A B            # a shortest call path from A to B
    python3 -m tools.exe.xref taken X             # who takes X's address in code, and where data holds it (tables)
    python3 -m tools.exe.xref field PATTERN       # who writes / reads a struct field: "Dynam.ground", "MecDyn@0xd8", "speed"
    python3 -m tools.exe.xref global PATTERN      # who writes / reads a global: name or address ("004ec6ac", "g_ed")
    python3 -m tools.exe.xref string TEXT         # functions using a string (case-insensitive substring)
    python3 -m tools.exe.xref grep REGEX [-m MOD] # the decompiles' lines matching REGEX (only module MOD's functions)
    python3 -m tools.exe.xref stats               # what the dump holds

X is an address (0x415c00, 00415c00, 415c00), a name, or a unique part of a name. Field and global matches are
case-insensitive substrings of "Struct.field@0xOFFSET" / "name@address". Fields come from typed accesses only
(`so->data.perso`): untyped ones (`*(float *)(param_1 + 0xd8)`) are found with grep. Indirect calls aren't callers:
see `taken` for who stores a function's address, and in which data (aligned values equal to its entry; tables filled
at run time, beyond the image, can't be seen).
"""
import collections
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART = os.path.join(ROOT, 'files/out/_decomp_artifacts')
ALL = os.path.join(ART, 'all')


class Dump:
    def __init__(self):
        path = os.path.join(ALL, 'index.json')
        if not os.path.exists(path):
            sys.exit(f'no {path}: make it with tools/ghidra/DumpAll.java (docs/EXE.md "Full dump")')
        data = json.load(open(path))
        self.fns = {f['addr']: f for f in data['functions']}
        self.externals = data.get('externals', {})
        try:
            self.modmap = json.load(open(os.path.join(ART, 'exe/modmap.json')))
        except OSError:
            self.modmap = {}
        self.callers = collections.defaultdict(set)
        self.takers = collections.defaultdict(set)
        for a, f in self.fns.items():
            for c in f['callees']:
                self.callers[c].add(a)
            for t in f['taken']:
                self.takers[t].add(a)

    def module(self, a):
        return (self.modmap.get(a) or {}).get('module') or '?'

    def find(self, x):
        """The address of X, or exit with the candidates."""
        h = x.lower().removeprefix('0x')
        if re.fullmatch(r'[0-9a-f]{5,8}', h) and h.rjust(8, '0') in self.fns:
            return h.rjust(8, '0')
        exact = [a for a, f in self.fns.items() if f['name'] == x]
        if exact:
            return exact[0]
        part = [a for a, f in self.fns.items() if x.lower() in f['name'].lower()]
        if len(part) == 1:
            return part[0]
        if not part:
            sys.exit(f'no function matches {x!r}')
        sys.exit(f'{x!r} matches {len(part)} functions:\n' + '\n'.join(self.line(a) for a in sorted(part)[:40]))

    def line(self, a):
        f = self.fns.get(a)
        if not f:
            return f'{self.externals[a]} (import)' if a in self.externals else f'{a} (not a function)'
        return f"{a} {f['name']}  [{self.module(a)}] {f['size']} B" + ('  (decompile failed)' if 'error' in f else '')


def tree(d, start, edges, depth):
    seen = {start}
    def walk(a, level):
        for b in sorted(edges(a)):
            print('  ' * level + d.line(b) + ('  ...' if b in seen and level < depth else ''))
            if b not in seen and level < depth:
                seen.add(b)
                walk(b, level + 1)
    print(d.line(start))
    walk(start, 1)


def matches(d, key, pattern):
    """{(name, mode): [addr...]} for fields or globals whose name contains pattern."""
    p = pattern.lower()
    out = collections.defaultdict(list)
    for a, f in d.fns.items():
        if key == 'fields':
            for name in f['fields_written']:
                if p in name.lower():
                    out[(name, 'w')].append(a)
            for name in f['fields_read']:
                if p in name.lower():
                    out[(name, 'r')].append(a)
        else:
            for name, mode in f['globals'].items():
                if p in name.lower():
                    for m in ('w', 'r') if mode == 'rw' else (mode,):
                        out[(name, m)].append(a)
    return out


def report(d, found, pattern):
    if not found:
        sys.exit(f'nothing matches {pattern!r}')
    names = sorted({n for n, _ in found})
    if len(names) > 30:
        print(f'{len(names)} names match {pattern!r}; narrow it down. Some:')
        for n in names[:30]:
            print('  ' + n)
        return
    labels = {'w': 'written by', 'r': 'read by', '&': 'address used by'}
    for n in names:
        print(n)
        for m in ('w', 'r', '&'):
            fs = sorted(found.get((n, m), []))
            if fs:
                print(f'  {labels[m]} {len(fs)}:')
                for a in fs:
                    print('    ' + d.line(a))


def main(argv):
    if not argv or argv[0] in ('-h', '--help'):
        sys.exit(__doc__)
    cmd, args = argv[0], argv[1:]
    depth = int(args[args.index('-d') + 1]) if '-d' in args else 1
    mod = args[args.index('-m') + 1] if '-m' in args else None
    args = [x for i, x in enumerate(args) if x not in ('-d', '-m') and (i == 0 or args[i - 1] not in ('-d', '-m'))]
    d = Dump()
    if cmd == 'stats':
        fs = list(d.fns.values())
        print(f"{len(fs)} functions, {sum(f['size'] for f in fs)} bytes of code, "
              f"{sum('error' in f for f in fs)} decompiles failed")
        print(f"{sum(len(f['fields_read']) + len(f['fields_written']) for f in fs)} field accesses, "
              f"{len({n for f in fs for n in f['fields_read'] + f['fields_written']})} distinct fields; "
              f"{len({n for f in fs for n in f['globals']})} globals; "
              f"{sum(len(f['taken']) for f in fs)} function addresses taken")
        by = collections.Counter(d.module(a) for a in d.fns)
        print('by module: ' + ', '.join(f'{m} {n}' for m, n in by.most_common()))
        failed = [a for a, f in d.fns.items() if 'error' in f]
        for a in failed[:20]:
            print(f"  failed: {d.line(a)}: {d.fns[a]['error'].strip()[:100]}")
    elif cmd == 'show':
        a = d.find(args[0])
        print(open(os.path.join(ALL, 'c', d.fns[a]['file'])).read())
    elif cmd == 'fn':
        a = d.find(args[0])
        f = d.fns[a]
        print(d.line(a))
        print(f"  {f['signature'] or '(no signature)'}")
        print(f"  file: {os.path.relpath(os.path.join(ALL, 'c', f['file']), ROOT)}")
        for title, xs in (('callers', d.callers.get(a, ())), ('callees', f['callees']),
                          ('address taken by', d.takers.get(a, ())), ('takes the address of', f['taken']),
                          ('stored in data at', f.get('stored_at', ()))):
            if xs:
                print(f'  {title} ({len(xs)}):')
                for b in sorted(xs):
                    print('    ' + d.line(b))
        if f['globals']:
            print(f"  globals: " + ', '.join(f'{n} ({m})' for n, m in f['globals'].items()))
        if f['fields_written']:
            print(f"  fields written: " + ', '.join(f['fields_written']))
        if f['fields_read']:
            print(f"  fields read: " + ', '.join(f['fields_read']))
        if f['strings']:
            print(f"  strings: " + ', '.join(repr(s) for s in f['strings'][:12]))
    elif cmd in ('callers', 'callees'):
        a = d.find(args[0])
        edges = (lambda x: d.callers.get(x, ())) if cmd == 'callers' else (lambda x: d.fns[x]['callees'] if x in d.fns else ())
        tree(d, a, edges, depth)
    elif cmd == 'path':
        a, b = d.find(args[0]), d.find(args[1])
        prev, todo = {a: None}, collections.deque([a])
        while todo and b not in prev:
            x = todo.popleft()
            for y in d.fns[x]['callees'] if x in d.fns else ():
                if y not in prev:
                    prev[y] = x
                    todo.append(y)
        if b not in prev:
            sys.exit(f'no static call path from {d.line(a)} to {d.line(b)} (indirect calls: try `taken`)')
        chain = []
        while b is not None:
            chain.append(b)
            b = prev[b]
        for i, x in enumerate(reversed(chain)):
            print('  ' * i + d.line(x))
    elif cmd == 'taken':
        a = d.find(args[0])
        print(d.line(a))
        for b in sorted(d.takers.get(a, ())):
            print('  taken by ' + d.line(b))
        for at in d.fns[a].get('stored_at', ()):
            print('  stored in data at ' + at)
    elif cmd in ('field', 'global'):
        report(d, matches(d, 'fields' if cmd == 'field' else 'globals', args[0]), args[0])
    elif cmd == 'string':
        p = args[0].lower()
        for a, f in sorted(d.fns.items()):
            hits = [s for s in f['strings'] if p in s.lower()]
            if hits:
                print(d.line(a) + '  ' + ', '.join(repr(s) for s in hits[:4]))
    elif cmd == 'grep':
        rx = re.compile(args[0])
        for a, f in sorted(d.fns.items()):
            if mod and d.module(a).lower() != mod.lower():
                continue
            lines = open(os.path.join(ALL, 'c', f['file']), errors='ignore').read().splitlines()
            hits = [(i + 1, l.strip()) for i, l in enumerate(lines) if rx.search(l)]
            if hits:
                print(d.line(a))
                for n, l in hits[:8]:
                    print(f'  {n}: {l[:160]}')
                if len(hits) > 8:
                    print(f'  ... {len(hits) - 8} more')
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])

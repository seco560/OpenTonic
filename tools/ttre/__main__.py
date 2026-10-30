"""Usage:
  python3 -m tools.ttre cnt-list ARCHIVE
  python3 -m tools.ttre cnt-verify ARCHIVE
  python3 -m tools.ttre cnt-extract ARCHIVE OUTDIR [--png]
  python3 -m tools.ttre level-info LEVEL
  python3 -m tools.ttre level-export LEVEL OUTDIR [--no-preview] [--no-persos]
  python3 -m tools.ttre level-ai LEVEL OUT.txt   (Ed's AI behaviours as indented trees, plus primitive usage counts)
  python3 -m tools.ttre level-ttai LEVEL OUT.ttai   (Ed's AI for the core's interpreter; format: see tools/ttre/ai.py)
  python3 -m tools.ttre level-ttlv LEVEL OUT.ttlv [--no-persos]   (for Open Tonic; current format: see tools/ttre/ttlv.py)
Level commands read from GAMEDATA/WORLD/LEVELS textures from files/out/Textures (so, generate textures before the levels)
"""
import os
import struct
import sys

from . import cnt, gf

# Run generation commands from project root directory
LEVELS = 'files/sources/GAMEDATA/WORLD/LEVELS'

def level_cmd(cmd, argv):
    from . import export, level, sna
    data = level.LevelData(sna.Level(LEVELS, argv[0]))
    sos = list(data.walk())
    geos = data.geometrics.values()
    print(f'{argv[0]}: {sum(s.kind == "sector" for s in sos)} sectors, {sum(s.kind == "ipo" for s in sos)} IPOs, '
          f'{len(geos)} geometric objects, {sum(len(g.vertices) for g in geos)} vertices, '
          f'{sum(len(e.triangles) for g in geos for e in g.elements)} triangles, {len(data.materials)} materials; '
          f'PTX textures fix {len(data.fix_textures)} + level {len(data.level_textures)}; '
          f'pointers {len(data.lv.pointers)} ({len(data.lv.unresolved)} unresolved)')
    persos = data.persos()
    print(f'  persos: {len(persos)} in dynamic world ({len(data.perso_in_fix)} from fix), '
          f'{sum(bool(p.parts) for p in persos)} with geometry, {sum(len(p.parts) for p in persos)} parts')
    with_persos = '--no-persos' not in argv
    for w in data.warnings:
        print('warning:', w, file=sys.stderr)
    shown = len(data.warnings)
    if cmd == 'level-ai':
        import collections
        from . import ai
        a = ai.AI(data, data.player())
        ai.dump(a, argv[1])
        use = collections.Counter((ai.KINDS[n.type], n.param) for n in a.nodes() if n.type in ai.KINDS)
        with open(os.path.splitext(argv[1])[0] + '_usage.tsv', 'w') as f:
            for (kind, idx), c in sorted(use.items()):
                f.write(f'{kind}\t{idx}\t{c}\n')
        print(f'wrote {argv[1]}: {sum(1 for b in a.behaviours if not b.reflex)} behaviours, '
              f'{sum(1 for b in a.behaviours if b.reflex)} reflexes, {sum(1 for _ in a.nodes())} nodes; '
              f'{len(use)} distinct primitives')
    if cmd == 'level-ttai':
        from . import ai
        r = ai.export(ai.AI(data, data.player()), argv[1])
        print(f"wrote {argv[1]} ({r['bytes']} bytes): {r['nodes']} nodes, {r['scripts']} scripts, behaviours "
              f"{r['behaviours']} (initial {r['initial']}), {r['dsgvars']} dsgvars, other persos {r['persos']}, {r['states']} states "
              f"(initial {r['initial_state']}, {r['transitions']} transitions; moving {r['moving']}), {len(r['buttons'])} buttons, "
              f"strings {r['strings']}, root motion {r['root_states']} states, sphere sets {r['sphere_sets']}, "
              f"unresolved {r['unresolved']}")
        names = [n.decode('latin-1') for n in r['portal_names']]
        ops = {ai.POP_GUARD: 'guard', ai.POP_SET: 'set', ai.POP_FORCE: 'force', ai.POP_FORCE_REFLEX: 'force-reflex',
               ai.POP_COVER: 'cover', ai.POP_WAIT: 'wait', ai.POP_TELEPORT: 'teleport', ai.POP_CAMERA: 'camera',
               ai.POP_LEVEL: 'level'}
        def show(op):
            o, kind, a, b, c, v = op
            if o == ai.POP_SET:
                return f'set dsg{a}=' + (f'{struct.unpack("<i", struct.pack("<I", b))[0]}', f'{v[0]:g}', 'self', f'({v[0]:.0f}, {v[1]:.0f}, {v[2]:.0f})')[kind]
            if o == ai.POP_GUARD:
                return f'guard dsg{a}=={b}'
            if o in (ai.POP_FORCE, ai.POP_FORCE_REFLEX):
                return f'{"B" if o == ai.POP_FORCE else "R"}{a}'
            if o == ai.POP_WAIT:
                return f'wait {b}ms'
            if o in (ai.POP_TELEPORT, ai.POP_CAMERA):
                return f'{ops[o]} ({v[0]:.1f}, {v[1]:.1f}, {v[2]:.1f})'
            if o == ai.POP_LEVEL:
                at = f' at {names[c]} / {names[a]}' if c != 0xFFFFFFFF else ''
                return f'LEVEL {names[b]}{at}'
            return ops[o]
        print(f"  portals {len(r['portals'])}, places {r['places']}" +
              (f", missing effect textures {r['missing_effects']}" if r['missing_effects'] else ''))
        for pos, radius, _, _, fire, fire_dsg, fire_value, cap, fire_ops, (fam, inst) in r['portals']:
            when = f'dsg{fire_dsg}=={fire_value}' if fire == 0 else 'covered' if fire == 1 else 'never (capture only)'
            print(f"    {inst} @({pos[0]:.0f}, {pos[1]:.0f}, {pos[2]:.0f}) r {radius:g}: "
                  f"{'; '.join(show(o) for o in cap)}  |  on {when}: {'; '.join(show(o) for o in fire_ops)}")
        for off, mask, kind, lo, hi in r['buttons']:
            print(f'  button {off:#x}: {[n for i, n in enumerate(ai.INPUTS) if mask >> i & 1]} analog {kind} [{lo}, {hi}]')
    if cmd == 'level-ttlv':
        from . import ttlv
        nm, ntri, ntex, missing, npe, npa, ncol, ed = ttlv.write(data, argv[1], persos=with_persos)
        print(f'wrote {argv[1]}: {nm} static meshes, {ntri} triangles, {ntex} textures, {len(missing)} not found; '
              f'{npe} animated persos, {npa} parts; {ncol} collision triangles; Ed anims: {", ".join(n for n in ed if not n[1:].isdigit()) or "none"}'
              f' + {sum(n[1:].isdigit() for n in ed)} state animations')
    if cmd == 'level-export':
        out = argv[1]
        nm, missing = export.write_obj(data, out)
        print(f'wrote {out}/{argv[0]}.obj ({nm} materials, {len(missing)} textures not found)')
        for m in missing:
            print('  missing texture:', m, file=sys.stderr)
        if '--no-preview' not in argv:
            for view in ('top', 'persp'):
                export.render_preview(data, os.path.join(out, f'preview_{view}.png'), view=view, persos=with_persos)
            print(f'wrote {out}/preview_top.png, preview_persp.png')
    for w in data.warnings[shown:]:  # raised while writing (e.g. LevelData.sectors, ttlv.Sections)
        print('warning:', w, file=sys.stderr)


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    cmd, path = argv[0], argv[1]
    if cmd.startswith('level-'):
        return level_cmd(cmd, argv[1:])
    a = cnt.Cnt(path)
    if cmd == 'cnt-list':
        for e in a.entries:
            print(f'{e.offset:10d} {e.size:9d}  {e.path}')
        print(f'{len(a.dirs)} dirs, {len(a.entries)} files', file=sys.stderr)
    elif cmd == 'cnt-verify':
        bad = a.verify()
        for e in bad:
            print(f'BAD {e.path} @ {e.offset}+{e.size}')
        print(f'{len(a.entries) - len(bad)}/{len(a.entries)} entries OK')
    elif cmd == 'cnt-extract':
        out, png = argv[2], '--png' in argv
        failed = 0
        for e in a.entries:
            dest = os.path.join(out, *e.path.split('\\'))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            data = a.read(e)
            if png and dest.lower().endswith('.gf'):
                try:
                    data = gf.to_png(*gf.decode(data))
                    dest = dest[:-3] + '.png'
                except Exception as ex:
                    failed += 1
                    print(f'decode failed: {e.path}: {ex}', file=sys.stderr)
            with open(dest, 'wb') as f:
                f.write(data)
        print(f'extracted {len(a.entries)} files ({failed} decode failures)')
    else:
        sys.exit(__doc__)


main(sys.argv[1:])

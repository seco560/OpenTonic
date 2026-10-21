"""Usage:
  python3 -m tools.ttre cnt-list ARCHIVE
  python3 -m tools.ttre cnt-verify ARCHIVE
  python3 -m tools.ttre cnt-extract ARCHIVE OUTDIR [--png]
  python3 -m tools.ttre level-info LEVEL
  python3 -m tools.ttre level-export LEVEL OUTDIR [--no-preview] [--no-persos]
  python3 -m tools.ttre level-ttlv LEVEL OUT.ttlv [--no-persos]   (for port/ TonicViewer; v5 adds collision + Ed)
Level commands read from GAMEDATA/WORLD/LEVELS textures from out/Textures (so, generate textures before the levels)
"""
import os
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
    if cmd == 'level-ttlv':
        from . import ttlv
        nm, ntri, ntex, missing, npe, npa, ncol, ed = ttlv.write(data, argv[1], persos=with_persos)
        print(f'wrote {argv[1]}: {nm} static meshes, {ntri} triangles, {ntex} textures, {len(missing)} not found; '
              f'{npe} animated persos, {npa} parts; {ncol} collision triangles; Ed anims: {", ".join(ed) or "none"}')
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

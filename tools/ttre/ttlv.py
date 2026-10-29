""".ttlv, a level for the core (reader: core/src/tc_level.c), in the chunk container of chunks.py. Little-endian.

Chunks, in file order (* = only when the level has one; every other chunk is required):
  LEVL  u32 light_set_count (baked radiosity sets per vertex, 1-3: set 0 everywhere, 1-2 only in Land / sud0; LAIS
        persos change the set shown, number keys pick one), f32 fog_near, f32 fog_far, f32 fog_rgb[3] (linear by view
        depth; fog_far <= fog_near = no fog; LevelData.fog: the visual environment most sectors use)
  LMAP* u16 len + utf8 path (relative to the .ttlv) of the per-triangle lightmap atlas PNG: 32x32 tiles in a square
        power of two. The lightmap value replaces the baked light (same x255/128 scale).
  LAIS* the level persos that set the baked light set (ai.light_persos: sud0's day / night cycle, Land's lightning),
        run by the core: u16 n, u16 0, n x { u32 size, size bytes = a .ttai SCPT chunk's body (their scripts,
        behaviours and dsgvars; ai.py), padded to 4 }. Their SetLightSet (PROC_283) changes the set and blends to it;
        without them a level stays on set 0.
  TEXS  u32 n, n x { u16 len, utf8 path relative to the .ttlv }
  MESH  u32 n, n x { surface, geometry }: the static world, engine world space
  PRSO  u32 n, n x {
          f32 world[12] (row-major 3x4, perso -> world)
          u16 n_channels, u16 n_samples, f32 sample_rate (samples per second; loop = n_samples / rate)
          n_channels x i16 parent channel at frame 0 (-1 = root; informational, matrices are composed)
          u32 part_count, part_count x { u16 channel, u16 object, surface, geometry (channel space, object-list scale
                                         applied) }
          n_samples x n_channels x f32[12] channel -> perso transform (row-major 3x4)
          n_samples x n_channels x u16 object shown by the channel (0xFFFF = none) }
  COLL  u32 n_triangles, n_triangles x f32[9]: collision, world space (LevelData.collision: static collide sets + perso
        collide meshes at frame 0). The .ttai's per-triangle tables follow this order; its LINK chunk holds this
        chunk's CRC-32.
  FLOR  u16 n_floors, u16 0, n_floors x { f32 grip, f32 rebound } (the mechanics material of the triangle's
        GameMaterial: grip = mat[2], rebound = mat[1]; NOTES "Floor mechanical material"),
        n_triangles x u16 floor index (0xFFFF = no material: grip 1, rebound 1)
  EDPL* the player (Ed, or the skier):
          f32 start[12] (row-major 3x4: the placement, GPT perso-in-fix matrix; he faces local -Y)
          i32 static_perso (his own entry in PRSO, -1 = none: hidden in play mode)
          u32 part_count, part_count x { u16 object, u16 0, surface, geometry (object space, scale applied) }
          u16 n_anims, u16 0, n_anims x {
            u16 name_len, utf8 name ("idle", "walk", "run", "jump", "jump_air", "fall", "land"),
            u16 n_channels, u16 n_samples, f32 sample_rate (as for persos, ANIM_TIME_SCALE applied),
            u8 loop, u8 0, u16 0, f32 ground_speed (units/s the feet cover at sample_rate: 2 x the feet's horizontal
            stride / loop length; 0 = not locomotion),
            n_samples x n_channels x f32[12] channel -> Ed, n_samples x n_channels x u16 object (0xFFFF none) }
        Parts are keyed by object only: channel numbering differs between his animations. After the 7 named clips come
        the state animations for the AI: one per distinct A3D, named "s<k>" (k = the first state using it; .ttai states
        point at it), one sample per animation frame, sample_rate = the A3D's own frames per second (no
        ANIM_TIME_SCALE; the AI's state clock picks the frame), loop 0.

  surface  = { i32 texture (-1 = none), u32 flags (TC_FLAG_*: alpha test / blend, and WRAP_U / WRAP_V: clamped
               unless set), f32 scroll[2] (UV per second: uv' = uv + scroll * t),
               u16 n_anim, u16 0, n_anim x { i32 texture, f32 seconds } (animated texture loop; 0 = static) }
  geometry = { u32 n_vertices, u32 n_indices, n_vertices x vertex, n_indices x u32 }
  vertex (52 bytes, = tc_vertex) = f32 pos[3], f32 normal[3], f32 uv[2], u32 rgba8 x 3 (the baked sets: radiosity
             clamped to 0..255, light = rgb / 128, see level.RADIOSITY_ONE; alpha 255 = baked, 0 = none: the viewer's
             fallback lighting, as for all perso parts), f32 uv2[2] (lightmap atlas coordinates, top-left origin;
             -1 = none; lightmapped triangles get their own vertices)
  Sprites (TC_FLAG_BILLBOARD meshes): every vertex holds the centre, normal.xy = the corner offset; unlit.

One mesh/part per material, LOD 0 only, Z up. Samples: the A3D is evaluated (keyframe lerp, see level.Anim) at
k = round(60 / fps) samples per animation frame. UVs are converted to top-left origin (v' = 1 - v; engine v=0 is the
image's bottom row). The engine subtracts its scroll offset (inferred: only then does sud0's rainbow pool flow over its
edge and down the falls). Engine time unit = 1/60 s (scroll speed and texture frame durations are per 1/60 s).
"""
import math
import os
import struct

from . import ai, chunks, export, gf, level

ALPHA_TEST = 1 << 0  # TC_FLAG_ALPHA_TEST: a cut-out (texture flags 0x12, see surface())
TRANSPARENT = 1 << 1  # TC_FLAG_TRANSPARENT: alpha blended (texture flag 0x8, see surface())
BILLBOARD = 1 << 2  # TC_FLAG_BILLBOARD: sprite quads, expanded towards the camera by the vertex shader
WRAP_U = 1 << 4  # TC_FLAG_WRAP_U / _V: the texture repeats along u / v, else it is clamped (see tiling())
WRAP_V = 1 << 5
# Empirical: the user compared against the original running in Wine and 0.25x matched.
# The exe's formula (frame += dt_ms/1000 * state FrameRate @0x3B, FUN_00415fb0) gives 1x, so some
# unit is still unexplained (open question in NOTES). Applied to persos only, not textures.
ANIM_TIME_SCALE = 0.25
# Empirical too: the user saw the portal flipbooks (GEN_ARCH01-04 and mirrored GEN_ARCH..R; Land,
# carota, Totalski; 4 frames x 5.4 ticks) play far too fast, while lava flipbooks and scrolls looked
# right. Texture-name prefix (case-insensitive, file name only) -> factor on the playback rate, so
# 0.25 makes each frame last 4x longer. Hypothesis: the same unexplained 4x as ANIM_TIME_SCALE.
TEXTURE_TIME_SCALE = {'GEN_ARCH': 0.5}  # user-matched: 0.25 was too slow, 2x that looked natural
LIGHT_SETS = 3  # baked radiosity sets per vertex (set 0 everywhere; 1-2 only in Land/sud0)


def texture_time_scale(tex):
    name = tex.name.replace('\\', '/').rsplit('/', 1)[-1].upper() if tex else ''
    return next((v for k, v in TEXTURE_TIME_SCALE.items() if name.startswith(k.upper())), 1.0)


def tiling(name):
    """WRAP_U / WRAP_V for a texture (fitted_surface then widens them per mesh), from its file name as the exe's
    GLIGLOU_Tex_0043f220 reads it (the result goes to the driver's texture +0x155, which GliDX6vr.dll turns into D3D
    ADDRESSU / ADDRESSV; DECOMP.md "Read from GliDX6vr.dll"). The name's stem (an "NZ" cut-out suffix skipped) ending in TXY repeats both ways, TY repeats u,
    TX repeats v (raymap's "repeat U" bit agrees); else "_TXY" / "_TX" / "_TY" anywhere in it; anything else is
    clamped on both axes. The level data's TextureInfo flags_byte agrees for 916 of 922 textures (6 levels); the
    name is what the exe uses."""
    s = os.path.splitext(os.path.basename(name.replace('\\', '/')))[0].upper()
    stem = s[:-2] if s.endswith('NZ') else s
    if stem.endswith('TXY'):
        rule = 3
    elif stem.endswith('TX'):
        rule = 1
    elif stem.endswith('TY'):
        rule = 2
    else:
        rule = 3 if '_TXY' in s else 1 if '_TX' in s else 2 if '_TY' in s else 0
    return (WRAP_U if rule & 2 else 0) | (WRAP_V if rule & 1 else 0)


def fitted_surface(m):
    """m's surface with the wrap flags widened to its geometry: an axis tiling() clamps is repeated anyway where the
    mesh's UVs leave 0..1 on it, or the surface scrolls. The exe clamps those too as far as read, but ~130 elements
    in the 21 levels would then smear their edge texels (73 are the LAVAGAIN lava flipbook, also MORT, PORTE, NG_04,
    OMBRE), which the original doesn't show: some handling of them is still unread (DECOMP.md "Read from
    GliDX6vr.dll"). Clamping keeps its use where it is safe: no filtering seams along in-range texture edges."""
    surf = m['surface']
    tex, flags, su, sv = struct.unpack_from('<iI2f', surf)
    if tex < 0 or flags & (WRAP_U | WRAP_V) == WRAP_U | WRAP_V:
        return surf
    out = lambda k: any(v[k] < -0.01 or v[k] > 1.01 for v in m['verts'])
    if su or sv or out(6):
        flags |= WRAP_U
    if su or sv or out(7):
        flags |= WRAP_V
    return surf[:4] + struct.pack('<I', flags) + surf[8:]


def has_alpha(png_path, cache={}):
    if png_path not in cache:
        _, _, rgba = export.read_png(png_path)
        cache[png_path] = any(a < 255 for a in rgba[3::4])
    return cache[png_path]


def rotate(m, n):
    """Apply the 3x3 part of a row-major 3x4 transform to a normal and renormalise."""
    x, y, z = (m[0] * n[0] + m[1] * n[1] + m[2] * n[2], m[4] * n[0] + m[5] * n[1] + m[6] * n[2],
               m[8] * n[0] + m[9] * n[1] + m[10] * n[2])
    l = math.sqrt(x * x + y * y + z * z)
    return (x / l, y / l, z / l) if l > 1e-9 else (0.0, 0.0, 0.0)


class LightmapAtlas:
    """Packs per-triangle lightmaps (TILE x TILE) into one square power-of-two image, tiles in first-use order."""
    TILE = 32
    CORNERS = ((0, 0), (1, 0), (0, 1))  # triangle vertex a, b, c -> lightmap corner (image top-left origin)

    def __init__(self, idx):
        self.idx, self.slots = idx, {}

    def uv(self, tex, corner):
        """Atlas coordinates (0..1, top-left origin) of a corner's texel centre, or None if the image is missing."""
        if tex not in self.slots:
            png = export.tex_png(self.idx, tex)
            if png is None:
                self.slots[tex] = None
            else:
                w, h, px = export.read_png(png)
                self.slots[tex] = (len([s for s in self.slots.values() if s]), w, h, px)
        s = self.slots[tex]
        if s is None:
            return None
        i, w, h, _ = s
        cu, cv = self.CORNERS[corner]
        n = self.side() // self.TILE
        # Legs end 4 texels short of the edge: the lightmapper fades the last ~3 texels before the hypotenuse
        # to black (anti-aliased triangle edge), which would show as a dark seam on every long edge.
        x = (i % n) * self.TILE + 0.5 + cu * (w - 5)
        y = (i // n) * self.TILE + 0.5 + cv * (h - 5)
        return x / self.side(), y / self.side()

    def side(self):
        """Fixed once the first tile is placed: room for every lightmap the level references (counted up front)."""
        return self._side

    def plan(self, count):
        n, self._side = max(1, math.ceil(math.sqrt(count))), self.TILE
        while self._side < n * self.TILE:
            self._side *= 2

    def png(self):
        a, n = self.side(), self.side() // self.TILE
        img = bytearray(a * a * 4)
        for s in self.slots.values():
            if s is None:
                continue
            i, w, h, px = s
            x0, y0 = (i % n) * self.TILE, (i // n) * self.TILE
            for y in range(min(h, self.TILE)):
                for x in range(min(w, self.TILE)):
                    # The triangle fills the upper-left half up to x + y = w - 2 (the diagonal itself is black);
                    # mirror it into the rest so filtering at the hypotenuse doesn't pull in black.
                    sx, sy = (x, y) if x + y <= w - 2 else (max(0, w - 2 - y), max(0, h - 2 - x))
                    o, so = ((y0 + y) * a + x0 + x) * 4, (sy * w + sx) * 4
                    img[o:o + 4] = px[so:so + 4]
        return gf.to_png(a, a, bytes(img))


def add_geometry(meshes, key, geo, m, new_mesh, atlas=None):
    """Append geo transformed by m to meshes[material] (created by new_mesh(material)), with the
    key's baked radiosity as rgba8 (alpha 0 = none) and, given an atlas, per-triangle lightmap
    coordinates (uv2; -1 = none)."""
    verts = [level.mat_apply(m, v) for v in geo.vertices]
    def packed(baked):
        return [struct.unpack('<I', bytes((*(min(max(x, 0), 255) for x in c), 255)))[0] for c in baked] if baked else None
    sets = [packed(level.baked_colors(key, i)) for i in range(LIGHT_SETS)]
    norms = [rotate(m, n) for n in geo.normals] if geo.normals else None  # fine for uniform scale
    flip = level.mat_det(m) < 0
    for el in geo.elements:
        mesh = meshes.get(el.material)
        if mesh is None:
            mesh = meshes[el.material] = dict(new_mesh(el.material), verts=[], index={}, indices=[])
        for k, tri in enumerate(el.triangles):
            uvt = el.uv_tris[k] if el.uv_tris else (None,) * 3
            lm = el.lightmaps[k] if atlas and k < len(el.lightmaps) else None
            uv2 = [atlas.uv(lm, c) for c in range(3)] if lm else [None] * 3  # corners follow the stored order
            if flip:
                tri, uvt, uv2 = tri[::-1], uvt[::-1], uv2[::-1]
            for vi, ti, l2 in zip(tri, uvt, uv2):
                u, v = el.uvs[ti] if ti is not None and el.uvs else (0.0, 0.0)
                vk = (key, vi, ti) if l2 is None else (key, vi, ti, id(el), k)  # lightmapped: own vertices
                if vk not in mesh['index']:
                    mesh['index'][vk] = len(mesh['verts'])
                    mesh['verts'].append((*verts[vi], *(norms[vi] if norms else (0.0, 0.0, 0.0)), u, 1.0 - v,
                                          *(c[vi] if c else 0 for c in sets), *(l2 or (-1.0, -1.0))))
                mesh['indices'].append(mesh['index'][vk])
    # Sprites: a quad per sprite in a BILLBOARD mesh; every vertex holds the centre (transformed by m) and
    # carries its corner offset (half extents x the object's scale) in the normal slot, unlit (no colour).
    sx = math.sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8])
    for ci, hx, hy, mat in geo.sprites:
        mesh = meshes.get(('sprite', mat))
        if mesh is None:
            base = new_mesh(mat)
            surf = bytearray(base['surface'])
            struct.pack_into('<I', surf, 4, struct.unpack_from('<I', surf, 4)[0] | BILLBOARD)
            mesh = meshes[('sprite', mat)] = dict(base, surface=bytes(surf), verts=[], index={}, indices=[])
        first = len(mesh['verts'])
        for dx, dy, u, v in ((-1, -1, 0, 1), (1, -1, 1, 1), (-1, 1, 0, 0), (1, 1, 1, 0)):
            mesh['verts'].append((*verts[ci], dx * hx * sx, dy * hy * sx, 0.0, u, v, *(0,) * LIGHT_SETS, -1.0, -1.0))
        mesh['indices'] += [first, first + 1, first + 2, first + 2, first + 1, first + 3]


def write(data, path, tex_root='files/out/Textures', persos=True):
    idx = export.texture_index(tex_root)
    out_dir = os.path.dirname(os.path.abspath(path))
    tex_paths, tex_ids, missing = [], {}, set()

    def texture_id(tex):
        png = export.tex_png(idx, tex)
        if png is None:
            if tex:
                missing.add(tex.name)
            return -1, 0
        if png not in tex_ids:
            tex_ids[png] = len(tex_paths)
            tex_paths.append(os.path.relpath(png, out_dir).replace(os.sep, '/'))
        return tex_ids[png], ALPHA_TEST if has_alpha(png) else 0

    def surface(mat):
        tid, alpha = texture_id(mat.texture) if mat else (-1, 0)
        # GliDX6vr.dll's choice (GLI_DRV_vDoOpaqueTextureSelection, DECOMP.md "Read from GliDX6vr.dll"), on the
        # texture flags: 0x12 (0x2 | 0x10: *_nz teeth, cloth, foliage) = a cut-out, alpha-tested at 225/255, before
        # anything else; else 0x8 = alpha blended (effects, OMBRE shadows, the sud0 rainbow water, *_ANU); else
        # opaque, the texture's alpha unused. A texture without alpha can't do either (every texture with alpha has
        # one of the two flags in all 21 levels). Flag 0x20 (additive, from "_AD" in the name) is in no level.
        t = mat.texture if mat else None
        flags = 0
        if t and alpha:
            flags = ALPHA_TEST if t.flags & 0x12 else TRANSPARENT if t.flags & 0x8 else 0
        if t:
            flags |= tiling(t.name)
        su, sv = (s * level.SCROLL_RATE for s in mat.scroll) if mat else (0, 0)
        frames = [(texture_id(t)[0], d / level.SCROLL_RATE / texture_time_scale(t)) for t, d in mat.frames] if mat else []
        return dict(surface=struct.pack('<iI2f2H', tid, flags, -su, sv, len(frames), 0)
                    + b''.join(struct.pack('<if', t, d) for t, d in frames))

    def geometry(m):
        return (struct.pack('<II', len(m['verts']), len(m['indices'])) + b''.join(struct.pack(f'<8f{LIGHT_SETS}I2f', *v) for v in m['verts'])
                + struct.pack(f'<{len(m["indices"])}I', *m['indices']))

    meshes = {}
    statics = list(data.meshes(False))
    lightmaps = {t for _, geo, _ in statics for el in geo.elements for t in el.lightmaps if t}
    atlas = LightmapAtlas(idx) if lightmaps else None
    if atlas:
        atlas.plan(len(lightmaps))
    for key, geo, m in statics:
        add_geometry(meshes, key, geo, m, surface, atlas)
    atlas_rel = ''
    if atlas:
        atlas_path = os.path.splitext(os.path.abspath(path))[0] + '_lightmap.png'
        with open(atlas_path, 'wb') as f:
            f.write(atlas.png())
        atlas_rel = os.path.relpath(atlas_path, out_dir).replace(os.sep, '/')
    blobs, n_parts, ed_blob = [], 0, None
    ed_loc = data.player()
    fix_ed = data.perso_in_fix[0][0] if data.perso_in_fix else None
    for p in data.persos() if persos else ():
        a = p.anim
        if not a or not p.objects or (p.loc == fix_ed and ed_loc != fix_ed):
            continue  # (the fix Ed is left out where another perso is the player: unplaced there)
        if p.loc == ed_loc:
            ed_blob = len(blobs)
        k = max(1, round(60 / a.fps)) if a.n_frames > 1 else 1
        poses = [a.pose(s / k) for s in range(a.n_frames * k)]
        parts = []
        for ch in range(len(a.chans)):
            for obj in sorted({row[ch] for row in a.objects} - {None}):
                if obj in p.objects:
                    geo, scale = p.objects[obj]
                    pm = {}
                    add_geometry(pm, None, geo, scale, surface)
                    parts += [struct.pack('<2H', ch, obj) + fitted_surface(mm) + geometry(mm) for mm in pm.values()]
        n_parts += len(parts)
        blobs.append(struct.pack('<12f2Hf', *p.world, len(a.chans), len(poses), a.fps * k * ANIM_TIME_SCALE)
                     + struct.pack(f'<{len(a.chans)}h', *a.parents(0)) + struct.pack('<I', len(parts)) + b''.join(parts)
                     + b''.join(struct.pack('<12f', *mat) for pose in poses for _, mat in pose)
                     + b''.join(struct.pack('<H', 0xFFFF if o is None or o not in p.objects else o)
                                for pose in poses for o, _ in pose))

    # Played as the skier (Totalski, skymap): the decals on the slope are not obstacles (LevelData.collision).
    fix_ed = data.perso_in_fix[0][0] if data.perso_in_fix else None
    tris, floors = data.collision(floors=True, skip_decals=data.player() != fix_ed)
    coll = struct.pack('<I', len(tris)) + b''.join(struct.pack('<9f', *a, *b, *c) for a, b, c in tris)
    table = sorted({f for f in floors if f is not None})
    index = {f: i for i, f in enumerate(table)}
    flor = struct.pack('<HH', len(table), 0) + b''.join(struct.pack('<2f', *f) for f in table)
    flor += struct.pack(f'<{len(floors)}H', *(0xFFFF if f is None else index[f] for f in floors))
    ed = data.ed(state_anims=True)
    edpl = None
    if ed:
        p, start, anims = ed
        parts = []
        for obj in sorted(p.objects):
            geo, scale = p.objects[obj]
            pm = {}
            add_geometry(pm, None, geo, scale, surface)
            parts += [struct.pack('<2H', obj, 0) + fitted_surface(mm) + geometry(mm) for mm in pm.values()]
        recs = []
        for name, a, loop in anims:
            state_anim = name.startswith('s') and name[1:].isdigit()
            k = max(1, round(60 / a.fps)) if a.n_frames > 1 and not state_anim else 1
            poses = [a.pose(s / k) for s in range(a.n_frames * k)]
            rate = a.fps * k * (1 if state_anim else ANIM_TIME_SCALE)
            b = name.encode()
            recs.append(struct.pack('<H', len(b)) + b + struct.pack('<2Hf2BHf', len(a.chans), len(poses), rate, int(loop), 0, 0,
                                                                    ground_speed(poses, rate) if name in ('walk', 'run') else 0.0)
                        + b''.join(struct.pack('<12f', *mat) for pose in poses for _, mat in pose)
                        + b''.join(struct.pack('<H', 0xFFFF if o is None or o not in p.objects else o) for pose in poses for o, _ in pose))
        edpl = (struct.pack('<12fiI', *start, ed_blob if ed_blob is not None else -1, len(parts)) + b''.join(parts)
                + struct.pack('<2H', len(recs), 0) + b''.join(recs))

    n_sets = max((len(getattr(so, 'color_sets', ())) for so in data.walk()), default=1)
    lais = []
    for a in ai.light_persos(data) if n_sets > 1 else ():
        body, unresolved = ai.export(a, None)
        if unresolved:
            data.warnings.append(f'light perso {a.perso[1]:#x}: unresolved script references {unresolved}')
        lais.append(struct.pack('<I', len(body)) + body + b'\0' * (-len(body) % 4))
    fog = data.fog() or (0.0, 0.0, (0.0, 0.0, 0.0))
    texs = struct.pack('<I', len(tex_paths)) + b''.join(struct.pack('<H', len(tp.encode())) + tp.encode() for tp in tex_paths)
    mesh = struct.pack('<I', len(meshes)) + b''.join(fitted_surface(m) + geometry(m) for m in meshes.values())
    out = [('LEVL', struct.pack('<I5f', max(1, n_sets), fog[0], fog[1], *fog[2])),
           ('LMAP', struct.pack('<H', len(atlas_rel.encode())) + atlas_rel.encode()) if atlas_rel else None,
           ('LAIS', struct.pack('<HH', len(lais), 0) + b''.join(lais)) if lais else None,
           ('TEXS', texs), ('MESH', mesh), ('PRSO', struct.pack('<I', len(blobs)) + b''.join(blobs)),
           ('COLL', coll), ('FLOR', flor), ('EDPL', edpl) if edpl is not None else None]
    chunks.write(path, 'TTLV', [c for c in out if c])
    return (len(meshes), sum(len(m['indices']) // 3 for m in meshes.values()), len(tex_paths), sorted(missing),
            len(blobs), n_parts, len(tris), [a[0] for a in ed[2]] if ed else [])


def ground_speed(poses, rate):
    """Units/s covered by the feet: the two channels lowest at the first sample are taken as the
    feet; 2 x their mean horizontal stride (max of the x and y ranges) / loop seconds."""
    lows = sorted(range(len(poses[0])), key=lambda c: poses[0][c][1][11])[:2]
    strides = []
    for c in lows:
        xs, ys = [p[c][1][3] for p in poses], [p[c][1][7] for p in poses]
        strides.append(max(max(xs) - min(xs), max(ys) - min(ys)))
    return 2 * sum(strides) / len(strides) / (len(poses) / rate) if strides else 0.0

"""Writer for .ttlv, the native viewer's level format (reader: port/core/src/tc_level.c).

Version 8 = v7 plus per-triangle lightmaps: right after the fog, u16 len + utf8 path (relative to the .ttlv) of
  a lightmap atlas PNG (len 0 = none), and 52-byte vertices: v7's 44 bytes + f32 uv2[2] = atlas coordinates
  (top-left origin, 0..1; -1 = no lightmap). Lightmapped triangles get their own vertices. The lightmap value
  replaces the baked light (same x255/128 scale). Atlas = 32x32 tiles in a square power of two.
Version 7 = v6 plus level fog right after play_offset: f32 fog_near, f32 fog_far, f32 fog_rgb[3]
  (linear by eye distance; fog_far <= fog_near = no fog). LevelData.fog: the visual environment most
  sectors use. Sky-sector IPOs ("...ciel..." sectors) carry neutral baked light (LevelData.mark_sky).
Version 6 = v5 plus floor materials right after the collision triangles in the play section:
  u16 n_floors, u16 0, n_floors x { f32 grip, f32 rebound }   (the mechanics material of the
      triangle's GameMaterial: grip = mat[2], rebound = mat[1]; see NOTES "Floor mechanical material")
  n_triangles x u16 floor index (0xFFFF = no material: grip 1, rebound 1), padded to 4 bytes
Version 5 = v4 plus u32 play_offset right after light_set_count (absolute file offset of the play
section, so headless tools can seek to it) and a play section after the perso section:
  u32 n_triangles, n_triangles x f32[9]   collision, world space (LevelData.collision: static
                                          collide sets + perso collide meshes at frame 0)
  u32 has_ed (0/1), then if 1:
    f32 start[12] (row-major 3x4: Ed's placement, GPT perso-in-fix matrix; he faces local -Y)
    i32 static_perso (index of Ed's own entry in the perso section, -1 = none: hide it in play mode)
    u32 part_count, part_count x { u16 object, u16 0, surface, geometry (object space, scale applied) }
    u16 n_anims, u16 0, n_anims x {
      u16 name_len, utf8 name ("idle", "walk", "run", "jump", "jump_air", "fall", "land"),
      u16 n_channels, u16 n_samples, f32 sample_rate (as for persos, ANIM_TIME_SCALE applied),
      u8 loop, u8 0, u16 0, f32 ground_speed (units/s the feet cover at sample_rate: 2 x the
      feet's horizontal stride / loop length; 0 = not locomotion. The viewer scales walk/run
      playback by its move speed / ground_speed so the feet don't slide),
      n_samples x n_channels x f32[12] channel -> Ed, n_samples x n_channels x u16 object (0xFFFF none) }
  Parts are keyed by object only: channel numbering differs between Ed's animations.
  After the 7 named clips come Ed's state animations for the AI (no format change): one per distinct A3D, named
  "s<k>" (k = the first state using it; .ttai states point at it), one sample per animation frame, sample_rate =
  the A3D's own frames per second (no ANIM_TIME_SCALE; the AI's state clock picks the frame), loop 0.
Version 4 = v3 plus u32 light_set_count right after the version, and 44-byte vertices carrying
3 baked sets (rgba8 each, alpha 0 = set absent: the viewer falls back to set 0). Number keys pick
the set. Version 3 (v2 = the same with 32-byte vertices, no colour; v1 = v2 without scroll/anim in the
surface record and no perso section, persos merged into the static meshes at frame 0; the viewer
reads all three):
  "TTLV", u32 version (3)
  u32 texture_count, texture_count x { u16 len, utf8 path relative to the .ttlv }
  surface = { i32 texture (-1 = none), u32 flags, f32 scroll[2] (Metal UV per second:
              uv' = uv + scroll * t),
              u16 n_anim, u16 0, n_anim x { i32 texture, f32 seconds } (animated texture loop;
              n_anim = 0: static texture) }
  geometry = { u32 n_vertices, u32 n_indices, n_vertices x vertex, n_indices x u32 }
  vertex (36) = f32 pos[3], f32 normal[3], f32 uv[2], u8 rgba: baked radiosity clamped to 0..255
              (light = rgb / 128, see level.RADIOSITY_ONE; a = 255 baked, a = 0 none: the viewer
              uses its fallback lighting, as for all perso parts)
  u32 mesh_count, mesh_count x { surface, geometry }          static world, engine world space
  u32 perso_count, perso_count x {
    f32 world[12] (row-major 3x4, perso -> world)
    u16 n_channels, u16 n_samples, f32 sample_rate (samples per second; loop = n_samples / rate)
    n_channels x i16 parent channel at frame 0 (-1 = root; informational, matrices are composed)
    u32 part_count, part_count x { u16 channel, u16 object, surface, geometry (channel space,
                                   object-list scale applied) }
    n_samples x n_channels x f32[12] channel -> perso transform (row-major 3x4)
    n_samples x n_channels x u16 object shown by the channel (0xFFFF = none) }
One mesh/part per material, LOD 0 only, Z up. Samples: the A3D is evaluated (keyframe lerp, see
level.Anim) at k = round(60 / fps) samples per animation frame. UVs are converted to Metal's
top-left origin (v' = 1 - v; engine v=0 is the image's bottom row). The engine subtracts its
scroll offset (inferred: only then does sud0's rainbow pool flow over its edge and down the falls).
Engine time unit = 1/60 s (scroll speed and texture frame durations are per 1/60 s).
"""
import math
import os
import struct

from . import export, gf, level

ALPHA_TEST = 1 << 0
TRANSPARENT = 1 << 1  # TC_FLAG_TRANSPARENT: alpha blended (texture flag 0x8 without 0x2, see surface())
BILLBOARD = 1 << 2  # TC_FLAG_BILLBOARD: sprite quads, expanded towards the camera by the vertex shader
# Empirical: the user compared against the original running in Wine and 0.25x matched.
# The exe's formula (frame += dt_ms/1000 * state FrameRate @0x3B, FUN_00415fb0) gives 1x, so some
# unit is still unexplained (open question in NOTES). Applied to persos only, not textures.
ANIM_TIME_SCALE = 0.25
# Empirical too: the user saw the portal flipbooks (GEN_ARCH01-04 and mirrored GEN_ARCH..R; Land,
# carota, Totalski; 4 frames x 5.4 ticks) play far too fast, while lava flipbooks and scrolls looked
# right. Texture-name prefix (case-insensitive, file name only) -> factor on the playback rate, so
# 0.25 makes each frame last 4x longer. Hypothesis: the same unexplained 4x as ANIM_TIME_SCALE.
TEXTURE_TIME_SCALE = {'GEN_ARCH': 0.5}  # user-matched: 0.25 was too slow, 2x that looked natural
VERSION = 8
LIGHT_SETS = 3  # baked radiosity sets per vertex (set 0 everywhere; 1-2 only in Land/sud0)


def texture_time_scale(tex):
    name = tex.name.replace('\\', '/').rsplit('/', 1)[-1].upper() if tex else ''
    return next((v for k, v in TEXTURE_TIME_SCALE.items() if name.startswith(k.upper())), 1.0)


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
        tid, flags = texture_id(mat.texture) if mat else (-1, 0)
        # raymap's pre-R3 rule: material flag 0x4000000 + texture flags 0x10A = transparent. In TT, texture flag
        # 0x8 alone marks blended textures (effects, OMBRE shadows, the sud0 rainbow water, *_ANU), while 0x8|0x2
        # marks hard cut-outs (*_nz teeth, cloth): those keep the alpha test.
        t = mat.texture if mat else None
        if t and mat.flags & 0x4000000 and t.flags & 0x8 and not t.flags & 0x2 and flags & ALPHA_TEST:
            flags = (flags & ~ALPHA_TEST) | TRANSPARENT
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
                    parts += [struct.pack('<2H', ch, obj) + mm['surface'] + geometry(mm) for mm in pm.values()]
        n_parts += len(parts)
        blobs.append(struct.pack('<12f2Hf', *p.world, len(a.chans), len(poses), a.fps * k * ANIM_TIME_SCALE)
                     + struct.pack(f'<{len(a.chans)}h', *a.parents(0)) + struct.pack('<I', len(parts)) + b''.join(parts)
                     + b''.join(struct.pack('<12f', *mat) for pose in poses for _, mat in pose)
                     + b''.join(struct.pack('<H', 0xFFFF if o is None or o not in p.objects else o)
                                for pose in poses for o, _ in pose))

    # Played as the skier (Totalski, skymap): the decals on the slope are not obstacles (LevelData.collision).
    fix_ed = data.perso_in_fix[0][0] if data.perso_in_fix else None
    tris, floors = data.collision(floors=True, skip_decals=data.player() != fix_ed)
    play = struct.pack('<I', len(tris)) + b''.join(struct.pack('<9f', *a, *b, *c) for a, b, c in tris)
    table = sorted({f for f in floors if f is not None})
    index = {f: i for i, f in enumerate(table)}
    play += struct.pack('<HH', len(table), 0) + b''.join(struct.pack('<2f', *f) for f in table)
    play += struct.pack(f'<{len(floors)}H', *(0xFFFF if f is None else index[f] for f in floors))
    play += b'\0' * (-len(floors) * 2 % 4)
    ed = data.ed(state_anims=True)
    if ed:
        p, start, anims = ed
        parts = []
        for obj in sorted(p.objects):
            geo, scale = p.objects[obj]
            pm = {}
            add_geometry(pm, None, geo, scale, surface)
            parts += [struct.pack('<2H', obj, 0) + mm['surface'] + geometry(mm) for mm in pm.values()]
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
        play += (struct.pack('<I12fiI', 1, *start, ed_blob if ed_blob is not None else -1, len(parts)) + b''.join(parts)
                 + struct.pack('<2H', len(recs), 0) + b''.join(recs))
    else:
        play += struct.pack('<I', 0)

    n_sets = max((len(getattr(so, 'color_sets', ())) for so in data.walk()), default=1)
    body = bytearray()
    for tp in tex_paths:
        b = tp.encode()
        body += struct.pack('<H', len(b)) + b
    body += struct.pack('<I', len(meshes))
    for m in meshes.values():
        body += m['surface'] + geometry(m)
    body += struct.pack('<I', len(blobs)) + b''.join(blobs)
    fog = data.fog() or (0.0, 0.0, (0.0, 0.0, 0.0))
    head = (b'TTLV' + struct.pack('<III', VERSION, max(1, n_sets), 0) + struct.pack('<5f', fog[0], fog[1], *fog[2])
            + struct.pack('<H', len(atlas_rel.encode())) + atlas_rel.encode() + struct.pack('<I', len(tex_paths)))
    head = head[:12] + struct.pack('<I', len(head) + len(body)) + head[16:]  # play_offset
    with open(path, 'wb') as f:
        f.write(head + body + play)
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

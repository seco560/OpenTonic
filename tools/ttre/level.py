"""Tonic Trouble level structures, walked from the level's global pointer table (.gpt).

All offsets are bytes from the start of the struct, little-endian; "ptr" = 32-bit engine
pointer (followed through sna.Level.ptr). Layouts follow raymap's TT (EngineVersion.TT) paths
and were checked against bar/sud0/Land data (pointer-map hits, sane floats and counts).

Level GPT:
  u32 n_perso_in_fix; n x { ptr perso (fix), 0x58 matrix, ptr new_super_object (lvl) }
  ptr actual_world, ptr dynamic_world, ptr father_sector,
  ptr ? (raymap: "soundEventIndex"), u32 num_always, {ptr head, ptr tail, u32 n} spawnable persos,
  ptr always_reusable_so, ptr ?, 3 x {ptr first, ptr last, u32 n} object type name tables,
  u8 ?, char[0x104] x4 level names ("bar", ...), ...
SuperObject (0x38): u32 type, ptr data, {ptr head, ptr tail, u32 n} children, ptr next, ptr prev,
  ptr parent, ptr matrix (0x20), ptr static_matrix (0x24), i32 ?, u32 draw_flags, u32 flags,
  ptr bounding_volume. TT types: 0 world, 4 perso, 8 sector, 0xD IPO, 0x15 IPO (variant).
Matrix (0x58): u32 type, f32 pos[3], f32 rot[3][3] (columns), f32 transform[3][3] (columns).
  type 0x80 = identity, 0x90 = translation, 0xD0 = translation + scale, (other bits: rotation).
  transform = rot * scale, complete: it equals rot in every rotated perso matrix (type 0x1B0) and
  holds the scale where rot = I (0x1F0). Transform = T * transform, local to the parent SuperObject.
IPO: ptr physical_object, ptr radiosity (runtime buffer: all zero in every level file), ptr ISI.
ISI: ptr radiosity (the baked set, non-zero, every level), ptr radiosity_2 (null except Land/sud0),
  ptr ? (Land/sud0: a third radiosity; elsewhere some other struct). Not traced in the exe.
Radiosity: u32 n_lod, ptr lods; lod = {u32 n_colors (= the LOD's n_vertices), ptr colors};
  color = i16 r, g, b, 0 (signed; median ~20..100, tails -5100..2400, see NOTES "Lighting").
PhysicalObject (TT, 0xC): ptr visual_set, ptr collide_set, ptr visual_bounding_volume.
CollSet: u32 x3 (zdm, zdd, zde; 0 for every static IPO), ptr zdr (GeometricObjectCollide).
GeometricObjectCollide (TT): u32 n_vertices, ptr vertices (f32[3]), ptr normals, ptr ?, i32,
  u32 n_elements (0x14), ptr element_types (u16[n], 0x18), ptr elements (ptr[n], 0x1C), ptr ?,
  i32 x2, u16, u16 n_parallel_boxes, f32 sphere_radius, f32 sphere_center[3].
  Collide element type 1 (indexed triangles): ptr game_material, u16 n_triangles, u16 n_mapping,
  ptr triangles (i16[3]), ptr mapping, ptr face_normals, ptr uvs, ptr ?, u16, i16 parallel_box.
  Types 7 (spheres) and 8 (axis-aligned boxes): props only, read as triangles (LevelData.collide_primitives).
  In every static IPO checked
  (bar, sud0) the collide object shares the visual LOD 0's vertex and triangle arrays (same
  pointers), and GameMaterial +0xC (collide material) is null, so world collision = visual mesh.
VisualSet: u32 0, u16 n_lod, u16 type (0 = GeometricObject, 1 = patch), ptr lod_distances (f32[n]),
  ptr lod_objects (ptr[n]), u32.
GeometricObject (TT, 0x40): u32 n_vertices, ptr vertices (f32[3]), ptr normals (f32[3]),
  ptr materials, i32, u32 n_elements, ptr element_types (u16[n]), ptr elements (ptr[n]),
  i32 x4, f32 sphere_radius, f32 sphere_center[3].
Element type 1, indexed triangles (0x2C): ptr game_material, u16 n_triangles, u16 n_uvs,
  ptr triangles (i16[3] each), ptr uv_mapping (i16[3] per triangle, into uvs),
  ptr face_normals (f32[3] per triangle), ptr uvs (f32[2]), ptr vertex_indices,
  u16 n_vertex_indices, u16 parallel_box, u32.  (Type 3 = sprites, not handled yet.)

Persos (dynamic world children, SuperObject type 4; data = Perso). Checked on bar/sud0:
Perso (0x44): ptr perso3d_data, ptr std_game, ptr dynam, ptr brain, ptr camera, ptr collset,
  ptr ms_way, ptr ms_light, u32, ptr sector_info, ...
Perso3dData: ptr state_initial, ptr state_current, ptr state_2, ptr object_list (current),
  ptr object_list_initial, ptr family, Matrix (0x18, inline 0x58: the perso's transform; the
  SuperObject's matrix pointer points here), ... runtime state.
Family: ptr next, ptr prev, ptr hdr, u32 index, {ptr head, ptr tail, u32 n} states (0x10),
  ptr default_object_list (0x1C), {head, tail, n} object lists (0x20), ptr bounding_volume, ...
State: ptr next, ptr prev, ptr hdr, ptr anim_ref (0x0C), {h, t, n} transitions,
  {h, t, n} prohibited, ptr next_state, ptr mechanics, ...
ObjectList: ptr next, ptr prev, ptr hdr, ptr entries (0x0C), ptr entries_copy, u16 n (0x14), u16.
  Entry (0x14): ptr scale (f32[3], null = 1), ptr physical_object, u32, u16, u16, u32.
AnimationReference (TT, 0x2C): u32, u16 n_frames, u8 speed, u8 n_channels, ptr events,
  f32[3], ptr morph, u32 x2, u16 anim_index, u8 n_events, u8 transition, ptr a3d (0x28).
A3D (TT): header 0x34 = u16 speed, n_vectors, n_quaternions, n_hierarchies, n_ntto,
  n_num_ntto, n_channels, n_frames, n_keyframes, n_events, ... (rest unused), then packed arrays:
  vectors f32[3]; quaternions i16[4] (x, y, z, w; |q| = 0x4000); hierarchies {i16 child,
  i16 parent} (TT: channel *indices*); ntto {u16 object_index (into the object list),
  u16 flags (2 = invisible), u8, u8}; frames (12) {u16 quat, u16 vec, u16 n_hier, u16 first_hier,
  u16 num_ntto, u16}; channels (12) {u16, i16 id, u16 vec, u16 num_ntto, u16 frames_kf, u16 kf};
  num_ntto u16[n_num_ntto * n_channels]; frames_kf u16[n_frames * n_channels] (absolute keyframe
  index at channel.frames_kf + frame); align 4; keyframes (36) {f32 pos[3], f32 pos_len,
  u16 frame, u16 flags, u16 quat, u16 quat2, u16 scale_vec, u16 pos_vec, u16 x3, u16 interp}.
  Channel local transform = T(pos) * R(quat) * S(vectors[scale_vec]) (R: see quat_matrix); pos = vectors[pos_vec] *
  pos_len (= the keyframe's pos[3]). Channel object = ntto[num_ntto[ch.num_ntto + frame.num_ntto]].
GPT perso-in-fix: the 0x58 matrix replaces the fix perso's Perso3dData matrix (the fix copy is
  stale) and the level SuperObject pointer goes to std_game + 0xC (one of the "unresolved"
  fix->level relocations; see NOTES).
GameMaterial (TT): ptr visual_material, ptr mechanics_material, u32 sound, ptr collide_material.
VisualMaterial (TT, from the exe's per-frame update GLI FUN_004457d0; raymap's TT offsets are 8
  too low and it skips the animated textures): u32 flags, f32 ambient[4], diffuse[4], specular[4],
  color[4], u32 (0x44), ptr texture (0x48), ptr ? (0x4C), ? (0x50, 0x54), f32 scroll_cur[2] (0x58),
  f32 scroll_speed[2] (0x60, UV per 1/60 s), u32 scroll_on (0x68), u32 last_update_frame (0x6C),
  ptr anim_first (0x70), ptr anim_current (0x74), u32 n_anim (0x78), f32 anim_time (0x7C), u8 (0x80).
  Animated texture node: ptr texture, f32 duration (1/60 s), ptr next (null = loop to first).
TextureInfo (TT): u32, u32, u32 flags (0x08), u32 height_, u32 width_ (0x0C/0x10), u32 x3,
  u32 height, u32 width (0x20/0x24), u32 x11, char[0x100] name (0x54, a .tga path relative to
  Textures.cnt), u8 (0x154), u8 flags_byte (0x155: 1 = repeat V, 2 = repeat U, 4/8 = mirror U/V).
PTX: u32 capacity (5120 texture slots), u32 n (fix: count; level: capacity - n_fix), ptr[n]
  TextureInfo (level slots are null after the last used one), u32 n_to_create,
  then fix: u32[n] memory channel (1); level: u32[capacity] channel (1 fix, 2 lvl,
  0xC0DE0005 unused); u32 current channel.
"""
import math
import struct
from dataclasses import dataclass, field

SO_TYPES = {0x0: 'world', 0x4: 'perso', 0x8: 'sector', 0xD: 'ipo', 0x15: 'ipo'}
IDENTITY = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)  # row-major 3x4
SCROLL_RATE = 60.0  # engine time unit = 1/60 s (frame delta = timer ms * 0.06; see NOTES)


@dataclass(eq=False)
class Texture:
    name: str
    flags: int
    width: int
    height: int
    flags_byte: int


@dataclass(eq=False)
class Material:
    flags: int
    ambient: tuple
    diffuse: tuple
    specular: tuple
    color: tuple
    texture: Texture = None
    scroll: tuple = (0.0, 0.0)  # UV per engine frame (1/60 s), (0, 0) unless scrolling is on
    frames: list = field(default_factory=list)  # animated texture: [(Texture, duration in 1/60 s)]


@dataclass(eq=False)
class Element:
    material: Material
    triangles: list  # (a, b, c) vertex indices
    uv_tris: list    # (a, b, c) indices into uvs
    uvs: list
    lightmaps: list = field(default_factory=list)  # per triangle: lightmap Texture or None (+0x18; see read below)


@dataclass(eq=False)
class Geometric:
    vertices: list
    normals: list
    elements: list = field(default_factory=list)
    skipped: dict = field(default_factory=dict)  # element type -> count not handled
    sprites: list = field(default_factory=list)  # (vertex index of the centre, half width, half height, Material)


@dataclass(eq=False)
class SuperObject:
    type: int
    loc: tuple
    matrix: tuple  # 3x4 local transform
    children: list = field(default_factory=list)
    lods: list = field(default_factory=list)  # Geometric per LOD (IPOs only)
    colors: list = field(default_factory=list)  # baked radiosity per LOD: [(r, g, b) i16 per vertex] (IPOs only)
    perso: 'Perso' = None
    collide: tuple = None  # (vertices, triangles, floors) of the collide set (IPOs only), see LevelData.collide

    @property
    def kind(self):
        return SO_TYPES.get(self.type, hex(self.type))


@dataclass(eq=False)
class Perso:
    loc: tuple
    family: int
    world: tuple = IDENTITY
    parts: list = field(default_factory=list)  # frame-0 pose: (Geometric LOD 0, 3x4 world matrix)
    note: str = ''  # why there are no parts (no object list / no animation)
    anim: 'Anim' = None
    objects: dict = field(default_factory=dict)  # object-list index -> (Geometric, 3x4 scale)
    collides: dict = field(default_factory=dict)  # object-list index -> collide (vertices, triangles)

    def posed(self, pose):
        """Anim.pose() result -> [(Geometric, world 3x4)] for the visible channels."""
        return [(self.objects[o][0], mat_mul(self.world, mat_mul(m, self.objects[o][1])))
                for o, m in pose if o in self.objects]

    def at(self, t):
        """Parts posed at t seconds into the looping initial-state animation."""
        if not self.anim:
            return self.parts
        return self.posed(self.anim.pose(t * self.anim.fps % self.anim.n_frames))


@dataclass(eq=False)
class Anim:
    """Decoded A3D. Playback follows raymap's TT path: integer frames at fps, each channel lerps
    from its keyframe (frames_kf[ch + frame]) to the next one (the channel's first keyframe after
    an end keyframe, flag 0x80), quaternions by normalised lerp; the hierarchy and the
    channel -> object (NTTO) mapping are per frame."""
    fps: int
    n_frames: int
    vec: list
    quat: list
    hier: list
    ntto: list
    frames: list
    chans: list
    num_ntto: tuple
    fkf: tuple
    kfs: list

    @property
    def objects(self):
        """[frame][channel] -> object-list index, or None when the NTTO is flagged invisible."""
        out = []
        for fr in self.frames:
            row = []
            for ch in self.chans:
                obj, flags = self.ntto[self.num_ntto[ch[3] + fr[4]]]
                row.append(None if flags & 2 else obj)
            out.append(row)
        return out

    def parents(self, fi):
        fr, parent = self.frames[fi], [-1] * len(self.chans)
        for c, par in self.hier[fr[3]:fr[3] + fr[2]]:
            if 0 <= c < len(parent):
                parent[c] = par
        return parent

    def local(self, ch, f):
        """Channel transform relative to its parent at (fractional) frame f."""
        nf, fi = self.n_frames, min(int(f), self.n_frames - 1)
        k = self.fkf[ch[4] + fi]
        kf = self.kfs[k]
        if kf[5] & 0x80:
            nx = self.kfs[self.fkf[ch[4]]]
            diff = nf - 1 + nx[4] - kf[4]
        else:
            nx = self.kfs[k + 1]
            diff = nx[4] - kf[4]
        a = max(0.0, min(1.0, (f - kf[4]) / diff)) if diff else 0.0

        def lerp(p, q):
            return [x + (y - x) * a for x, y in zip(p, q)]
        mul = kf[3] + (nx[3] - kf[3]) * a
        px, py, pz = (c * mul for c in lerp(self.vec[kf[9]], self.vec[nx[9]]))
        q0, q1 = self.quat[kf[6]], self.quat[nx[6]]
        if sum(x * y for x, y in zip(q0, q1)) < 0:
            q1 = [-x for x in q1]
        sx, sy, sz = lerp(self.vec[kf[8]], self.vec[nx[8]])
        return mat_mul((1, 0, 0, px, 0, 1, 0, py, 0, 0, 1, pz),
                       mat_mul(quat_matrix(*lerp(q0, q1)), (sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0)))

    def pose(self, f):
        """[(object index or None, channel transform relative to the perso)] per channel."""
        fi = min(int(f), self.n_frames - 1)
        parent, objs = self.parents(fi), self.objects[fi]
        local = [self.local(ch, f) for ch in self.chans]
        glob = {}

        def g(i, depth=0):
            if i not in glob:
                par = parent[i]
                glob[i] = local[i] if par < 0 or depth > len(local) else mat_mul(g(par, depth + 1), local[i])
            return glob[i]
        return [(objs[i], g(i)) for i in range(len(local))]


# Ed's family ("NewEd", fix) state indices (state-list order) used by the viewer's play mode.
# TT has no state names; picked from filmstrips of every candidate (out/scratch/ed_fs*.png, see
# NOTES "Play as Ed"): idle = the initial state; walk/run = looping strides with a short start
# state leading in (9 -> 1, 158 -> 10); jump = crouch-and-extend take-off (39 -> 43); jump_air =
# tucked airborne loop; fall = arms-up dangling loop (40 -> 44); land = squash back to idle (41 -> 0).
ED_ANIMS = (('idle', 0, True), ('walk', 1, True), ('run', 10, True), ('jump', 39, False),
            ('jump_air', 43, True), ('fall', 44, True), ('land', 41, False))

def forward_stride(anim):
    """Units per animation frame Ed's feet cover when the animation walks forward (local -Y), else 0: the two
    channels lowest at frame 0 are the feet; 2 x their mean Y range / frames, if both feet sweep mostly along Y
    (range >= 0.6 and >= 2 x the X range). Sideways and backward cycles are not told apart yet (0 / forward).
    Checked: run (state 10) 0.100 x 45 fps x the script's 222 % rate = 9.9 u/s, the speed matched by eye earlier."""
    if anim.n_frames < 4:
        return 0.0
    poses = [anim.pose(f) for f in range(anim.n_frames)]
    lows = sorted(range(len(poses[0])), key=lambda c: poses[0][c][1][11])[:2]
    ys = []
    for c in lows:
        x = [q[c][1][3] for q in poses]
        y = [q[c][1][7] for q in poses]
        rx, ry = max(x) - min(x), max(y) - min(y)
        if ry < 0.6 or ry < 2 * rx:
            return 0.0
        ys.append(ry)
    return 2 * sum(ys) / len(ys) / len(poses)


def box_triangles(lo, hi):
    """12 outward-facing (counter-clockwise) triangles of the axis-aligned box lo..hi."""
    x = (lo[0], hi[0]); y = (lo[1], hi[1]); z = (lo[2], hi[2])
    c = lambda i, j, k: (x[i], y[j], z[k])
    quads = [(c(0, 0, 0), c(0, 1, 0), c(1, 1, 0), c(1, 0, 0)),   # bottom (-z)
             (c(0, 0, 1), c(1, 0, 1), c(1, 1, 1), c(0, 1, 1)),   # top (+z)
             (c(0, 0, 0), c(1, 0, 0), c(1, 0, 1), c(0, 0, 1)),   # -y
             (c(0, 1, 0), c(0, 1, 1), c(1, 1, 1), c(1, 1, 0)),   # +y
             (c(0, 0, 0), c(0, 0, 1), c(0, 1, 1), c(0, 1, 0)),   # -x
             (c(1, 0, 0), c(1, 1, 0), c(1, 1, 1), c(1, 0, 1))]   # +x
    out = []
    for a, b, cc, d in quads:
        out += [(a, b, cc), (a, cc, d)]
    return out


def sphere_triangles(center, r):
    """80 outward triangles of an icosphere (icosahedron subdivided once, vertices on the sphere)."""
    import math
    t = (1 + 5 ** 0.5) / 2
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
         (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
         (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
         (8, 6, 7), (9, 8, 1)]
    unit = lambda p: tuple(c / math.sqrt(sum(q * q for q in p)) for c in p)
    mid = lambda a, b: unit(tuple((a[i] + b[i]) / 2 for i in range(3)))
    v = [unit(p) for p in v]
    tris = []
    for a, b, c in f:
        A, B, C = v[a], v[b], v[c]
        ab, bc, ca = mid(A, B), mid(B, C), mid(C, A)
        tris += [(A, ab, ca), (B, bc, ab), (C, ca, bc), (ab, bc, ca)]
    place = lambda p: tuple(center[i] + r * p[i] for i in range(3))
    return [tuple(place(p) for p in tri) for tri in tris]


RADIOSITY_ONE = 128.0  # radiosity value that leaves the texture unchanged (guessed visually, see NOTES)


def fill_unbaked(so):
    """An element whose vertices are all exactly (0, 0, 0) in one baked set but lit in another was
    not baked for that set (baked values are signed and noisy, never exactly zero over a whole
    element): copy the first lit set's values. Only sud0's rainbow water (O_ZARBI_AUNTXY, sets 0
    and 2 zero, set 1 ~95) matches in the levels checked; it is visible in the game in every set.
    north has 25 elements that are zero in its only set; with nothing to copy they stay black."""
    sets = so.color_sets
    for lod, geo in enumerate(so.lods):
        for e in geo.elements:
            vs = {v for t in e.triangles for v in t}
            cols = [s[lod] if lod < len(s) else None for s in sets]
            if not vs or any(c is not None and max(vs) >= len(c) for c in cols):
                continue
            lit = [c for c in cols if c and any(c[v] != (0, 0, 0) for v in vs)]
            if not lit:
                continue
            for c in cols:
                if c and all(c[v] == (0, 0, 0) for v in vs):
                    for v in vs:
                        c[v] = lit[0][v]


def baked_colors(key, index=0):
    """Raw radiosity (r, g, b) per vertex of baked set `index` for a meshes() key (static IPO
    SuperObject, LOD 0), else None."""
    if not isinstance(key, SuperObject):
        return None
    sets = getattr(key, 'color_sets', None) or [key.colors]
    c = sets[index] if index < len(sets) else None
    return (c[0] or None) if c else None


def baked_light(c):
    """Radiosity (r, g, b) -> light factors multiplying the texture: clamped to 0..255 (what .ttlv
    v3 stores as unorm8), / RADIOSITY_ONE (1.0 = texture unchanged, up to ~2x overbright)."""
    return tuple(min(max(x, 0), 255) / RADIOSITY_ONE for x in c)


def vertex_light(key):
    cols = baked_colors(key)
    return [baked_light(c) for c in cols] if cols else None


def quat_matrix(x, y, z, w):
    """A3D quaternion -> row-major 3x4 rotation. The engine's matrix is the transpose of the
    textbook one (i.e. it rotates by the conjugate); the textbook form visibly breaks Ed."""
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = -x / n, -y / n, -z / n, w / n
    return (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0,
            2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0,
            2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0)


def mat_det(m):
    return (m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8])
            + m[2] * (m[4] * m[9] - m[5] * m[8]))


def mat_mul(a, b):
    """Compose two row-major 3x4 affine transforms (a applied after b)."""
    r = []
    for i in range(3):
        row = a[4 * i:4 * i + 4]
        for j in range(4):
            v = sum(row[k] * b[4 * k + j] for k in range(3))
            r.append(v + (row[3] if j == 3 else 0))
    return tuple(r)


def mat_apply(m, v):
    x, y, z = v
    return (m[0] * x + m[1] * y + m[2] * z + m[3],
            m[4] * x + m[5] * y + m[6] * z + m[7],
            m[8] * x + m[9] * y + m[10] * z + m[11])


class LevelData:
    def __init__(self, lv):
        self.lv = lv
        self.textures = {}   # loc -> Texture (cache, also filled by materials)
        self.materials = {}  # visual material loc -> Material
        self.geometrics = {}
        self.so_seen = set()
        self.warnings = []
        g = lv.gpt
        n, = self.un('I', (g, 0))
        self.perso_in_fix = [(lv.ptr((g, 4 + 0x60 * i)), lv.ptr((g, 0x60 + 0x60 * i))) for i in range(n)]
        self.fix_matrices = {p: self.matrix((g, 8 + 0x60 * i)) for i, (p, _) in enumerate(self.perso_in_fix) if p}
        o = 4 + 0x60 * n
        self.actual_world, self.dynamic_world, self.father_sector = (lv.ptr((g, o + 4 * i)) for i in range(3))
        self.fix_textures = self.read_ptx(lv.fix_ptx)
        self.level_textures = self.read_ptx(lv.ptx)
        self.root = self.read_so(self.father_sector, IDENTITY)
        self.mark_sky()
        self.dynamic = self.read_so(self.dynamic_world, IDENTITY)

    def un(self, fmt, loc, off=0):
        return struct.unpack_from('<' + fmt, loc[0].data, loc[1] + off)

    def read_ptx(self, pf):
        cap, n = self.un('II', (pf, 0))
        return [self.texture(p) for p in (self.lv.ptr((pf, 8 + 4 * i)) for i in range(n)) if p]

    def texture(self, loc):
        if loc is None:
            return None
        t = self.textures.get(loc)
        if t is None:
            flags, h, w = self.un('III', loc, 8)
            raw = loc[0].data[loc[1] + 0x54:loc[1] + 0x154]
            name = raw.split(b'\0')[0].decode('latin-1')
            t = self.textures[loc] = Texture(name, flags, w, h, loc[0].data[loc[1] + 0x155])
        return t

    def material(self, game_mat):
        vm = self.lv.ptr(game_mat) if game_mat else None
        if vm is None:
            return None
        m = self.materials.get(vm)
        if m is None:
            f = self.un('I16f', vm)
            speed, on = self.un('2fI', vm, 0x60)[:2], self.un('I', vm, 0x68)[0]
            m = self.materials[vm] = Material(f[0], f[1:5], f[5:9], f[9:13], f[13:17],
                                              self.texture(self.lv.ptr(vm, 0x48)), speed if on else (0.0, 0.0))
            node, n = self.lv.ptr(vm, 0x70), self.un('I', vm, 0x78)[0]
            for _ in range(n if node else 0):
                m.frames.append((self.texture(self.lv.ptr(node)), self.un('f', node, 4)[0]))
                node = self.lv.ptr(node, 8)
                if node is None:
                    break
        return m

    def geometric(self, loc):
        if loc in self.geometrics:
            return self.geometrics[loc]
        lv = self.lv
        nv, = self.un('I', loc)
        ne, = self.un('I', loc, 20)
        pv, pn = lv.ptr(loc, 4), lv.ptr(loc, 8)
        verts = [self.un('3f', pv, 12 * i) for i in range(nv)] if pv else []
        norms = [self.un('3f', pn, 12 * i) for i in range(nv)] if pn else []
        geo = self.geometrics[loc] = Geometric(verts, norms)
        types = self.un(f'{ne}H', lv.ptr(loc, 24)) if ne else ()
        elems = lv.ptr(loc, 28)
        for i, t in enumerate(types):
            el = lv.ptr(elems, 4 * i)
            if t == 3 and el is not None and nv:
                self.read_sprites(geo, el)
                continue
            if t != 1 or el is None:
                geo.skipped[t] = geo.skipped.get(t, 0) + 1
                continue
            nt, nuv = self.un('HH', el, 4)
            pt, pm, puv = lv.ptr(el, 8), lv.ptr(el, 12), lv.ptr(el, 20)
            tris = [self.un('3h', pt, 6 * k) for k in range(nt)] if pt else []
            uvt = [self.un('3h', pm, 6 * k) for k in range(nt)] if pm else []
            uvs = [self.un('2f', puv, 8 * k) for k in range(nuv)] if puv else []
            bad = [k for k, tri in enumerate(tris) if max(tri) >= nv or min(tri) < 0]
            if bad or any(max(u) >= nuv or min(u) < 0 for u in uvt):
                self.warnings.append(f'element {el[1]:#x}: out-of-range indices')
                continue
            # +0x18: per-triangle array of ptr {ptr TextureInfo, u32 1} = that triangle's own lightmap (32x32 BMP,
            # the triangle drawn in the upper-left half: vertex a -> (0, 0), b -> (1, 0), c -> (0, 1), image
            # top-left origin; found by edge continuity, see NOTES). bar, death, PharmMap, pyr_fou, SpyCarota,
            # spynorth have one per triangle; radiosity levels have none.
            lm_arr, lms = lv.ptr(el, 0x18), []
            for k in range(nt if lm_arr else 0):
                e = lv.ptr(lm_arr, 4 * k)
                t = self.texture(lv.ptr(e)) if e else None
                lms.append(t if t and 'LIGHTMAP' in t.name.upper() else None)
            geo.elements.append(Element(self.material(lv.ptr(el, 0)), tris, uvt, uvs, lms if any(lms) else []))
        return geo

    def read_sprites(self, geo, el):
        """Element type 3 (TT): u32 n, ptr sprites, u32. Sprite (0x10): u32, ptr info, f32 size[2].
        Info: u32, ptr, ptr, ptr scale f32[2] (+0xC: half extents), ptr f32[2] (+0x10), ptr (+0x14) ->
        {ptr GameMaterial, ...}. TT has no centre-point field (raymap reads none before Montreal); sprite k
        sits on vertex k (these objects have one vertex per sprite). Camera-facing billboards in the game."""
        lv, un = self.lv, self.un
        n, = un('I', el)
        sp = lv.ptr(el, 4)
        for k in range(min(n, 256) if sp else 0):
            info = lv.ptr(sp, 0x10 * k + 4)
            sx, sy = un('2f', sp, 0x10 * k + 8)
            scale = lv.ptr(info, 0xC) if info else None
            hx, hy = un('2f', scale) if scale else (0.5, 0.5)
            mp = lv.ptr(info, 0x14) if info else None
            mat = self.material(lv.ptr(mp)) if mp else None
            if mat is not None:
                geo.sprites.append((min(k, len(geo.vertices) - 1), abs(hx * sx), abs(hy * sy), mat))

    def matrix(self, loc):
        """Matrix -> row-major 3x4 = T * M, M = the second 3x3 (the complete rotation x scale
        transform; the first 3x3 is the rotation alone). Using R * M squared every perso's yaw."""
        if loc is None:
            return IDENTITY
        f = self.un('I21f', loc)
        pos, s = f[1:4], f[13:22]
        return tuple(v for i in range(3) for v in (s[i], s[3 + i], s[6 + i], pos[i]))  # stored as columns

    def read_so(self, loc, parent_world):
        if loc is None or loc in self.so_seen:
            return None
        self.so_seen.add(loc)
        lv = self.lv
        t, = self.un('I', loc)
        data = lv.ptr(loc, 4)
        fix_m = self.fix_matrices.get(data) if t == 4 else None
        so = SuperObject(t, loc, fix_m or self.matrix(lv.ptr(loc, 0x20)))
        so.world = mat_mul(parent_world, so.matrix)
        if so.kind == 'ipo':
            so.lods = self.lods(lv.ptr(data)) if data else []
            so.collide = self.collide(lv.ptr(data)) if data else None
            isi = lv.ptr(data, 8) if data else None
            so.colors = self.radiosity(isi, so.lods) if data else []
            # Extra baked sets (day/night variants) exist only where ISI+4 is set (Land, sud0).
            so.color_sets = [so.colors] + ([self.radiosity(isi, so.lods, i) for i in (1, 2)]
                                           if isi and lv.ptr(isi, 4) else [])
            fill_unbaked(so)
        elif so.kind == 'perso' and data:
            so.perso = self.read_perso(data, so.world)
        c = lv.ptr(loc, 8)
        while c is not None:
            child = self.read_so(c, so.world)
            if child is None:
                break
            so.children.append(child)
            c = lv.ptr(c, 0x14)
        return so

    def lods(self, po):
        """PhysicalObject -> list of Geometric (one per LOD)."""
        lv = self.lv
        vs = lv.ptr(po) if po else None
        if not vs:
            return []
        n_lod, vtype = self.un('HH', vs, 4)
        if vtype != 0:
            self.warnings.append(f'visual set {vs[1]:#x}: type {vtype} not handled')
            return []
        objs = lv.ptr(vs, 12)
        return [self.geometric(p) for p in (lv.ptr(objs, 4 * i) for i in range(n_lod)) if p]

    def radiosity(self, isi, lods, index=0):
        """ISI -> baked per-vertex colours per LOD; a LOD whose count doesn't match its vertices
        (bar/cock01/marmite/pyramide: 8 IPOs, their ISI has no radiosity at +0) gets None."""
        rad = self.lv.ptr(isi, 4 * index) if isi else None
        if rad is None:
            return []
        n_lod, = self.un('I', rad)
        lp, out = self.lv.ptr(rad, 4), []
        for i, geo in enumerate(lods[:n_lod] if lp else ()):
            nc, = self.un('I', lp, 8 * i)
            cp = self.lv.ptr(lp, 8 * i + 4)
            ok = cp is not None and nc == len(geo.vertices)
            out.append([self.un('3h', cp, 8 * k) for k in range(nc)] if ok else None)
        return out

    def read_perso(self, loc, world):
        """Perso with its initial state's animation (see the module docstring): objects = the
        object list's (Geometric LOD 0, scale) per entry index, anim = the decoded A3D."""
        lv, un = self.lv, self.un
        p3d = lv.ptr(loc)
        fam = lv.ptr(p3d, 0x14) if p3d else None
        p = Perso(loc, un('I', fam, 12)[0] if fam else -1, world)
        if not p3d:
            p.note = 'no 3d data'
            return p
        ol = lv.ptr(p3d, 0xC) or lv.ptr(p3d, 0x10) or (lv.ptr(fam, 0x1C) if fam else None)
        state = lv.ptr(p3d, 0) or lv.ptr(p3d, 4)
        ar = lv.ptr(state, 0xC) if state else None
        a3 = lv.ptr(ar, 0x28) if ar else None
        if not ol:
            p.note = 'no object list'
            return p
        if not a3:
            p.note = 'no animation'
            return p
        try:
            p.anim = self.read_a3d(a3, un('B', state, 0x3B)[0])
            pose = p.anim.pose(0)
        except (IndexError, struct.error) as e:
            self.warnings.append(f'a3d {a3[1]:#x}: {e}')
            p.note = 'bad animation'
            return p
        self.load_objects(p, ol, {o for t in p.anim.objects for o in t if o is not None})
        p.parts = p.posed(pose)
        return p

    def load_objects(self, p, ol, used):
        """Fill p.objects / p.collides for the object-list entries in `used`."""
        lv, un = self.lv, self.un
        n_ent, = un('H', ol, 0x14)
        ents = lv.ptr(ol, 0xC)
        for obj in sorted(used - set(p.objects)):
            if obj >= n_ent or not ents:
                self.warnings.append(f'perso {p.loc[1]:#x}: object {obj} >= {n_ent}')
                continue
            sp, po = lv.ptr(ents, 0x14 * obj), lv.ptr(ents, 0x14 * obj + 4)
            lods = self.lods(po) if po else []
            if lods:
                sx, sy, sz = un('3f', sp) if sp else (1, 1, 1)
                p.objects[obj] = (lods[0], (sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0))
                col = self.collide(po)
                if col:
                    p.collides[obj] = col

    def collide(self, po):
        """PhysicalObject -> (vertices, triangles, floors) from its collide set's indexed-triangle
        elements (layout in the module docstring), or None. floors[k] = (grip, rebound) of triangle
        k's mechanical material, or None when it has none. Other element types are counted in
        self.collide_skipped."""
        if not hasattr(self, 'collide_cache'):
            self.collide_cache, self.collide_skipped = {}, {}
        if po in self.collide_cache:
            return self.collide_cache[po]
        lv, un = self.lv, self.un
        cs = lv.ptr(po, 4) if po else None
        zdr = lv.ptr(cs, 12) if cs else None
        out = None
        if zdr:
            nv, = un('I', zdr)
            ne, = un('I', zdr, 0x14)
            pv, pt = lv.ptr(zdr, 4), lv.ptr(zdr, 0x18)
            verts = [un('3f', pv, 12 * i) for i in range(nv)] if pv else []
            tris, floors = [], []
            for i, t in enumerate(un(f'{ne}H', pt) if ne and pt else ()):
                el = lv.ptr(lv.ptr(zdr, 0x1C), 4 * i)
                if t in (7, 8) and el is not None:
                    self.collide_primitives(el, t, verts, tris, floors)
                    continue
                if t != 1 or el is None:
                    self.collide_skipped[t] = self.collide_skipped.get(t, 0) + 1
                    continue
                nt, = un('H', el, 4)
                ti = lv.ptr(el, 8)
                floor = self.floor_material(lv.ptr(el))
                for k in range(nt if ti else 0):
                    tri = un('3h', ti, 6 * k)
                    if 0 <= min(tri) and max(tri) < nv:
                        tris.append(tri)
                        floors.append(floor)
            out = (verts, tris, floors) if tris else None
        self.collide_cache[po] = out
        return out

    def collide_primitives(self, el, t, verts, tris, floors):
        """Collide element type 7 (spheres) or 8 (axis-aligned boxes, in the object's axes) -> triangles appended to
        verts / tris / floors. Element = {u32 n, ptr entries, entries...} (the entries follow inline): sphere entry
        (12 bytes) {u16 centre vertex, u16, f32 radius, ptr game material}; box entry (8 bytes) {u16 min vertex, u16
        max vertex, ptr game material}. Only props use them: Land's kitkatland paths and landpont bridges (boxes),
        gen_pierres stones (spheres, r 1.9), gates (grille), drawbridges, carota's jaws ... Spheres become an
        80-face icosphere (vertices on the sphere)."""
        lv, un = self.lv, self.un
        n, = un('I', el)
        ents = lv.ptr(el, 4)
        size = 12 if t == 7 else 8
        for k in range(min(n, 64) if ents else 0):
            if t == 8:
                a, b = un('2H', ents, size * k)
                gm = lv.ptr(ents, size * k + 4)
                if max(a, b) >= len(verts):
                    continue
                lo, hi = verts[a], verts[b]
                faces = box_triangles(lo, hi)
            else:
                c, = un('H', ents, size * k)
                r, = un('f', ents, size * k + 4)
                gm = lv.ptr(ents, size * k + 8)
                if c >= len(verts) or not r > 0:
                    continue
                faces = sphere_triangles(verts[c], r)
            floor = self.floor_material(gm)
            for f in faces:
                base = len(verts)
                verts.extend(f)
                tris.append((base, base + 1, base + 2))
                floors.append(floor)

    def floor_material(self, gm):
        """GameMaterial -> (grip, rebound) from its mechanics material (+4; 0x1C bytes, 5 floats read
        by the engine: [1] rebound, [2] grip, see NOTES "Floor mechanical material"), or None."""
        mm = self.lv.ptr(gm, 4) if gm else None
        if mm is None:
            return None
        _, rebound, grip = self.un('3f', mm)
        return (grip, rebound)

    def collision(self, floors=False, skip_decals=False):
        """World-space collision triangles [((x, y, z) * 3)]: every static IPO's collide set, plus
        perso collide meshes posed at frame 0 of their initial state (sud0's lava-pit platforms,
        doors, ...; moving persos are treated as static). Ed's family has none.
        floors=True returns (triangles, per-triangle (grip, rebound) or None).
        skip_decals: leave out collide sets that are a single flat quad facing up (2 triangles, normal z > 0.5): the
        ski slopes' speed arrows, spring stars and red direction arrows lying on the snow, which the skier rides over
        (their effects come from trigger persos next to them, ai.ski_portals); as collision their edges stopped him."""
        out, out_floors = [], []

        def decal(col, m):
            if len(col[1]) != 2:
                return False
            vs = [mat_apply(m, v) for v in col[0]]
            for t in col[1]:
                a, b, c = (vs[i] for i in t)
                u = [b[k] - a[k] for k in range(3)]
                v = [c[k] - a[k] for k in range(3)]
                n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
                if abs(n[2]) <= 0.5 * math.sqrt(sum(x * x for x in n)):
                    return False
            return True

        def add(col, m):
            if skip_decals and decal(col, m):
                return
            verts = [mat_apply(m, v) for v in col[0]]
            flip = mat_det(m) < 0
            out.extend(tuple(verts[i] for i in (t[::-1] if flip else t)) for t in col[1])
            out_floors.extend(col[2])
        for so in self.walk():
            if so.collide:
                add(so.collide, so.world)
        for p in self.persos():
            if p.collides and p.anim:
                for o, m in p.anim.pose(0):
                    if o in p.collides:
                        add(p.collides[o], mat_mul(p.world, mat_mul(m, p.objects[o][1])))
        return (out, out_floors) if floors else out

    def family_states(self, fam):
        st, out = self.lv.ptr(fam, 0x10), []
        while st is not None and len(out) < 4096:
            out.append(st)
            st = self.lv.ptr(st)
        return out

    def state_anim_keys(self, states):
        """Per state: the index of the first state sharing its A3D (states sharing one animation get the same key),
        or -1 without an animation."""
        first, keys = {}, []
        for i, st in enumerate(states):
            ar = self.lv.ptr(st, 0xC)
            a3 = self.lv.ptr(ar, 0x28) if ar else None
            keys.append(first.setdefault(a3, i) if a3 else -1)
        return keys

    def player(self):
        """Location of the perso the player controls. Normally Ed = the first GPT perso-in-fix (family "NewEd").
        Totalski and skymap instead hold a level perso of the same family with its own AI model that reads the
        controls (the skier, ID card type 2 states); that one is the player there. Inferred from the data (the
        exe's switch is not found yet; the fix Ed's placement there is the stale fix matrix): see docs/NOTES.md
        "Skiing"."""
        if not self.perso_in_fix or not self.perso_in_fix[0][0]:
            return None
        if not hasattr(self, '_player'):
            from . import ai
            lv, fix_ed = self.lv, self.perso_in_fix[0][0]
            fam = lv.ptr(lv.ptr(fix_ed), 0x14)
            self._player = fix_ed
            for p in self.persos():
                if p.loc != fix_ed and lv.ptr(lv.ptr(p.loc), 0x14) == fam and \
                        any(n.type == 14 for n in ai.AI(self, p.loc).nodes()):
                    self._player = p.loc
                    break
        return self._player

    def ed(self, state_anims=False):
        """The player (see player(): Ed, the first GPT perso-in-fix, family "NewEd" in all 21 levels, 369 states;
        the skier on Totalski / skymap).
        Returns (Perso, start 3x4 world matrix, [(name, Anim, loop)]) for ED_ANIMS, with every
        object those animations show loaded into perso.objects; None if absent. With state_anims, also every
        distinct state animation, named "s<k>" (k = state_anim_keys), at the A3D's own speed, loop False."""
        loc = self.player()
        if loc is None:
            return None
        p = next((x for x in self.persos() if x.loc == loc), None)
        p3d = self.lv.ptr(loc)
        fam = self.lv.ptr(p3d, 0x14) if p3d else None
        ol = (self.lv.ptr(p3d, 0xC) or self.lv.ptr(p3d, 0x10)) if p3d else None
        if p is None or not fam or not ol:
            return None
        states, anims = self.family_states(fam), []
        for name, i, loop in ED_ANIMS:
            a3 = self.lv.ptr(self.lv.ptr(states[i], 0xC), 0x28) if i < len(states) else None
            if a3:
                a = self.read_a3d(a3, self.un('B', states[i], 0x3B)[0])
                self.load_objects(p, ol, {o for t in a.objects for o in t if o is not None})
                anims.append((name, a, loop))
        if state_anims:
            for k in sorted(set(self.state_anim_keys(states)) - {-1}):
                a = self.read_a3d(self.lv.ptr(self.lv.ptr(states[k], 0xC), 0x28))
                self.load_objects(p, ol, {o for t in a.objects for o in t if o is not None})
                anims.append((f's{k}', a, False))
        return p, self.fix_matrices.get(loc, p.world), anims

    def read_a3d(self, a3, state_speed=0):
        """Parse a TT A3D (layout in the module docstring) into an Anim."""
        un = self.un
        speed, nv, nq, nh, nn, nnn, nch, nf, nkf = un('9H', a3)
        o = 0x34
        vec = [un('3f', a3, o + 12 * i) for i in range(nv)]
        o += 12 * nv
        quat = [un('4h', a3, o + 8 * i) for i in range(nq)]
        o += 8 * nq
        hier = [un('2h', a3, o + 4 * i) for i in range(nh)]
        o += 4 * nh
        ntto = [un('HH', a3, o + 6 * i) for i in range(nn)]
        o += 6 * nn
        frames = [un('6H', a3, o + 12 * i) for i in range(nf)]
        o += 12 * nf
        chans = [un('Hh4H', a3, o + 12 * i) for i in range(nch)]
        o += 12 * nch
        num_ntto = un(f'{nnn * nch}H', a3, o)
        o += 2 * nnn * nch
        fkf = un(f'{nf * nch}H', a3, o)
        o = (o + 2 * nf * nch + 3) & ~3
        kfs = [un('4f10H', a3, o + 36 * i) for i in range(nkf)]
        return Anim(state_speed or speed, nf, vec, quat, hier, ntto, frames, chans, num_ntto, fkf, kfs)
    def object_names(self):
        """The GPT's 3 object type name tables (family, model, instance names), as lists indexed like the perso's
        std game block (+0 family, +4 model, +8 instance). Each table is {first, last, n} of entries {next, prev,
        head, ptr name}; the fix's entries come first and the link from the last fix entry to the level's first is
        one of the unresolved fix -> level pointers, so the level part is found as the entry whose prev points back
        at it (and shares its head pointer). Land: 52 families (gen_abeille, NewEd, Camera, ..., then the level's), 99 models, 243 instances."""
        if hasattr(self, '_object_names'):
            return self._object_names
        lv, un = self.lv, self.un
        g = (lv.gpt, 0)
        off = 4 + 0x60 * un('I', g)[0] + 40
        prev_of = {}
        for where, dest in lv.pointers.items():
            prev_of.setdefault(dest, []).append(where)
        tables = []
        for t in range(3):
            count, = un('I', g, off + 12 * t + 8)
            e, names = lv.ptr(g, off + 12 * t), []
            while e is not None and len(names) < count:
                q = lv.ptr(e, 12)
                names.append(bytes(un('64B', q)).split(b'\0')[0].decode('latin-1') if q else '?')
                nxt = lv.ptr(e)
                if nxt is None:  # unresolved fix -> level link: the entry whose prev (+4) points here
                    cands = [(s, o - 4) for s, o in prev_of.get(e, [])
                             if o >= 4 and (s, o - 4) != e and lv.ptr((s, o - 4), 8) == lv.ptr(e, 8)]
                    nxt = cands[0] if cands else None
                e = nxt
            tables.append(names)
        self._object_names = tables
        return tables

    def perso_names(self, loc):
        """(family, model, instance) names of the perso at loc ('' where unknown)."""
        sg = self.lv.ptr(loc, 4)
        if sg is None:
            return ('', '', '')
        tables = self.object_names()
        return tuple(tables[t][k] if k < len(tables[t]) else '' for t, k in enumerate(self.un('3I', sg)))

    def persos(self):
        return [so.perso for so in self.walk(self.dynamic) if so.perso] if self.dynamic else []

    def sector_name(self, so):
        """Sector SuperObject -> its name (sector data +0x94, NUL-terminated, e.g. "sctciel"), or ''."""
        data = self.lv.ptr(so.loc, 4) if so.kind == 'sector' else None
        if data is None:
            return ''
        raw = data[0].data[data[1] + 0x94:data[1] + 0x94 + 0x40]
        return raw.split(b'\0')[0].decode('latin-1')

    def fog(self):
        """The level's fog: (near, far, (r, g, b)) of the visual environment most sectors use, or None when
        it is the engine default (near 20000: no visible fog). Sector data +4 = list of environment entries
        {ptr mechanics env, ptr ?, ptr visual env}; visual env = {u32, ptr, f32 near, far, f32 (third
        distance, unused here), f32 rgb}. north: black 80..120 (night), Totalski: light blue 200..400."""
        lv, un, count = self.lv, self.un, {}
        for so in self.walk():
            data = lv.ptr(so.loc, 4) if so.kind == 'sector' else None
            node = lv.ptr(data, 4) if data else None
            for _ in range(8):
                if node is None:
                    break
                env = lv.ptr(node)
                ve = lv.ptr(env, 8) if env else None
                if ve:
                    near, far = un('2f', ve, 8)
                    key = (near, far, un('3f', ve, 0x14))
                    count[key] = count.get(key, 0) + 1
                node = lv.ptr(node, 4)
        if not count:
            return None
        near, far, rgb = max(count, key=count.get)
        return (near, far, rgb) if near < 10000 and far > near else None

    def mark_sky(self):
        """Sky sectors are named "...ciel..." (sctciel, sct3ciel02, BigBoss "ciel"). Their baked radiosity is
        unusable (sud0 -13, Totalski 0, north 0..267) while the game shows the skies evenly lit, so their
        IPOs get neutral light (RADIOSITY_ONE: texture unchanged) in every baked set. Inferred from the data
        and the in-game look, not from the exe. so.sky marks them."""
        for sec in self.walk():
            if sec.kind != 'sector' or 'CIEL' not in self.sector_name(sec).upper():
                continue
            for so in self.walk(sec):
                if so.kind != 'ipo' or not so.lods:
                    continue
                so.sky = True
                one = (int(RADIOSITY_ONE),) * 3
                n = max(1, len(so.color_sets))
                so.colors = [[one] * len(g.vertices) for g in so.lods]
                so.color_sets = [so.colors] + [[[one] * len(g.vertices) for g in so.lods] for _ in range(n - 1)]

    def surfaces(self):
        """Water surfaces: [(loc, Geometric, world 3x4)], each once (several sectors list the same
        one). Sector data +0x10 = {ptr head, ptr tail, u32 n} of nodes {ptr surface, ptr next, ...};
        surface = {ptr GeometricObject, Matrix (0x58, world), Matrix (copy), ...}. sud0: the rainbow
        river, the pools at the top and foot of the falls, two OO_MER01 sea quads."""
        if hasattr(self, '_surfaces'):
            return self._surfaces
        lv, out, seen = self.lv, [], set()
        for so in self.walk():
            data = lv.ptr(so.loc, 4) if so.kind == 'sector' else None
            node = lv.ptr(data, 0x10) if data else None
            n = self.un('I', data, 0x18)[0] if data else 0
            for _ in range(min(n, 64)):
                if node is None:
                    break
                srf = lv.ptr(node)
                geo = lv.ptr(srf) if srf else None
                if srf and geo and srf not in seen:
                    seen.add(srf)
                    out.append((srf, self.geometric(geo), self.matrix((srf[0], srf[1] + 4))))
                node = lv.ptr(node, 4)
        self._surfaces = out
        return out

    def meshes(self, persos=True, t=None):
        """Yield (key, Geometric LOD 0, world 3x4) for static IPOs, water surfaces and (optionally)
        perso parts, posed at frame 0 or, given t, at t seconds into their animation."""
        for so in self.walk():
            if so.lods:
                yield so, so.lods[0], so.world
        for loc, geo, m in self.surfaces():
            yield ('surface', loc), geo, m
        if persos and self.dynamic:
            for so in self.walk(self.dynamic):
                for i, (geo, m) in enumerate((so.perso.parts if t is None else so.perso.at(t)) if so.perso else ()):
                    yield (so, i), geo, m

    def walk(self, so=None):
        so = so or self.root
        yield so
        for c in so.children:
            yield from self.walk(c)

"""The cast: the level persos whose own AI the core runs (.ttlv CAST chunk; reader core/src/tc_level.c, runtime
core/src/tc_cast.c). docs/NOTES.md "The cast".

Every level perso with an AI model runs its scripts in the original. The port runs them too, except:
  - the player, the fix persos (Ed, the camera: their own code paths) and the families in EXCLUDED_FAMILIES (the
    camera directors and the cheat-code reader, whose effects the port does not model). World (the level's game flow:
    the collect-N rewards, north's fall height) runs since 2026-11-04; the respawn after a death stays the port's;
  - the persos the converter already reduced to data: the portals (ai.portals: the arches, the walk-in passages) and
    level changers (scripts that teleport or change level: PROC 7 / 8 / 29; .ttai PORT), the skier's start (PROC 6 deactivates the fix Ed), the light persos (PROC 283;
    .ttlv LAIS), Totalski's obstacles (.ttai TRIG).

Per perso the std game's activity fields are exported as the snapshot holds them (GAM_00428070 / GAM_PersoFrame_0042b520):
+0x14 hit points, +0x15 their maximum, +0x17 "dead" (PROC 5 cannot wake it), +0x18 active, +0x20 1 = re-initialised
when PROC 257 takes it out, +0x2A run the mechanics, +0x2B play the animation without mechanics, +0x2C the squared
activation radius (-1: always; else the AI runs only while the main actor's origin is closer).

CAST: u16 main_actor (the player's perso id: level perso k + 1, as perso nodes), u16 n_families, u16 n_cast,
  u16 main_alias (the fix Ed's id where another perso is the player: the skier; he stands for the main actor; 0 none),
  u16 n_main_zones, u16 0, n_main_zones x a zone as below (the main actor's ZDD zones, in his axes: they move with him)
  u16 n_ghosts, u16 0, n_ghosts x { u16 perso id, u16 0, f32 position[3], u32 size, size bytes = its dsgvars as a .ttai
      writes them (ai.perso_dsg_block) }: the persos the scripts name that do not run (one shared copy each)
  n_families x {
    u16 n_states, u16 n_anims
    n_states x { i16 next (-1 none), u16 frames, u8 speed (frames per second, 0: 30), u8 flags (1: only the listed
                 transitions), u8 repeats, u8 0, u16 anim (index in this family's anims, 0xFFFF none),
                 u16 n_transitions, u16 n_prohibited, u16 n_sounds, n_transitions x { u16 target, u16 via },
                 n_prohibited x u16, n_sounds x { u16 frame, u16 global sound event, u8 first, u8 period, u16 0 } }
    n_anims x { u16 n_channels, u16 n_frames, f32 frames per second (the A3D's own; the AI's state clock picks the
                frame), n_frames x n_channels x f32[12] channel -> perso (row-major 3x4), n_frames x n_channels x u16
                object shown (0xFFFF none) } }
  n_cast x {
    u16 perso id, u16 PRSO index (0xFFFF: not drawn), u16 family, u16 sector (0xFFFF none),
    u16 initial state, u8 hit points, u8 max, u8 active, u8 dead, u8 mechanics, u8 animation, u8 re-init,
    u8 flags (1: may force the main actor's behaviours, MAY_FORCE_MAIN; 2: a save point, SAVE_POINTS; 4: on the engine's
      always-awake list, 8: on its run-first list: LevelData.engine_lists),
    u16 0, f32 radius2 (raw +0x2C), f32 world[12] (row-major 3x4),
    u16 n_zones, u16 0, [n_zones > 0: f32 inverse world[12]], n_zones x { u16 k (1-based ZDD zone of COND_13(Me, k, ..)),
      u16 n_prims, n_prims x { u32 kind (7 sphere: a centre, b.x radius; 8 box: a min, b max), f32 a[3], f32 b[3] } },
    u32 size, size bytes = a .ttai SCPT body (ai.py; buttons index the main actor's), padded to 4 }
A cast perso's PRSO entry carries a part for every object its family's animations show: those its initial animation
does not use come after the others, with channel 0xFFFF (the baked loop never draws them).
"""
import struct

from . import ai

EXCLUDED_FAMILIES = {'Camera', 'GenD_Cam', 'gen_cheatCode', 'gen_arche'}
# Port choice: only these families may force the main actor's behaviour or reflex (PROC 237 / 238). The others (the
# talking characters, Agent XYZ, the cutscene and trap triggers) hand Ed to behaviours that wait for conversations,
# cameras and cutscenes the port does not run yet: he would stand there for good. Checked headless (castcheck).
MAY_FORCE_MAIN = {'sud_mecalevier', 'distrib',  # the levers; the popcorn (Crunch) machines: Super Ed
                  'World'}  # the game flow: north's fall height (R0: z < -105 -> Ed's B60), the collect-N reward (B41)
# ... and these AI models, whatever their family: the "holes of nothingness" (genplainesud; 11 on carota, cock01, Land,
# sud0, one script): when Ed drops below the marker, his falling death (B15, then B60: falling 1 s, then B73's cover),
# his hit points 0, the camera frozen watching him (docs/NOTES.md "Falling into pits").
MAY_FORCE_MAIN_MODELS = {'ZOR_LesTrousDeNeantCestBonMangezEn'}
# ... but not these instances: marmite's magnet switch keeps Ed holding it (his dsg3 = 1: Jump and Shoot steer the
# magnet; leaning it to let go with Action does not take in the port yet, castcheck 2026-11-02): he would be stuck.
MAY_NOT_FORCE_MAIN = {'JET_SwAimant'}
# The save points (DFM_Save: within their radius they show the "saved" text, PROC 118, and take themselves out): port
# choice, Ed comes back where he stood when the last one fired, after dying (core tc_ed_ai; flag 2).
SAVE_POINTS = {'DFM_Save'}
HANDLED_PROCS = {6, 7, 8, 29, 283}


def _calls(a, procs):
    def walk(ns):
        return any((n.type == 4 and n.param in procs) or walk(n.children) for n in ns)
    return any(walk(s) for b in a.behaviours for s in b.scripts + ([b.schedule] if b.schedule else []))


def select(d):
    """[(perso location, perso id, AI)] in level perso order, and {reason: count} of the persos left out."""
    lv = d.lv
    player = d.player()
    fix = {x[0] for x in d.perso_in_fix if x and x[0]}
    skipped = {}
    triggers, gates = set(), set()
    if player:
        try:
            pa = ai.AI(d, player)
            triggers = {names for *_, names in ai.proximity_triggers(pa)}
            gates = {g[3] for g in ai.portals(pa, {})}  # perso ids (k + 1)
        except Exception:
            pass
    out = []
    for k, p in enumerate(d.persos()):
        if p.loc == player or p.loc in fix or not lv.ptr(p.loc, 0xC):
            continue
        names = d.perso_names(p.loc)
        reason = None
        try:
            a = ai.AI(d, p.loc)
        except Exception:
            a, reason = None, 'unreadable'
        if a is not None and not a.behaviours:
            reason = 'no behaviours'
        elif a is not None and names[0] in EXCLUDED_FAMILIES:
            reason = names[0]
        elif a is not None and names[:3:2] in triggers:
            reason = 'trigger (TRIG)'
        elif a is not None and k + 1 in gates:
            reason = 'portal (PORT)'
        elif a is not None and _calls(a, HANDLED_PROCS):
            reason = 'portal / level / light (PORT, LAIS)'
        if reason:
            skipped[reason] = skipped.get(reason, 0) + 1
            continue
        out.append((p.loc, k + 1, a))
    return out, skipped


def object_list(d, loc):
    p3d = d.lv.ptr(loc)
    fam = d.lv.ptr(p3d, 0x14) if p3d else None
    return (d.lv.ptr(p3d, 0xC) or d.lv.ptr(p3d, 0x10) or (d.lv.ptr(fam, 0x1C) if fam else None)) if p3d else None


def family_anims(d, states):
    """{state animation key: Anim} for the family's distinct A3Ds (level.state_anim_keys)."""
    keys = d.state_anim_keys(states)
    out = {}
    for k in sorted(set(keys) - {-1}):
        try:
            out[k] = d.read_a3d(d.lv.ptr(d.lv.ptr(states[k], 0xC), 0x28))
        except (IndexError, struct.error) as e:
            d.warnings.append(f'cast a3d of state {k}: {e}')
    return keys, out


def load_cast_objects(d, perso, anims):
    """Load every object the family's animations show into perso.objects (level.Perso)."""
    ol = object_list(d, perso.loc)
    if ol:
        d.load_objects(perso, ol, {o for a in anims.values() for t in a.objects for o in t if o is not None})


def _states_block(d, a, keys, anim_index, snd_table):
    from .sfx import sound_at
    lv, out = d.lv, b''
    for i, s in enumerate(a.states):
        nxt = lv.ptr(s, 0x28)
        ar = lv.ptr(s, 0xC)
        n_frames = d.un('H', ar, 4)[0] if ar else 0
        speed = d.un('B', s, 0x3B)[0] or (d.un('B', ar, 6)[0] if ar else 0)
        flags, repeats = d.un('B', s, 0x3C)[0], d.un('B', s, 0x3A)[0]
        trans, prohib = [], []
        e = lv.ptr(s, 0x10)
        while e and len(trans) < 512:
            tgt, via = lv.ptr(e, 0xC), lv.ptr(e, 0x10)
            if tgt in a.state_index and via in a.state_index:
                trans.append((a.state_index[tgt], a.state_index[via]))
            e = lv.ptr(e)
        e = lv.ptr(s, 0x1C)
        while e and len(prohib) < 512:
            tgt = lv.ptr(e, 0xC)
            if tgt in a.state_index:
                prohib.append(a.state_index[tgt])
            e = lv.ptr(e)
        sounds = []
        ev = lv.ptr(ar, 8) if ar else None
        n_ev = d.un('I', ar, 0xC)[0] if ev else 0
        for k in range(n_ev if n_ev < 256 else 0):  # as ai.export's SNDS
            typ, value, frame = d.un('3I', ev, 20 * k)
            first, period = d.un('2B', ev, 20 * k + 0xB)
            snd = sound_at(snd_table, (ev[0], ev[1] + 20 * k + 4), value) if typ == 0 and value else None
            if snd is not None and (frame & 0xFFFF) <= n_frames:
                sounds.append((frame & 0xFFFF, snd, first, period))
        anim = anim_index.get(keys[i], 0xFFFF)
        out += struct.pack('<hHBBBBHHHH', a.state_index.get(nxt, -1), n_frames, speed, flags, repeats, 0, anim,
                           len(trans), len(prohib), len(sounds))
        out += b''.join(struct.pack('<HH', *t) for t in trans) + b''.join(struct.pack('<H', x) for x in prohib)
        out += b''.join(struct.pack('<2H2BH', *r, 0) for r in sorted(sounds))
    return out


def _anim_block(a):
    poses = [a.pose(f) for f in range(a.n_frames)]
    return (struct.pack('<HHf', len(a.chans), len(poses), float(a.fps))
            + b''.join(struct.pack('<12f', *m) for pose in poses for _, m in pose)
            + b''.join(struct.pack('<H', 0xFFFF if o is None else o) for pose in poses for o, _ in pose))


PORT_FREEZE = 0x8101  # a procedure number of the port's own (core tc_ai.c TC_AI_PROC_FREEZE): the target stays, a prop


def freeze_story_removals(a):
    """Port choice (user, 2026-11-02: "keep them around, even if just as temporary props"): a RemovePerso (PROC 257) under
    an If on a GlobalFlag (COND 90) or a save value (FUNC 93) is a story check (Doc and Suzy leave when flag 7, the port's
    all-on abilities, says Doc is rescued): it becomes PORT_FREEZE. Returns how many were rewritten."""
    count = 0

    def gated(cond):
        return any((x.type == 1 and x.param == 90) or (x.type == 3 and x.param == 93) for x in ai.walk_nodes([cond]))

    def walk(ns):
        nonlocal count
        for i, n in enumerate(ns):
            if n.type == 0 and n.param == 0 and n.children and gated(n.children[0]):
                for b in ns[i + 1:i + 3]:  # its Then and Else
                    if b.type == 0 and b.param in (1, 2):
                        for x in ai.walk_nodes([b]):
                            if x.type == 4 and x.param == 257:
                                x.param = PORT_FREEZE
                                count += 1
            walk(n.children)
    for b in a.behaviours:
        for sc in b.scripts + ([b.schedule] if b.schedule else []):
            walk(sc)
    return count


def zones(d, loc, a):
    """The ZDD zones its scripts test with COND_13(Me, k, ...) (1-based k): [(k, primitives)] (ai.zdd_zone: spheres and
    boxes in the perso's axes; triangle zones are left out)."""
    ks = set()
    for b in a.behaviours:
        for sc in b.scripts + ([b.schedule] if b.schedule else []):
            for n in ai.walk_nodes(sc):
                c = n.children
                if n.type == 1 and n.param == 13 and len(c) == 3 and c[0].type == 0 and c[0].param == 4 and c[1].type == 12:
                    ks.add(c[1].param)
    out = []
    for k in sorted(ks):
        prims = ai.zdd_zone(d, loc, k - 1) if k >= 1 else None
        if prims:
            out.append((k, prims[:8]))
    return out


def chunk(d, cast, prso_index, family_cache):
    """The CAST chunk's body, and a listing (lines). cast: select()'s list; prso_index: {perso location: PRSO index};
    family_cache: {family location: (keys, anims)} as load_cast_objects used them."""
    lv = d.lv
    behaviours = {}
    player = d.player()
    for owner in [player] + [loc for loc, _, _ in cast]:
        if not owner:
            continue
        try:
            o = ai.AI(d, owner)
        except Exception:
            continue
        for ref, name in o.beh_names.items():
            behaviours.setdefault(ref, int(name[1:]) | (0x10000 if name[0] == 'R' else 0))
    from .sfx import level_snd, sound_at
    sounds = level_snd(d.lv.name)
    persos = d.persos()
    idx_of = {p.loc: k + 1 for k, p in enumerate(persos)}
    main = next((k + 1 for k, p in enumerate(persos) if p.loc == player), 0)
    main_buttons = ai.button_order(ai.AI(d, player)) if player else {}
    families, fam_blocks, entries, listing = {}, [], [], []
    always, first, _ = d.engine_lists()
    always, first = set(always), set(first)
    for loc, pid, a in cast:
        p3d = lv.ptr(loc)
        fam = lv.ptr(p3d, 0x14) if p3d else None
        if fam not in families:
            keys, anims = family_cache.get(fam) or family_anims(d, a.states)
            anim_index = {k: i for i, k in enumerate(sorted(anims))}
            fam_blocks.append(struct.pack('<HH', len(a.states), len(anims)) + _states_block(d, a, keys, anim_index, sounds)
                              + b''.join(_anim_block(anims[k]) for k in sorted(anims)))
            families[fam] = len(families)
        frozen = freeze_story_removals(a)
        for n in a.nodes():  # the level's own sounds (sfx.level_snd): resolved here, by the node's address
            if n.type == 34 and n.loc:
                n.event = sound_at(sounds, n.loc, n.param)
        body, unresolved = ai.export(a, None, behaviours, main_buttons)
        sg = lv.ptr(loc, 4)
        hp, hp_max, _, dead, active = d.un('5B', sg, 0x14) if sg else (0, 0, 0, 0, 1)
        reinit = d.un('I', sg, 0x20)[0] == 1 if sg else False
        mech, anim, radius2 = d.un('2Bf', sg, 0x2A) if sg else (0, 1, -1.0)
        # the state the baked loop shows (level.read_perso): Perso3dData +0, else +4 (the playing one)
        init_state = a.state_index.get(lv.ptr(p3d, 0) or lv.ptr(p3d, 4), 0) if p3d else 0
        sector = d.perso_sector(loc)
        world = persos[pid - 1].world
        names = d.perso_names(loc)
        flags = 1 if (names[0] in MAY_FORCE_MAIN or names[1] in MAY_FORCE_MAIN_MODELS) and names[2] not in MAY_NOT_FORCE_MAIN else 0
        if names[0] in SAVE_POINTS:
            flags |= 2
        if loc in always:
            flags |= 4  # the engine's always-awake list (LevelData.engine_lists)
        if loc in first:
            flags |= 8  # its run-first list
        zs = zones(d, loc, a)
        zone_block = struct.pack('<HH', len(zs), 0)
        if zs:
            zone_block += struct.pack('<12f', *ai.mat_inverse(world))
            for k, prims in zs:
                zone_block += struct.pack('<HH', k, len(prims)) + b''.join(struct.pack('<I3f3f', t, *x, *y) for t, x, y in prims)
        entries.append(struct.pack('<5H6B2BHf12f', pid, prso_index.get(loc, 0xFFFF), families[fam],
                                   0xFFFF if sector is None else sector, init_state, hp, hp_max, active, dead, mech,
                                   anim, int(reinit), flags, 0, radius2, *world)
                       + zone_block + struct.pack('<I', len(body)) + body + b'\0' * (-len(body) % 4))
        listing.append(f'{pid:5d} {names[0]:24s} {names[2]:32s} family {families[fam]:3d} states {len(a.states):3d} '
                       f'prso {prso_index.get(loc, -1):4d} sector {sector} active {active} mech {mech} anim {anim} '
                       f'r2 {radius2:g}' + (' always' if flags & 4 else '') + (' first' if flags & 8 else '')
                       + (f' story removals kept {frozen}' if frozen else '')
                       + (f' unresolved {unresolved}' if unresolved else ''))
    # the main actor's own ZDD zones (spheres, boxes), for the cast's COND_13(Ed, k, Me): the machine in front of Ed
    main_zones = [(k, z[:8]) for k, z in ((k, ai.zdd_zone(d, player, k - 1)) for k in range(1, 9)) if z] if player else []
    zblock = struct.pack('<HH', len(main_zones), 0) + b''.join(
        struct.pack('<HH', k, len(z)) + b''.join(struct.pack('<I3f3f', t, *x, *y) for t, x, y in z) for k, z in main_zones)
    # the persos the cast's scripts name that do not run (World, the camera, the excluded ones): one shared copy of their
    # dsgvars each, so what one script writes there the others read (cavedoc's HEG_Inventaire counts its jingle into
    # World's dsg26: without the copy the write was lost and the jingle replayed every few ticks)
    # where another perso is the player (the skier), the fix Ed stands for him: Counter34(fix Ed) is the main
    # character's hit points (FUNC_Counter34Etc_0049ad30, the global table): an alias of the main actor, not a ghost
    fix_ed = d.perso_in_fix[0][0] if d.perso_in_fix else None
    alias = idx_of.get(fix_ed, 0) if fix_ed and fix_ed != player else 0
    running = {pid for _, pid, _ in cast} | {main, alias}
    named = set()
    for _, _, a in cast:
        for n in a.nodes():
            if n.type == 24 and n.ref in idx_of:
                named.add(idx_of[n.ref])
    ghosts = sorted(named - running)
    gblock = struct.pack('<HH', len(ghosts), 0)
    for gid in ghosts:
        loc = persos[gid - 1].loc
        dsg, _ = ai.perso_dsg_block(d, loc)
        w = persos[gid - 1].world
        gblock += struct.pack('<HH3fI', gid, 0, w[3], w[7], w[11], len(dsg)) + dsg
    head = struct.pack('<4H', main, len(fam_blocks), len(entries), alias) + zblock + gblock
    return head + b''.join(fam_blocks) + b''.join(entries), listing

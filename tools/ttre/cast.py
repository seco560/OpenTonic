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

CAST: u16 main_actor (the player's perso id: level perso k + 1, as perso nodes; Special Edition persos: SE_ID_BASE +
  k + 1), u16 n_families, u16 n_cast,
  u16 main_alias (the fix Ed's id where another perso is the player: the skier; he stands for the main actor; 0 none),
  u16 n_main_zones, u16 world (the World keyword's perso id, 0 none),
  n_main_zones x a zone as below (the main actor's ZDD zones, in his axes: they move with him)
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
      always-awake list, 8: on its run-first list: LevelData.engine_lists; 16: a scene's actor, SCENE; 32: TABLES;
      64: a spawnable template, SPAWN, out until instanced),
    u16 0, f32 radius2 (raw +0x2C), f32 world[12] (row-major 3x4),
    u16 n_zones, u16 0, [n_zones > 0: f32 inverse world[12]], n_zones x { u16 k (1-based ZDD zone of COND_13(Me, k, ..)),
      u16 n_prims, n_prims x { u32 kind (7 sphere: a centre, b.x radius; 8 box: a min, b max), f32 a[3], f32 b[3] } },
    u32 size, size bytes = a .ttai SCPT body (ai.py; buttons index the main actor's), padded to 4,
    [flags & 32: u32 size, size bytes = the STRS body then the VECS body its scripts index, padded to 4] }
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
                  'World',  # the game flow: north's fall height (R0: z < -105 -> Ed's B60), the collect-N reward (B41)
                  'grille',  # cavedoc's popcorn grille: Super Ed holding Action in its zone works it (Ed's dsg58 = 1)
                  'mais',  # cavedoc's corn cob boss: its intro / end hold Ed (B15 + R4, then B103), as the robot's
                  'genski'}  # the ski levels' flow: the finish (SYM_Gen_Arrivee, NBD_Arrivee_Et_Retry) writes the
#                              skier's arrival waypoints and next / retry (dsg35 / 36 / 40) and the fix Ed's dsg148 = 3,
#                              which start his own finish (B19: state 321, stepping off); the start (PAT_DEPARTESSAI)
# ... and these AI models, whatever their family: the "holes of nothingness" (genplainesud; 11 on carota, cock01, Land,
# sud0, one script): when Ed drops below the marker, his falling death (B15, then B60: falling 1 s, then B73's cover),
# his hit points 0, the camera frozen watching him (docs/NOTES.md "Falling into pits").
MAY_FORCE_MAIN_MODELS = {'ZOR_LesTrousDeNeantCestBonMangezEn',
                         # cavedoc's robot (FRB_RoBneus), the power 7 boss: its intro holds Ed (B15 + R4, B103 after
                         # its line); beaten, it ends his Super Ed (dsg15 := 0), sends him to take the power (B76 sets
                         # his dsg58 = 1) and gives power 7 (its B11). Kept off Ed, the fight ran but gave nothing.
                         'FRB_RoboBne'}
# ... but not these instances: marmite's magnet switch keeps Ed holding it (his dsg3 = 1: Jump and Shoot steer the
# magnet; leaning it to let go with Action does not take in the port yet, castcheck 2026-11-02): he would be stuck.
MAY_NOT_FORCE_MAIN = {'JET_SwAimant'}
# The save points (DFM_Save: within their radius they show the "saved" text, PROC 118, and take themselves out): port
# choice, Ed comes back where he stood when the last one fired, after dying (core tc_ed_ai; flag 2).
SAVE_POINTS = {'DFM_Save'}
HANDLED_PROCS = {6, 7, 8, 29, 283}
# The Special Edition levels whose persos run their own scripts (translated to retail numbering, ai.se_translate),
# read one by one (docs/SE.md "The outro"). Elsewhere the SE persos stay props. On these: no SE perso may force or
# write the retail Ed (its behaviour references are SE Ed's numbering), PROC 6 is a plain DeactivatePerso (no skier
# start there), and the cast is a scene: its removals and deactivations take wherever Ed is (TC_CAST_SCENE, no removal
# guard: the outro's relay of characters, each taking itself out after its turn).
SE_CAST_LEVELS = {'outro'}
SE_HANDLED_PROCS = {7, 8, 29, 283}
# On the other SE levels only the items run (docs/SE.md "Pickups"), with World, which counts them: the silver icons
# (gen_icon_argen, World's dsg1 towards its dsg0 = 10: +1 max hit points), the gold (gen_icon_or: refills Ed) and red
# ones (gen_icon3: + 1 hit point), the beehives (gen_ruche_j) and Doc's machine parts (doc_*: the inventory), the save
# points (SE_SAVE_MODELS).
SE_ITEM_FAMILIES = {'gen_icon_argen', 'gen_icon_or', 'gen_icon3', 'gen_ruche_j', 'doc_domino', 'doc_elicon', 'doc_pig',
                    'doc_plum', 'doc_ressor', 'doc_kyou', 'World'}
SE_SAVE_MODELS = {'ARG_Sauvegarde', 'JOB_Sauvegarde', 'POC_POC'}  # genplainenord: Proc_SaveGame within dsg0, then out
SCENE = 16  # entry flag: core tc_level.h TC_CAST_SCENE
TALK = 128  # entry flag (TC_CAST_TALK): a hint scene's speaker: asleep until Ed talks to it
# The hint scenes (docs/ENGINE.md "Walkable scenes"): a level of one speaker who tells the scene's own Ed a hint, then hands
# over to the main level. Port: Ed walks in himself, so the speaker runs (its ChangeLevel is the way out), and the scene's
# Ed is left out altogether.
SCENE_SPEAKERS = {'PAT_BarmanAstuce', 'PAT_EspionSpynorth', 'PAT_Fou_Grille', 'PAT_Mammouth_Astuce', 'PAT_Pharmacien_Astuce',
                  'PAT_EspionJournal'}
SCENE_ED = 'PAT_Ed_Map_Astuce'
SPAWN = 64  # entry flag (TC_CAST_SPAWN): a spawnable template (LevelData.spawnables), out until META 11 instances it
TABLES = 32  # entry flag (TC_CAST_TABLES): its scripts' strings and constant vectors follow them (the STRS and VECS
             # bodies of a .ttai), which the cast had none of before 2026-11-05 (every string and vector read as nothing)


def _calls(a, procs, fix=frozenset()):
    """Whether its scripts call one of procs. PROC 6 (DeactivatePerso) counts only when it deactivates a fix perso (the
    skier's start: PROC_6(fix Ed)); on Me or another level perso it is the perso's own logic (cavedoc's platforms that
    fall apart, BEM_PFQuiSeDesagrege: B3 deactivates itself), which was left out before 2026-11-05."""
    def hit(n):
        if n.type != 4 or n.param not in procs:
            return False
        if n.param != 6:
            return True
        c = n.children[0] if n.children else None
        return c is not None and c.type == 24 and getattr(c, 'ref', None) in fix

    def walk(ns):
        return any(hit(n) or walk(n.children) for n in ns)
    return any(walk(s) for b in a.behaviours for s in b.scripts + ([b.schedule] if b.schedule else []))


def select(d):
    """[(perso location, perso id, AI)] in level perso order, and {reason: count} of the persos left out."""
    lv = d.lv
    handled = SE_HANDLED_PROCS if d.se else HANDLED_PROCS
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
        if d.se and d.lv.name.lower() not in SE_CAST_LEVELS and names[0] not in SE_ITEM_FAMILIES \
                and names[1] not in SE_SAVE_MODELS:
            skipped['special edition: not an item'] = skipped.get('special edition: not an item', 0) + 1
            continue
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
        elif a is not None and names[1] not in SCENE_SPEAKERS and _calls(a, handled, frozenset(fix)):
            reason = 'portal / level / light (PORT, LAIS)'
        if reason:
            skipped[reason] = skipped.get(reason, 0) + 1
            continue
        out.append((p.loc, k + 1, a))
    ids = d.perso_ids()
    for t in d.spawnables():  # the templates the scripts instance (SPAWN: inactive and undrawn until then)
        try:
            a = ai.AI(d, t.loc)
        except Exception:
            continue
        if a.behaviours and t.anim and t.objects:
            out.append((t.loc, ids[t.loc], a))
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
            a = d.read_se_anim(d.lv.ptr(states[k], 0xC)) if d.se else d.read_a3d(d.lv.ptr(d.lv.ptr(states[k], 0xC), 0x28))
            if a:
                out[k] = a
        except (IndexError, struct.error) as e:
            d.warnings.append(f'cast anim of state {k}: {e}')
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
        n_frames, ar_speed = d.anim_ref_frames(ar) if ar else (0, 0)
        speed = d.un('B', s, 0x3B)[0] or ar_speed
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
        ev = lv.ptr(ar, 8) if ar and not d.se else None  # the SE's animation events: not read (no A3D layout)
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


# The Special Edition's persos are numbered from here (cast.chunk): the borrowed retail Ed's .ttai names persos in his
# donor level's numbering (2 = the camera, ...), and the two share the core's id space (tc_cast find). SE Ed's own
# references go to the main actor.
SE_ID_BASE = 0x2000


def chunk(d, cast, prso_index, family_cache, donor_head=None):
    """The CAST chunk's body, and a listing (lines). cast: select()'s list; prso_index: {perso location: PRSO index};
    family_cache: {family location: (keys, anims)} as load_cast_objects used them. donor_head: the first 8 bytes of a
    donor's CAST (se.donor_cast: the skier's main actor and alias, in the donor's numbering) for an SE level."""
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
    sounds = {} if d.se else level_snd(d.lv.name)  # the SE's own sound tables: not read
    persos = d.persos()
    spawn_locs = {t.loc for t in d.spawnables()}
    by_loc = {p.loc: p for p in persos + d.spawnables()}
    base = SE_ID_BASE if d.se else 0
    idx_of = {p.loc: base + k + 1 for k, p in enumerate(persos)} if d.se else dict(d.perso_ids())
    # the SE: the retail donor Ed stands in SE Ed's place (none of whose behaviours resolve: they are SE Ed's, no SE perso
    # forces him); SE Ed's references are the main actor's: his own number, or the donor's (the skier's) with donor_head
    main_loc = d.perso_in_fix[0][0] if d.se and d.perso_in_fix else player
    main = idx_of.get(main_loc, 0)
    donor_alias = 0
    if d.se and donor_head:
        main, _, _, donor_alias = struct.unpack_from('<4H', donor_head)
    if d.se and main_loc:
        idx_of[main_loc] = main
    # the World keyword's perso: the engine's world perso (gpt -> 0x8c1744, LevelData.engine_lists), else (the SE's
    # .gpt is not read) the level's World family perso
    world_loc = d.engine_lists()[2]
    world_id = idx_of.get(world_loc) or next((idx_of[p.loc] for p in persos if d.perso_names(p.loc)[0] == 'World'), 0)
    main_buttons = ai.button_order(ai.AI(d, player)) if player else {}
    families, fam_blocks, entries, listing = {}, [], [], []
    cards, fam_cards = {}, []  # CMEC: the ID cards of the families' states (cast mechanics, core tc_cast_mec.c)
    fam_walk = []  # CMEC's walker tail: each family's root motion and ZDM bodies (walk_block)
    fam_zde = []  # CMEC's ZDE tail: each family's ZDE zones (ai.zde_block)
    always, first, _ = d.engine_lists()
    always, first = set(always), set(first)
    for loc, _, a in cast:
        pid = idx_of[loc]
        p3d = lv.ptr(loc)
        fam = lv.ptr(p3d, 0x14) if p3d else None
        if fam not in families:
            keys, anims = family_cache.get(fam) or family_anims(d, a.states)
            anim_index = {k: i for i, k in enumerate(sorted(anims))}
            fam_blocks.append(struct.pack('<HH', len(a.states), len(anims)) + _states_block(d, a, keys, anim_index, sounds)
                              + b''.join(_anim_block(anims[k]) for k in sorted(anims)))
            families[fam] = len(families)
            row = []
            for st in a.states:  # State +0x30, as ai.export's STAT rows
                c = lv.ptr(st, 0x30)
                row.append(cards.setdefault(c, len(cards)) if c and d.un('I', c)[0] in ai.CARD_SIZES else 0xFFFF)
            fam_cards.append(row)
            fam_walk.append(walk_block(d, loc, a.states, keys))
            fam_zde.append(ai.zde_block(d, loc, len(a.states)))
        frozen = freeze_story_removals(a)
        for n in a.nodes():  # the level's own sounds (sfx.level_snd): resolved here, by the node's address
            if n.type == 34 and n.loc and not d.se:
                n.event = sound_at(sounds, n.loc, n.param)
        body, unresolved, tables = ai.export(a, None, behaviours, main_buttons, tables=True, perso_ids=idx_of)
        sg = lv.ptr(loc, 4)
        hp, hp_max, _, dead, active = d.un('5B', sg, 0x14) if sg else (0, 0, 0, 0, 1)
        reinit = d.un('I', sg, 0x20)[0] == 1 if sg else False
        mech, anim, radius2 = d.un('2Bf', sg, 0x2A) if sg else (0, 1, -1.0)
        if d.se:  # the SE's std game ends before +0x2A (+0x28 = 6, then pointers): no activation radius nor these flags
            mech, anim, radius2 = 0, 1, -1.0  # (retail added them with SetActivity, PROC 17-22: docs/SE.md)
        # the state the baked loop shows (level.read_perso): Perso3dData +0, else +4 (the playing one)
        init_state = a.state_index.get(lv.ptr(p3d, 0) or lv.ptr(p3d, 4), 0) if p3d else 0
        sector = d.perso_sector(loc)
        world = by_loc[loc].world
        names = d.perso_names(loc)
        flags = 1 if (names[0] in MAY_FORCE_MAIN or names[1] in MAY_FORCE_MAIN_MODELS) and names[2] not in MAY_NOT_FORCE_MAIN else 0
        if names[1] in SCENE_SPEAKERS:
            flags |= TALK
        if d.se:  # SE_CAST_LEVELS: a scene; elsewhere the items, and the save points as retail's DFM_Save
            flags = SCENE if d.lv.name.lower() in SE_CAST_LEVELS else 2 if names[1] in SE_SAVE_MODELS else 0
        if names[0] in SAVE_POINTS:
            flags |= 2
        if loc in always:
            flags |= 4  # the engine's always-awake list (LevelData.engine_lists)
        if loc in first:
            flags |= 8  # its run-first list
        if loc in spawn_locs:
            flags |= SPAWN
        flags |= TABLES
        zs = zones(d, loc, a)
        zone_block = struct.pack('<HH', len(zs), 0)
        if zs:
            zone_block += struct.pack('<12f', *ai.mat_inverse(world))
            for k, prims in zs:
                zone_block += struct.pack('<HH', k, len(prims)) + b''.join(struct.pack('<I3f3f', t, *x, *y) for t, x, y in prims)
        entries.append(struct.pack('<5H6B2BHf12f', pid, prso_index.get(loc, 0xFFFF), families[fam],
                                   0xFFFF if sector is None else sector, init_state, hp, hp_max, active, dead, mech,
                                   anim, int(reinit), flags, 0, radius2, *world)
                       + zone_block + struct.pack('<I', len(body)) + body + b'\0' * (-len(body) % 4)
                       + struct.pack('<I', len(tables)) + tables + b'\0' * (-len(tables) % 4))
        listing.append(f'{pid:5d} {names[0]:24s} {names[2]:32s} family {families[fam]:3d} states {len(a.states):3d} '
                       f'prso {prso_index.get(loc, -1):4d} sector {sector} active {active} mech {mech} anim {anim} '
                       f'r2 {radius2:g}' + (' always' if flags & 4 else '') + (' first' if flags & 8 else '')
                       + (f' story removals kept {frozen}' if frozen else '')
                       + (f' unresolved {unresolved}' if unresolved else ''))
    # the main actor's own ZDD zones (spheres, boxes), for the cast's COND_13(Ed, k, Me): the machine in front of Ed
    main_zones = [(k, z[:8]) for k, z in ((k, ai.zdd_zone(d, player, k - 1)) for k in range(1, 9)) if z] if player else []
    zblock = struct.pack('<HH', len(main_zones), world_id) + b''.join(
        struct.pack('<HH', k, len(z)) + b''.join(struct.pack('<I3f3f', t, *x, *y) for t, x, y in z) for k, z in main_zones)
    # the persos the cast's scripts name that do not run (World, the camera, the excluded ones): one shared copy of their
    # dsgvars each, so what one script writes there the others read (cavedoc's HEG_Inventaire counts its jingle into
    # World's dsg26: without the copy the write was lost and the jingle replayed every few ticks)
    # where another perso is the player (the skier), the fix Ed stands for him: Counter34(fix Ed) is the main
    # character's hit points (FUNC_Counter34Etc_0049ad30, the global table): an alias of the main actor, not a ghost
    fix_ed = d.perso_in_fix[0][0] if d.perso_in_fix else None
    alias = donor_alias if d.se else idx_of.get(fix_ed, 0) if fix_ed and fix_ed != player else 0
    # ... whose dsgvars are still his own (Totalski's race: dsg148 / 149 / 150): retail writes his ghost too (core
    # tc_ai.h alias_dsg); the Special Edition's donor numbering keeps the old way
    running = {idx_of[loc] for loc, _, _ in cast} | {main} | ({alias} if d.se else set())
    named = set()
    for _, _, a in cast:
        for n in a.nodes():
            if n.type == 24 and n.ref in idx_of:
                named.add(idx_of[n.ref])
    ghosts = sorted(named - running)
    loc_of = {v: k for k, v in idx_of.items() if k in by_loc}
    gblock = struct.pack('<HH', len(ghosts), 0)
    for gid in ghosts:
        loc = loc_of[gid]
        dsg, _ = ai.perso_dsg_block(d, loc, idx_of)
        w = by_loc[loc].world
        gblock += struct.pack('<HH3fI', gid, 0, w[3], w[7], w[11], len(dsg)) + dsg
    head = struct.pack('<4H', main, len(fam_blocks), len(entries), alias) + zblock + gblock
    own = []  # CMEC: each entry's own sweep body (its collide set: a single sphere, +0x36 / +0x30) and +0x34 / +0x35
    for loc, _, _ in cast:
        cs = lv.ptr(loc, 0x14)
        sg = lv.ptr(loc, 4)
        ftype = d.un('I', sg)[0] & 0xFFFF if sg and not d.se else 0xFFFF  # its family's index (T22 nodes hold it)
        if cs:
            diameter, = d.un('f', cs, 0x30)
            prio, meets, single = d.un('3B', cs, 0x34)
            own.append((diameter * 0.5 if single and diameter > 0 else 0.0, prio, 1 if meets else 0, ftype))
        else:
            own.append((0.0, 0, 0, ftype))
    return (head + b''.join(fam_blocks) + b''.join(entries), listing,
            mech_chunk(d, cards, fam_cards, own, fam_walk, fam_zde))


def walk_block(d, perso, states, keys):
    """A family's walker data (CMEC's last part; the card types 0 / 4 of core tc_cast_mec.c), from its first cast perso,
    as Ed's .ttai BODY + ZDMF (ai.py): u32 n_rows, n_rows x f32[3] (root motion: each A3D frame's own vector, units per
    animation frame in its axes), n_states x { u32 first row, u16 n_rows, u16 body (0xFFFF none) }, u16 n_bodies, u16 0,
    n_bodies x { u16 n, u16 0, n x { f32 centre[3], f32 radius, u16 collide flags, u16 element type (7 sphere, 5
    point), f32 rebound, f32 grip } } (ai.zdm_sets: its collide set's ZDM zones active per state; a collide set with
    +0x36: the one sphere at its origin). The Special Edition's: none (its collide sets are not read)."""
    n = len(states)
    if d.se:
        return struct.pack('<I', 0) + struct.pack('<IHH', 0, 0, 0xFFFF) * n + struct.pack('<HH', 0, 0)
    roots = ai.root_motion(d, states, keys)
    sets, per_state = ai.zdm_sets(d, perso, n)
    rows, index = [], []
    for k in keys:
        r = roots.get(k, [])
        index.append((len(rows), len(r)))
        rows += r
    out = struct.pack('<I', len(rows)) + b''.join(struct.pack('<3f', *r) for r in rows)
    out += b''.join(struct.pack('<IHH', first, cnt, b) for (first, cnt), b in zip(index, per_state))
    out += struct.pack('<HH', len(sets), 0)
    for zs in sets:
        out += struct.pack('<HH', len(zs), 0) + b''.join(struct.pack('<4f2H2f', *z[:4], z[4], z[5], z[6], z[7]) for z in zs)
    return out


def mech_chunk(d, cards, fam_cards, own=(), walk=(), zde=()):
    """CMEC (the cast's mechanics; core tc_cast_mec.c, docs/ENGINE.md "Moving collision, read"): u16 n_cards, u16 n_families
    (= CAST's), n_cards x { u32 type (0-12), u32 n_words, n_words x u32 (the card after its type word, raw: as the .ttai
    CARD chunk) }, n_families x { u16 n_states, n_states x u16 card (0xFFFF none) },
    u16 n_entries (= CAST's), u16 0, n_entries x { f32 radius (its collide set's single sphere at its origin, +0x36 with
    +0x30 x 0.5 (DAT_004d2928), as MEC_SweepSphere_0043d480 sweeps it; 0 none), u8 priority (+0x34), u8 meets persos
    (+0x35), u16 family index (std game +0: what T22 nodes hold; 0xFFFF none) },
    u16 n_families (= CAST's; 0: none), u16 0, n_families x walk_block (root motion and ZDM bodies),
    u16 n_families (0: none), u16 0, n_families x ai.zde_block (ZDE zones and their activation per state)."""
    out = struct.pack('<HH', len(cards), len(fam_cards))
    for c in cards:
        t = d.un('I', c)[0]
        n = ai.CARD_SIZES[t] // 4 - 1
        out += struct.pack('<II', t, n) + struct.pack(f'<{n}I', *d.un(f'{n}I', c, 4))
    for row in fam_cards:
        out += struct.pack(f'<H{len(row)}H', len(row), *row)
    out += struct.pack('<HH', len(own), 0) + b''.join(struct.pack('<f2BH', r, p, m, f) for r, p, m, f in own)
    out += struct.pack('<HH', len(walk), 0) + b''.join(walk)
    out += struct.pack('<HH', len(zde), 0) + b''.join(zde)
    return out

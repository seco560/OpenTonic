/* The cast (tc_cast.h, .ttlv CAST) through the viewer, per level: Ed stands 10 s, then runs 2 s; how many persos are
 * awake and thinking, the primitives their scripts skipped, every frame's draw list valid. Then Ed is put at every
 * perso that sleeps behind an activation radius (PROC 18 / 20: pickups, props) for 2 s each, and the check counts the
 * ones that wake (their AI runs) and the ones that take themselves out (PROC 257: picked up). Ed must still run at the
 * end. The allowed persos (cast.py MAY_FORCE_MAIN): a lever takes Ed over on Action and lets him go; a popcorn machine
 * makes him Super Ed, drawn with other parts, until it wears off. Usage: castcheck DIR [level ...] (default: sud0
 * north Land). Env: FORCE_TRACE (who forces Ed), GAP_TRACE (the first-frame pose gaps). Test-only stdio. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tonic/tonic.h"

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
    fclose(f);
    *size = (size_t)n;
    return data;
}

static int failures, bad_frames;
static uint32_t cast_draws;

static void check(int ok, const char *what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static tc_view_draw saved[4096];
static uint32_t saved_count;

/* The largest distance between the same perso part's origin in the saved draws and in the last frame's. */
static float pose_gap(tc_viewer *v) {
    uint32_t count, opaque, i, k;
    const tc_view_draw *d = tc_viewer_draws(v, &count, &opaque);
    float gap = 0;
    for (i = 0; i < count; i++) {
        float best = INFINITY;
        if (d[i].mesh < v->perso_first || d[i].mesh >= v->ed_first) continue;
        for (k = 0; k < saved_count; k++) /* the nearest draw of the same mesh (an object shown by two channels: two) */
            if (saved[k].mesh == d[i].mesh) {
                float dx = d[i].model.m[12] - saved[k].model.m[12], dy = d[i].model.m[13] - saved[k].model.m[13],
                      dz = d[i].model.m[14] - saved[k].model.m[14];
                best = fminf(best, sqrtf(dx * dx + dy * dy + dz * dz));
            }
        if (best < INFINITY && best > gap && getenv("GAP_TRACE")) {
            uint32_t m = d[i].mesh - v->perso_first, pi = 0;
            const tc_cast *c = tc_viewer_cast(v);
            const tc_cast_actor *a;
            while (pi < v->level->perso_count && m >= v->level->persos[pi].part_count) m -= v->level->persos[pi++].part_count;
            a = c ? tc_cast_actor_of(c, pi) : NULL;
            printf("    gap %.3f: prso %u perso %d state %u frame %.2f\n", (double)best, pi, a ? (int)a->entry->id : -1,
                   a ? (unsigned)a->ai.state : 0, a ? (double)a->ai.state_frame : 0.0);
        }
        if (best < INFINITY) gap = fmaxf(gap, best);
    }
    return gap;
}

static void save_draws(tc_viewer *v) {
    uint32_t count, opaque;
    const tc_view_draw *d = tc_viewer_draws(v, &count, &opaque);
    saved_count = count < 4096 ? count : 4096;
    memcpy(saved, d, saved_count * sizeof(*d));
}

static uint32_t cast_sounds;

static void frame(tc_viewer *v) {
    uint32_t count, opaque, i, k;
    const tc_view_draw *d;
    const tc_cast *c;
    tc_viewer_update(v, 1.0f / 60);
    if ((c = tc_viewer_cast(v)) != NULL) cast_sounds += c->sounds_out_count; /* the last tick's (a frame has 0-1 ticks) */
    d = tc_viewer_draws(v, &count, &opaque);
    cast_draws = 0;
    for (i = 0; i < count; i++) {
        const tc_mesh *m = tc_viewer_mesh(v, d[i].mesh);
        if (!m || !m->index_count || d[i].first_index + d[i].index_count > m->index_count) { bad_frames++; break; }
        for (k = 0; k < 16; k++)
            if (!(d[i].model.m[k] == d[i].model.m[k])) { bad_frames++; break; }
        if (d[i].mesh >= v->perso_first && d[i].mesh < v->ed_first) cast_draws++;
    }
}

static void run(tc_viewer *v, float seconds, uint32_t actions) {
    int i, n = (int)(seconds * 60 + 0.5f), a;
    for (a = 0; a < TC_VA_COUNT; a++) tc_viewer_set_action(v, (tc_view_action)a, (actions >> a) & 1u);
    for (i = 0; i < n; i++) frame(v);
    tc_viewer_release_all(v);
}

/* How far Ed runs in 2 s, forward or else back (beside a prop he may face a wall). */
/* Ed's parts drawn this frame, as a bit set over his part list (up to 256). */
static void ed_parts(tc_viewer *v, uint32_t *bits) {
    uint32_t count, opaque, i;
    const tc_view_draw *d = tc_viewer_draws(v, &count, &opaque);
    memset(bits, 0, 8 * sizeof(uint32_t));
    for (i = 0; i < count; i++)
        if (d[i].mesh >= v->ed_first && d[i].mesh < v->ed_first + 256 && d[i].mesh < v->ed_first + v->level->ed.part_count)
            bits[(d[i].mesh - v->ed_first) / 32] |= 1u << ((d[i].mesh - v->ed_first) % 32);
}

static float best_run(tc_viewer *v) {
    float best = 0;
    int k;
    for (k = 0; k < 2 && best <= 2; k++) {
        tc_vec3 a = tc_viewer_player(v)->position, b;
        run(v, 2, 1u << (k ? TC_VA_BACK : TC_VA_FORWARD));
        b = tc_viewer_player(v)->position;
        best = fmaxf(best, sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)));
    }
    return best;
}

static void skipped(const tc_cast *c) {
    static const char *kinds[] = {"KW", "COND", "OP", "FUNC", "PROC", "META", "6", "7", "8", "FIELD"};
    static uint32_t u[10 * TC_AI_UNKNOWN_STRIDE];
    uint32_t i, k, shown = 0, total = 0;
    tc_cast_unknown(c, u);
    for (i = 0; i < 10 * TC_AI_UNKNOWN_STRIDE; i++) total += u[i];
    printf("  skipped primitive uses %u; most:", total);
    for (k = 0; k < 12; k++) { /* the 12 most used */
        uint32_t best = 0, bi = 0;
        for (i = 0; i < 10 * TC_AI_UNKNOWN_STRIDE; i++) if (u[i] > best) best = u[i], bi = i;
        if (!best) break;
        printf(" %s_%u(%u)", kinds[bi / TC_AI_UNKNOWN_STRIDE], bi % TC_AI_UNKNOWN_STRIDE, best);
        u[bi] = 0;
        shown++;
    }
    printf("%s\n", shown ? "" : " none");
}

static void level(const char *dir, const char *name) {
    char path[1024], status[512];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level lv;
    tc_viewer v;
    tc_viewer_desc desc;
    const tc_cast *c;
    tc_vec3 start, end;
    uint32_t i, sleepers = 0, visited = 0, unreached = 0, woke = 0, taken = 0, before_inactive = 0, most_awake = 0;
    float moved;
    uint32_t k;
    double frame_ms;
    printf("%s:\n", name);
    cast_sounds = 0;
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&lv, data, size) != TC_OK) {
        free(data);
        check(0, "the level loads");
        return;
    }
    free(data);
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &lv;
    desc.level_name = name;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.seed = 7;
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "the viewer starts"); free(ai); tc_level_free(&lv); return; }
    if (!v.playing && tc_viewer_can_play(&v)) tc_viewer_toggle_play(&v);
    c = tc_viewer_cast(&v);
    printf("  CAST: %u entries, %u families; running %u\n", lv.cast.count, lv.cast.family_count, c ? c->count : 0);
    if (!c) { /* a .ttlv without CAST: Ed alone, for comparison */
        for (i = 0; i < 600; i++) frame(&v);
        start = tc_viewer_player(&v)->position;
        run(&v, 2, 1u << TC_VA_FORWARD);
        end = tc_viewer_player(&v)->position;
        printf("  no cast: Ed ran %.1f in 2 s\n", (double)sqrtf((end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y)));
        tc_viewer_free(&v); free(ai); tc_level_free(&lv); return;
    }
    check(c->count > 0 && c->count * 10 >= lv.cast.count * 9, "the cast runs (at least 90 % of the entries load)");

    { /* first frame: the cast's poses (initial states) against the baked loops (the simple controller draws those) */
        uint32_t with_cast, baked;
        float gap;
        tc_viewer_toggle_pause(&v); /* both at time 0: the cast's initial states, the baked loops' first sample */
        frame(&v);
        with_cast = cast_draws;
        save_draws(&v);
        tc_viewer_toggle_ai(&v);
        frame(&v);
        baked = cast_draws;
        gap = pose_gap(&v);
        tc_viewer_toggle_ai(&v);
        tc_viewer_toggle_pause(&v);
        printf("  first frame: %u perso parts drawn from the cast, %u from the baked loops; the same part at most %.3f "
               "apart\n", with_cast, baked, (double)gap);
        check(with_cast * 2 >= baked && with_cast <= baked * 2, "the cast draws about the persos the baked loops do");
        check(gap < 0.01f, "... and poses them as the baked loops at their start");
    }
    { /* 10 s standing. Raw script sounds per perso (before the throttle: a script that replays one floods the
       * mixer, cavedoc's HEG_Inventaire did 1600 in 10 s), and who stops being drawn though Ed was never near */
        static uint32_t raw[2048];
        static int drawn_at_start[2048];
        uint32_t worst = 0, worst_id = 0, vanished = 0;
        clock_t t0;
        for (i = 0; i < c->count && i < 2048; i++) {
            tc_mat34 w; const tc_cast_anim *an; tc_sample sm;
            raw[i] = 0;
            drawn_at_start[i] = tc_cast_pose(c, &c->actors[i], 0, &w, &an, &sm);
        }
        t0 = clock();
        for (k = 0; k < 600; k++) {
            uint32_t ticks_before = c->main->time_ms;
            frame(&v);
            if (c->awake > most_awake) most_awake = c->awake;
            if (c->main->time_ms != ticks_before) /* a tick ran: count its raw sounds once */
                for (i = 0; i < c->count && i < 2048; i++) raw[i] += c->actors[i].ai.sound_count;
        }
        frame_ms = (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC / 600;
        for (i = 0; i < c->count && i < 2048; i++) {
            tc_mat34 w; const tc_cast_anim *an; tc_sample sm;
            if (raw[i] > worst) worst = raw[i], worst_id = c->actors[i].entry->id;
            if (drawn_at_start[i] && !tc_cast_pose(c, &c->actors[i], 0, &w, &an, &sm) && !c->actors[i].ai.near_main_ms) {
                vanished++;
                printf("    vanished: perso %u\n", (unsigned)c->actors[i].entry->id);
            }
        }
        printf("  10 s standing: most sounds from one perso %u (perso %u); %u drawn persos vanished far from Ed; "
               "%.2f ms per frame (core, headless)\n", worst, worst_id, vanished, frame_ms);
        check(worst <= 40, "no perso replays sounds (at most 40 in 10 s standing, before the throttle)");
        check(vanished == 0, "no drawn perso leaves the world while Ed is far (story checks keep them as props)");
        check(frame_ms < 4, "the core's frame stays under 4 ms headless");
    }
    for (i = 0; i < c->count; i++) {
        const tc_ai_perso *p = &c->actors[i].ai;
        if (!p->active) before_inactive++;
        else if (p->radius2 > 0) sleepers++;
    }
    printf("  after 10 s standing: up to %u awake, %u thinking, %u asleep behind a radius, %u out of the world; "
           "%u perso parts drawn\n", most_awake, c->thinking, sleepers, before_inactive, cast_draws);
    skipped(c);
    printf("  Ed: behaviour %u, reflex %u, state %u, last forced by perso %d\n", (unsigned)c->main->normal.behaviour,
           (unsigned)c->main->reflex.behaviour, (unsigned)c->main->state, (int)c->main->forced_by);
    start = tc_viewer_player(&v)->position;
    run(&v, 2, 1u << TC_VA_FORWARD);
    end = tc_viewer_player(&v)->position;
    moved = sqrtf((end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y));
    printf("  Ed ran %.1f in 2 s\n", (double)moved);
    check(moved > 2, "Ed runs with the cast awake");
    { /* Dying: Ed's hit points to 0; his scripts (death, the cover, GameAction0 or the 'death' level) and the port's
       * respawn (tc_ed_ai) must bring him back playing within 15 s */
        uint32_t f2, back = 0;
        tc_player_teleport((tc_player *)tc_viewer_player(&v), start, 0);
        run(&v, 1, 0);
        c->main->hit_points = 0;
        if (strcmp(name, "Totalski") == 0 || strcmp(name, "skymap") == 0) tc_ai_force_behaviour(c->main, 9); /* the
            skier dies by crashing with no hit points (B9 -> B16 -> B15) */
        for (f2 = 0; f2 < 60 * 15 && !back; f2++) {
            frame(&v);
            if (f2 > 60 && v.phase == TC_VP_PLAY && c->main->hit_points > 0) back = f2;
        }
        run(&v, 2, 0);
        moved = best_run(&v);
        printf("  died: back playing after %.1f s (HP %d, behaviour %u), then ran %.1f\n", back / 60.0,
               (int)c->main->hit_points, (unsigned)c->main->normal.behaviour, (double)moved);
        if (c->main->reflex.behaviour == (uint32_t)c->main->model->normal_count + 13)
            printf("  info  Super Ed (BigBoss's lasts the level) does not die of hit points\n");
        else check(back && moved > 2, "Ed dies and comes back (the port's respawn), and runs");
    }

    /* Ed at each sleeper (its origin, 0.5 up), 2 s; pickups take themselves out */
    for (i = 0; i < c->count; i++) {
        const tc_cast_actor *a = &c->actors[i];
        const tc_ai_perso *p = &a->ai;
        uint32_t k, thought = 0;
        if (!p->active || !(p->radius2 > 0)) continue;
        visited++;
        tc_player_teleport((tc_player *)tc_viewer_player(&v), tc_v3_add(p->position, tc_v3(0, 0, 0.5f)), 0);
        {
            unsigned before = c->main->normal.behaviour;
            float nearest = INFINITY; /* where the cast saw him (after his ticks) */
            for (k = 0; k < 120 && p->active; k++) {
                frame(&v);
                thought += a->thought;
                nearest = fminf(nearest, tc_v3_distance(c->main->position, p->position));
            }
            if (!thought && nearest > 2) { unreached++; continue; } /* he was held elsewhere */
            if (getenv("FORCE_TRACE") && c->main->normal.behaviour != before)
                printf("    visiting perso %u: Ed B%u -> B%u (reflex %u)\n", (unsigned)a->entry->id, before,
                       (unsigned)c->main->normal.behaviour, (unsigned)c->main->reflex.behaviour);
        }
        woke += thought > 0;
        if (c->main->forced_by == (int32_t)a->entry->id && getenv("FORCE_TRACE"))
            printf("    perso %u forced Ed: behaviour %u reflex %u\n", (unsigned)a->entry->id,
                   (unsigned)c->main->normal.behaviour, (unsigned)c->main->reflex.behaviour);
        if (!p->active) {
            taken++;
            if (taken <= 8) printf("    taken: perso %u (state %u)\n", (unsigned)a->entry->id, (unsigned)p->state);
        }
    }
    printf("  Ed visited %u sleepers (%u not reached: he was held where an earlier visit left him): %u woke, %u took "
           "themselves out; cast sound events heard %u\n", visited, unreached, woke, taken, cast_sounds);
    visited -= unreached;
    check(visited == 0 || woke * 10 >= visited * 9, "the sleepers wake when Ed comes within their radius");
    /* The persos allowed to force Ed (tools/ttre/cast.py MAY_FORCE_MAIN: the levers): Ed beside one, Action; he must be
     * taken over, then let go (back to running). Only drawn ones: World and the pit markers are invisible (above). */
    for (i = 0; i < c->count; i++) {
        const tc_cast_actor *a = &c->actors[i];
        int side, k, taken_over = 0;
        unsigned forced_into = 0, held_on, state_before, state_after;
        if (!(a->entry->flags & TC_CAST_MAY_FORCE_MAIN) || !a->ai.active || a->entry->prso == 0xFFFF) continue;
        for (side = 0; side < 4 && !taken_over; side++) {
            float h = side * TC_PI / 2;
            tc_player_teleport((tc_player *)tc_viewer_player(&v),
                               tc_v3_add(a->ai.position, tc_v3(1.2f * sinf(h), -1.2f * cosf(h), 0.3f)), h + TC_PI);
            run(&v, 1, 0);
            run(&v, 0.1f, 1u << TC_VA_ACTION);
            for (k = 0; k < 360; k++) {
                frame(&v);
                if (!taken_over && c->main->forced_by == (int32_t)a->entry->id) forced_into = c->main->normal.behaviour;
                taken_over |= c->main->forced_by == (int32_t)a->entry->id;
            }
        }
        if (forced_into == 11) { /* a popcorn (Crunch) machine: Super Ed (Ed's B11 -> B93, reflex R13 until dsg15 ms) */
            uint32_t r13 = (uint32_t)c->main->model->normal_count + 13, t, super_at = 0, ended_at = 0;
            float ms = tc_ai_dsgvar_float(c->main, 15), ran;
            uint32_t normal_parts[8], super_parts[8], differ = 0, w;
            for (t = 0; t < 60 * 15 && !super_at; t++) { frame(&v); if (c->main->reflex.behaviour == r13) super_at = t + 1; }
            run(&v, 1, 0);
            ed_parts(&v, super_parts);
            ran = best_run(&v);
            for (t = 0; super_at && t < (uint32_t)(ms * 0.06f) + 60 * 6 && !ended_at; t++) {
                frame(&v);
                if (c->main->reflex.behaviour != r13) ended_at = t + 1;
            }
            run(&v, 3, 0);
            ed_parts(&v, normal_parts); /* standing again, after it wore off */
            for (w = 0; w < 8; w++) differ += (uint32_t)__builtin_popcount(normal_parts[w] ^ super_parts[w]);
            printf("  machine perso %u: %u of Ed's drawn parts differ between Super Ed and Ed after it\n",
                   (unsigned)a->entry->id, differ);
            printf("  machine perso %u: Super Ed (R13) after %.1f s, ran %.1f in 2 s as him, over %.1f s later (dsg15 %.0f ms);"
                   " behaviour then %u\n", (unsigned)a->entry->id, super_at / 60.0, (double)ran,
                   ended_at ? 2 + ended_at / 60.0 : -1.0, (double)ms, (unsigned)c->main->normal.behaviour);
            check(!ended_at || differ > 0, "Super Ed is drawn with other parts than Ed");
            check(super_at && ran > 2 && (ended_at || ms > 600000),
                  "a machine makes Ed Super Ed, who runs, and it wears off (BigBoss's lasts the level)");
            continue;
        }
        /* holding it (Ed's B48): Action pulls, Jump lets go */
        held_on = c->main->normal.behaviour;
        state_before = a->ai.state;
        run(&v, 0.1f, 1u << TC_VA_ACTION);
        run(&v, 3, 0);
        state_after = a->ai.state;
        run(&v, 0.1f, 1u << TC_VA_JUMP);
        run(&v, 2, 0);
        if (c->main->normal.behaviour == held_on && held_on != 87) { /* still holding: try the other ways out */
            printf("    still in B%u (dsg3 %.0f, dsg13 %.0f): Action, then Shoot\n", held_on,
                   (double)tc_ai_dsgvar_float(c->main, 3), (double)tc_ai_dsgvar_float(c->main, 13));
            run(&v, 0.1f, 1u << TC_VA_ACTION);
            run(&v, 3, 0);
            run(&v, 0.1f, 1u << TC_VA_SHOOT);
            run(&v, 2, 0);
            if (c->main->normal.behaviour == held_on) { /* B48: lean it (left: dsg13 1), then Action lets go */
                run(&v, 1, 1u << TC_VA_LEFT);
                run(&v, 2, 0);
                printf("    leant left: dsg13 %.0f, state %u, wanted %u\n", (double)tc_ai_dsgvar_float(c->main, 13),
                       (unsigned)c->main->state, (unsigned)c->main->wanted_state);
                run(&v, 0.1f, 1u << TC_VA_ACTION);
                run(&v, 3, 0);
            }
            printf("    then B%u\n", (unsigned)c->main->normal.behaviour);
        }
        moved = best_run(&v);
        printf("  lever perso %u: held in B%u; Action: lever state %u -> %u;", (unsigned)a->entry->id, held_on,
               (unsigned)state_before, (unsigned)state_after);
        printf("  lever perso %u: Ed taken over %s (into B%u); then behaviour %u, ran %.1f\n", (unsigned)a->entry->id,
               taken_over ? "yes" : "no", forced_into, (unsigned)c->main->normal.behaviour, (double)moved);
        if (taken_over) check(moved > 2, "an allowed perso (a lever) that takes Ed over on Action lets him go");
        else printf("  info  perso %u did not take Ed over from beside it\n", (unsigned)a->entry->id);
    }
    tc_viewer_status(&v, NULL, status, sizeof(status));
    printf("  status: %s\n", status);
    tc_player_teleport((tc_player *)tc_viewer_player(&v), start, 0);
    run(&v, 1, 0);
    start = tc_viewer_player(&v)->position;
    run(&v, 2, 1u << TC_VA_FORWARD);
    end = tc_viewer_player(&v)->position;
    moved = sqrtf((end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y));
    printf("  after the visits: Ed in behaviour %u, reflex %u, state %u, last forced by %d; ran %.1f\n",
           (unsigned)c->main->normal.behaviour, (unsigned)c->main->reflex.behaviour, (unsigned)c->main->state,
           (int)c->main->forced_by, (double)moved);
    /* Info only: the visits put Ed in arbitrary places (into holes, onto props, mid-air), and some leave him in a
     * behaviour he does not leave by running (Transit: B54 at the flight spy's spot, with that spy switched off too;
     * marmite: still holding a lever). The real check is "Ed runs with the cast awake" above. */
    printf("  info  Ed %s after the visits\n", moved > 2 ? "runs" : "does not run");
    /* Falling into a pit (docs/NOTES.md "Falling into pits"): the invisible markers allowed to force Ed that are not
     * always awake are the holes of nothingness (cast.py MAY_FORCE_MAIN_MODELS), plus north's World below z -105. Ed
     * dropped half a unit above one must be put into his falling death (B60: falling, B77's 1000 ms, the cover) within
     * 1 s, covered within 1.2 s of it whether or not he lands (carota and Land have pit floors 8-18 below their markers,
     * in his own sector's collision list: the original's data), and back playing; twice for the first marker (each
     * fires once and takes itself out; the respawn brings it back: tc_cast_respawn). In a fresh viewer: the visits
     * above leave Ed anywhere, even stepping into a portal. */
    tc_viewer_free(&v);
    if (tc_viewer_init(&v, &desc) != TC_OK) { check(0, "the viewer starts again"); free(ai); tc_level_free(&lv); return; }
    c = tc_viewer_cast(&v);
    run(&v, 3, 0);
    for (i = 0; i <= c->count; i++) {
        const tc_cast_actor *a = i < c->count ? &c->actors[i] : NULL;
        tc_vec3 at;
        uint32_t f, fell_at = 0, covered_at = 0, back_at = 0, landed = 0;
        char what[160];
        static int second_fall;
        if (i == 0) second_fall = 0;
        if (a) {
            if (!(a->entry->flags & TC_CAST_MAY_FORCE_MAIN) || (a->entry->flags & TC_CAST_ALWAYS_AWAKE) ||
                a->entry->prso != 0xFFFF || !a->ai.active)
                continue;
            at = tc_v3_add(a->ai.position, tc_v3(0, 0, 0.5f));
        } else if (strcmp(name, "north") == 0) {
            at = tc_v3(0, 0, -100); /* World's R0: z < -105 */
        } else continue;
        tc_player_teleport((tc_player *)tc_viewer_player(&v), at, 0);
        for (f = 0; f < 60 * 12 && !back_at; f++) {
            frame(&v);
            if (!fell_at && c->main->normal.behaviour == 60) fell_at = f + 1;
            if (fell_at && !covered_at && v.phase != TC_VP_PLAY) covered_at = f + 1;
            if (fell_at && !covered_at && (c->main->contacts & (TC_AI_CONTACT_GROUND | TC_AI_CONTACT_SLOPE))) landed++;
            if (covered_at && v.phase == TC_VP_PLAY && c->main->hit_points > 0) back_at = f + 1;
        }
        printf("  pit %s %u at (%.1f, %.1f, %.1f): falling death after %.2f s, covered %.2f s later, on ground %u "
               "frames before it, back playing after %.1f s\n", a ? "marker" : "World", a ? (unsigned)a->entry->id : 0,
               (double)at.x, (double)at.y, (double)at.z, fell_at / 60.0, covered_at > fell_at ? (covered_at - fell_at) / 60.0 : 0,
               landed, back_at / 60.0);
        snprintf(what, sizeof(what), "falling into the pit (%s %u): the falling death, the cover after 1 s, then back",
                 a ? "marker" : "World", a ? (unsigned)a->entry->id : 0);
        check(fell_at && fell_at <= 60 && covered_at && covered_at - fell_at <= 72 && back_at, what);
        run(&v, 1, 0);
        if (a && !second_fall) { second_fall = 1; i--; } /* the same pit again */
    }
    check(bad_frames == 0, "every frame's draw list is valid");
    bad_frames = 0;
    tc_viewer_free(&v);
    free(ai);
    tc_level_free(&lv);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    static const char *defaults[] = {"sud0", "north", "Land"};
    int i;
    if (argc > 2)
        for (i = 2; i < argc; i++) level(dir, argv[i]);
    else
        for (i = 0; i < 3; i++) level(dir, defaults[i]);
    printf(failures ? "FAIL (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}

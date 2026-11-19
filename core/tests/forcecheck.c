/* The cast's forces on the player (docs/ARCHITECTURE.md stage 5): every cast perso whose scripts force the main actor
 * (ForceBehaviour / ForceReflex, PROC 237 / 238, on MainActor or his perso) may, as in the exe (tools/ttre/cast.py:
 * until 2026-11-19 only allow-listed ones might, and the rest were kept off him silently). This is the report that
 * replaced the allow-lists: per perso, in a fresh level, Ed is set in front of it, presses Action and waits; if it takes
 * him (he is forced by it), the player gets past what holds him as he would (Jump for a line, a picture, the
 * director's shot; Action, then Jump), and then Ed must run. One that keeps him is a gap in the port (a behaviour of
 * his, or of the scene, not finished): a FAIL line, never a silent block. Those it never takes from in front of it
 * (most: they wait for a story step, a zone, a timer) are counted, not judged. The portals and level changers
 * (TC_CAST2_MOVES_MAIN) are portalcheck's. A perso on cast.py KEPT_OFF_MAIN is listed with it.
 *
 * Usage: forcecheck DIR [level ...] (default: every level of DIR with a cast but the hint scenes; the gate runs a few).
 * Env: FORCE_VERBOSE (every forcer's line, not only the ones taking Ed), FORCE_ONLY=id (that perso, Ed's changes traced).
 * Exit 1 on a FAIL. Test-only stdio. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "tonic/tonic.h"

static int failures;

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

static const char *left_for;           /* a level change asked for since the perso took Ed (its scene's way out) */
static int trace_id = -1;              /* env FORCE_ONLY=id: that perso alone, Ed's every change printed */
static unsigned trace_last = 0xFFFFFFFF;
static uint32_t trace_frames;

/* A frame, the player taking each line away (Jump), as castcheck's. */
static void frame(tc_viewer *v) {
    static int tapped;
    const tc_cast *c = tc_viewer_cast(v);
    trace_frames++;
    if (trace_id >= 0 && c) {
        unsigned key = c->main->normal.behaviour * 4096u + c->main->reflex.behaviour * 8u + (c->globals.main_character != 0) + (tc_viewer_speech(v) != 0) * 2u + (v->game->picture_left > 0) * 4u;
        if (key != trace_last) {
            tc_vec3 at = c->main->position;
            printf("    %6.2f Ed B%u reflex %u state %u forced by %d at (%.2f %.2f %.2f)%s%s%s\n", trace_frames / 60.0,
                   (unsigned)c->main->normal.behaviour, (unsigned)c->main->reflex.behaviour, (unsigned)c->main->state,
                   (int)c->main->forced_by, (double)at.x, (double)at.y, (double)at.z,
                   tc_viewer_speech(v) ? " line" : "", v->game->picture_left > 0 ? " picture" : "",
                   tc_viewer_directed(v) ? " directed" : "");
            trace_last = key;
        }
    }
    if (tc_viewer_speech(v) || tapped) {
        tapped = !tapped && tc_viewer_speech(v);
        tc_viewer_set_action(v, TC_VA_JUMP, tapped);
    }
    tc_viewer_update(v, 1.0f / 60);
    { /* drawn, as a frontend does: the camera's sight (what the scripts see on screen) reads the draw list. Without it
       * the spies that wait to be seen never start their line */
        uint32_t count, opaque;
        (void)tc_viewer_draws(v, &count, &opaque);
    }
    if (!left_for && tc_viewer_pending_level(v)) left_for = tc_viewer_pending_level(v)->level;
}

static void run(tc_viewer *v, float seconds, uint32_t actions) {
    int i, n = (int)(seconds * 60 + 0.5f), a;
    for (a = 0; a < TC_VA_COUNT; a++) tc_viewer_set_action(v, (tc_view_action)a, (actions >> a) & 1u);
    for (i = 0; i < n; i++) frame(v);
    tc_viewer_release_all(v);
}

/* A scripted picture or the director's shot: the player taps Jump past them (port choice), at most 20 times. */
static void past_holds(tc_viewer *v) {
    int taps;
    for (taps = 0; taps < 20 && (v->game->picture_left > 0 || tc_viewer_directed(v)); taps++) {
        run(v, 0.1f, 1u << TC_VA_JUMP);
        run(v, 0.1f, 0);
    }
}

/* How far Ed runs in 2 s, forward or else back or sideways (a wall may be on any side). */
static float best_run(tc_viewer *v) {
    static const tc_view_action ways[] = {TC_VA_FORWARD, TC_VA_BACK, TC_VA_LEFT, TC_VA_RIGHT};
    float best = 0;
    int k;
    for (k = 0; k < 4 && best <= 2; k++) {
        tc_vec3 a = tc_viewer_player(v)->position, b;
        run(v, 2, 1u << ways[k]);
        b = tc_viewer_player(v)->position;
        best = fmaxf(best, sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)));
        if (tc_viewer_driver(v) && tc_viewer_driver(v)->portal >= 0) break; /* a portal took him */
    }
    return best;
}

/* Whether its scripts force the main actor: PROC 237 / 238 whose first argument is MainActor (KW 5) or his perso. */
static bool forces_main(const tc_ai_model *m, const tc_cast *c) {
    uint32_t n;
    for (n = 0; n + 1 < m->node_count; n++) {
        const tc_ai_node *p = &m->nodes[n], *who = &m->nodes[n + 1];
        if (p->type != TC_AI_PROC || (p->param != 237 && p->param != 238) || who->depth != p->depth + 1) continue;
        if ((who->type == TC_AI_KEYWORD && who->param == 5) || (who->type == TC_AI_PERSO && tc_cast_is_main_id(c, (int32_t)who->param)))
            return true;
    }
    return false;
}

/* Ed playing: not held by a line, a picture or the director, nor by a level's opening scene (Land's entrance camera,
 * cock01's guard, pyramide's projector: B15 / B102 while it lasts, at most 30 s), and the player's (no chameleon). */
static void settle(tc_viewer *v) {
    const tc_cast *c = tc_viewer_cast(v);
    int k;
    for (k = 0; c && k < 60 && (c->main->normal.behaviour == 15 || c->main->normal.behaviour == 102); k++) {
        past_holds(v);
        run(v, 0.5f, 0);
    }
    for (k = 0; k < 3 && c && (c->globals.main_character != 0 || !c->main->active); k++) { /* a chameleon: Action back */
        run(v, 0.2f, 1u << TC_VA_ACTION);
        run(v, 5, 0);
    }
    past_holds(v);
}

typedef struct counts { unsigned forcers, kept_off, taken, let_go, held, untaken; } counts;

/* Scenes the player comes into another way than straight at the forcing perso: Ed is set in front of `by` instead,
 * and the forcer takes him from there (the test's approach, not a rule of the game's). */
static const struct approach { const char *level, *perso, *by, *why; } approaches[] = {
    {"sud0", "HEG_BossMush", "SDO_EspionMagicMush",
     "the boss's first hint holds Ed until its spy has talked, and the spy (at the arena's way in, 22 from the boss, "
     "talking within 5) is met on the way: set beside the boss, Ed never is"},
};

/* LEVEL_cast.txt beside the level (tools/ttre/cast.py's listing): perso id -> instance name; empty when not found. */
static char cast_names[4096][48];
static void read_names(const char *dir, const char *name) {
    char path[1024], line[1024];
    FILE *f;
    memset(cast_names, 0, sizeof(cast_names));
    snprintf(path, sizeof(path), "%s/%s_cast.txt", dir, name);
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof(line), f)) {
        unsigned id;
        char family[64], instance[64];
        if (sscanf(line, "%u %63s %63s", &id, family, instance) == 3 && id < 4096)
            snprintf(cast_names[id], sizeof(cast_names[id]), "%s", instance);
    }
    fclose(f);
}

/* The cast actor called `instance`, or NULL. */
static const tc_cast_actor *named(const tc_cast *c, const char *instance) {
    uint32_t i;
    for (i = 0; i < c->count; i++)
        if (c->actors[i].entry->id < 4096 && strcmp(cast_names[c->actors[i].entry->id], instance) == 0) return &c->actors[i];
    return NULL;
}

/* The chameleon that has the player (the main character not Ed), or NULL. */
static const tc_cast_actor *chameleon(const tc_cast *c) {
    uint32_t i;
    for (i = 0; c && c->globals.main_character != 0 && i < c->count; i++)
        if ((int32_t)c->actors[i].entry->id == c->globals.main_character) return &c->actors[i];
    return NULL;
}

/* How far the chameleon that has the player goes in 2 s of the stick forward (or back, or sideways). */
static float chameleon_run(tc_viewer *v, const tc_cast_actor *a) {
    static const tc_view_action ways[] = {TC_VA_FORWARD, TC_VA_BACK, TC_VA_LEFT, TC_VA_RIGHT};
    float best = 0;
    int k;
    for (k = 0; k < 4 && best <= 2; k++) {
        tc_vec3 p = a->ai->position, q;
        run(v, 2, 1u << ways[k]);
        q = a->ai->position;
        best = fmaxf(best, sqrtf((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y)));
    }
    return best;
}

static bool start_viewer(tc_viewer *v, const tc_viewer_desc *desc) {
    if (tc_viewer_init(v, desc) != TC_OK) return false;
    if (!v->playing && tc_viewer_can_play(v)) tc_viewer_toggle_play(v);
    return tc_viewer_cast(v) != NULL;
}

static void level(const char *dir, const char *name, counts *all) {
    char path[1024];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level lv;
    tc_viewer v;
    tc_viewer_desc desc;
    const tc_cast *c;
    uint32_t i, count;
    static uint32_t forcer[1024];
    counts n;
    float base;
    bool verbose = getenv("FORCE_VERBOSE") != NULL;
    memset(&n, 0, sizeof(n));
    read_names(dir, name);
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&lv, data, size) != TC_OK) {
        free(data);
        printf("%s: FAIL the level loads\n", name);
        failures++;
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
    desc.seed = getenv("FORCE_SEED") ? (uint32_t)atoi(getenv("FORCE_SEED")) : 7;
    if (!start_viewer(&v, &desc)) {
        printf("%s: no cast\n", name);
        tc_viewer_free(&v); free(ai); tc_level_free(&lv);
        return;
    }
    c = tc_viewer_cast(&v);
    count = 0;
    for (i = 0; i < c->count && count < 1024; i++) /* the forcers (the same models in every fresh viewer) */
        if (forces_main(&c->actors[i].model, c) && !(c->actors[i].entry->flags2 & TC_CAST2_MOVES_MAIN)) forcer[count++] = i;
    /* the level's own start: Ed must run there first, or nothing below says anything */
    run(&v, 3, 0);
    settle(&v);
    base = best_run(&v);
    tc_viewer_free(&v);
    printf("%s: %u persos force Ed; Ed at the start runs %.1f\n", name, count, (double)base);
    if (base <= 2) {
        printf("  info  Ed does not run at the start: not judged\n");
        free(ai); tc_level_free(&lv);
        return;
    }
    for (i = 0; i < count; i++) {
        const tc_cast_actor *a, *by, *cham;
        int side, k;
        int32_t id;
        bool taken = false;
        unsigned into = 0, reflex = 0, state;
        float moved;
        tc_vec3 home;
        if (!start_viewer(&v, &desc)) break;
        c = tc_viewer_cast(&v);
        a = &c->actors[forcer[i]];
        id = (int32_t)a->entry->id;
        if (trace_id >= 0 && id != trace_id) { tc_viewer_free(&v); continue; }
        trace_frames = 0;
        n.forcers++;
        if (!(a->entry->flags & TC_CAST_MAY_FORCE_MAIN)) {
            n.kept_off++;
            printf("  kept off  perso %d (cast.py KEPT_OFF_MAIN)\n", id);
            tc_viewer_free(&v);
            continue;
        }
        run(&v, 1, 0);
        for (k = 0; k < 60 && (c->main->normal.behaviour == 15 || c->main->normal.behaviour == 102); k++) run(&v, 0.5f, 0);
        settle(&v);
        home = tc_viewer_player(&v)->position;
        by = a;
        for (k = 0; k < (int)(sizeof(approaches) / sizeof(approaches[0])); k++)
            if (strcmp(approaches[k].level, name) == 0 && id < 4096 && strcmp(approaches[k].perso, cast_names[id]) == 0 &&
                named(c, approaches[k].by)) {
                by = named(c, approaches[k].by);
                if (verbose) printf("  approach  perso %d by %s: %s\n", id, approaches[k].by, approaches[k].why);
            }
        for (side = 0; side < 4 && !taken; side++) { /* in front of it, from each side in turn */
            float h = side * TC_PI / 2;
            tc_vec3 at = by->ai->position;
            if (!a->ai->active || !by->ai->active) break; /* not in the world (yet): it cannot take him from here */
            tc_player_teleport((tc_player *)tc_viewer_player(&v), tc_v3_add(at, tc_v3(1.2f * sinf(h), -1.2f * cosf(h), 0.3f)),
                               h + TC_PI);
            run(&v, 1, 0);
            run(&v, 0.1f, 1u << TC_VA_ACTION);
            for (k = 0; k < 360 && !taken; k++) {
                frame(&v);
                if (c->main->forced_by == id) {
                    taken = true;
                    into = c->main->normal.behaviour;
                    reflex = c->main->reflex.behaviour;
                }
            }
        }
        if (!taken) {
            n.untaken++;
            if (verbose) printf("  untaken   perso %d\n", id);
            tc_viewer_free(&v);
            continue;
        }
        n.taken++;
        left_for = NULL;
        /* what the player does to get on: lines and pictures (Jump), the prompts (Action, then Jump), up to 20 s */
        for (k = 0; k < 4; k++) {
            run(&v, 3, 0);
            settle(&v);
            run(&v, 0.1f, 1u << (k & 1 ? TC_VA_JUMP : TC_VA_ACTION));
            run(&v, 2, 0);
        }
        settle(&v);
        state = c->main->state;
        if ((cham = chameleon(c)) != NULL) { /* a chameleon has the player (docs/ENGINE.md "The chameleons"): it must go
                                              * where he steers it (it gives him back at its own goal: marmite's Grogh) */
            moved = chameleon_run(&v, cham);
            if (verbose || moved <= 2)
                printf("  %s perso %d gave the player to the chameleon %d, which went %.1f in 2 s\n",
                       moved > 2 ? "ok       " : "FAIL     ", id, (int)cham->entry->id, (double)moved);
            if (moved > 2) n.let_go++;
            else n.held++, failures++;
            tc_viewer_free(&v);
            continue;
        }
        moved = best_run(&v);
        if (moved <= 2) { /* beside it there may be no room to run (a ledge, a niche): from the level's start (a held Ed
                           * runs nowhere; a talking perso he walked off from may take him again on Action, not here) */
            tc_player_teleport((tc_player *)tc_viewer_player(&v), home, 0);
            run(&v, 1, 0);
            moved = best_run(&v);
        }
        if (left_for) { /* it took him to another level (an arch's capture, a scene's ChangeLevel): its way out */
            n.let_go++;
            if (verbose) printf("  ok        perso %d took Ed (B%u, reflex %u) to the level %s\n", id, into, reflex, left_for);
        } else if (moved > 2 || (tc_viewer_driver(&v) && tc_viewer_driver(&v)->portal >= 0)) {
            n.let_go++;
            if (verbose) printf("  ok        perso %d took Ed (B%u, reflex %u), let him go: he ran %.1f\n", id, into, reflex,
                                (double)moved);
        } else {
            n.held++;
            failures++;
            printf("  FAIL      perso %d took Ed (B%u, reflex %u) and keeps him: B%u reflex %u state %u, forced by %d, "
                   "ran %.1f\n", id, into, reflex, (unsigned)c->main->normal.behaviour, (unsigned)c->main->reflex.behaviour,
                   state, (int)c->main->forced_by, (double)moved);
        }
        tc_viewer_free(&v);
    }
    printf("  %u persos force Ed: %u kept off, %u took him from in front of them (%u let him go, %u keep him), %u did not\n",
           n.forcers, n.kept_off, n.taken, n.let_go, n.held, n.untaken);
    all->forcers += n.forcers, all->kept_off += n.kept_off, all->taken += n.taken;
    all->let_go += n.let_go, all->held += n.held, all->untaken += n.untaken;
    free(ai);
    tc_level_free(&lv);
}

static int by_name(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    counts all;
    int i;
    memset(&all, 0, sizeof(all));
    if (getenv("FORCE_ONLY")) trace_id = atoi(getenv("FORCE_ONLY"));
    if (argc > 2) {
        for (i = 2; i < argc; i++) level(dir, argv[i], &all);
    } else { /* every .ttlv of DIR (the imported ones have no cast: "no cast") */
        DIR *d = opendir(dir);
        struct dirent *e;
        char *names[512];
        int count = 0;
        while (d && (e = readdir(d)) != NULL && count < 512) {
            size_t len = strlen(e->d_name);
            if (len > 5 && strcmp(e->d_name + len - 5, ".ttlv") == 0 && e->d_name[0] != '.') {
                names[count] = strdup(e->d_name);
                names[count][len - 5] = 0;
                count++;
            }
        }
        if (d) closedir(d);
        qsort(names, (size_t)count, sizeof(*names), by_name);
        for (i = 0; i < count; i++) { level(dir, names[i], &all); free(names[i]); }
    }
    printf("all: %u persos force Ed: %u kept off, %u took him (%u let him go, %u keep him), %u did not from in front\n",
           all.forcers, all.kept_off, all.taken, all.let_go, all.held, all.untaken);
    printf(failures ? "FAIL (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}

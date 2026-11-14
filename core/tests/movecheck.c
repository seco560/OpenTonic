/* What makes the persos move (docs/ENGINE.md "Perso motion"): per level, Ed is put near each drawn perso with mechanics
 * for a few seconds, and the check measures what that perso does: how far it travels, how much it turns, how many states
 * it goes through. It fails on what must never happen (a position that is not a number, a perso leaving the world);
 * everything else is a measurement, written as rows for a diff between two builds:
 *     M <level> <id> <card types> <path> <net> <turn degrees> <state changes> <% awake> <% thinking> <behaviour> <state>
 *     U <level> <primitive> <uses>            the primitives the cast's scripts skipped
 * Two scenarios check what is known to work: sud0's boxer chases and strikes, pyramide's cat paw lobs onto Ed.
 * Usage: movecheck DIR [level ...] (default: every playable level). Env: MOVE_SECONDS (default 5), MOVE_DISTANCE (how far
 * from the perso Ed stands, default 3: chasers need more room to show). Test-only stdio. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static int failures;

static void check(int ok, const char *level, const char *what) {
    if (ok) return;
    printf("  FAIL  %s: %s\n", level, what);
    failures++;
}

static bool finite3(tc_vec3 v) { return isfinite(v.x) && isfinite(v.y) && isfinite(v.z); }

static void card_types(const tc_level *lv, const tc_cast_actor *a, char *out, size_t size) {
    const tc_cast_family *f = &lv->cast.families[a->entry->family];
    uint32_t seen = 0, k;
    size_t n = 0;
    out[0] = 0;
    for (k = 0; f->cards && k < f->state_count; k++)
        if (f->cards[k] != 0xFFFF) seen |= 1u << (lv->cast.cards[f->cards[k]].type & 31);
    for (k = 0; k < 32 && n + 4 < size; k++)
        if (seen >> k & 1) n += (size_t)snprintf(out + n, size - n, "%s%u", n ? "," : "", (unsigned)k);
    if (!n) snprintf(out, size, "-");
}

static void level(const char *dir, const char *name, float seconds, float distance) {
    char path[512], cards[64];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_level lv;
    tc_viewer v;
    tc_viewer_desc desc;
    const tc_cast *c;
    uint32_t i, moved = 0, turned = 0, visited = 0, frames = (uint32_t)(seconds * 60 + 0.5f);
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&lv, data, size) != TC_OK) { free(data); return; }
    free(data);
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = &lv;
    desc.level_name = name;
    desc.ai_data = ai;
    desc.ai_size = ai_size;
    desc.seed = 7;
    if (tc_viewer_init(&v, &desc) != TC_OK) { free(ai); tc_level_free(&lv); return; }
    if (!v.playing && tc_viewer_can_play(&v)) tc_viewer_toggle_play(&v);
    c = tc_viewer_cast(&v);
    if (!c || !c->main) { tc_viewer_free(&v); free(ai); tc_level_free(&lv); return; }
    for (i = 0; i < c->count; i++) {
        const tc_cast_actor *a = &c->actors[i];
        const tc_ai_perso *p = a->ai;
        tc_vec3 start, last;
        float path_len = 0, turn = 0, heading;
        uint32_t k, states = 0, awake = 0, thought = 0;
        uint16_t state;
        if (!p->active || !p->run_mechanics || a->entry->prso == 0xFFFF) continue;
        c->main->hit_points = c->main->hit_points_max;
        tc_player_teleport((tc_player *)tc_viewer_player(&v), tc_v3_add(p->position, tc_v3(distance, 0, 0.5f)), 0);
        start = last = p->position;
        heading = p->heading;
        state = p->state;
        for (k = 0; k < frames && p->active; k++) {
            tc_viewer_update(&v, 1.0f / 60);
            path_len += tc_v3_distance(p->position, last);
            turn += fabsf(remainderf(p->heading - heading, 2 * TC_PI));
            states += p->state != state;
            awake += a->awake, thought += a->thought;
            last = p->position, heading = p->heading, state = p->state;
            if (!finite3(p->position)) break;
        }
        check(finite3(p->position), name, "a perso's position is a number");
        card_types(&lv, a, cards, sizeof(cards));
        printf("M\t%s\t%u\t%s\t%.2f\t%.2f\t%.0f\t%u\t%u\t%u\t%u\t%u\n", name, (unsigned)a->entry->id, cards, (double)path_len,
               (double)tc_v3_distance(p->position, start), (double)(turn * 180 / TC_PI), (unsigned)states,
               k ? awake * 100 / k : 0, k ? thought * 100 / k : 0, (unsigned)p->normal.behaviour, (unsigned)p->state);
        visited++;
        moved += path_len > 0.05f;
        turned += turn > 0.02f;
    }
    {
        static uint32_t unknown[10 * TC_AI_UNKNOWN_STRIDE];
        static const char *kinds[] = {"KW", "COND", "OP", "FUNC", "PROC", "META", "?", "?", "?", "FIELD"};
        memset(unknown, 0, sizeof(unknown));
        tc_cast_unknown(c, unknown);
        for (i = 0; i < 10 * TC_AI_UNKNOWN_STRIDE; i++)
            if (unknown[i]) printf("U\t%s\t%s_%u\t%u\n", name, kinds[i / TC_AI_UNKNOWN_STRIDE],
                                   (unsigned)(i % TC_AI_UNKNOWN_STRIDE), (unsigned)unknown[i]);
    }
    printf("S\t%s\tvisited %u, moved %u, turned %u\n", name, (unsigned)visited, (unsigned)moved, (unsigned)turned);
    tc_viewer_free(&v);
    free(ai);
    tc_level_free(&lv);
}

/* A viewer on a level for the scenarios below; false when the level is not there. */
static bool open_level(const char *dir, const char *name, tc_level *lv, tc_viewer *v, uint8_t **ai) {
    char path[512];
    size_t size, ai_size = 0;
    uint8_t *data;
    tc_viewer_desc desc;
    snprintf(path, sizeof(path), "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(lv, data, size) != TC_OK) { free(data); return false; }
    free(data);
    snprintf(path, sizeof(path), "%s/%s.ttai", dir, name);
    *ai = read_file(path, &ai_size);
    memset(&desc, 0, sizeof(desc));
    desc.level = lv;
    desc.level_name = name;
    desc.ai_data = *ai;
    desc.ai_size = ai_size;
    desc.seed = 7;
    if (tc_viewer_init(v, &desc) != TC_OK) { free(*ai); tc_level_free(lv); return false; }
    if (!v->playing && tc_viewer_can_play(v)) tc_viewer_toggle_play(v);
    return true;
}

static const tc_cast_actor *actor(const tc_cast *c, uint32_t id) {
    uint32_t i;
    for (i = 0; c && i < c->count; i++)
        if (c->actors[i].entry->id == id) return &c->actors[i];
    return NULL;
}

/* sud0's boxing mushroom (perso 7): its B1 is Action_Pursuit(MainActor, 1), entered in state 14 (its run), and its scripts
 * set no speed: the run cycle's root motion does (GAM_0042a8d0). It must reach Ed, then strike (state 31). */
static void pursuit_scenario(const char *dir) {
    tc_level lv;
    tc_viewer v;
    uint8_t *ai;
    const tc_cast_actor *a;
    tc_ai_perso *p;
    float nearest = INFINITY, top = 0;
    int i, struck = 0;
    if (!open_level(dir, "sud0", &lv, &v, &ai)) return;
    if ((a = actor(tc_viewer_cast(&v), 7)) != NULL) {
        p = (tc_ai_perso *)a->ai;
        tc_player_teleport((tc_player *)tc_viewer_player(&v), tc_v3_add(p->position, tc_v3(4, 0, 0.5f)), 0);
        for (i = 0; i < 30; i++) tc_viewer_update(&v, 1.0f / 60);
        tc_ai_force_behaviour(p, 1);
        p->state = p->wanted_state = 14;
        p->state_frame = 0;
        for (i = 0; i < 120; i++) {
            tc_viewer_update(&v, 1.0f / 60);
            nearest = fminf(nearest, tc_v3_distance(p->position, tc_viewer_cast(&v)->main->position));
            top = fmaxf(top, tc_v3_length(a->dyn->velocity));
            struck |= p->state == 31;
        }
        printf("P\tsud0 boxer: nearest %.2f, top speed %.2f, struck %d\n", (double)nearest, (double)top, struck);
        check(nearest < 1.5f && top > 2, "sud0", "the boxer runs Ed down at its run cycle's speed");
        check(struck, "sud0", "the boxer strikes when it reaches Ed");
    }
    tc_viewer_free(&v);
    free(ai);
    tc_level_free(&lv);
}

/* pyramide's cat paw (perso 13): type 10 mode 0x13 lobs it at a point (ProposalVecA is the target, [0] the apex). It must
 * come down near Ed, not leave the level (read as a velocity it flew 776 units in 5 s). */
static void lob_scenario(const char *dir) {
    tc_level lv;
    tc_viewer v;
    uint8_t *ai;
    const tc_cast_actor *a;
    tc_ai_perso *p;
    tc_vec3 start;
    float nearest = INFINITY, furthest = 0;
    int i;
    if (!open_level(dir, "pyramide", &lv, &v, &ai)) return;
    if ((a = actor(tc_viewer_cast(&v), 13)) != NULL) {
        p = (tc_ai_perso *)a->ai;
        start = p->position;
        tc_player_teleport((tc_player *)tc_viewer_player(&v), tc_v3_add(p->position, tc_v3(3, 0, 0.5f)), 0);
        for (i = 0; i < 30; i++) tc_viewer_update(&v, 1.0f / 60);
        tc_ai_force_behaviour(p, 3);
        for (i = 0; i < 300; i++) {
            tc_viewer_update(&v, 1.0f / 60);
            nearest = fminf(nearest, tc_v3_distance(p->position, tc_viewer_cast(&v)->main->position));
            furthest = fmaxf(furthest, tc_v3_distance(p->position, start));
        }
        printf("P\tpyramide paw: nearest to Ed %.2f, furthest from its start %.2f\n", (double)nearest, (double)furthest);
        check(furthest < 60, "pyramide", "the paw stays around its target");
        /* Info only for now (user, 2026-11-14): the scripts that aim it at Ed are not ported yet; back to a check then */
        printf("  info  pyramide: the paw %s on Ed (nearest %.2f)\n", nearest < 4 ? "comes down" : "does not come down",
               (double)nearest);
    }
    tc_viewer_free(&v);
    free(ai);
    tc_level_free(&lv);
}

int main(int argc, char **argv) {
    static const char *defaults[] = {"sud0", "north", "Land", "cavedoc", "carota", "cock01", "marmite", "pyramide",
                                     "skymap", "LeCanyon", "BigBoss", "Totalski"};
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv", *s = getenv("MOVE_SECONDS"), *d = getenv("MOVE_DISTANCE");
    float seconds = s ? (float)atof(s) : 5, distance = d ? (float)atof(d) : 3;
    int i;
    if (argc > 2)
        for (i = 2; i < argc; i++) level(dir, argv[i], seconds, distance);
    else
        for (i = 0; i < 12; i++) level(dir, defaults[i], seconds, distance);
    pursuit_scenario(dir);
    lob_scenario(dir);
    printf(failures ? "FAIL (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}

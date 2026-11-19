/* The probe (docs/ARCHITECTURE.md stage 1, docs/OVERVIEW.md "The probe"): a level played headless under a short script of
 * commands, printing what the named persos, Ed and the game do. It replaces the scratch probes each session used to
 * write (place Ed, press a button, print a perso's behaviour, state and hit points). Test-only stdio.
 *
 *   probe [-d DIR] LEVEL COMMAND...                 DIR: the converted levels (default ../files/out/ttlv)
 *   probe [-d DIR] -r FILE.ttrec SECONDS COMMAND... a recording played to SECONDS of its session, then the commands
 *   probe [-d DIR] -f SCRIPT [COMMAND...]           the words from a file first ('#' to the line's end: a comment)
 *
 * A perso (P below) is an id, an instance name (PAT_Barman_Astuce01) or a family name (barman: its first perso), both
 * from LEVEL_cast.txt beside the level, any case; `ed` is Ed (his driver's AI). Time runs as a frontend's at 60 frames
 * a second (the game ticks at 30 inside), drawing each frame (the camera's sight reads the draw list). A level change
 * is followed, as the frontends do: the next level starts with the carry.
 *
 *   time       run S              S seconds                  frames N           N frames
 *              until P B S        until P's normal behaviour is B, at most S seconds
 *   Ed         place X Y Z        there (his heading kept)   heading R          radians, his front along (sin, -cos)
 *              near P [D]         D (1.5) in front of P, facing it              at P DX DY DZ      P's place + that
 *              face P             turned to P once           aim P | aim off    turned to P every frame
 *              portal K           into Ed's PORT portal K (half its radius in, facing in)
 *   input      hold A | release A held from now (A: jump action shoot lateral fast forward back left right)
 *              tap A              held 6 frames, run         pulse A S          6 frames every S seconds (0: off)
 *              stick X Y          the move stick (-1..1, y forward)             free               all of the above off
 *   print      watch W | unwatch W   W: P, ed or game: a line whenever its behaviours, state, hit points ... change
 *              every S            the watched lines every S seconds too (0: changes only)
 *              show W             one line now               info P             its static data (family, states, cards)
 *              list               every cast perso           skipped [P]        primitives the scripts skipped
 *              portals            Ed's PORT portals: where, and the level they lead to
 *   state      power MASK         the game's powers (0x... ok)                  value K V          save value K
 *              hp P N             hit points                 dsg P K            dsgvar K, as a float
 *              setdsg P K V       dsgvar K = V (float)
 * Exit 0, or 1 with the reason on a command it could not do. */
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tonic/tonic.h"

#define FRAME (1.0f / 60)
#define TAP_FRAMES 6
#define MAX_WORDS 4096
#define MAX_WATCH 16
#define MAX_NAMES 1024
#define ED (-1)
#define GAME (-2)

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t *)malloc(n > 0 ? (size_t)n + 1 : 1);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
    if (data) data[n > 0 ? n : 0] = 0;
    fclose(f);
    *size = (size_t)n;
    return data;
}

/* The level's file in folder, any case. */
static bool find_level(const char *folder, const char *level, const char *ext, char *path, size_t size) {
    DIR *d = opendir(folder);
    struct dirent *e;
    size_t n = strlen(level), m = strlen(ext);
    bool found = false;
    if (!d) return false;
    while (!found && (e = readdir(d))) {
        size_t i;
        if (strlen(e->d_name) != n + m) continue;
        for (i = 0; i < n && tolower((unsigned char)e->d_name[i]) == tolower((unsigned char)level[i]); i++) {}
        if (i == n && !strcmp(e->d_name + n, ext)) {
            snprintf(path, size, "%s/%s", folder, e->d_name);
            found = true;
        }
    }
    closedir(d);
    return found;
}

/* ---- The session: one level at a time ---- */

typedef struct name_row { int32_t id; char family[48], instance[64]; } name_row;

static struct {
    const char *dir;
    char level_name[64];
    tc_level level;
    uint8_t *ai;
    size_t ai_size;
    tc_viewer v;
    bool live;
    name_row names[MAX_NAMES];
    uint32_t name_count;
    uint32_t frame;               /* frames since the probe began (every level) */
    double time;                  /* seconds since then (a recording's own dts, then 1/60 a frame) */
    /* input */
    bool held[TC_VA_COUNT];
    float pulse[TC_VA_COUNT];     /* seconds between presses, 0 off */
    float stick_x, stick_y;
    int32_t aim;                  /* perso id Ed keeps turned to, 0 none */
    /* print */
    int32_t watch[MAX_WATCH];     /* perso id, ED, GAME */
    char seen[MAX_WATCH][160];    /* the watched line's last state, to print on a change */
    uint32_t watch_count;
    float every;
} S;

static int fail(const char *why, const char *what) {
    printf("probe: %s%s%s\n", why, what ? ": " : "", what ? what : "");
    return 0;
}

static void load_names(void) {
    char path[640];
    size_t size;
    char *text, *line;
    S.name_count = 0;
    if (!find_level(S.dir, S.level_name, "_cast.txt", path, sizeof path) || !(text = (char *)read_file(path, &size))) return;
    for (line = strtok(text, "\n"); line && S.name_count < MAX_NAMES; line = strtok(NULL, "\n")) {
        name_row r;
        if (sscanf(line, " %d %47s %63s family", &r.id, r.family, r.instance) == 3) S.names[S.name_count++] = r;
    }
    free(text);
}

static bool same_name(const char *a, const char *b) {
    for (; *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); a++, b++) {}
    return !*a && !*b;
}

static const char *instance_of(int32_t id) {
    uint32_t i;
    for (i = 0; i < S.name_count; i++) if (S.names[i].id == id) return S.names[i].instance;
    return "?";
}

static void unload_level(void) {
    if (S.live) tc_viewer_free(&S.v);
    S.live = false;
    free(S.ai);
    S.ai = NULL;
    tc_level_free(&S.level);
}

static int load_level(const char *folder, const char *name, const tc_viewer_carry *carry, uint32_t seed) {
    char path[640];
    size_t size;
    uint8_t *data, *hud;
    tc_viewer_desc desc;
    if (!find_level(folder, name, ".ttlv", path, sizeof path) || !(data = read_file(path, &size)))
        return fail("no such level", name);
    memset(&S.level, 0, sizeof S.level);
    if (tc_level_load(&S.level, data, size) != TC_OK) { free(data); return fail("the level doesn't load", path); }
    free(data);
    S.ai = NULL;
    S.ai_size = 0;
    if (find_level(folder, name, ".ttai", path, sizeof path)) S.ai = read_file(path, &S.ai_size);
    snprintf(S.level_name, sizeof S.level_name, "%s", name);
    memset(&desc, 0, sizeof desc);
    desc.level = &S.level;
    desc.ai_data = S.ai;
    desc.ai_size = S.ai_size;
    desc.level_name = S.level_name;
    desc.carry = carry;
    desc.seed = seed;
    if (tc_viewer_init(&S.v, &desc) != TC_OK) { unload_level(); return fail("no viewer for", name); }
    S.live = true;
    /* as the frontends: the HUD (its start texts hold Totalski's skier), a 16:9 window */
    snprintf(path, sizeof path, "%s/hud.tthd", folder);
    if ((hud = read_file(path, &size)) != NULL) { tc_viewer_set_hud(&S.v, hud, size); free(hud); }
    tc_viewer_set_aspect(&S.v, 16.0f / 9.0f);
    if (!S.v.playing && tc_viewer_can_play(&S.v)) tc_viewer_toggle_play(&S.v);
    load_names();
    printf("%7.2f level %s%s%s%s\n", S.time, name, carry ? " (from " : "", carry ? (carry->place[0] ? carry->place : "its start") : "",
           carry ? ")" : "");
    return 1;
}

static tc_cast *cast(void) { return (tc_cast *)tc_viewer_cast(&S.v); }
static tc_player *player(void) { return (tc_player *)tc_viewer_player(&S.v); }
static tc_ai_perso *ed_ai(void) {
    const tc_ed_ai *d = tc_viewer_driver(&S.v);
    return d ? (tc_ai_perso *)&d->ai : NULL;
}

static tc_cast_actor *actor(int32_t id) {
    tc_cast *c = cast();
    int32_t k;
    if (!c || id < 0 || (uint32_t)id >= c->id_count || (k = c->by_id[id]) < 0 || (uint32_t)k >= c->count) return NULL;
    return &c->actors[k];
}

/* A perso word: an id, an instance name, a family name (its first perso), ed; or game where allowed. 0: none. */
static int32_t resolve(const char *w, bool allow_game) {
    char *end;
    long n = strtol(w, &end, 10);
    uint32_t i;
    if (!strcmp(w, "ed")) return ED;
    if (allow_game && !strcmp(w, "game")) return GAME;
    if (*w && !*end) return actor((int32_t)n) ? (int32_t)n : 0;
    for (i = 0; i < S.name_count; i++) if (same_name(S.names[i].instance, w) && actor(S.names[i].id)) return S.names[i].id;
    for (i = 0; i < S.name_count; i++) if (same_name(S.names[i].family, w) && actor(S.names[i].id)) return S.names[i].id;
    return 0;
}

static tc_ai_perso *ai_of(int32_t id) {
    tc_cast_actor *a;
    if (id == ED) return ed_ai();
    return (a = actor(id)) ? a->ai : NULL;
}

/* ---- Printing ---- */

static const char *phase_name(int p) {
    static const char *n[] = {"play", "cover", "black", "loading", "pending", "reveal"};
    return p >= 0 && p <= TC_VP_REVEAL ? n[p] : "?";
}

/* A behaviour index, "-" for none (0xFFFF: no reflex). */
static const char *beh(uint16_t b, char *buf) {
    if (b == 0xFFFF) return "-";
    snprintf(buf, 8, "%u", b);
    return buf;
}

/* The watched thing's state (what a change is) and its place (printed, not compared). */
static void describe(int32_t w, char *state, size_t n, char *where, size_t m) {
    tc_cast *c = cast();
    const tc_player *p = player();
    *where = 0;
    if (w == GAME) {
        snprintf(state, n, "speech %u cam_target %d main %d phase %s%s%s%s powers %#x value0 %d", c ? c->globals.speech : 0,
                 c ? c->globals.cam_target : 0, c ? c->globals.main_character : 0, phase_name((int)S.v.phase),
                 tc_viewer_directed(&S.v) ? " directed" : "", tc_viewer_fixed_shot(&S.v) ? " fixed-shot" : "",
                 tc_viewer_talk_prompt(&S.v) ? " talk-prompt" : "", c ? (unsigned)c->globals.powers : 0u, c ? c->globals.values[0] : 0);
    } else if (w == ED) {
        const tc_ai_perso *e = ed_ai();
        if (!e) { snprintf(state, n, "(no AI)"); }
        else {
            char r[8];
            snprintf(state, n, "B%u R%s st%u hp%d/%d %s%s forced_by %d", e->normal.behaviour, beh(e->reflex.behaviour, r), e->state,
                     e->hit_points, e->hit_points_max, e->contacts & TC_AI_CONTACT_GROUND ? "ground" : "air",
                     e->contacts & TC_AI_CONTACT_WALL ? "+wall" : "", e->forced_by);
        }
        if (p) snprintf(where, m, "at (%.2f %.2f %.2f) h %.2f sector %u", (double)p->position.x, (double)p->position.y,
                        (double)p->position.z, (double)p->heading, (unsigned)tc_viewer_ed_sector(&S.v));
    } else {
        const tc_cast_actor *a = actor(w);
        char r[8];
        if (!a) { snprintf(state, n, "(gone)"); return; }
        snprintf(state, n, "B%u R%s st%u hp%d/%d %s%s%s%s%s", a->ai->normal.behaviour, beh(a->ai->reflex.behaviour, r), a->ai->state,
                 a->ai->hit_points, a->ai->hit_points_max, a->ai->active ? "active" : "inactive", a->ai->dead ? " dead" : "",
                 a->ai->frozen ? " frozen" : "", a->awake ? "" : " asleep", a->talking ? " talking" : "");
        if (p) {
            float dx = a->ai->position.x - p->position.x, dy = a->ai->position.y - p->position.y;
            snprintf(where, m, "at (%.2f %.2f %.2f) d %.2f dz %.2f", (double)a->ai->position.x, (double)a->ai->position.y,
                     (double)a->ai->position.z, (double)sqrtf(dx * dx + dy * dy), (double)(a->ai->position.z - p->position.z));
        }
    }
}

static void label(int32_t w, char *out, size_t n) {
    if (w == GAME) snprintf(out, n, "game");
    else if (w == ED) snprintf(out, n, "ed");
    else snprintf(out, n, "%d %s", w, instance_of(w));
}

static void print_line(int32_t w, const char *state, const char *where) {
    char l[96];
    label(w, l, sizeof l);
    printf("%7.2f [%s] %s%s%s\n", S.time, l, state, *where ? " | " : "", where);
}

static void show(int32_t w) {
    char state[160], where[128];
    describe(w, state, sizeof state, where, sizeof where);
    print_line(w, state, where);
}

/* After each frame: the watched lines that changed, or all of them on `every`'s beat. */
static void report(void) {
    uint32_t i;
    bool beat = S.every > 0 && S.frame % (uint32_t)fmaxf(1, roundf(S.every / FRAME)) == 0;
    for (i = 0; i < S.watch_count; i++) {
        char state[160], where[128];
        describe(S.watch[i], state, sizeof state, where, sizeof where);
        if (beat || strcmp(state, S.seen[i])) print_line(S.watch[i], state, where);
        snprintf(S.seen[i], sizeof S.seen[i], "%s", state);
    }
}

static void info(int32_t id) {
    const tc_cast_actor *a = actor(id);
    const tc_cast_entry *e = a->entry;
    const tc_cast_family *f = &S.level.cast.families[e->family];
    uint32_t s;
    printf("perso %d %s: family %u (type %d), %u states, initial state %u, sector %u, prso %d\n", id, instance_of(id), e->family,
           e->family_type == 0xFFFF ? -1 : (int)e->family_type, f->state_count, e->initial_state, e->sector,
           e->prso == 0xFFFF ? -1 : (int)e->prso);
    printf("  flags %#x flags2 %#x%s%s%s%s%s%s%s%s r2 %.1f hp %u/%u own body %s%.2f prio %u meets %u, %u ZDD zones, ZDE %s\n",
           e->flags, e->flags2, e->flags & TC_CAST_MAY_FORCE_MAIN ? " may-force-main" : "", e->flags & TC_CAST_SAVE_POINT ? " save-point" : "",
           e->flags & TC_CAST_ALWAYS_AWAKE ? " always-awake" : "", e->flags & TC_CAST_RUN_FIRST ? " run-first" : "",
           e->flags & TC_CAST_TALK ? " talk" : "", e->flags & TC_CAST_SPAWN ? " spawn" : "",
           e->flags2 & TC_CAST2_MOVES_MAIN ? " moves-main" : "", e->flags2 & TC_CAST2_DIRECTOR ? " director" : "",
           (double)e->radius2, e->hit_points, e->hit_points_max, e->own_radius < 0 ? "none (family's) " : "", (double)e->own_radius,
           e->own_priority, e->own_meets, e->zone_count, e->has_zde ? "its own" : "its family's");
    printf("  AI: keep_off_main %d removal_guard %d read_only_for_cast %d forces_blocked %u removals_kept %u\n", a->ai->keep_off_main,
           a->ai->removal_guard, a->ai->read_only_for_cast, a->ai->forces_blocked, a->ai->removals_kept);
    for (s = 0; s < f->state_count; s++) {
        uint16_t card = f->cards ? f->cards[s] : 0xFFFF;
        uint16_t body = f->body ? f->body[s] : 0xFFFF;
        printf("  state %3u  card %5d type %3d  body %d\n", s, card == 0xFFFF ? -1 : card,
               card == 0xFFFF ? -1 : (int)S.level.cast.cards[card].type, body == 0xFFFF ? -1 : body);
    }
}

static void list(void) {
    tc_cast *c = cast();
    uint32_t i;
    if (!c) { printf("(no cast)\n"); return; }
    for (i = 0; i < c->count; i++) {
        char state[160], where[128];
        describe(c->actors[i].entry->id, state, sizeof state, where, sizeof where);
        const tc_cast_entry *e = c->actors[i].entry;
        printf("  %4u %-32s %s%s%s%s | %s\n", e->id, instance_of(e->id), state, e->flags2 & TC_CAST2_MOVES_MAIN ? " moves-main" : "",
               e->flags2 & TC_CAST2_DIRECTOR ? " director" : "", e->flags & TC_CAST_TALK ? " talk" : "", where);
    }
}

static void skipped(int32_t id) {
    static uint32_t sum[10 * TC_AI_UNKNOWN_STRIDE];
    static const char *kinds[] = {"KW", "COND", "OP", "FUNC", "PROC", "META", "6", "7", "8", "FIELD"};
    const uint32_t *u = NULL;
    uint32_t k, n = 0;
    if (id) { const tc_ai_perso *ai = ai_of(id); u = ai ? ai->unknown : NULL; }
    else if (cast()) { tc_cast_unknown(cast(), sum); u = sum; }
    printf("skipped%s:", id ? "" : " by the cast");
    for (k = 0; u && k < 10 * TC_AI_UNKNOWN_STRIDE; k++)
        if (u[k]) { printf(" %s_%u(%u)", kinds[k / TC_AI_UNKNOWN_STRIDE], k % TC_AI_UNKNOWN_STRIDE, u[k]); n++; }
    printf("%s\n", n ? "" : " none");
}

/* Ed's portals (.ttai PORT: the stand-ins the driver runs; the arches that run themselves are cast persos, `list`'s
 * moves-main): where, how wide, and the level each goes to. */
static const tc_ai_model *ed_model(void) { const tc_ai_perso *e = ed_ai(); return e ? e->model : NULL; }

static void portals(void) {
    const tc_ai_model *m = ed_model();
    uint32_t k, o;
    if (!m || !m->portal_count) { printf("no PORT portals on %s\n", S.level_name); return; }
    for (k = 0; k < m->portal_count; k++) {
        const tc_ai_portal *q = &m->portals[k];
        printf("  portal %u: perso %u %s at (%.2f %.2f %.2f) radius %.2f%s fire %u", k, q->perso, instance_of(q->perso),
               (double)q->position.x, (double)q->position.y, (double)q->position.z, (double)q->radius, q->zone_count ? " (zone)" : "", q->fire);
        for (o = 0; o < (uint32_t)q->capture_count + q->fire_count; o++) {
            const tc_ai_portal_op *op = &q->ops[o];
            if (op->op == TC_AI_POP_LEVEL && op->b < m->name_count)
                printf(" -> %s%s%s", m->names[op->b], op->c < m->name_count ? " at " : "", op->c < m->name_count ? m->names[op->c] : "");
            if (op->op == TC_AI_POP_TELEPORT) printf(" -> (%.2f %.2f %.2f)", (double)op->v.x, (double)op->v.y, (double)op->v.z);
        }
        printf("\n");
    }
}

/* ---- Running ---- */

static float heading_to(tc_vec3 from, tc_vec3 to) { return atan2f(to.x - from.x, -(to.y - from.y)); }

/* One frontend frame: the inputs, the update, the draw; a level change followed. */
static int frame(void) {
    const tc_viewer_carry *pending;
    uint32_t a;
    tc_player *p = player();
    for (a = 0; a < TC_VA_COUNT; a++) {
        bool down = S.held[a];
        if (S.pulse[a] > 0) down = down || S.frame % (uint32_t)fmaxf(1, roundf(S.pulse[a] / FRAME)) < TAP_FRAMES;
        tc_viewer_set_action(&S.v, (tc_view_action)a, down);
    }
    tc_viewer_set_move_stick(&S.v, S.stick_x, S.stick_y);
    if (S.aim && p) {
        const tc_cast_actor *t = actor(S.aim);
        if (t) p->heading = heading_to(p->position, t->ai->position);
    }
    tc_viewer_update(&S.v, FRAME);
    { uint32_t count, opaque; tc_viewer_draws(&S.v, &count, &opaque); }
    S.frame++;
    S.time += FRAME;
    report();
    if ((pending = tc_viewer_pending_level(&S.v)) != NULL) {
        tc_viewer_carry carry = *pending;
        char folder[512];
        snprintf(folder, sizeof folder, "%s", S.dir);
        printf("%7.2f level change -> %s (place %s)\n", S.time, carry.level, carry.place[0] ? carry.place : "its start");
        unload_level();
        if (!load_level(folder, carry.level, &carry, S.frame)) return 0;
        S.aim = 0;
    }
    return 1;
}

static int run_frames(uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) if (!frame()) return 0;
    return 1;
}

static int action_of(const char *w) {
    static const struct { const char *name; tc_view_action a; } t[] = {
        {"jump", TC_VA_JUMP}, {"action", TC_VA_ACTION}, {"shoot", TC_VA_SHOOT}, {"lateral", TC_VA_LATERAL}, {"fast", TC_VA_FAST},
        {"forward", TC_VA_FORWARD}, {"back", TC_VA_BACK}, {"left", TC_VA_LEFT}, {"right", TC_VA_RIGHT}};
    uint32_t i;
    for (i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp(w, t[i].name)) return (int)t[i].a;
    return -1;
}

static void place_ed(tc_vec3 at, float heading) {
    tc_player *p = player();
    if (p) tc_player_teleport(p, at, heading);
}

/* ---- The script ---- */

static char *words[MAX_WORDS];
static int word_count, at_word;

static const char *next(void) { return at_word < word_count ? words[at_word++] : NULL; }
static int need(int n, const char *cmd) {
    if (at_word + n > word_count) return fail("too few words after", cmd);
    return 1;
}
static float num(void) { return (float)strtod(next(), NULL); }

static int perso_word(const char *cmd, bool allow_ed, bool allow_game, int32_t *out) {
    const char *w = next();
    *out = resolve(w, allow_game);
    if (!*out || (*out == ED && !allow_ed)) {
        printf("probe: %s: no such perso here: %s\n", cmd, w);
        return 0;
    }
    if (*out == ED && !ed_ai() && strcmp(cmd, "watch") && strcmp(cmd, "show") && strcmp(cmd, "unwatch")) return fail("Ed has no AI on", S.level_name);
    return 1;
}

static int command(const char *cmd) {
    static const struct { const char *name; int args; } arity[] = {
        {"run", 1}, {"frames", 1}, {"until", 3}, {"place", 3}, {"heading", 1}, {"near", 1}, {"at", 4}, {"face", 1}, {"aim", 1},
        {"hold", 1}, {"release", 1}, {"tap", 1}, {"pulse", 2}, {"stick", 2}, {"free", 0}, {"watch", 1}, {"unwatch", 1},
        {"every", 1}, {"show", 1}, {"info", 1}, {"list", 0}, {"skipped", 0}, {"power", 1}, {"value", 2}, {"hp", 2},
        {"dsg", 2}, {"setdsg", 3}, {"portals", 0}, {"portal", 1}};
    uint32_t i;
    int32_t id;
    tc_player *p = player();
    for (i = 0; i < sizeof arity / sizeof arity[0] && strcmp(arity[i].name, cmd); i++) {}
    if (i == sizeof arity / sizeof arity[0]) return fail("unknown command", cmd);
    if (!need(arity[i].args, cmd)) return 0;

    if (!strcmp(cmd, "run")) return run_frames((uint32_t)lroundf(num() / FRAME));
    if (!strcmp(cmd, "frames")) return run_frames((uint32_t)atoi(next()));
    if (!strcmp(cmd, "until")) {
        uint32_t b, n, k;
        if (!perso_word(cmd, true, false, &id)) return 0;
        b = (uint32_t)atoi(next());
        n = (uint32_t)lroundf(num() / FRAME);
        for (k = 0; k < n; k++) {
            const tc_ai_perso *ai = ai_of(id);
            if (ai && ai->normal.behaviour == b) break;
            if (!frame()) return 0;
        }
        printf("%7.2f until: %s B%u\n", S.time, k < n ? "reached" : "NOT reached, gave up at", b);
        return 1;
    }
    if (!strcmp(cmd, "place")) {
        float x = num(), y = num(), z = num();
        place_ed(tc_v3(x, y, z), p ? p->heading : 0);
        return 1;
    }
    if (!strcmp(cmd, "heading")) { float h = num(); if (p) p->heading = h; return 1; }
    if (!strcmp(cmd, "near") || !strcmp(cmd, "at") || !strcmp(cmd, "face") || !strcmp(cmd, "aim")) {
        const tc_cast_actor *a;
        if (!strcmp(cmd, "aim") && !strcmp(words[at_word], "off")) { at_word++; S.aim = 0; return 1; }
        if (!perso_word(cmd, false, false, &id)) return 0;
        a = actor(id);
        if (!p) return fail("no Ed to move on", S.level_name);
        if (!strcmp(cmd, "near")) {
            float d = 1.5f, h = a->ai->heading;
            tc_vec3 to;
            char *end;
            if (at_word < word_count) { float x = (float)strtod(words[at_word], &end); if (!*end) { d = x; at_word++; } }
            to = tc_v3_add(a->ai->position, tc_v3(sinf(h) * d, -cosf(h) * d, 0.3f));
            place_ed(to, heading_to(to, a->ai->position));
        } else if (!strcmp(cmd, "at")) {
            float x = num(), y = num(), z = num();
            tc_vec3 to = tc_v3_add(a->ai->position, tc_v3(x, y, z));
            place_ed(to, p->heading);
        } else if (!strcmp(cmd, "face")) p->heading = heading_to(p->position, a->ai->position);
        else S.aim = id;
        return 1;
    }
    if (!strcmp(cmd, "hold") || !strcmp(cmd, "release") || !strcmp(cmd, "tap") || !strcmp(cmd, "pulse")) {
        const char *w = next();
        int a = action_of(w);
        if (a < 0) return fail("no such action (jump action shoot lateral fast forward back left right)", w);
        if (!strcmp(cmd, "pulse")) { S.pulse[a] = num(); return 1; }
        if (!strcmp(cmd, "tap")) {
            bool was = S.held[a];
            S.held[a] = true;
            if (!run_frames(TAP_FRAMES)) return 0;
            S.held[a] = was;
            return 1;
        }
        S.held[a] = !strcmp(cmd, "hold");
        return 1;
    }
    if (!strcmp(cmd, "stick")) { S.stick_x = num(); S.stick_y = num(); return 1; }
    if (!strcmp(cmd, "free")) {
        memset(S.held, 0, sizeof S.held);
        memset(S.pulse, 0, sizeof S.pulse);
        S.stick_x = S.stick_y = 0;
        S.aim = 0;
        return 1;
    }
    if (!strcmp(cmd, "watch") || !strcmp(cmd, "unwatch") || !strcmp(cmd, "show")) {
        if (!strcmp(cmd, "unwatch") && !strcmp(words[at_word], "all")) { at_word++; S.watch_count = 0; return 1; }
        if (!perso_word(cmd, true, true, &id)) return 0;
        if (!strcmp(cmd, "show")) { show(id); return 1; }
        for (i = 0; i < S.watch_count && S.watch[i] != id; i++) {}
        if (!strcmp(cmd, "unwatch")) {
            if (i < S.watch_count) {
                memmove(&S.watch[i], &S.watch[i + 1], (S.watch_count - i - 1) * sizeof S.watch[0]);
                memmove(&S.seen[i], &S.seen[i + 1], (S.watch_count - i - 1) * sizeof S.seen[0]);
                S.watch_count--;
            }
            return 1;
        }
        if (i == S.watch_count) {
            char where[128];
            if (S.watch_count == MAX_WATCH) return fail("too many watches", NULL);
            S.watch[S.watch_count] = id;
            describe(id, S.seen[S.watch_count], sizeof S.seen[0], where, sizeof where);
            print_line(id, S.seen[S.watch_count], where);
            S.watch_count++;
        }
        return 1;
    }
    if (!strcmp(cmd, "every")) { S.every = num(); return 1; }
    if (!strcmp(cmd, "info")) { if (!perso_word(cmd, false, false, &id)) return 0; info(id); return 1; }
    if (!strcmp(cmd, "list")) { list(); return 1; }
    if (!strcmp(cmd, "portals")) { portals(); return 1; }
    if (!strcmp(cmd, "portal")) { /* half its radius in on its way in, facing in (replaycheck's placing) */
        const tc_ai_model *m = ed_model();
        uint32_t k = (uint32_t)atoi(next());
        const tc_ai_portal *q;
        if (!m || k >= m->portal_count) return fail("no such portal (see portals)", words[at_word - 1]);
        q = &m->portals[k];
        place_ed(tc_v3_add(q->position, tc_v3(-q->rows[1] * q->radius * 0.5f, -q->rows[4] * q->radius * 0.5f, 0.3f)),
                 atan2f(q->rows[1], -q->rows[4]));
        return 1;
    }
    if (!strcmp(cmd, "skipped")) {
        id = 0;
        if (at_word < word_count && resolve(words[at_word], false)) id = resolve(next(), false);
        skipped(id);
        return 1;
    }
    if (!cast()) return fail("no cast on", S.level_name);
    if (!strcmp(cmd, "power")) { cast()->globals.powers = (uint16_t)strtol(next(), NULL, 0); return 1; }
    if (!strcmp(cmd, "value")) {
        int k = atoi(next()), val = atoi(next());
        if (k < 0 || k >= 31) return fail("save values are 0-30", NULL);
        cast()->globals.values[k] = (int16_t)val;
        return 1;
    }
    if (!perso_word(cmd, true, false, &id)) return 0;
    if (!strcmp(cmd, "hp")) { ai_of(id)->hit_points = atoi(next()); return 1; }
    if (!strcmp(cmd, "dsg")) {
        uint32_t k = (uint32_t)atoi(next());
        char l[96];
        label(id, l, sizeof l);
        printf("%7.2f [%s] dsg%u = %g\n", S.time, l, k, (double)tc_ai_dsgvar_float(ai_of(id), k));
        return 1;
    }
    /* setdsg */
    { uint32_t k = (uint32_t)atoi(next()); tc_ai_dsgvar_set_float(ai_of(id), k, num()); }
    return 1;
}

/* Words from a script file: whitespace-separated, '#' to the line's end a comment. */
static int read_script(const char *path) {
    size_t size;
    char *text = (char *)read_file(path, &size), *c;
    if (!text) return fail("can't read", path);
    for (c = text; *c;) {
        if (*c == '#') { while (*c && *c != '\n') *c++ = ' '; continue; }
        c++;
    }
    for (c = strtok(text, " \t\r\n"); c && word_count < MAX_WORDS; c = strtok(NULL, " \t\r\n")) words[word_count++] = c;
    return 1; /* text stays: the words point into it */
}

/* A recording's session played up to `seconds` (its segments' run times summed), with the segment's own level and carry. */
static int start_from_recording(const char *path, float seconds) {
    static tc_replay r;
    size_t size;
    uint8_t *data = read_file(path, &size);
    double before = 0;
    uint32_t seg;
    float dt;
    char folder[600];
    if (!data) return fail("can't read", path);
    if (tc_replay_open(&r, data, size) != TC_OK) return fail("not a recording this build reads", path);
    for (seg = 0; seg + 1 < r.segment_count && before + r.segments[seg].seconds < seconds; seg++) before += r.segments[seg].seconds;
    if (!r.segment_count) return fail("no levels in", path);
    S.time = before; /* the session's clock: the timeline's (replay timeline) for this segment is S.time - before */
    {
        const tc_replay_segment *s = &r.segments[seg];
        if (s->edition) snprintf(folder, sizeof folder, "%s/../se/ttlv", S.dir);
        else snprintf(folder, sizeof folder, "%s", S.dir);
        if (!load_level(folder, s->level, s->has_carry ? &s->carry : NULL, s->seed)) return 0;
    }
    tc_replay_begin(&r, seg, &S.v, false);
    while (before + r.time < seconds && tc_replay_step(&r, &dt)) { S.frame++; S.time += dt; }
    tc_viewer_release_all(&S.v);
    printf("%7.2f recording %s: level %u (%s) at %.2f s of it\n", S.time, path, seg, r.segments[seg].level, r.time);
    return 1;
}

int main(int argc, char **argv) {
    int i = 1, ok = 1;
    const char *recording = NULL, *script = NULL;
    float rec_seconds = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    S.dir = "../files/out/ttlv";
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-d") && i + 1 < argc) { S.dir = argv[i + 1]; i += 2; }
        else if (!strcmp(argv[i], "-r") && i + 2 < argc) { recording = argv[i + 1]; rec_seconds = (float)atof(argv[i + 2]); i += 3; }
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) { script = argv[i + 1]; i += 2; }
        else break;
    }
    if (script && !read_script(script)) return 1;
    for (; i < argc && word_count < MAX_WORDS; i++) words[word_count++] = argv[i];
    if (!recording && word_count == 0) {
        printf("usage: probe [-d DIR] LEVEL COMMAND... | probe [-d DIR] -r FILE.ttrec SECONDS COMMAND... | probe -f SCRIPT\n"
               "       (the commands: the head of core/tests/probe.c, or docs/OVERVIEW.md \"The probe\")\n");
        return 1;
    }
    if (recording) ok = start_from_recording(recording, rec_seconds);
    else ok = load_level(S.dir, next(), NULL, 0);
    while (ok && at_word < word_count) ok = command(next());
    unload_level();
    return ok ? 0 : 1;
}

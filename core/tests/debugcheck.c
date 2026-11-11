/* The debug menu headless (tc_debug.h; docs/ENGINE.md "The debug menu"), on sud0:
 *   - open, the game holds: Ed and the cast's clock stand still, and the held actions move the menu;
 *   - a power switched off is off in the game's state at once, and the picture says so;
 *   - the survey (progress.ttpg, when the folder has one) names who uses the chosen flag;
 *   - "reload the level" leaves as a level change into the same level with the progress as set, and the level made from
 *     it starts with it: without power 7, the wall across the river stands;
 *   - a recorded session with the menu in it replays exact, and ends with the same powers.
 * Usage: debugcheck [out/ttlv] [picture.ppm]: the second writes the menu's picture over black, to look at. Test-only stdio. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tonic/tonic.h"

static int failures;

static void check(int ok, const char *what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

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

typedef struct memfile { uint8_t *data; size_t size, cap; } memfile;

static void sink(void *user, const void *data, size_t size) {
    memfile *m = (memfile *)user;
    if (m->size + size > m->cap) {
        m->cap = (m->size + size) * 2;
        m->data = (uint8_t *)realloc(m->data, m->cap);
    }
    memcpy(m->data + m->size, data, size);
    m->size += size;
}

static tc_level level;
static uint8_t *ai, *survey;
static size_t ai_size, survey_size;

static bool make(tc_viewer *v, uint32_t seed, tc_recorder *rec, const tc_viewer_carry *carry) {
    tc_viewer_desc d;
    memset(&d, 0, sizeof d);
    d.level = &level;
    d.ai_data = ai;
    d.ai_size = ai_size;
    d.level_name = "sud0";
    d.seed = seed;
    d.recorder = rec;
    d.carry = carry;
    if (tc_viewer_init(v, &d) != TC_OK) return false;
    if (!v->playing && tc_viewer_can_play(v)) tc_viewer_toggle_play(v);
    if (survey) tc_viewer_set_survey(v, survey, survey_size);
    return true;
}

static void frames(tc_viewer *v, int n) {
    uint32_t count, opaque;
    while (n-- > 0) {
        tc_viewer_update(v, 1.0f / 60);
        tc_viewer_draws(v, &count, &opaque);
    }
}

/* A key pressed for two frames, then let go for two. */
static void tap(tc_viewer *v, tc_view_action a) {
    tc_viewer_set_action(v, a, true);
    frames(v, 2);
    tc_viewer_set_action(v, a, false);
    frames(v, 2);
}

static uint16_t powers(const tc_viewer *v) {
    const tc_cast *c = tc_viewer_cast(v);
    return c ? c->globals.powers : 0;
}

static const char *menu_text(tc_viewer *v) {
    static char text[8192];
    const tc_cast *c = tc_viewer_cast(v);
    tc_debug_text(&v->debug, c ? &c->globals : NULL, "sud0", text, sizeof text);
    return text;
}

/* The session both the run and the recording make: walk, open the menu, switch power 7 off, close, walk on. */
static void session(tc_viewer *v) {
    tc_viewer_set_action(v, TC_VA_FORWARD, true);
    frames(v, 90);
    tc_viewer_set_action(v, TC_VA_FORWARD, false);
    frames(v, 10);
    tc_viewer_toggle_debug(v);
    tap(v, TC_VA_BACK);   /* row 1: power 7 */
    tap(v, TC_VA_JUMP);   /* off */
    tc_viewer_toggle_debug(v);
    tc_viewer_set_action(v, TC_VA_FORWARD, true);
    frames(v, 60);
    tc_viewer_set_action(v, TC_VA_FORWARD, false);
}

static bool wall_stands(tc_viewer *v) {
    tc_collision *world = tc_viewer_player(v)->world;
    const uint8_t *solid = world->solid;
    tc_hit h;
    bool hit;
    world->solid = NULL; /* every sector's triangles */
    hit = tc_collision_raycast(world, tc_v3(-112, 45, 1), tc_v3(1, 0, 0), 20, &h) && h.distance > 6 && h.distance < 12;
    world->solid = solid;
    return hit;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    char path[1024];
    size_t size;
    uint8_t *data;
    tc_viewer v;
    tc_view_speech_panel panel;
    tc_vec3 at;
    const tc_viewer_carry *pending;
    tc_viewer_carry carry;
    const char *text;
    int i;

    snprintf(path, sizeof path, "%s/sud0.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) {
        printf("no %s: nothing to check\nok\n", path);
        return 0;
    }
    free(data);
    snprintf(path, sizeof path, "%s/sud0.ttai", dir);
    ai = read_file(path, &ai_size);
    snprintf(path, sizeof path, "%s/progress.ttpg", dir);
    survey = read_file(path, &survey_size);

    printf("the menu on sud0\n");
    if (!make(&v, 7, NULL, NULL) || !tc_viewer_cast(&v)) {
        printf("  info  sud0 has no cast here: nothing to check\nok\n");
        return 0;
    }
    frames(&v, 60);
    check(!tc_viewer_debug_panel(&v, 1.6f, &panel), "closed: no picture");
    check(powers(&v) == TC_AI_POWERS_ALL, "the powers start all on");
    at = tc_viewer_player(&v)->position;
    tc_viewer_toggle_debug(&v);
    tc_viewer_set_action(&v, TC_VA_FORWARD, true); /* would walk Ed; here it is "up", to the last row */
    frames(&v, 2);
    tc_viewer_set_action(&v, TC_VA_FORWARD, false);
    frames(&v, 40);
    check(tc_viewer_debug_open(&v) && tc_viewer_debug_panel(&v, 1.6f, &panel) && panel.rgba &&
          panel.width == TC_DEBUG_PANEL_W && panel.rect[0] >= 0 && panel.rect[2] <= 1 && panel.rect[3] <= 1,
          "open: a picture inside the frame");
    {
        tc_vec3 now = tc_viewer_player(&v)->position;
        check(fabsf(now.x - at.x) + fabsf(now.y - at.y) + fabsf(now.z - at.z) < 1e-6f, "open: Ed stands still, the key moved the menu");
    }
    check(v.debug.row[TC_DEBUG_POWERS] == tc_debug_row_count(TC_DEBUG_POWERS) - 1, "up from the top row wraps to the last");
    tap(&v, TC_VA_BACK);
    tap(&v, TC_VA_BACK);
    check(v.debug.row[TC_DEBUG_POWERS] == 1, "down twice: the first power's row");
    text = menu_text(&v);
    check(strstr(text, "power 0x0007  ON") != NULL, "the picture shows power 7 on");
    if (survey) {
        check(strstr(text, "HEG_LaBelleSuzy") != NULL, "the survey names Suzy among power 7's users in sud0");
        check(strstr(text, "FRB_RoboBne") == NULL, "this level only: cavedoc's robot is not listed");
    } else printf("  info  no progress.ttpg in %s (made by the conversion: reinstall): the survey is not checked\n", dir);
    tap(&v, TC_VA_JUMP);
    check(powers(&v) == (uint16_t)(TC_AI_POWERS_ALL & ~7u), "Jump switches power 7 off in the game's state");
    check(strstr(menu_text(&v), "power 0x0007  off") != NULL, "and the picture shows it off");
    {
        uint32_t version = panel.version;
        check(tc_viewer_debug_panel(&v, 1.6f, &panel) && panel.version != version, "the picture's version moved on");
    }
    tap(&v, TC_VA_RIGHT);
    check(powers(&v) == TC_AI_POWERS_ALL, "right switches it on again");
    tap(&v, TC_VA_LEFT);
    check(powers(&v) == (uint16_t)(TC_AI_POWERS_ALL & ~7u), "left switches it off");

    /* the save values' page */
    tap(&v, TC_VA_FORWARD); /* the page row */
    tap(&v, TC_VA_RIGHT);
    check(v.debug.page == TC_DEBUG_VALUES, "right on the page row: the save values");
    tap(&v, TC_VA_BACK);
    tap(&v, TC_VA_RIGHT);
    tap(&v, TC_VA_RIGHT);
    check(tc_viewer_cast(&v)->globals.values[0] == 2, "right twice on value 0: 2");
    tc_viewer_set_action(&v, TC_VA_FAST, true);
    tap(&v, TC_VA_RIGHT);
    tc_viewer_set_action(&v, TC_VA_FAST, false);
    check(tc_viewer_cast(&v)->globals.values[0] == 12, "with Fast: by 10");
    if (survey) {
        tc_viewer_set_action(&v, TC_VA_BACK, true); /* held: repeats down the list */
        frames(&v, 600);
        tc_viewer_set_action(&v, TC_VA_BACK, false);
        frames(&v, 2);
        check(v.debug.row[TC_DEBUG_VALUES] != 1, "a held direction repeats");
    }
    if (argc > 2 && tc_viewer_debug_panel(&v, 1.6f, &panel)) {
        FILE *f;
        uint32_t k;
        v.debug.page = TC_DEBUG_POWERS;
        v.debug.all_levels = true;
        tc_viewer_debug_panel(&v, 1.6f, &panel);
        f = fopen(argv[2], "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", panel.width, panel.height);
            for (k = 0; k < panel.width * panel.height; k++) {
                const uint8_t *p = panel.rgba + 4 * k;
                uint8_t rgb[3] = {(uint8_t)(p[0] * p[3] / 255), (uint8_t)(p[1] * p[3] / 255), (uint8_t)(p[2] * p[3] / 255)};
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
            printf("  info  wrote %s\n", argv[2]);
        }
        v.debug.all_levels = false;
    }

    /* reload: the last row of the powers' page */
    v.debug.page = TC_DEBUG_POWERS;
    v.debug.row[TC_DEBUG_POWERS] = (uint8_t)(tc_debug_row_count(TC_DEBUG_POWERS) - 1);
    tap(&v, TC_VA_ACTION);
    check(!tc_viewer_debug_open(&v), "reload: the menu closes");
    for (i = 0; i < 600 && !tc_viewer_pending_level(&v); i++) frames(&v, 1);
    pending = tc_viewer_pending_level(&v);
    check(pending && !strcmp(pending->level, "sud0") && pending->has_progress && !pending->place[0] &&
          pending->powers == (uint16_t)(TC_AI_POWERS_ALL & ~7u) && pending->values[0] == 12,
          "reload: a level change into sud0, at Ed's start, with the powers and save values as set");
    if (pending) carry = *pending;
    tc_viewer_free(&v);

    if (pending && make(&v, 9, NULL, &carry)) {
        bool sector = false;
        uint16_t s;
        for (s = 0; s < level.sectors.count; s++)
            if (level.sectors.sectors[s].name && !strcmp(level.sectors.sectors[s].name, "sctporte")) sector = true;
        frames(&v, 240);
        check((powers(&v) & 7u) == 0 && tc_viewer_cast(&v)->globals.values[0] == 12, "the reloaded level starts with them");
        if (sector) check(wall_stands(&v), "without power 7 the wall across the river stands after the reload");
        else printf("  info  this export has no river wall (made before 2026-11-11: reinstall)\n");
        tc_viewer_free(&v);
    } else check(0, "the reloaded level opens");

    /* recorded, then replayed */
    {
        memfile m = {0};
        tc_recorder *rec = tc_recorder_create(sink, &m);
        tc_replay r;
        uint16_t ended;
        float dt;
        if (!make(&v, 0x51u, rec, NULL)) { check(0, "viewer"); return 1; }
        session(&v);
        ended = powers(&v);
        tc_viewer_free(&v);
        tc_recorder_free(rec);
        check(ended == (uint16_t)(TC_AI_POWERS_ALL & ~7u), "recorded: the session ends without power 7");
        check(tc_replay_open(&r, m.data, m.size) == TC_OK && r.segment_count == 1, "the recording reads back");
        if (r.segment_count == 1 && make(&v, r.segments[0].seed, NULL, NULL)) {
            tc_replay_begin(&r, 0, &v, true);
            while (tc_replay_step(&r, &dt)) {}
            if (r.drift.drifted) printf("        frame %u: %s\n", r.drift.first_frame, r.drift.first_what);
            check(!r.drift.drifted && r.drift.samples > 0, "replayed: exact");
            check(powers(&v) == ended && !tc_viewer_debug_open(&v), "replayed: the same powers at the end, the menu closed");
            tc_viewer_free(&v);
        }
        tc_replay_close(&r);
        free(m.data);
    }

    free(ai);
    free(survey);
    tc_level_free(&level);
    printf(failures ? "FAIL (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}

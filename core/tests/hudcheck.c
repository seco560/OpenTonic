/* The game's HUD headless (tc_hud.h; docs/ENGINE.md "The HUD"):
 *   - hud.tthd loads: the texts by the scripts' numbers, the fonts' glyphs in the atlas;
 *   - Totalski, a new game: the scripts' "press Jump to start" blinks in a text slot, and once started the race clock
 *     runs in another;
 *   - sud0: the standard HUD comes in at the level's start and leaves after five seconds; its quads stay on a screen of
 *     any shape, the left ones by the left edge and the right ones by the right;
 *   - the inventory: a bonus picked up raises its save value, and Ed's shots take bees.
 * Usage: hudcheck [out/ttlv] [pictures prefix]: with a prefix, writes <prefix>_<name>.ppm (the HUD over grey, 4:3 and
 * 16:9) to look at. Levels or a hud.tthd from before this was built: an info line, no failure. Test-only stdio. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tonic/tonic.h"

static int failures;
static const char *prefix;

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

static uint8_t *hud_bytes;
static size_t hud_size;

typedef struct game { tc_level level; tc_viewer v; bool ok; } game;

static bool start(game *g, const char *dir, const char *name) {
    char path[1024];
    size_t size, ai_size = 0;
    uint8_t *data, *ai;
    tc_viewer_desc d;
    memset(g, 0, sizeof(*g));
    snprintf(path, sizeof path, "%s/%s.ttlv", dir, name);
    if (!(data = read_file(path, &size)) || tc_level_load(&g->level, data, size) != TC_OK) {
        free(data);
        printf("  info  no %s: skipped\n", path);
        return false;
    }
    free(data);
    snprintf(path, sizeof path, "%s/%s.ttai", dir, name);
    ai = read_file(path, &ai_size);
    memset(&d, 0, sizeof d);
    d.level = &g->level;
    d.ai_data = ai;
    d.ai_size = ai_size;
    d.level_name = name;
    if (tc_viewer_init(&g->v, &d) != TC_OK) { free(ai); tc_level_free(&g->level); check(0, "the viewer starts"); return false; }
    free(ai);
    if (!g->v.playing && tc_viewer_can_play(&g->v)) tc_viewer_toggle_play(&g->v);
    tc_viewer_set_hud(&g->v, hud_bytes, hud_size);
    g->ok = true;
    return true;
}

static void stop(game *g) {
    if (!g->ok) return;
    tc_viewer_free(&g->v);
    tc_level_free(&g->level);
}

static void frames(tc_viewer *v, int n) {
    uint32_t count, opaque;
    while (n-- > 0) {
        tc_viewer_update(v, 1.0f / 60);
        tc_viewer_draws(v, &count, &opaque);
    }
}

/* The HUD of this frame over grey, as a frontend draws it (nearest texel). */
static void picture(tc_viewer *v, const char *name, int w, int h) {
    char path[1024];
    tc_view_hud hud;
    uint8_t *rgb;
    FILE *f;
    uint32_t k;
    int x, y;
    if (!prefix) return;
    rgb = (uint8_t *)malloc((size_t)w * h * 3);
    memset(rgb, 0x58, (size_t)w * h * 3);
    if (tc_viewer_hud(v, (float)w / (float)h, &hud))
        for (k = 0; k < hud.count; k++) {
            const float *r = hud.quads[k].rect, *t = hud.quads[k].uv;
            int x0 = (int)floorf(r[0] * w), x1 = (int)ceilf(r[2] * w), y0 = (int)floorf(r[1] * h), y1 = (int)ceilf(r[3] * h);
            for (y = y0 < 0 ? 0 : y0; y < y1 && y < h; y++)
                for (x = x0 < 0 ? 0 : x0; x < x1 && x < w; x++) {
                    float fu = ((x + 0.5f) / w - r[0]) / (r[2] - r[0]), fv = ((y + 0.5f) / h - r[1]) / (r[3] - r[1]);
                    int tx, ty;
                    const uint8_t *s;
                    uint8_t *d = rgb + 3 * ((size_t)y * w + x);
                    if (fu < 0 || fu >= 1 || fv < 0 || fv >= 1) continue;
                    tx = (int)((t[0] + fu * (t[2] - t[0])) * hud.width);
                    ty = (int)((t[1] + fv * (t[3] - t[1])) * hud.height);
                    if (tx < 0 || ty < 0 || tx >= (int)hud.width || ty >= (int)hud.height) continue;
                    s = hud.rgba + 4 * ((size_t)ty * hud.width + tx);
                    d[0] = (uint8_t)((s[0] * s[3] + d[0] * (255 - s[3])) / 255);
                    d[1] = (uint8_t)((s[1] * s[3] + d[1] * (255 - s[3])) / 255);
                    d[2] = (uint8_t)((s[2] * s[3] + d[2] * (255 - s[3])) / 255);
                }
        }
    snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    if ((f = fopen(path, "wb")) != NULL) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        fwrite(rgb, 3, (size_t)w * h, f);
        fclose(f);
        printf("  wrote %s\n", path);
    }
    free(rgb);
}

/* The quads' bounds for a viewport of this aspect; false: none. */
static bool bounds(tc_viewer *v, float aspect, float out[4]) {
    tc_view_hud hud;
    uint32_t k;
    if (!tc_viewer_hud(v, aspect, &hud) || !hud.count) return false;
    out[0] = out[1] = 9;
    out[2] = out[3] = -9;
    for (k = 0; k < hud.count; k++) {
        const float *r = hud.quads[k].rect;
        if (r[0] < out[0]) out[0] = r[0];
        if (r[1] < out[1]) out[1] = r[1];
        if (r[2] > out[2]) out[2] = r[2];
        if (r[3] > out[3]) out[3] = r[3];
    }
    return true;
}

static void data_checks(void) {
    tc_hud h;
    const tc_hud_data *d = &h.data;
    const tc_hud_font *pc = NULL, *hit = NULL, *lips = NULL;
    uint32_t k, drawn = 0;
    char what[256];
    tc_hud_init(&h);
    check(tc_hud_load(&h, hud_bytes, hud_size) == TC_OK, "hud.tthd loads");
    if (!h.loaded) return;
    check(d->text_count > 0x250 && d->texts[0x250].bytes && strcmp(d->texts[0x250].bytes, "Press 'Jump' to start") == 0,
          "text 0x250 is Totalski's \"Press 'Jump' to start\" (the scripts' number = game.dsb's record)");
    check(d->standard[TC_HUD_LIVES] < d->text_count && d->texts[d->standard[TC_HUD_LIVES]].bytes &&
          strcmp(d->texts[d->standard[TC_HUD_LIVES]].bytes, "x999") == 0, "the standard HUD's lives text is \"x999\" (fix.gpt's block)");
    for (k = 0; k < d->font_count; k++) {
        uint32_t j;
        if (strcmp(d->fonts[k].name, "FontPC") == 0) pc = &d->fonts[k];
        if (strcmp(d->fonts[k].name, "FixHitPoints") == 0) hit = &d->fonts[k];
        if (strcmp(d->fonts[k].name, "FontLips") == 0) lips = &d->fonts[k];
        for (j = 0; j < d->fonts[k].glyph_count; j++) drawn += d->fonts[k].glyphs[j].cell[0] != d->fonts[k].glyphs[j].cell[2];
    }
    snprintf(what, sizeof what, "%u fonts, %u glyphs drawn into a %u x %u atlas; the counters', the clock's and the strings' fonts are there",
             (unsigned)d->font_count, (unsigned)drawn, (unsigned)d->atlas_width, (unsigned)d->atlas_height);
    check(pc && hit && lips && drawn > 150 && d->atlas, what);
    tc_hud_free(&h);
}

static void totalski(const char *dir) {
    game g;
    const tc_cast *c;
    char text[256], what[512];
    bool start_text = false, empty_text = false, has_fonts = false;
    int i, clock_slot = -1, first_ms = 0, last_ms = 0;
    uint32_t k;
    printf("Totalski, a new game:\n");
    if (!start(&g, dir, "Totalski")) return;
    c = tc_viewer_cast(&g.v);
    if (!c) { check(0, "Totalski has a cast"); stop(&g); return; }
    snprintf(what, sizeof what, "a new game's progress: powers %#x, bees %d, Doc's objects %d", (unsigned)c->globals.powers,
             (int)c->globals.values[0], (int)c->globals.values[1]);
    check(c->globals.powers == 0 && c->globals.values[0] == 0 && c->globals.values[1] == -1 && c->globals.values[6] == -1 &&
          c->globals.values[7] == 0, what);
    for (i = 0; i < 180; i++) { /* three seconds: the start text blinks every 300 ms */
        frames(&g.v, 1);
        for (k = 0; k < TC_AI_HUD_SLOTS; k++) {
            const tc_ai_hud_slot *s = &c->globals.hud[k];
            if (!s->active || s->kind != TC_AI_HUD_STRING) continue;
            if (s->font != 0xFF) has_fonts = true;
            if (s->text == 0x250 && tc_hud_slot_text(&g.v.hud, s, 0, 0, text, sizeof text)) {
                if (!start_text) picture(&g.v, "totalski_start_4x3", 640, 480), picture(&g.v, "totalski_start_16x9", 1280, 720);
                start_text = true;
            }
            if (s->text == 0x31) empty_text = true;
        }
    }
    if (!has_fonts && (start_text || empty_text)) {
        printf("  info  the slots' fonts are not exported (levels from before the HUD: reinstall); the rest is not checked\n");
        stop(&g);
        return;
    }
    check(start_text && empty_text, "the start text blinks in a slot: \"Press 'Jump' to start\", then the empty text");
    if (!g.v.hud.data.has_head) { /* hud.tthd without Ed's head: a conversion from before the start gate's zone was exported */
        printf("  info  converted before the race's start gate was exported (cast.py named_zones: reinstall); the clock is not checked\n");
        stop(&g);
        return;
    }
    tc_viewer_set_action(&g.v, TC_VA_JUMP, true);
    frames(&g.v, 4);
    tc_viewer_set_action(&g.v, TC_VA_JUMP, false);
    for (i = 0; i < 600; i++) { /* ten seconds: he rides off, crosses the start gate, and the clock runs from there */
        frames(&g.v, 1);
        for (k = 0; k < TC_AI_HUD_SLOTS; k++) {
            const tc_ai_hud_slot *s = &c->globals.hud[k];
            if (!s->active || s->kind != TC_AI_HUD_CLOCK || s->text != 5) continue; /* IA_CHRONOVALUE: this run's time */
            if (clock_slot < 0) { clock_slot = (int)k; first_ms = s->value; }
            if ((int)k == clock_slot) last_ms = s->value;
        }
    }
    text[0] = 0;
    if (clock_slot >= 0) tc_hud_slot_text(&g.v.hud, &c->globals.hud[clock_slot], 0, 0, text, sizeof text);
    snprintf(what, sizeof what, "after Jump the race clock runs in slot %d: %d ms -> %d ms in ten seconds, shown \"%s\"",
             clock_slot, first_ms, last_ms, text);
    check(clock_slot >= 0 && last_ms > first_ms + 1000 && strlen(text) == 8, what);
    picture(&g.v, "totalski_race_4x3", 640, 480);
    picture(&g.v, "totalski_race_16x9", 1280, 720);
    stop(&g);
}

static void sud0(const char *dir) {
    game g;
    const tc_cast *c;
    tc_cast *cast;
    char what[512];
    float b43[4], b169[4], b54[4];
    bool on, wide, narrow;
    int16_t slot = -1;
    uint32_t i, tried = 0;
    int before, after = 0, bees, shots = 0, given = 0;
    printf("sud0:\n");
    if (!start(&g, dir, "sud0")) return;
    c = tc_viewer_cast(&g.v);
    if (!c || !g.level.cast.inv_count) {
        printf("  info  sud0 has no inventory objects (a level from before the HUD: reinstall); not checked\n");
        stop(&g);
        return;
    }
    cast = (tc_cast *)c;
    frames(&g.v, 60);
    on = bounds(&g.v, 4.0f / 3, b43);
    wide = bounds(&g.v, 16.0f / 9, b169);
    narrow = bounds(&g.v, 5.0f / 4, b54);
    snprintf(what, sizeof what, "the standard HUD is in after a second: life %d of %d, lives %d, the level's counters %d and %d",
             (int)g.v.hud.now.hit_points, (int)g.v.hud.now.hit_points_max, (int)g.v.hud.now.lives, (int)g.v.hud.now.tings,
             (int)g.v.hud.now.spade);
    check(on && g.v.hud.out_life < 0.05f && g.v.hud.now.hit_points > 0 && g.v.hud.now.tings >= 0, what);
    picture(&g.v, "sud0_4x3", 640, 480);
    picture(&g.v, "sud0_16x9", 1280, 720);
    snprintf(what, sizeof what, "its quads at 4:3 span x %.3f..%.3f, y %.3f..%.3f; at 16:9 x %.3f..%.3f; at 5:4 x %.3f..%.3f", b43[0],
             b43[2], b43[1], b43[3], b169[0], b169[2], b54[0], b54[2]);
    check(on && wide && narrow && b43[0] >= -0.01f && b43[2] <= 1.01f && b43[1] >= -0.01f && b43[3] <= 1.01f &&
          b169[0] >= -0.01f && b169[2] <= 1.01f && b54[0] >= -0.01f && b54[2] <= 1.01f, what);
    snprintf(what, sizeof what, "the left elements keep their distance to the left edge, the right ones to the right: "
             "%.1f and %.1f px of 480 lines at 4:3, %.1f and %.1f at 16:9", b43[0] * 640, (1 - b43[2]) * 640, b169[0] * 853.33f,
             (1 - b169[2]) * 853.33f);
    check(on && wide && fabsf(b43[0] * 640 - b169[0] * 853.33f) < 1 && fabsf((1 - b43[2]) * 640 - (1 - b169[2]) * 853.33f) < 1, what);
    frames(&g.v, 60 * 7);
    check(g.v.hud.out_life == 0 && bounds(&g.v, 4.0f / 3, b43), "seven seconds on it is still there: the HUD stays, as the game's does by default");
    g.v.hud.auto_hide = true; /* the game's other mode (a setting for later): an element leaves five seconds after its last change */
    for (i = 0; i < 60 * 15 && (g.v.hud.asked || g.v.hud.out_life < 2.0f || g.v.hud.out_lives < 2.0f ||
                                g.v.hud.out_tings < 2.0f || g.v.hud.out_spade < 2.0f || g.v.hud.out_bees < 2.0f); i++) frames(&g.v, 1);
    snprintf(what, sizeof what, "set to hide, it leaves once nothing has changed for five seconds (after %.1f s; life slid %.2f of 2 out)",
             i / 60.0, g.v.hud.out_life);
    check(g.v.hud.out_life >= 2.0f && g.v.hud.out_lives >= 2.0f && !bounds(&g.v, 4.0f / 3, b43), what);
    g.v.hud.auto_hide = false;

    /* the inventory: Ed onto the level's bonuses (the persos whose object shares the World's save value) */
    for (i = 0; i < g.level.cast.inv_count; i++)
        if (g.level.cast.inv[i].id == g.level.cast.world) slot = g.level.cast.inv[i].slot;
    before = slot >= 0 ? c->globals.values[slot] : 0;
    for (i = 0; slot >= 0 && i < c->count && tried < 12 && after <= before; i++) {
        const tc_cast_actor *a = &c->actors[i];
        uint32_t k;
        bool bonus = false;
        for (k = 0; k < g.level.cast.inv_count; k++)
            bonus = bonus || (g.level.cast.inv[k].id == a->entry->id && g.level.cast.inv[k].slot == slot &&
                              g.level.cast.inv[k].amount > 0 && a->entry->id != g.level.cast.world);
        if (!bonus || !a->ai->active) continue;
        tried++;
        tc_player_teleport((tc_player *)tc_viewer_player(&g.v), tc_v3_add(a->ai->position, tc_v3(0, 0, 0.3f)), 0);
        frames(&g.v, 45);
        after = c->globals.values[slot];
    }
    snprintf(what, sizeof what, "a bonus picked up raises save value %d (the World's): %d -> %d, after %u bonuses tried; the HUD "
             "asks for its counter (%#x)", (int)slot, before, after, (unsigned)tried, (unsigned)g.v.hud.asked);
    check(slot >= 0 && after > before && (g.v.hud.asked & 0x14), what);
    picture(&g.v, "sud0_bonus_16x9", 1280, 720);

    /* Ed's shots take bees (FUNC 96 on the bee's object, save value 0): none, none leaves; with three, three at most */
    cast->globals.values[0] = 3;
    tc_viewer_set_action(&g.v, TC_VA_ACTION, true);
    frames(&g.v, 12);
    tc_viewer_set_action(&g.v, TC_VA_ACTION, false);
    frames(&g.v, 60);
    bees = 3;
    for (i = 0; i < 8 * 46; i++) { /* eight presses of Shoot, 6 frames down and 40 up */
        tc_viewer_set_action(&g.v, TC_VA_SHOOT, i % 46 < 6);
        frames(&g.v, 1);
        if (c->globals.values[0] < bees) shots += bees - c->globals.values[0];
        if (c->globals.values[0] > bees) given += c->globals.values[0] - bees;
        bees = c->globals.values[0];
    }
    tc_viewer_set_action(&g.v, TC_VA_SHOOT, false);
    snprintf(what, sizeof what, "with 3 bees and the blowpipe out, eight presses of Shoot take %d (%d given meanwhile by the level); "
             "%d left, never fewer than none", shots, given, bees);
    check(shots >= 1 && shots <= 3 + given && bees == 3 + given - shots && bees >= 0, what);
    picture(&g.v, "sud0_bees_16x9", 1280, 720);
    stop(&g);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    char path[1024];
    prefix = argc > 2 ? argv[2] : NULL;
    snprintf(path, sizeof path, "%s/hud.tthd", dir);
    hud_bytes = read_file(path, &hud_size);
    if (!hud_bytes) {
        printf("  info  no %s (made by the conversion: reinstall): the HUD is not checked\nok\n", path);
        return 0;
    }
    printf("hud.tthd:\n");
    data_checks();
    totalski(dir);
    sud0(dir);
    free(hud_bytes);
    printf(failures ? "%d FAILED\n" : "ok\n", failures);
    return failures ? 1 : 0;
}

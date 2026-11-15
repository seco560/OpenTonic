/* The main menu and saved games headless (tc_menu.h, tc_save.h; docs/ENGINE.md "The menus", "Saved games"):
 *   - menu.ttmn loads: the two fonts, the two pictures, their layers; game.dsb's texts are found by name;
 *   - the screens' choices and moves: START GAME -> NEW GAME (a name typed, an existing one asks to erase), LOAD GAME,
 *     DELETE GAME (asks first), OPTIONS (values step and say so), TONIC VIEWER, EXIT GAME;
 *   - every screen's letters stay on the screen, at 4:3 and at 16:9; the backgrounds turn over time;
 *   - the options' file round trip, and tc_viewer_set_view;
 *   - a game saved on sud0 (progress, a checkpoint, Ed's life, the level-saved table) comes back as it was in a new
 *     viewer made from the file, Ed at the checkpoint.
 * Usage: menucheck [out/ttlv] [prefix]: with a prefix, each screen's picture as the core lays it out (the layers, then
 * the letters) goes to <prefix><screen>.ppm, 1024 x 576, to look at. Test-only stdio. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tonic/tonic.h"

static int failures;
static const char *g_prefix;
static void picture(const tc_menu *m, const char *path, int W, int H);

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

static const char *label(const tc_menu *m, tc_menu_screen s, int k) {
    static char buf[96];
    return tc_menu_label(m, s, k, buf, sizeof buf);
}

/* Every quad inside the viewport (a hair of slack). */
static bool on_screen(const tc_menu *m) {
    uint32_t k;
    for (k = 0; k < m->quad_count; k++) {
        const float *r = m->quads[k].rect;
        if (r[0] < -0.002f || r[1] < -0.002f || r[2] > 1.002f || r[3] > 1.002f) {
            printf("        quad %u off the screen: %.3f %.3f %.3f %.3f\n", k, r[0], r[1], r[2], r[3]);
            return false;
        }
    }
    return true;
}

static void menu_checks(const char *dir) {
    char path[512];
    size_t size;
    uint8_t *data;
    tc_menu m;
    tc_menu_game games[2];
    tc_options o, back;
    char text[512];
    int s, a, ok;
    static const float aspects[] = {4.0f / 3, 16.0f / 9};

    printf("menu\n");
    snprintf(path, sizeof path, "%s/menu.ttmn", dir);
    tc_menu_init(&m);
    data = read_file(path, &size);
    check(data && tc_menu_load(&m, data, size) == TC_OK, "menu.ttmn loads");
    free(data);
    if (!m.loaded) return;
    check(m.data.font_count == 2 && !strcmp(m.data.fonts[0].name, "TitleEd") && !strcmp(m.data.fonts[1].name, "FontPC"),
          "its fonts: TitleEd, FontPC");
    check(m.data.image_count == 2 && m.data.bg_count == 2 && strstr(m.data.images[0].name, "TTSPACE_BKGRND") &&
              strstr(m.data.images[1].name, "TTSPACE_STARS"),
          "its backgrounds: TTSPACE_BKGRND, TTSPACE_STARS");
    check(!strcmp(label(&m, TC_MS_MAIN, 0), "START GAME") && !strcmp(label(&m, TC_MS_MAIN, 1), "OPTIONS") &&
              !strcmp(label(&m, TC_MS_MAIN, 2), "TONIC VIEWER") && !strcmp(label(&m, TC_MS_MAIN, 3), "REPLAYS") &&
              !strcmp(label(&m, TC_MS_MAIN, 4), "EXIT GAME"),
          "the main menu: START GAME, OPTIONS, TONIC VIEWER, REPLAYS, EXIT GAME");
    check(!strcmp(label(&m, TC_MS_START, 0), "NEW GAME") && !strcmp(label(&m, TC_MS_START, 1), "LOAD GAME") &&
              !strcmp(label(&m, TC_MS_START, 2), "DELETE GAME"),
          "START GAME: NEW GAME, LOAD GAME (game.dsb's), DELETE GAME");
    {
        char t[64];
        check(!strcmp(tc_menu_level_title(&m, "sud0", t, sizeof t), "South Plain") &&
                  !strcmp(tc_menu_level_title(&m, "se:Totalski", t, sizeof t), "Ski Slope") &&
                  !strcmp(tc_menu_level_title(&m, "bar", t, sizeof t), "bar"),
              "level titles from game.dsb (sud0: South Plain)");
    }

    /* moves */
    tc_menu_input_press(&m, TC_MI_DOWN);
    tc_menu_input_press(&m, TC_MI_DOWN);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(tc_menu_take_command(&m) == TC_MC_TONIC_VIEWER, "TONIC VIEWER asks for the levels grid");
    tc_menu_input_press(&m, TC_MI_DOWN);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(tc_menu_take_command(&m) == TC_MC_REPLAYS, "REPLAYS asks for the recordings");
    tc_menu_input_press(&m, TC_MI_DOWN);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(tc_menu_take_command(&m) == TC_MC_EXIT, "EXIT GAME asks to quit");
    tc_menu_input_press(&m, TC_MI_DOWN); /* round to START GAME */
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(m.screen == TC_MS_START, "START GAME opens its menu");
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(m.screen == TC_MS_NEW_GAME && !m.name[0], "NEW GAME asks for a name");
    tc_menu_type(&m, ' ');
    tc_menu_type(&m, 'e');
    tc_menu_type(&m, 'd');
    tc_menu_type(&m, '!');
    tc_menu_type(&m, ' ');
    tc_menu_type(&m, '2');
    tc_menu_type(&m, 'x');
    tc_menu_input_press(&m, TC_MI_ERASE);
    check(!strcmp(m.name, "ED 2"), "the name: upper case, letters, digits and inner spaces (\"ED 2\")");
    memset(games, 0, sizeof games);
    snprintf(games[0].name, sizeof games[0].name, "ED 2");
    snprintf(games[0].level, sizeof games[0].level, "sud0");
    snprintf(games[1].name, sizeof games[1].name, "SUZY");
    snprintf(games[1].level, sizeof games[1].level, "Totalski");
    tc_menu_set_games(&m, games, 2);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(m.screen == TC_MS_NEW_ERASE && m.selected[TC_MS_NEW_ERASE] == 1 && tc_menu_take_command(&m) == TC_MC_NONE,
          "an existing name asks first, NO selected");
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(m.screen == TC_MS_NEW_GAME && tc_menu_take_command(&m) == TC_MC_NONE, "NO: back to the name");
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    tc_menu_input_press(&m, TC_MI_LEFT);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(tc_menu_take_command(&m) == TC_MC_NEW_GAME && !strcmp(m.name, "ED 2"), "YES: a new game of that name");
    tc_menu_show(&m, TC_MS_START);
    tc_menu_input_press(&m, TC_MI_DOWN);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(m.screen == TC_MS_LOAD, "LOAD GAME");
    tc_menu_input_press(&m, TC_MI_DOWN);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(tc_menu_take_command(&m) == TC_MC_LOAD_GAME && m.command_game == 1, "a list: down to the second game; loads it");
    tc_menu_show(&m, TC_MS_START); /* the selection is kept: LOAD GAME */
    tc_menu_input_press(&m, TC_MI_DOWN);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(m.screen == TC_MS_DELETE, "DELETE GAME");
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(m.screen == TC_MS_DELETE_ERASE && m.selected[TC_MS_DELETE_ERASE] == 1, "asks first, NO selected");
    tc_menu_input_press(&m, TC_MI_RIGHT);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(tc_menu_take_command(&m) == TC_MC_DELETE_GAME && m.command_game == m.selected[TC_MS_DELETE] &&
              m.screen == TC_MS_DELETE,
          "YES: deletes the game shown, back on DELETE GAME");
    tc_menu_input_press(&m, TC_MI_BACK);
    tc_menu_input_press(&m, TC_MI_BACK);
    check(m.screen == TC_MS_MAIN, "back, back: the main menu");

    /* the game menu */
    tc_menu_show(&m, TC_MS_GAME);
    check(!strcmp(label(&m, TC_MS_GAME, 0), "RESUME GAME") && !strcmp(label(&m, TC_MS_GAME, 1), "OPTIONS") &&
              !strcmp(label(&m, TC_MS_GAME, 2), "PHOTO MODE") && !strcmp(label(&m, TC_MS_GAME, 3), "MAIN MENU") &&
              tc_menu_item_count(&m, TC_MS_GAME) == 4 && tc_menu_in_game(&m),
          "the game menu: RESUME GAME, OPTIONS, PHOTO MODE, MAIN MENU (quitting is the main menu's)");
    tc_menu_input_press(&m, TC_MI_BACK);
    check(tc_menu_take_command(&m) == TC_MC_RESUME, "Esc there resumes the game");
    tc_menu_input_press(&m, TC_MI_DOWN);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    tc_menu_input_press(&m, TC_MI_BACK);
    check(m.screen == TC_MS_GAME, "its OPTIONS go back to it");
    tc_menu_input_press(&m, TC_MI_DOWN);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(tc_menu_take_command(&m) == TC_MC_PHOTO_MODE, "PHOTO MODE");
    tc_menu_input_press(&m, TC_MI_DOWN);
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(tc_menu_take_command(&m) == TC_MC_MAIN_MENU, "MAIN MENU");
    tc_menu_build(&m, 16.0f / 9);
    check(m.layer_count == 3 && m.layers[0].image == TC_MENU_IMAGE_DIM && m.layers[1].image == TC_MENU_IMAGE_SWIRL,
          "over the game: a dim and the swirls, not the space pictures");

    /* the pointer */
    tc_menu_show(&m, TC_MS_MAIN);
    tc_menu_build(&m, 16.0f / 9);
    {
        int h, found = -1;
        for (h = 0; h < (int)m.hit_count; h++)
            if (m.hits[h].item == 2) found = h;
        if (found >= 0) {
            float x = 0.5f * (m.hits[found].rect[0] + m.hits[found].rect[2]), y = 0.5f * (m.hits[found].rect[1] + m.hits[found].rect[3]);
            check(tc_menu_pointer(&m, x, y) && m.selected[TC_MS_MAIN] == 2, "the pointer over TONIC VIEWER selects it");
            check(!tc_menu_pointer(&m, 0.02f, 0.98f) && m.selected[TC_MS_MAIN] == 2, "off every choice it changes nothing");
            check(tc_menu_click(&m, x, y) && tc_menu_take_command(&m) == TC_MC_TONIC_VIEWER, "a click takes it");
        } else check(0, "the main menu's choices can be hit");
        tc_menu_show(&m, TC_MS_OPTIONS);
        m.selected[TC_MS_OPTIONS] = 0;
        tc_menu_build(&m, 16.0f / 9);
        found = -1;
        for (h = 0; h < (int)m.hit_count; h++)
            if (m.hits[h].item == 2 && m.hits[h].part < 0) found = h;
        if (found >= 0) {
            bool was = m.options.dolby;
            tc_menu_click(&m, 0.5f * (m.hits[found].rect[0] + m.hits[found].rect[2]), 0.5f * (m.hits[found].rect[1] + m.hits[found].rect[3]));
            check(m.options.dolby != was && tc_menu_take_command(&m) == TC_MC_OPTIONS, "a click on a switch's value flips it");
            m.options.dolby = was;
        } else check(0, "an option's value can be hit");
        /* a slider (MUSIC VOLUME, the first row): a click on its bar puts the value there, and it follows the pointer
         * while held; a click elsewhere lets go */
        found = -1;
        for (h = 0; h < (int)m.hit_count; h++)
            if (m.hits[h].item == 0 && m.hits[h].part == 2) found = h;
        if (found >= 0) {
            const float *r = m.hits[found].rect;
            float y = 0.5f * (r[1] + r[3]);
            tc_menu_click(&m, r[0] + 0.001f, y);
            check(m.options.music_volume == 0 && tc_menu_take_command(&m) == TC_MC_OPTIONS, "a click at a slider's start: 0");
            check(tc_menu_drag(&m, 0.5f * (r[0] + r[2]), 0.9f) && fabsf(m.options.music_volume - 0.5f) < 1e-4f,
                  "held, it follows the pointer: the middle is 50");
            check(tc_menu_drag(&m, 2.0f, y) && m.options.music_volume == 1, "past its end: 100");
            check(!tc_menu_drag(&m, 2.0f, y), "no change, no command");
            tc_menu_take_command(&m);
            tc_menu_click(&m, 0.02f, 0.98f);
            tc_menu_show(&m, TC_MS_OPTIONS);
            check(!tc_menu_drag(&m, r[0], y) && m.options.music_volume == 1, "let go, it stays");
        } else check(0, "a slider's bar can be hit");
        /* the list scrolls: the wheel moves it and takes the selection along; the keys bring the selection into view */
        m.selected[TC_MS_OPTIONS] = 0;
        tc_menu_scroll(&m, 2);
        check(m.options_first == 2 && m.selected[TC_MS_OPTIONS] == 2, "the wheel scrolls OPTIONS, the selection kept in view");
        tc_menu_scroll(&m, 50);
        check(m.options_first == 3, "it stops at the list's end (11 options, 8 in view)");
        tc_menu_input_press(&m, TC_MI_UP);
        tc_menu_input_press(&m, TC_MI_UP);
        check(m.selected[TC_MS_OPTIONS] == 1 && m.options_first == 1, "moving up past the view scrolls it");
        tc_menu_input_press(&m, TC_MI_UP);
        tc_menu_input_press(&m, TC_MI_UP);
        check(!strcmp(label(&m, TC_MS_OPTIONS, m.selected[TC_MS_OPTIONS]), "BACK") && m.options_first == 3,
              "up from the first: BACK, the list at its end");
        tc_menu_build(&m, 4.0f / 3);
        found = 0;
        for (h = 0; h < (int)m.hit_count; h++) found += m.hits[h].rect[0] < 0 || m.hits[h].rect[2] > 1;
        check(found == 0 && m.quad_count > 0, "at 4:3 every hit box is on the screen");
        tc_menu_show(&m, TC_MS_MAIN);
    }

    /* options */
    tc_options_defaults(&o);
    tc_menu_set_options(&m, &o);
    tc_menu_show(&m, TC_MS_OPTIONS);
    /* MUSIC VOLUME, SOUND VOLUME, DOLBY SURROUND, FIELD OF VIEW, GAMMA, LIGHTING, FOG, HUD AUTO-HIDE, LOOK SPEED,
     * INVERT LOOK, ANIMATION SPEED, BACK */
    check(tc_menu_item_count(&m, TC_MS_OPTIONS) == 12 && !strcmp(label(&m, TC_MS_OPTIONS, 0), "MUSIC VOLUME 100") &&
              !strcmp(label(&m, TC_MS_OPTIONS, 3), "FIELD OF VIEW 86") && !strcmp(label(&m, TC_MS_OPTIONS, 11), "BACK"),
          "OPTIONS: the sound first, the field of view at the game's 86, BACK last");
    m.selected[TC_MS_OPTIONS] = 0;
    tc_menu_input_press(&m, TC_MI_LEFT);
    tc_menu_input_press(&m, TC_MI_LEFT);
    check(tc_menu_take_command(&m) == TC_MC_OPTIONS && fabsf(m.options.music_volume - 0.8f) < 1e-4f, "MUSIC VOLUME steps by 10");
    tc_menu_input_press(&m, TC_MI_DOWN);
    for (s = 0; s < 14; s++) tc_menu_input_press(&m, TC_MI_LEFT);
    check(m.options.sound_volume == 0 && !strcmp(label(&m, TC_MS_OPTIONS, 1), "SOUND VOLUME 0"), "SOUND VOLUME stops at 0");
    m.selected[TC_MS_OPTIONS] = 3;
    tc_menu_input_press(&m, TC_MI_RIGHT);
    check(m.options.fov == 91, "FIELD OF VIEW steps by 5 degrees, as , .");
    for (s = 0; s < 10; s++) tc_menu_input_press(&m, TC_MI_RIGHT);
    check(m.options.fov == 120, "and stops at 120");
    m.selected[TC_MS_OPTIONS] = 4;
    tc_menu_input_press(&m, TC_MI_RIGHT); /* gamma up */
    check(tc_menu_take_command(&m) == TC_MC_OPTIONS && fabsf(m.options.gamma - 1.1f) < 1e-4f, "GAMMA steps x1.1 and says so");
    m.selected[TC_MS_OPTIONS] = 10;
    tc_menu_input_press(&m, TC_MI_LEFT);
    check(fabsf(m.options.animation_speed - 0.75f) < 1e-4f, "ANIMATION SPEED steps as [ ]");
    m.selected[TC_MS_OPTIONS] = 6;
    tc_menu_input_press(&m, TC_MI_CONFIRM);
    check(!m.options.fog && !strcmp(label(&m, TC_MS_OPTIONS, 6), "FOG OFF"), "FOG toggles (\"FOG OFF\")");
    o = m.options;
    o.dolby = true;
    o.music_volume = 0.35f;
    o.look_sensitivity = 1.5625f;
    tc_options_write(&o, text, sizeof text);
    tc_options_parse(&back, text, strlen(text));
    check(fabsf(back.gamma - o.gamma) < 1e-3f && back.animation_speed == o.animation_speed && back.fog == o.fog &&
              back.dolby && fabsf(back.look_sensitivity - o.look_sensitivity) < 1e-3f &&
              fabsf(back.music_volume - 0.35f) < 1e-3f && back.sound_volume == 0 && back.fov == 120,
          "the options' file round trip");
    {
        const char *bad = "gamma=9\nanimation_speed=0.6\nnonsense\nfog=0\nmusic_volume=7\nfov=10\n";
        tc_options_parse(&back, bad, strlen(bad));
    }
    check(back.gamma == 2.5f && fabsf(back.animation_speed - 2.0f / 3) < 1e-4f && !back.fog && back.look_sensitivity == 1 &&
              back.music_volume == 1 && back.sound_volume == 1 && back.fov == 50,
          "bad values are brought in range, unknown lines passed by");

    /* layout */
    ok = 1;
    for (a = 0; a < 2; a++)
        for (s = 0; s < TC_MS_COUNT; s++) {
            int k;
            for (k = 0; k < tc_menu_item_count(&m, (tc_menu_screen)s) || k == 0; k++) {
                tc_menu_show(&m, (tc_menu_screen)s);
                if (s == TC_MS_NEW_GAME) snprintf(m.name, sizeof m.name, "WWWWWWWWWWWWWW");
                m.selected[s] = k;
                tc_menu_build(&m, aspects[a]);
                if (m.quad_count < 3 || !on_screen(&m)) {
                    printf("        screen %d, choice %d, aspect %.2f: %u quads\n", s, k, aspects[a], m.quad_count);
                    ok = 0;
                }
            }
        }
    check(ok, "every screen's letters are on the screen (4:3, 16:9), with the longest name too");
    {
        float before[2], after[2];
        tc_menu_show(&m, TC_MS_MAIN);
        tc_menu_build(&m, 4.0f / 3);
        before[0] = m.layers[0].uv[0][0], before[1] = m.layers[1].uv[0][0];
        tc_menu_update(&m, 2.0f / 60);
        tc_menu_update(&m, 0.4f);
        tc_menu_build(&m, 4.0f / 3);
        after[0] = m.layers[0].uv[0][0], after[1] = m.layers[1].uv[0][0];
        check(m.layer_count == 2 && before[0] != after[0] && before[1] != after[1], "both backgrounds turn over time");
    }
    if (g_prefix) {
        static const char *names[] = {"main", "start", "new", "new_erase", "load", "delete", "delete_erase", "options", "game"};
        static const char *levels[] = {"sud0", "Totalski", "cock01", "carota", "LeCanyon", "north", "marmite", "Land", "bar"};
        tc_menu_game many[9];
        memset(many, 0, sizeof many);
        for (s = 0; s < 9; s++) {
            snprintf(many[s].name, sizeof many[s].name, s == 2 ? "WWWWWWWWWWWWWW" : "AGENT ED %d", s + 1);
            snprintf(many[s].level, sizeof many[s].level, "%s", levels[s]);
        }
        tc_menu_set_games(&m, many, 9);
        m.selected[TC_MS_LOAD] = 7;
        tc_menu_set_options(&m, &o);
        for (s = 0; s < TC_MS_COUNT; s++) {
            char out[600];
            tc_menu_show(&m, (tc_menu_screen)s);
            if (s == TC_MS_NEW_GAME || s == TC_MS_NEW_ERASE) snprintf(m.name, sizeof m.name, "AGENT ED");
            m.erase_from = TC_MS_LOAD;
            if (s != TC_MS_LOAD) m.selected[s] = s == TC_MS_OPTIONS ? 2 : 0;
            m.time = 0.3f;
            tc_menu_build(&m, 16.0f / 9);
            snprintf(out, sizeof out, "%s%s.ppm", g_prefix, names[s]);
            picture(&m, out, 1024, 576);
        }
        printf("  pictures: %s*.ppm\n", g_prefix);
    }
    tc_menu_free(&m);
}

/* ---- pictures, to look at ---- */

static void sample(const uint8_t *px, int w, int h, float u, float v, bool wrap, float out[4]) {
    float x = u * (float)w - 0.5f, y = v * (float)h - 0.5f, fx, fy;
    int x0 = (int)floorf(x), y0 = (int)floorf(y), k, dx, dy;
    fx = x - (float)x0, fy = y - (float)y0;
    for (k = 0; k < 4; k++) out[k] = 0;
    for (dy = 0; dy < 2; dy++)
        for (dx = 0; dx < 2; dx++) {
            int xx = x0 + dx, yy = y0 + dy;
            float wt = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            if (wrap) xx = ((xx % w) + w) % w, yy = ((yy % h) + h) % h;
            else xx = xx < 0 ? 0 : xx >= w ? w - 1 : xx, yy = yy < 0 ? 0 : yy >= h ? h - 1 : yy;
            for (k = 0; k < 4; k++) out[k] += wt * px[4 * (yy * w + xx) + k];
        }
}

static void picture(const tc_menu *m, const char *path, int W, int H) {
    float *img = (float *)calloc((size_t)W * H * 3, sizeof(float));
    FILE *f;
    uint32_t i;
    int x, y;
    for (i = 0; i < m->layer_count; i++) {
        const tc_menu_layer *l = &m->layers[i];
        const tc_menu_image *im;
        if (l->image == TC_MENU_IMAGE_DIM) { /* over a grey stand-in for the game */
            for (x = 0; x < W * H * 3; x++) img[x] = 140 * (1 - l->alpha);
            continue;
        }
        if (l->image >= m->data.image_count) continue; /* the swirl: the frontend's own picture */
        im = &m->data.images[l->image];
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                float s = ((float)x + 0.5f) / W, t = ((float)y + 0.5f) / H, c[4], a;
                float u = (1 - s) * (1 - t) * l->uv[0][0] + s * (1 - t) * l->uv[1][0] + (1 - s) * t * l->uv[2][0] + s * t * l->uv[3][0];
                float v = (1 - s) * (1 - t) * l->uv[0][1] + s * (1 - t) * l->uv[1][1] + (1 - s) * t * l->uv[2][1] + s * t * l->uv[3][1];
                float *p = img + 3 * (y * W + x);
                sample(im->rgba, im->width, im->height, u, v, true, c);
                a = i == 0 ? 1 : c[3] / 255 * l->alpha;
                p[0] = p[0] * (1 - a) + c[0] * a, p[1] = p[1] * (1 - a) + c[1] * a, p[2] = p[2] * (1 - a) + c[2] * a;
            }
    }
    for (i = 0; i < m->quad_count; i++) {
        const tc_menu_quad *q = &m->quads[i];
        int x0 = (int)(q->rect[0] * W), x1 = (int)(q->rect[2] * W), y0 = (int)(q->rect[1] * H), y1 = (int)(q->rect[3] * H);
        for (y = y0 < 0 ? 0 : y0; y < y1 && y < H; y++)
            for (x = x0 < 0 ? 0 : x0; x < x1 && x < W; x++) {
                float s = ((float)x + 0.5f - q->rect[0] * W) / ((q->rect[2] - q->rect[0]) * W);
                float t = ((float)y + 0.5f - q->rect[1] * H) / ((q->rect[3] - q->rect[1]) * H), c[4], a;
                float *p = img + 3 * (y * W + x);
                sample(m->data.atlas, m->data.atlas_width, m->data.atlas_height, q->uv[0] + s * (q->uv[2] - q->uv[0]),
                       q->uv[1] + t * (q->uv[3] - q->uv[1]), false, c);
                a = c[3] / 255 * q->color[3];
                p[0] = p[0] * (1 - a) + c[0] * q->color[0] * a;
                p[1] = p[1] * (1 - a) + c[1] * q->color[1] * a;
                p[2] = p[2] * (1 - a) + c[2] * q->color[2] * a;
            }
    }
    if ((f = fopen(path, "wb"))) {
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (x = 0; x < W * H * 3; x++) fputc(img[x] < 0 ? 0 : img[x] > 255 ? 255 : (int)img[x], f);
        fclose(f);
    }
    free(img);
}

static tc_level level;
static uint8_t *ai;
static size_t ai_size;

static bool make(tc_viewer *v, const tc_viewer_carry *carry) {
    tc_viewer_desc d;
    memset(&d, 0, sizeof d);
    d.level = &level;
    d.ai_data = ai;
    d.ai_size = ai_size;
    d.level_name = "sud0";
    d.carry = carry;
    if (tc_viewer_init(v, &d) != TC_OK) return false;
    if (!v->playing && tc_viewer_can_play(v)) tc_viewer_toggle_play(v);
    return true;
}

static void frames(tc_viewer *v, int n) {
    uint32_t count, opaque;
    while (n-- > 0) {
        tc_viewer_update(v, 1.0f / 60);
        tc_viewer_draws(v, &count, &opaque);
    }
}

static void save_checks(const char *dir) {
    char path[512];
    size_t size, saved_size;
    uint8_t *data, *saved;
    tc_viewer a, b;
    tc_cast *ca;
    const tc_cast *cb;
    tc_save_info info;
    tc_save_game game;
    tc_vec3 spot;
    uint32_t table;
    char file[32];

    printf("saved games (sud0)\n");
    snprintf(path, sizeof path, "%s/sud0.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&level, data, size) != TC_OK) {
        check(0, "sud0.ttlv loads");
        return;
    }
    snprintf(path, sizeof path, "%s/sud0.ttai", dir);
    ai = read_file(path, &ai_size);
    if (!make(&a, NULL) || !(ca = (tc_cast *)tc_viewer_cast(&a))) {
        check(0, "sud0 plays with its cast");
        return;
    }
    frames(&a, 120);
    tc_viewer_set_view(&a, 1.3f, 0.5f, false, 1);
    check(fabsf(a.gamma - 1.3f) < 1e-5f && a.animation_speed == 0.5f && !a.fog_enabled &&
              (!level.has_lighting || a.lighting == TC_VL_PORT),
          "tc_viewer_set_view sets gamma, speed, fog, lighting");
    check(tc_viewer_fov(&a) == 86, "the field of view starts at the game's 86");
    tc_viewer_set_fov(&a, 100);
    check(tc_viewer_fov(&a) == 100, "tc_viewer_set_fov sets it (the menus' FIELD OF VIEW)");
    tc_viewer_step_fov(&a, 1);
    check(tc_viewer_fov(&a) == 105, "and , . step on from there");
    tc_viewer_set_fov(&a, 300);
    check(tc_viewer_fov(&a) == 120, "held in 50 - 120");
    tc_viewer_set_fov(&a, 86);
    tc_viewer_set_view(&a, 1, 1, true, 0);
    check(tc_viewer_save_serial(&a) == 0, "no save asked for at the start");
    {   /* the game menu over the game */
        tc_vec3 ed = tc_viewer_driver(&a)->player->position, eye0, eye1;
        float zoom = a.orbit_distance, clock;
        tc_vec3 paused_eye = a.camera.position;
        float paused_yaw = a.camera.yaw, paused_pitch = a.camera.pitch, moved;
        tc_viewer_set_game_menu(&a, true);
        clock = a.animation_time;
        check(a.paused && a.game_menu && a.orbit_distance == zoom, "the game menu holds the game, the zoom as it was");
        frames(&a, 1);
        moved = tc_v3_length(tc_v3_sub(a.camera.position, paused_eye));
        printf("  the game menu's first frame: the eye %.4f from where the game was paused, yaw %+.5f, pitch %+.5f\n",
               (double)moved, (double)(a.camera.yaw - paused_yaw), (double)(a.camera.pitch - paused_pitch));
        check(moved < 0.01f && fabsf(a.camera.yaw - paused_yaw) < 0.002f && fabsf(a.camera.pitch - paused_pitch) < 0.002f,
              "its first frame is the view the game was paused on");
        frames(&a, 29);
        eye0 = a.camera.position;
        frames(&a, 120);
        eye1 = a.camera.position;
        check(tc_v3_length(tc_v3_sub(eye1, eye0)) > 0.05f && tc_v3_length(tc_v3_sub(tc_viewer_driver(&a)->player->position, ed)) < 1e-4f &&
                  a.animation_time == clock,
              "behind it the camera drifts round Ed, who stays, the clock stopped");
        tc_viewer_set_game_menu(&a, false);
        check(!a.paused && !a.game_menu && a.orbit_distance == zoom, "closed: the game and the zoom as they were");
    }
    ca->globals.powers = 0x0123;
    ca->globals.lives = 7;
    ca->globals.values[0] = 42;
    ca->globals.values[12] = -3;
    spot = tc_v3_add(tc_viewer_driver(&a)->player->position, tc_v3(1.5f, -0.5f, 0.25f));
    ca->globals.has_checkpoint = true;
    ca->globals.checkpoint = spot;
    ca->globals.checkpoint_heading = 1.25f;
    ((tc_ed_ai *)tc_viewer_driver(&a))->ai.hit_points = 2;
    ((tc_ed_ai *)tc_viewer_driver(&a))->ai.hit_points_max = 6;
    saved = tc_save_make(&a, "ED 2", 1763164800ull, &saved_size);
    table = tc_cast_level_saves_count();
    check(saved != NULL && saved_size > 64, "a save is made");
    check(table > 0, "the level-saved table has sud0's actors in it");
    tc_viewer_free(&a);
    if (!saved) return;
    check(tc_save_read_info(saved, saved_size, &info) && !strcmp(info.name, "ED 2") && !strcmp(info.level, "sud0") &&
              info.edition == 0 && info.time == 1763164800ull && info.lives == 7 && info.powers == 0x0123,
          "its info: name, level, edition, time, lives, powers");
    tc_cast_level_saves_clear();
    check(tc_save_read(saved, saved_size, &game) == TC_OK && tc_cast_level_saves_count() == table,
          "read back: the level-saved table is put back whole");
    check(!strcmp(game.carry.level, "sud0") && game.carry.has_progress && game.carry.has_checkpoint && game.carry.dsg_size > 0,
          "its carry: the level, the progress, the checkpoint, Ed's variables");
    if (!make(&b, &game.carry) || !(cb = tc_viewer_cast(&b))) {
        check(0, "a viewer from the save");
        free(saved);
        return;
    }
    {
        tc_vec3 at = tc_viewer_driver(&b)->player->position;
        check(cb->globals.powers == 0x0123 && cb->globals.lives == 7 && cb->globals.values[0] == 42 &&
                  cb->globals.values[12] == -3,
              "the new level's progress is the saved one");
        check(cb->globals.has_checkpoint && tc_v3_length(tc_v3_sub(at, spot)) < 1e-3f &&
                  tc_v3_length(tc_v3_sub(cb->globals.checkpoint, spot)) < 1e-3f,
              "Ed is at the checkpoint, and it is the level's checkpoint again");
        check(tc_viewer_driver(&b)->ai.hit_points == 2 && tc_viewer_driver(&b)->ai.hit_points_max == 6,
              "his life as saved (2 of 6)");
    }
    frames(&b, 60);
    check(tc_viewer_driver(&b)->ai.hit_points > 0, "and he plays on");
    tc_viewer_free(&b);
    free(saved);
    tc_save_file_name("ed 2/x", file, sizeof file);
    check(!strcmp(file, "ED_2_X"), "a game's file name (\"ed 2/x\" -> ED_2_X)");
    tc_level_free(&level);
    free(data);
    free(ai);
}

/* Totalski: opened from the levels grid (no carry) with every power, as any level; the main menu's NEW GAME (its carry)
 * a new game. */
static void new_game_checks(const char *dir) {
    char path[512];
    size_t size, ai_size;
    uint8_t *data, *ai_bytes;
    tc_level lv;
    printf("Totalski: Tonic Viewer and a new game\n");
    snprintf(path, sizeof path, "%s/Totalski.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&lv, data, size) != TC_OK) {
        printf("  info  no Totalski: skipped\n");
        free(data);
        return;
    }
    snprintf(path, sizeof path, "%s/Totalski.ttai", dir);
    ai_bytes = read_file(path, &ai_size);
    for (int fresh = 0; fresh < 2; fresh++) {
        tc_viewer v;
        tc_viewer_desc d;
        tc_viewer_carry carry;
        tc_cast_level_value lvv;
        const tc_cast *c;
        memset(&d, 0, sizeof d);
        memset(&lvv, 0, sizeof lvv);
        lvv.size = 1;
        tc_cast_level_saves_put(&lvv); /* something left from an earlier run */
        if (fresh) tc_save_start_new_game(&carry, "Totalski");
        d.level = &lv;
        d.ai_data = ai_bytes;
        d.ai_size = ai_size;
        d.level_name = "Totalski";
        d.carry = fresh ? &carry : NULL;
        if (tc_viewer_init(&v, &d) != TC_OK || !(c = tc_viewer_cast(&v))) { check(0, "Totalski plays"); break; }
        if (!fresh) check(c->globals.powers == TC_AI_POWERS_ALL, "opened from the levels grid: every power");
        else check(c->globals.powers == 0 && c->globals.values[1] == -1 && c->globals.lives == 3 && tc_cast_level_saves_count() == 0,
                   "NEW GAME's carry: no powers, a new game's save values, three lives, no level-saved values left");
        tc_viewer_free(&v);
    }
    free(ai_bytes);
    tc_level_free(&lv);
    free(data);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    g_prefix = argc > 2 ? argv[2] : NULL;
    menu_checks(dir);
    save_checks(dir);
    new_game_checks(dir);
    printf("%s\n", failures ? "menucheck: FAILED" : "menucheck: ok");
    return failures ? 1 : 0;
}

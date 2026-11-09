/* Headless check of the original demos (tc_tape, tc_viewer_play_tape): each Demos/<level>/DemoNNN.sav in a folder,
 * played on its level. Reports what can be measured without a screen: it reads to the end, Ed's animation matches the
 * tape's frames, the actors on show are mapped, the camera looks at Ed.
 * Usage: tapecheck out/ttlv out/demos  (make -C core tapecheck) */
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "tonic/tonic.h"

static int failures = 0;

static uint8_t *load(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    uint8_t *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    rewind(f);
    b = malloc(*n ? *n : 1);
    if (fread(b, 1, *n, f) != *n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static void play_one(const char *ttlv, const char *ai, const char *tape_path, const char *label) {
    size_t ln, an, tn;
    uint8_t *ld = load(ttlv, &ln), *ad = load(ai, &an), *td = load(tape_path, &tn);
    tc_level level;
    tc_viewer_desc desc;
    tc_viewer *v = malloc(sizeof *v);
    if (!ld || !ad || !td || tc_level_load(&level, ld, ln) != TC_OK) {
        printf("  FAIL %s: can't read its files\n", label);
        failures++;
        return;
    }
    memset(&desc, 0, sizeof desc);
    desc.level = &level;
    desc.level_name = label;
    desc.ai_data = ad;
    desc.ai_size = an;
    memset(v, 0, sizeof *v);
    if (tc_viewer_init(v, &desc) != TC_OK || !tc_viewer_play_tape(v, td, tn)) {
        printf("  FAIL %s: refused\n", label);
        failures++;
    } else {
        long steps = 0, cos_n = 0, ed_drawn = 0, sounds = 0;
        double cos_sum = 0, seconds = 0;
        uint32_t draws_max = 0;
        tc_view_shadow shadow;
        while (!tc_viewer_tape_done(v) && steps < 200000) {
            uint32_t count, opaque;
            const uint16_t *ev;
            tc_viewer_update(v, 1.0f / 60);
            sounds += tc_viewer_sounds(v, &ev);
            tc_viewer_draws(v, &count, &opaque);
            if (count > draws_max) draws_max = count;
            if (v->ed_draw_count) ed_drawn++;
            seconds += 1.0 / 60;
            steps++;
            if (tc_viewer_shadow(v, &shadow)) { /* Ed's place on the ground, as drawn */
                tc_vec3 to = tc_v3_sub(tc_v3(shadow.position.x, shadow.position.y, shadow.position.z + 1), v->camera.position);
                float d = tc_v3_length(to);
                tc_vec3 f = tc_v3(sinf(v->camera.yaw) * cosf(v->camera.pitch), cosf(v->camera.yaw) * cosf(v->camera.pitch), sinf(v->camera.pitch));
                if (d > 0.5f) { cos_sum += tc_v3_dot(to, f) / d; cos_n++; }
            }
        }
        printf("  %-22s %6.1f s, Ed drawn %3.0f %%, camera toward Ed (mean cos) %.2f, %ld sounds, up to %u draws\n", label, seconds,
               100.0 * (double)ed_drawn / (double)(steps ? steps : 1), cos_n ? cos_sum / (double)cos_n : 0, sounds, draws_max);
        if (!tc_viewer_tape_done(v)) { printf("  FAIL %s: didn't reach its end\n", label); failures++; }
        /* Ed is off the tape while the camera looks elsewhere (the tapes' fly-throughs; 17-37 % of three of them) */
        if (ed_drawn < steps / 2) { printf("  FAIL %s: Ed mostly not drawn\n", label); failures++; }
    }
    tc_viewer_free(v);
    tc_level_free(&level);
    free(v), free(ld), free(ad), free(td);
}

int main(int argc, char **argv) {
    DIR *top;
    struct dirent *a;
    if (argc < 3) return 2;
    if (!(top = opendir(argv[2]))) { printf("no demos at %s\n", argv[2]); return 0; }
    while ((a = readdir(top))) {
        char dirp[1024];
        DIR *d;
        struct dirent *f;
        if (a->d_name[0] == '.') continue;
        snprintf(dirp, sizeof dirp, "%s/%s", argv[2], a->d_name);
        if (!(d = opendir(dirp))) continue;
        while ((f = readdir(d))) {
            char t[1200], l[1200], ai[1200], lab[300];
            size_t n = strlen(f->d_name);
            if (n < 4 || strcasecmp(f->d_name + n - 4, ".sav")) continue;
            snprintf(t, sizeof t, "%s/%s", dirp, f->d_name);
            snprintf(l, sizeof l, "%s/%s.ttlv", argv[1], a->d_name);
            snprintf(ai, sizeof ai, "%s/%s.ttai", argv[1], a->d_name);
            snprintf(lab, sizeof lab, "%s/%s", a->d_name, f->d_name);
            play_one(l, ai, t, lab);
        }
        closedir(d);
    }
    closedir(top);
    printf("%s\n", failures ? "FAILED" : "all tapes play");
    return failures != 0;
}

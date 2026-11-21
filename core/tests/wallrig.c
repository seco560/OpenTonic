/* Ed stands with a wall right behind his head (the follow's line blocked within 1), no input: frames with the eye in
 * him (hide_target) from 0.5 s to 2.5 s, over spots on every sector's floor. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tonic/tonic.h"
#include "tonic/tc_collision.h"
#include "tonic/tc_sector.h"
static uint8_t *rd(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *d = malloc(*n); fread(d, 1, *n, f); fclose(f); return d; }
int main(int argc, char **argv) {
    for (int a = 2; a < argc; a++) {
        char path[512]; size_t n, an; static tc_level L; static tc_viewer v; tc_viewer_desc desc; tc_collision col;
        memset(&L, 0, sizeof L);
        snprintf(path, sizeof path, "%s/%s.ttlv", argv[1], argv[a]); uint8_t *d = rd(path, &n);
        if (!d || tc_level_load(&L, d, n) != TC_OK) continue;
        snprintf(path, sizeof path, "%s/%s.ttai", argv[1], argv[a]); uint8_t *ai = rd(path, &an);
        memset(&desc, 0, sizeof desc); desc.level = &L; desc.ai_data = ai; desc.ai_size = an; desc.level_name = argv[a];
        if (tc_viewer_init(&v, &desc) != TC_OK) continue;
        tc_collision_init(&col, L.collision, 3 * L.collision_triangle_count, L.collision_floors);
        tc_collision_set_sectors(&col, L.sectors.collision_sector, L.sectors.collision_count, L.sectors.count);
        col.solid = tc_sectors_camera_solid(&L.sectors);
        for (int f = 0; f < 60; f++) { uint32_t c0, c1; tc_viewer_update(&v, 1.0f / 60); tc_viewer_draws(&v, &c0, &c1); }
        int spots = 0, hidden = 0, stuck = 0, total = 0;
        tc_sectors *s = &L.sectors;
        for (uint32_t i = 0; i < s->count && spots < 80; i++) {
            if (s->sectors[i].is_virtual) continue;
            uint32_t step = s->sectors[i].floor_count / 6 + 1;
            for (uint32_t k = 0; k < s->sectors[i].floor_count && spots < 80; k += step) {
                tc_vec3 *fv = &s->sectors[i].floors[4 * k];
                tc_vec3 p = tc_v3((fv[0].x + fv[1].x + fv[2].x) / 3, (fv[0].y + fv[1].y + fv[2].y) / 3, (fv[0].z + fv[1].z + fv[2].z) / 3);
                for (int y = 0; y < 8; y++) {
                    float h = y * 3.14159265f / 4; /* Ed's front (sin h, -cos h): behind him (-sin h, cos h) */
                    tc_vec3 head = tc_v3_add(p, tc_v3(0, 0, 1.35f)), back = tc_v3_normalize(tc_v3(-sinf(h) * 0.9f, cosf(h) * 0.9f, 0.44f));
                    tc_hit hit;
                    if (!tc_collision_raycast(&col, head, back, 1.0f, &hit)) continue;
                    if (tc_collision_raycast(&col, tc_v3_add(p, tc_v3(0, 0, 3)), tc_v3(0, 0, -1), 3.5f, &hit) == 0) continue;
                    tc_player_teleport((tc_player *)tc_viewer_player(&v), tc_v3_add(p, tc_v3(0, 0, 0.05f)), h);
                    tc_viewer_release_all(&v);
                    int hid = 0, last = 0, fr;
                    for (fr = 0; fr < 150; fr++) {
                        uint32_t c0, c1;
                        tc_viewer_update(&v, 1.0f / 60);
                        tc_viewer_draws(&v, &c0, &c1);
                        if (fr >= 30) { hid += v.cam.hide_target; total++; }
                        if (getenv("TRACE_SPOT") && atoi(getenv("TRACE_SPOT")) == spots && fr % 3 == 0)
                            printf("    f%3d reach %.2f hide %d swing %d to %+.2f yaw %+.2f climb %.2f pull %.2f rest %.2f watch %.2f dist %.2f since %.2f sit %d shot %d\n", fr, v.cam.reach, v.cam.hide_target, v.cam.swinging, v.cam.swing_yaw, v.camera.yaw, v.cam.climb, v.cam.pull, v.cam.swing_rest, v.cam.watch, v.cam.distance, v.cam.since_look, tc_viewer_camera_situation(&v), tc_viewer_camera_shot(&v));
                        last = v.cam.hide_target;
                    }
                    if (last && getenv("SURVEY")) { /* where Ed is now: the static geometry's clearance round his head */
                        tc_vec3 ed = tc_viewer_player(&v)->position, head = tc_v3_add(ed, tc_v3(0, 0, 1.35f));
                        float best = 0; int open = 0;
                        printf("  stuck #%d Ed at (%.2f %.2f %.2f) state %u ground %d:", spots, ed.x, ed.y, ed.z, v.game->main.driver.ai.state, (v.game->main.driver.ai.contacts & 1) != 0);
                        for (int e = 0; e < 4; e++) for (int q = 0; q < 16; q++) {
                            float el = -0.2f + e * 0.45f, yw = q * 3.14159265f / 8; tc_hit hh;
                            tc_vec3 dd = tc_v3(sinf(yw) * cosf(el), cosf(yw) * cosf(el), sinf(el));
                            float cl = tc_collision_raycast(&col, head, dd, 7, &hh) ? hh.distance : 7;
                            best = fmaxf(best, cl); open += cl >= 2;
                            if (cl >= 2 && getenv("SURVEY_DIRS")) printf(" [el %.2f yaw %.2f: %.1f]", el, yw, cl);
                        }
                        printf(" best %.2f, %d of 64 open to 2\n", best, open);
                    }
                    spots++; hidden += hid; stuck += last; if (getenv("PER_SPOT") && hid) printf("  spot #%d (%.1f %.1f %.1f) h %d: %d frames in Ed%s\n", spots - 1, p.x, p.y, p.z, y, hid, last ? " (still)" : "");
                    break;
                }
            }
        }
        printf("%-10s spots %3d: eye in Ed %5d of %5d frames (%.1f %%), still in him at the end %d\n", argv[a], spots, hidden, total, total ? 100.0 * hidden / total : 0, stuck);
        tc_viewer_free(&v); tc_collision_free(&col); tc_level_free(&L); free(ai);
    }
    return 0;
}

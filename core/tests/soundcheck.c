/* 3D sound (tc_sound.h; docs/ENGINE.md "3D sound"):
 *   - the exe's helpers on hand-worked cases (their 16.16 integer steps done on paper from the decompiles:
 *     _SND_fn_rPseudoNormeVectorSnd, _SND_fn_ucDistanceToVolume / Ex, _SND_fn_ucPositionToPan, the mixer's gains);
 *   - the levels' SND3 data, where the export has it: sud0's per-perso rolloffs and its muted sectors, as the snapshot
 *     holds them (perso +0x40, sector +0x70; counted by hand with tools/ttre/level.py);
 *   - a cast's sounds heard from a listener: one placed at a perso hears it at full volume, centred; one 200 units away
 *     hears nothing of it (default rolloff: silent from 64); a perso without a sector is not heard; a perso's own sound
 *     stops once it is deactivated.
 * Usage: soundcheck [out/ttlv]. Test-only stdio. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tonic/tonic.h"

static int failures;

static void expect_int(const char *what, long got, long want) {
    if (got == want) return;
    printf("  FAIL  %s: %ld, expected %ld\n", what, got, want);
    failures++;
}

static void expect_near(const char *what, float got, float want) {
    if (fabsf(got - want) < 1e-4f) return;
    printf("  FAIL  %s: %.5f, expected %.5f\n", what, got, want);
    failures++;
}

static void maths(void) {
    tc_sound_rolloff r;
    tc_vec3 right = tc_v3(1, 0, 0);

    /* f(3, 4) = 4 + 1.5, f(5.5, 0) = 5.5 */
    expect_int("distance (3, 4, 0)", tc_sound_distance(tc_v3(3, 4, 0)), 360448);
    /* f(f(1, 2), 2) = f(2.5, 2) = 2.5 + 1 */
    expect_int("distance (1, -2, 2)", tc_sound_distance(tc_v3(1, -2, 2)), 229376);
    /* the default rolloff: 127 below 8; at 36 (64 - 36) / 56 = 0.5 -> 32768 x 127 >> 16 = 63; 0 from 64 */
    expect_int("default, 7.9", tc_sound_volume((int32_t)(7.9 * 65536), NULL), 127);
    expect_int("default, 36", tc_sound_volume(36 * 65536, NULL), 63);
    expect_int("default, 64", tc_sound_volume(64 * 65536, NULL), 0);
    expect_int("default, 65", tc_sound_volume(65 * 65536, NULL), 0);
    /* its own: min 10, max 20; at 50 held at max's: (80 - 20) / 70 = 0.857 -> 56173 -> 108 */
    r.min = 10 * 65536, r.max = 20 * 65536;
    expect_int("min 10 max 20, 5", tc_sound_volume(5 * 65536, &r), 127);
    expect_int("min 10 max 20, 15", tc_sound_volume(15 * 65536, &r), 117); /* (80 - 15) / 70 -> 60855 -> 117 */
    expect_int("min 10 max 20, 50", tc_sound_volume(50 * 65536, &r), 108);
    expect_int("min 10 max 20, 500", tc_sound_volume(500 * 65536, &r), 108);
    /* min 3, max 0 (< min: no floor): at 12 (24 - 12) / 21 -> 37449 -> 72; 0 from 24 */
    r.min = 3 * 65536, r.max = 0;
    expect_int("min 3 max 0, 12", tc_sound_volume(12 * 65536, &r), 72);
    expect_int("min 3 max 0, 30", tc_sound_volume(30 * 65536, &r), 0);
    /* min 30 max 100: beyond 100 held at (240 - 100) / 210 -> 43690 -> 84 */
    r.min = 30 * 65536, r.max = 100 * 65536;
    expect_int("min 30 max 100, 400", tc_sound_volume(400 * 65536, &r), 84);
    r.min = 0, r.max = 0;
    expect_int("min 0 max 0, 1", tc_sound_volume(65536, &r), 0);
    /* pan: right of the listener 127, left 0, ahead 64; (3, 4, 0): 768 x 127 / 2816 + 64 = 98 */
    expect_int("pan right", tc_sound_pan(tc_v3(5, 0, 0), right), 127);
    expect_int("pan left", tc_sound_pan(tc_v3(-5, 0, 0), right), 0);
    expect_int("pan ahead", tc_sound_pan(tc_v3(0, 5, 0), right), 64);
    expect_int("pan (3, 4, 0)", tc_sound_pan(tc_v3(3, 4, 0), right), 98);
    expect_int("pan (-3, 4, 0)", tc_sound_pan(tc_v3(-3, 4, 0), right), 30);
    expect_int("pan at the listener", tc_sound_pan(tc_v3(0, 0, 0), right), 64);
    /* gains (the mixer's words over 65536): v = 127 x 127 >> 7 = 126; centred 126 x 64 x 2 = 16128 on each side; hard
     * left 32256 on the left */
    {
        tc_sound_mix m;
        tc_sound_gains(127, 64, 0, 1, false, &m);
        expect_near("centred, left", m.left, 16128.0f / 65536);
        expect_near("centred, right", m.right, 16128.0f / 65536);
        tc_sound_gains(127, 0, 0, 1, false, &m);
        expect_near("hard left, left", m.left, 32256.0f / 65536);
        expect_near("hard left, right", m.right, 0);
        tc_sound_gains(2, 64, 0, 1, false, &m); /* 127 x 2 >> 7 = 1 < 3: not mixed */
        expect_near("under 3, left", m.left, 0);
        /* Dolby: ahead (0): 126 x 64 x 128 >> 5 = 32256, no rear; behind (127): 126 x 64 x 1 >> 5 = 252, rear 32004 */
        tc_sound_gains(127, 64, 0, 1, true, &m);
        expect_near("dolby ahead, left", m.left, 32256.0f / 65536);
        expect_near("dolby ahead, rear", m.rear, 0);
        tc_sound_gains(127, 64, 127, 1, true, &m);
        expect_near("dolby behind, left", m.left, 252.0f / 65536);
        expect_near("dolby behind, rear", m.rear, 32004.0f / 65536);
    }
    /* the rear value: ahead 0, behind 127, to the side 63; (3, 4, 0) with front +Y: 63 - 1024 x 127 / 2816 = 17 */
    expect_int("dolby ahead", tc_sound_dolby(tc_v3(0, 5, 0), tc_v3(0, 1, 0)), 0);
    expect_int("dolby behind", tc_sound_dolby(tc_v3(0, -5, 0), tc_v3(0, 1, 0)), 127);
    expect_int("dolby side", tc_sound_dolby(tc_v3(5, 0, 0), tc_v3(0, 1, 0)), 63);
    expect_int("dolby (3, 4, 0)", tc_sound_dolby(tc_v3(3, 4, 0), tc_v3(0, 1, 0)), 17);
    /* the pitch spread 0.1 (6553): rand 0 -> (65536 - 6553) / 65536; rand 32767 -> (256 x 51 + 65536 - 6553) / 65536 */
    expect_near("pitch, rand 0", tc_sound_pitch(0.1f, 0), (65536.0f - 6553) / 65536);
    expect_near("pitch, rand max", tc_sound_pitch(0.1f, 32767), (13056.0f + 65536 - 6553) / 65536);
    expect_near("pitch, no spread", tc_sound_pitch(0, 1234), 1);
    /* the encoder's filter: a Hilbert transform's (odd about its middle tap, which is 0), sum |c| just under 32768 */
    {
        int16_t c[32];
        long sum = 0;
        int i, odd = 1;
        tc_sound_dolby_filter(c, 32, 22050);
        for (i = 0; i < 32; i++) sum += c[i] < 0 ? -c[i] : c[i];
        for (i = 1; i < 16; i++) odd &= c[16 + i] == -c[16 - i];
        expect_int("filter's middle tap", c[16], 0);
        expect_int("filter odd about the middle", odd, 1);
        if (sum > 32768 || sum < 32768 - 32) {
            printf("  FAIL  filter's sum |c|: %ld\n", sum);
            failures++;
        }
    }
    printf("  the exe's sound maths: %s\n", failures ? "differ" : "as worked by hand");
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

/* sud0's SND3 and a cast heard from two places. */
static void level_data(const char *dir) {
    char path[1024];
    size_t size;
    uint8_t *data;
    tc_level lv;
    tc_cast cast;
    tc_ai_perso main;
    uint32_t i, muted = 0, lists = 0;
    snprintf(path, sizeof(path), "%s/sud0.ttlv", dir);
    if (!(data = read_file(path, &size)) || tc_level_load(&lv, data, size) != TC_OK) {
        printf("  skipped: %s not there\n", path);
        free(data);
        return;
    }
    if (!lv.sound_rolloff_count) {
        printf("  FAIL  sud0: no SND3 rolloffs (an export from before 2026-11-14: re-export)\n");
        failures++;
    } else {
        for (i = 0; i < lv.sectors.count; i++) {
            muted += lv.sectors.sectors[i].sound_muted_count;
            lists += lv.sectors.sectors[i].sound_muted_count > 0;
        }
        expect_int("sud0 persos with their own rolloff", (long)lv.sound_rolloff_count, 45);
        expect_int("sud0 sectors with a sound list", (long)lists, 7);
        expect_int("sud0 muted entries", (long)muted, 14);
        printf("  sud0: %u persos with their own rolloff, %u sectors muting %u others\n", lv.sound_rolloff_count, lists,
               muted);
    }
    memset(&main, 0, sizeof(main));
    if (tc_cast_init(&cast, &lv, NULL) == TC_OK && cast.count > 0) {
        tc_sound_source s;
        tc_sound_listener l;
        int vol, pan, dolby;
        uint16_t sector;
        memset(&s, 0, sizeof(s));
        memset(&l, 0, sizeof(l));
        s.kind = TC_SOUND_OTHER;
        s.emitter = cast.actors[0].entry->id;
        l.position = cast.actors[0].ai->position;
        l.right = tc_v3(1, 0, 0);
        l.sector = TC_SECTOR_NONE;
        l.valid = true;
        vol = tc_cast_sound_place(&cast, &s, &l, &pan, &dolby);
        expect_int("a perso heard where it is: volume", vol, 127);
        expect_int("a perso heard where it is: pan", pan, 64);
        l.position = tc_v3_add(l.position, tc_v3(200, 0, 0));
        vol = tc_cast_sound_place(&cast, &s, &l, &pan, &dolby);
        expect_int("a perso heard from 200 away: volume", vol, 0);
        expect_int("a perso heard from 200 away: pan (it is to the left)", pan, 0);
        l.position = cast.actors[0].ai->position;
        sector = cast.actors[0].sector.sector;
        cast.actors[0].sector.sector = TC_SECTOR_NONE;
        cast.sectors = &lv.sectors;
        expect_int("a perso without a sector: unheard", tc_cast_sound_place(&cast, &s, &l, &pan, &dolby), 0);
        cast.actors[0].sector.sector = sector;
        cast.actors[0].ai->sound_serial++;
        s.kind = TC_SOUND_OWN;
        expect_int("its own sound after a deactivation: stop", tc_cast_sound_place(&cast, &s, &l, &pan, &dolby), -1);
        tc_cast_free(&cast);
    }
    tc_level_free(&lv);
    free(data);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../files/out/ttlv";
    maths();
    level_data(dir);
    printf(failures ? "FAIL (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}

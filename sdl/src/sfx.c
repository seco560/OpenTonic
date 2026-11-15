#include "sfx.h"
#include <SDL3/SDL.h>
#include "tonic/tc_mixer.h"
#include "util.h"

typedef enum sfx_kind { SFX_NONE, SFX_PLAY, SFX_LOOP, SFX_STOP, SFX_VOICE } sfx_kind;
#define VOICE_STOP 8191 /* the core's TC_AI_VOICE_STOP: a line was dismissed */
#define MIX_RATE 44100  /* the mixer's: the highest among the files (nearly all are 22050); every sound is read to it */

/* A WAV, read once and made mono float at MIX_RATE. Heap-allocated so its address stays put: the mixer's voices read
 * it on the audio thread. */
typedef struct sfx_sound {
    char *file;
    float *samples;
    int count;                   /* 0: could not be read (not retried until the next level) */
} sfx_sound;

typedef struct sfx_event {
    sfx_kind kind;
    char *file;                  /* NULL: no sound */
    float volume;
    int stops;                   /* SFX_STOP: the loop event it ends, -1 none */
    bool follow;                 /* placed again every frame from its emitter */
    float spread;                /* its pitch spread (0: none) */
} sfx_event;

/* One stream to the device, fed by feed() from the core's mixer (tc_mixer.h): the stream's lock guards the mixer. */
static struct {
    SDL_AudioStream *stream;
    bool no_audio;               /* the device could not be opened (tried again at the next level) */
    char *folder;                /* out/audio/sfx */
    sfx_event *events;           /* by event index */
    int event_count;
    sfx_sound **sounds;
    int sound_count, sound_cap;
    sfx_sound *speech;           /* the line being said: read when said (163 of them), freed at the next */
    tc_mixer mixer;
    bool mixer_made;
    bool dolby, regain;          /* regain: the Dolby switch changed, every voice's gains again at the next update */
    bool full_logged;            /* "all voices busy" said for this level */
} sfx;

static void lock(void) { if (sfx.stream) SDL_LockAudioStream(sfx.stream); }
static void unlock(void) { if (sfx.stream) SDL_UnlockAudioStream(sfx.stream); }

static tc_mixer *mixer(void) {
    if (!sfx.mixer_made) {
        tc_mixer_init(&sfx.mixer, MIX_RATE);
        sfx.mixer_made = true;
    }
    return &sfx.mixer;
}

static bool grow(void **p, int *cap, int need, size_t elem) {
    if (need <= *cap) return true;
    int c = *cap ? *cap : 16;
    while (c < need) c *= 2;
    void *n = SDL_realloc(*p, (size_t)c * elem);
    if (!n) return false;
    *p = n;
    *cap = c;
    return true;
}

/* Audio thread (the stream locked): `additional` bytes more of the mix, as stereo floats. Nothing playing: nothing is
 * put (the device plays silence). */
static void SDLCALL feed(void *user, SDL_AudioStream *stream, int additional, int total) {
    float buf[2 * TC_MIXER_BLOCK];
    int frames = (additional + 7) / 8;
    (void)user;
    (void)total;
    while (frames > 0) {
        int n = SDL_min(frames, TC_MIXER_BLOCK);
        if (!tc_mixer_render(&sfx.mixer, buf, n)) return;
        if (!SDL_PutAudioStreamData(stream, buf, n * 2 * (int)sizeof(float))) return;
        frames -= n;
    }
}

static bool open_device(void) {
    if (sfx.stream) return true;
    if (sfx.no_audio) return false;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("Sound effects: no audio (%s)", SDL_GetError());
        sfx.no_audio = true;
        return false;
    }
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, MIX_RATE};
    mixer();
    sfx.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!sfx.stream) {
        SDL_Log("Sound effects: %s", SDL_GetError());
        sfx.no_audio = true;
        return false;
    }
    SDL_ResumeAudioStreamDevice(sfx.stream);
    return true;
}

static void free_sound(sfx_sound *s) {
    if (!s) return;
    SDL_free(s->file);
    SDL_free(s->samples);
    SDL_free(s);
}

static void free_table(void) {
    for (int i = 0; i < sfx.event_count; i++) SDL_free(sfx.events[i].file);
    SDL_free(sfx.events);
    sfx.events = NULL;
    sfx.event_count = 0;
    for (int i = 0; i < sfx.sound_count; i++) free_sound(sfx.sounds[i]);
    SDL_free(sfx.sounds);
    sfx.sounds = NULL;
    sfx.sound_count = sfx.sound_cap = 0;
    SDL_free(sfx.folder);
    sfx.folder = NULL;
}

/* A WAV as mono floats at MIX_RATE (SDL's resampler, once); s->count stays 0 when it can't be read. */
static void read_sound(sfx_sound *s, const char *path) {
    SDL_AudioSpec spec, mono;
    Uint8 *data = NULL, *out = NULL;
    Uint32 size = 0;
    int out_size = 0;
    if (!SDL_LoadWAV(path, &spec, &data, &size)) {
        SDL_Log("Sound effects: cannot open %s: %s", path, SDL_GetError());
        return;
    }
    mono.format = SDL_AUDIO_F32;
    mono.channels = 1;
    mono.freq = MIX_RATE;
    if (SDL_ConvertAudioSamples(&spec, data, (int)size, &mono, &out, &out_size) && out_size >= (int)sizeof(float)) {
        s->samples = (float *)out;
        s->count = out_size / (int)sizeof(float);
    } else {
        SDL_Log("Sound effects: cannot convert %s: %s", path, SDL_GetError());
        SDL_free(out);
    }
    SDL_free(data);
}

/* The event's sound, read on first use; NULL when it has none or it can't be read. */
static const sfx_sound *sound_for(const sfx_event *e) {
    if (!e->file || !sfx.folder) return NULL;
    for (int i = 0; i < sfx.sound_count; i++)
        if (SDL_strcmp(sfx.sounds[i]->file, e->file) == 0) return sfx.sounds[i]->count ? sfx.sounds[i] : NULL;
    if (!grow((void **)&sfx.sounds, &sfx.sound_cap, sfx.sound_count + 1, sizeof *sfx.sounds)) return NULL;
    sfx_sound *s = SDL_calloc(1, sizeof *s);
    if (!s || !(s->file = SDL_strdup(e->file))) { SDL_free(s); return NULL; }
    char *path = path_join(sfx.folder, e->file);
    if (path) read_sound(s, path);
    SDL_free(path);
    sfx.sounds[sfx.sound_count++] = s;  /* failures too, so they aren't retried every time */
    return s->count ? s : NULL;
}

void sfx_load(const char *audio_dir) {
    char *dir = path_join(audio_dir, "sfx");
    if (!dir) return;
    sfx.no_audio = false; /* a device that could not be opened: tried again with each level */
    open_device();
    /* Once per folder, unless its table could not be read then; the sounds that could not be read are tried again. */
    if (sfx.folder && SDL_strcmp(dir, sfx.folder) == 0 && sfx.event_count) {
        for (int i = sfx.sound_count - 1; i >= 0; i--)
            if (!sfx.sounds[i]->count) {
                free_sound(sfx.sounds[i]);
                sfx.sounds[i] = sfx.sounds[--sfx.sound_count];
            }
        SDL_free(dir);
        return;
    }
    sfx_stop_all();
    free_table();
    sfx.folder = dir;
    char *path = path_join(dir, "events.tsv");
    char *table = path ? SDL_LoadFile(path, NULL) : NULL;
    SDL_free(path);
    if (!table) return;
    char *save = NULL;
    for (char *line = SDL_strtok_r(table, "\n", &save); line; line = SDL_strtok_r(NULL, "\n", &save)) {
        if (line[0] == '#') continue;
        size_t len = SDL_strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[len - 1] = 0;
        /* Tab-separated, empty columns kept (an event without a file). */
        char *cols[7] = {NULL};
        int n = 0;
        for (char *c = line; c && n < 7; n++) {
            cols[n] = c;
            c = SDL_strchr(c, '\t');
            if (c) *c++ = 0;
        }
        char *end;
        long k = n >= 4 ? SDL_strtol(cols[0], &end, 10) : -1;
        if (n < 4 || end == cols[0] || k < 0 || k > 65535) continue;
        if (k >= sfx.event_count) {
            int cap = sfx.event_count;
            if (!grow((void **)&sfx.events, &cap, (int)k + 1, sizeof *sfx.events)) break;
            SDL_memset(sfx.events + sfx.event_count, 0, (size_t)(cap - sfx.event_count) * sizeof *sfx.events);
            sfx.event_count = cap;
        }
        sfx_event *e = &sfx.events[k];
        SDL_free(e->file);
        e->kind = SDL_strcmp(cols[1], "play") == 0 ? SFX_PLAY : SDL_strcmp(cols[1], "loop") == 0 ? SFX_LOOP
                : SDL_strcmp(cols[1], "stop") == 0 ? SFX_STOP : SDL_strcmp(cols[1], "voice") == 0 ? SFX_VOICE : SFX_NONE;
        e->file = cols[2][0] ? SDL_strdup(cols[2]) : NULL;
        e->volume = (float)SDL_strtod(cols[3], &end);
        if (end == cols[3]) e->volume = 1;
        e->stops = n > 4 && cols[4][0] ? SDL_atoi(cols[4]) : -1;
        e->follow = n > 5 && SDL_strcmp(cols[5], "1") == 0;
        e->spread = n > 6 ? (float)SDL_strtod(cols[6], NULL) : 0;
    }
    SDL_free(table);
}

/* The line being said ends, and its samples go (no voice reads them from here on). */
static void stop_speech(void) {
    lock();
    tc_mixer_stop_speech(mixer());
    unlock();
    free_sound(sfx.speech);
    sfx.speech = NULL;
}

void sfx_play(const tc_viewer *view) {
    const uint16_t *events;
    uint32_t count = tc_viewer_sounds(view, &events);
    const tc_sound_source *sources = tc_viewer_sound_sources(view);
    tc_mixer *m = mixer();
    for (uint32_t i = 0; i < count; i++) {
        int k = events[i];
        const tc_sound_source *src = &sources[i];
        if (k == VOICE_STOP) { stop_speech(); continue; }
        if (k >= sfx.event_count || sfx.events[k].kind == SFX_NONE) continue;
        const sfx_event *e = &sfx.events[k];
        if (e->kind == SFX_STOP) { /* its loop, from that emitter (any, when the stop has none: a global one) */
            lock();
            tc_mixer_stop_loop(m, e->stops, src);
            unlock();
            continue;
        }
        tc_mixer_voice v = {0};
        tc_sound_mix mix;
        if (!tc_viewer_sound_mix(view, src, e->volume, sfx.dolby, &mix)) continue; /* its perso was already taken out */
        if (e->kind == SFX_VOICE) {
            stop_speech();
            sfx_sound *line = e->file && sfx.folder ? SDL_calloc(1, sizeof *line) : NULL;
            char *path = line ? path_join(sfx.folder, e->file) : NULL;
            if (path) read_sound(line, path);
            SDL_free(path);
            if (!line || !line->count) { free_sound(line); continue; }
            sfx.speech = line;
            v.samples = line->samples, v.count = (uint32_t)line->count, v.speech = true;
        } else {
            const sfx_sound *s = sound_for(e);
            if (!s) continue;
            v.samples = s->samples, v.count = (uint32_t)s->count;
        }
        v.loop = e->kind == SFX_LOOP ? k : -1;
        v.left = mix.left, v.right = mix.right, v.rear = mix.rear;
        v.volume = e->volume, v.follow = e->follow, v.source = *src;
        v.pitch = e->spread != 0 ? tc_sound_pitch(e->spread, (int)SDL_rand(32768)) : 1; /* SND_004048e0's, for its life */
        if (!open_device()) continue; /* no audio: nothing is rendered, so nothing would ever end */
        lock();
        int got = tc_mixer_start(m, &v);
        unlock();
        if (got == TC_MIXER_BUSY && !sfx.full_logged) { /* as the original: not played. Said once a level */
            SDL_Log("Sound effects: all %d voices are busy, event %d not played", TC_MIXER_VOICES, k);
            sfx.full_logged = true;
        }
    }
}

void sfx_update(const tc_viewer *view) {
    bool all = sfx.regain;
    tc_mixer *m = mixer();
    sfx.regain = false;
    lock(); /* a few dozen voices' arithmetic: the audio thread waits microseconds at most */
    m->dolby = sfx.dolby;
    for (int i = 0; i < TC_MIXER_VOICES; i++) {
        tc_mixer_voice *v = &m->voices[i];
        tc_sound_mix mix;
        if (!v->samples || (v->source.kind == TC_SOUND_FLAT && !all)) continue;
        if (!tc_viewer_sound_mix(view, &v->source, v->volume, sfx.dolby, &mix)) { tc_mixer_stop(m, i); continue; }
        if (!v->follow && !all) continue;
        v->left = mix.left, v->right = mix.right, v->rear = mix.rear;
    }
    unlock();
}

void sfx_set_dolby(bool on) {
    sfx.dolby = on;
    sfx.regain = true;
}

bool sfx_dolby(void) { return sfx.dolby; }

void sfx_stop_all(void) {
    lock();
    tc_mixer_stop_all(mixer());
    unlock();
    free_sound(sfx.speech);
    sfx.speech = NULL;
    sfx.full_logged = false;
}

void sfx_set_muted(bool muted) {
    lock();
    mixer()->muted = muted;  /* what is already queued (a few ms) plays out */
    unlock();
}

void sfx_set_volume(float volume) {
    lock();
    mixer()->gain = SDL_clamp(volume, 0.0f, 1.0f);
    unlock();
}

void sfx_quit(void) {
    sfx_stop_all();
    if (sfx.stream) SDL_DestroyAudioStream(sfx.stream); /* also closes its device; no more callbacks */
    sfx.stream = NULL;
    free_table();
}

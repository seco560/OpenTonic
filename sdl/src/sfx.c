#include "sfx.h"
#include <SDL3/SDL.h>
#include "util.h"

typedef enum sfx_kind { SFX_NONE, SFX_PLAY, SFX_LOOP, SFX_STOP, SFX_VOICE } sfx_kind;
#define VOICE_STOP 8191 /* the core's TC_AI_VOICE_STOP: a line was dismissed */
#define SPEECH_VOICE (-2) /* sfx_voice.loop: a character's line */
#define MAX_VOICES 64     /* the original's software mixer's (WAVMWSVR.DLL: 0x40); all busy, a sound isn't played */
#define MAX_TAPS 256      /* the Dolby encoder's filter: the original's 32 taps at 22050 Hz, as long a span at a sound's rate */

/* A WAV, read once and made mono float at its own rate. Heap-allocated so its address stays put: a voice's audio thread
 * callback reads it. */
typedef struct sfx_sound {
    char *file;
    int rate;
    float *samples;
    int count;                   /* 0: could not be read (not retried) */
} sfx_sound;

typedef struct sfx_event {
    sfx_kind kind;
    char *file;                  /* NULL: no sound */
    float volume;
    int stops;                   /* SFX_STOP: the loop event it ends, -1 none */
    bool follow;                 /* placed again every frame from its emitter */
    float spread;                /* its pitch spread (0: none) */
} sfx_event;

/* A voice: a stereo stream bound to the device, fed by feed() from its sound at its gains (the stream's lock guards
 * them). Heap-allocated: the callback holds its address. */
typedef struct sfx_voice {
    SDL_AudioStream *stream;
    const sfx_sound *sound;
    int at;                      /* the next sample */
    bool ended;                  /* a one-shot that has fed its last sample */
    int loop;                    /* the loop event it plays, -1 = a one-shot sound, SPEECH_VOICE */
    float left, right, rear;     /* its gains now */
    float volume;                /* its event's */
    int taps;                    /* Dolby: its encoder's filter, at its sound's rate */
    float coef[MAX_TAPS];
    float history[MAX_TAPS];     /* the rear signal's last taps samples, oldest first */
    bool follow;
    tc_sound_source source;
    sfx_sound *owned;            /* a line's sound, freed with the voice */
} sfx_voice;

static struct {
    SDL_AudioDeviceID device;
    bool no_audio;
    char *folder;                /* out/audio/sfx */
    sfx_event *events;           /* by event index */
    int event_count;
    sfx_sound **sounds;
    int sound_count, sound_cap;
    sfx_voice *voices[MAX_VOICES];
    int voice_count;
    bool muted, dolby, regain;   /* regain: the Dolby switch changed, every voice's gains again at the next update */
} sfx;

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

static bool open_device(void) {
    if (sfx.device) return true;
    if (sfx.no_audio) return false;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("Sound effects: no audio (%s)", SDL_GetError());
        sfx.no_audio = true;
        return false;
    }
    sfx.device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!sfx.device) {
        SDL_Log("Sound effects: %s", SDL_GetError());
        sfx.no_audio = true;
        return false;
    }
    return true;
}

static void free_sound(sfx_sound *s) {
    if (!s) return;
    SDL_free(s->file);
    SDL_free(s->samples);
    SDL_free(s);
}

static void free_voice(int i) {
    sfx_voice *v = sfx.voices[i];
    SDL_DestroyAudioStream(v->stream); /* the callback is done with it once this returns */
    free_sound(v->owned);
    SDL_free(v);
    sfx.voices[i] = sfx.voices[--sfx.voice_count];
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

/* A WAV as mono floats at its own rate; s->count stays 0 when it can't be read. */
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
    mono.freq = spec.freq;
    if (SDL_ConvertAudioSamples(&spec, data, (int)size, &mono, &out, &out_size) && out_size >= (int)sizeof(float)) {
        s->samples = (float *)out;
        s->count = out_size / (int)sizeof(float);
        s->rate = spec.freq;
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

/* Audio thread (the stream locked): `additional` bytes more of the voice, its sound at its gains, as stereo floats. */
static void SDLCALL feed(void *user, SDL_AudioStream *stream, int additional, int total) {
    sfx_voice *v = user;
    float buf[512];
    (void)total;
    while (additional > 0 && !v->ended) {
        int frames = SDL_min((int)(sizeof(buf) / sizeof(buf[0])) / 2, (additional + 7) / 8), n = 0;
        float gl = sfx.muted ? 0 : v->left, gr = sfx.muted ? 0 : v->right, gb = sfx.muted || !sfx.dolby ? 0 : v->rear;
        while (n < frames) {
            if (v->at >= v->sound->count) {
                if (v->loop < 0 || v->loop == SPEECH_VOICE) { v->ended = true; break; }
                v->at = 0;
            }
            float x = v->sound->samples[v->at++], y = 0;
            if (v->taps) { /* the encoder (WAVMWSVR.DLL 0x1000d823): y[n] = sum c[k] rear[n - taps + k], L + y, R - y */
                for (int k = 0; k < v->taps; k++) y += v->coef[k] * v->history[k];
                SDL_memmove(v->history, v->history + 1, (size_t)(v->taps - 1) * sizeof(float));
                v->history[v->taps - 1] = x * gb;
            }
            buf[2 * n] = x * gl + y;
            buf[2 * n + 1] = x * gr - y;
            n++;
        }
        if (n == 0 || !SDL_PutAudioStreamData(stream, buf, n * 2 * (int)sizeof(float))) return;
        additional -= n * 8;
    }
}

void sfx_load(const char *audio_dir) {
    char *dir = path_join(audio_dir, "sfx");
    if (!dir) return;
    if (sfx.folder && SDL_strcmp(dir, sfx.folder) == 0) { SDL_free(dir); return; }
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

static void stop_speech(void) {
    for (int v = sfx.voice_count - 1; v >= 0; v--)
        if (sfx.voices[v]->loop == SPEECH_VOICE) free_voice(v);
}

/* The one-shot sounds that have played out. */
static void reap(void) {
    for (int v = sfx.voice_count - 1; v >= 0; v--) {
        sfx_voice *x = sfx.voices[v];
        bool ended;
        SDL_LockAudioStream(x->stream);
        ended = x->ended;
        SDL_UnlockAudioStream(x->stream);
        if (ended && SDL_GetAudioStreamQueued(x->stream) == 0) free_voice(v);
    }
}

/* The encoder's filter for a sound's rate (made once per rate). */
static int dolby_filter(int rate, float *out) {
    static struct { int rate, taps; float c[MAX_TAPS]; } made[4];
    static int count;
    int taps = (int)(32.0 * rate / 22050 + 1) & ~1, i;
    if (taps < 2) taps = 2;
    if (taps > MAX_TAPS) taps = MAX_TAPS;
    for (i = 0; i < count && made[i].rate != rate; i++) {}
    if (i == count) {
        int16_t c[MAX_TAPS];
        i = count < 4 ? count++ : 3;
        tc_sound_dolby_filter(c, taps, (float)rate);
        made[i].rate = rate, made[i].taps = taps;
        for (int k = 0; k < taps; k++) made[i].c[k] = (float)c[k] / 8192; /* its sum >> 13 */
    }
    SDL_memcpy(out, made[i].c, (size_t)made[i].taps * sizeof(float));
    return made[i].taps;
}

/* A voice for `s` at its gains and pitch; owned: a line's sound, the voice's to free. */
static void start_voice(const sfx_sound *s, sfx_sound *owned, int loop, const sfx_event *e, const tc_sound_source *src,
                        const tc_sound_mix *mix) {
    if (sfx.voice_count >= MAX_VOICES || !open_device()) { free_sound(owned); return; }
    sfx_voice *v = SDL_calloc(1, sizeof *v);
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, s->rate};
    if (!v) { free_sound(owned); return; }
    v->sound = s, v->owned = owned, v->loop = loop;
    v->left = mix->left, v->right = mix->right, v->rear = mix->rear;
    v->volume = e->volume, v->follow = e->follow, v->source = *src;
    v->taps = dolby_filter(s->rate, v->coef);
    v->stream = SDL_CreateAudioStream(&spec, NULL);  /* to the device's format when bound */
    if (v->stream && e->spread != 0) /* SND_004048e0's pitch, fixed for the voice's life */
        SDL_SetAudioStreamFrequencyRatio(v->stream, tc_sound_pitch(e->spread, (int)SDL_rand(32768)));
    if (!v->stream || !SDL_SetAudioStreamGetCallback(v->stream, feed, v) || !SDL_BindAudioStream(sfx.device, v->stream)) {
        SDL_Log("Sound effects: %s", SDL_GetError());
        SDL_DestroyAudioStream(v->stream);
        free_sound(owned);
        SDL_free(v);
        return;
    }
    sfx.voices[sfx.voice_count++] = v;
}

void sfx_play(const tc_viewer *view) {
    const uint16_t *events;
    uint32_t count = tc_viewer_sounds(view, &events);
    const tc_sound_source *sources = tc_viewer_sound_sources(view);
    if (count) reap();
    for (uint32_t i = 0; i < count; i++) {
        int k = events[i];
        const tc_sound_source *src = &sources[i];
        if (k == VOICE_STOP) { stop_speech(); continue; }
        if (k >= sfx.event_count || sfx.events[k].kind == SFX_NONE) continue;
        const sfx_event *e = &sfx.events[k];
        if (e->kind == SFX_STOP) { /* its loop, from that emitter (any, when the stop has none: a global one) */
            for (int v = 0; e->stops >= 0 && v < sfx.voice_count; v++)
                if (sfx.voices[v]->loop == e->stops &&
                    (src->kind == TC_SOUND_FLAT || sfx.voices[v]->source.emitter == src->emitter)) { free_voice(v); break; }
            continue;
        }
        bool playing = false;
        for (int v = 0; e->kind == SFX_LOOP && v < sfx.voice_count; v++)
            playing |= sfx.voices[v]->loop == k && sfx.voices[v]->source.emitter == src->emitter;
        if (playing) continue;
        tc_sound_mix mix;
        if (!tc_viewer_sound_mix(view, src, e->volume, sfx.dolby, &mix)) continue; /* its perso was already taken out */
        if (e->kind == SFX_VOICE) {
            stop_speech();
            sfx_sound *line = e->file ? SDL_calloc(1, sizeof *line) : NULL;
            char *path = line ? path_join(sfx.folder, e->file) : NULL;
            if (path) read_sound(line, path);
            SDL_free(path);
            if (!line || !line->count) { free_sound(line); continue; }
            start_voice(line, line, SPEECH_VOICE, e, src, &mix);
            continue;
        }
        const sfx_sound *s = sound_for(e);
        if (s) start_voice(s, NULL, e->kind == SFX_LOOP ? k : -1, e, src, &mix);
    }
}

void sfx_update(const tc_viewer *view) {
    bool all = sfx.regain;
    sfx.regain = false;
    for (int i = sfx.voice_count - 1; i >= 0; i--) {
        sfx_voice *v = sfx.voices[i];
        tc_sound_mix mix;
        if (v->source.kind == TC_SOUND_FLAT && !all) continue;
        if (!tc_viewer_sound_mix(view, &v->source, v->volume, sfx.dolby, &mix)) { free_voice(i); continue; }
        if (!v->follow && !all) continue;
        SDL_LockAudioStream(v->stream);
        v->left = mix.left, v->right = mix.right, v->rear = mix.rear;
        SDL_UnlockAudioStream(v->stream);
    }
}

void sfx_set_dolby(bool on) {
    sfx.dolby = on;
    sfx.regain = true;
}

bool sfx_dolby(void) { return sfx.dolby; }

void sfx_stop_all(void) {
    while (sfx.voice_count > 0) free_voice(sfx.voice_count - 1);
}

void sfx_set_muted(bool muted) {
    sfx.muted = muted;  /* feed() reads it: what is already queued (a few ms) plays out */
}

void sfx_quit(void) {
    sfx_stop_all();
    free_table();
    if (sfx.device) SDL_CloseAudioDevice(sfx.device);
    sfx.device = 0;
}

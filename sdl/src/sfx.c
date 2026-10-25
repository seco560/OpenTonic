#include "sfx.h"
#include <SDL3/SDL.h>
#include "util.h"

typedef enum sfx_kind { SFX_NONE, SFX_PLAY, SFX_LOOP, SFX_STOP } sfx_kind;

/* A WAV, read once. Heap-allocated so its address stays put: a loop's audio thread callback reads it. */
typedef struct sfx_sound {
    char *file;
    SDL_AudioSpec spec;
    Uint8 *data;
    Uint32 size;                 /* 0: could not be read (not retried) */
} sfx_sound;

typedef struct sfx_event {
    sfx_kind kind;
    char *file;                  /* NULL: no sound */
    float volume;
    int stops;                   /* SFX_STOP: the loop event it ends, -1 none */
} sfx_event;

typedef struct sfx_voice {
    SDL_AudioStream *stream;     /* bound to the device */
    int loop;                    /* the loop event it plays, -1 = a one-shot sound */
    float volume;
} sfx_voice;

static struct {
    SDL_AudioDeviceID device;
    bool no_audio;
    char *folder;                /* out/audio/sfx */
    sfx_event *events;           /* by event index */
    int event_count;
    sfx_sound **sounds;
    int sound_count, sound_cap;
    sfx_voice *voices;
    int voice_count, voice_cap;
    bool muted;
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

static void free_voice(int i) {
    SDL_DestroyAudioStream(sfx.voices[i].stream);
    sfx.voices[i] = sfx.voices[--sfx.voice_count];
}

static void free_table(void) {
    for (int i = 0; i < sfx.event_count; i++) SDL_free(sfx.events[i].file);
    SDL_free(sfx.events);
    sfx.events = NULL;
    sfx.event_count = 0;
    for (int i = 0; i < sfx.sound_count; i++) {
        SDL_free(sfx.sounds[i]->file);
        SDL_free(sfx.sounds[i]->data);
        SDL_free(sfx.sounds[i]);
    }
    SDL_free(sfx.sounds);
    sfx.sounds = NULL;
    sfx.sound_count = sfx.sound_cap = 0;
    SDL_free(sfx.folder);
    sfx.folder = NULL;
}

/* The event's WAV, read on first use; NULL when it has none or it can't be read. */
static const sfx_sound *sound_for(const sfx_event *e) {
    if (!e->file || !sfx.folder) return NULL;
    for (int i = 0; i < sfx.sound_count; i++)
        if (SDL_strcmp(sfx.sounds[i]->file, e->file) == 0) return sfx.sounds[i]->size ? sfx.sounds[i] : NULL;
    if (!grow((void **)&sfx.sounds, &sfx.sound_cap, sfx.sound_count + 1, sizeof *sfx.sounds)) return NULL;
    sfx_sound *s = SDL_calloc(1, sizeof *s);
    if (!s || !(s->file = SDL_strdup(e->file))) { SDL_free(s); return NULL; }
    char *path = path_join(sfx.folder, e->file);
    if (path && !SDL_LoadWAV(path, &s->spec, &s->data, &s->size)) {
        SDL_Log("Sound effects: cannot open %s: %s", path, SDL_GetError());
        s->data = NULL;
        s->size = 0;
    }
    SDL_free(path);
    sfx.sounds[sfx.sound_count++] = s;  /* failures too, so they aren't retried every time */
    return s->size ? s : NULL;
}

/* Audio thread: a loop's voice, fed its whole sound again whenever it runs low. */
static void SDLCALL loop_feed(void *user, SDL_AudioStream *stream, int additional, int total) {
    const sfx_sound *s = user;
    (void)total;
    while (additional > 0) {
        if (!SDL_PutAudioStreamData(stream, s->data, (int)s->size)) return;
        additional -= (int)s->size;
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
        char *cols[5] = {NULL};
        int n = 0;
        for (char *c = line; c && n < 5; n++) {
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
                : SDL_strcmp(cols[1], "stop") == 0 ? SFX_STOP : SFX_NONE;
        e->file = cols[2][0] ? SDL_strdup(cols[2]) : NULL;
        e->volume = (float)SDL_strtod(cols[3], &end);
        if (end == cols[3]) e->volume = 1;
        e->stops = n > 4 && cols[4][0] ? SDL_atoi(cols[4]) : -1;
    }
    SDL_free(table);
}

void sfx_play(const uint16_t *events, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        int k = events[i];
        if (k >= sfx.event_count || sfx.events[k].kind == SFX_NONE) continue;
        const sfx_event *e = &sfx.events[k];
        if (e->kind == SFX_STOP) {
            for (int v = 0; e->stops >= 0 && v < sfx.voice_count; v++)
                if (sfx.voices[v].loop == e->stops) { free_voice(v); break; }
            continue;
        }
        bool playing = false;
        for (int v = 0; e->kind == SFX_LOOP && v < sfx.voice_count; v++) playing |= sfx.voices[v].loop == k;
        if (playing) continue;
        /* One-shot sounds that have played out. */
        for (int v = sfx.voice_count - 1; v >= 0; v--)
            if (sfx.voices[v].loop < 0 && SDL_GetAudioStreamQueued(sfx.voices[v].stream) == 0) free_voice(v);
        const sfx_sound *s = sound_for(e);
        if (!s || !open_device()) continue;
        if (!grow((void **)&sfx.voices, &sfx.voice_cap, sfx.voice_count + 1, sizeof *sfx.voices)) return;
        SDL_AudioStream *stream = SDL_CreateAudioStream(&s->spec, NULL);  /* to the device's format when bound */
        if (!stream) continue;
        SDL_SetAudioStreamGain(stream, sfx.muted ? 0.0f : e->volume);
        if (e->kind == SFX_LOOP) SDL_SetAudioStreamGetCallback(stream, loop_feed, (void *)s);
        /* Bound before any data goes in: a stream takes none until it has an output format, which binding sets. */
        bool ok = SDL_BindAudioStream(sfx.device, stream);
        if (ok && e->kind != SFX_LOOP) ok = SDL_PutAudioStreamData(stream, s->data, (int)s->size) && SDL_FlushAudioStream(stream);
        if (!ok) {
            SDL_Log("Sound effects: %s", SDL_GetError());
            SDL_DestroyAudioStream(stream);
            continue;
        }
        sfx.voices[sfx.voice_count++] = (sfx_voice){stream, e->kind == SFX_LOOP ? k : -1, e->volume};
    }
}

void sfx_stop_all(void) {
    while (sfx.voice_count > 0) free_voice(sfx.voice_count - 1);
}

void sfx_set_muted(bool muted) {
    sfx.muted = muted;
    for (int v = 0; v < sfx.voice_count; v++)
        SDL_SetAudioStreamGain(sfx.voices[v].stream, muted ? 0.0f : sfx.voices[v].volume);
}

void sfx_quit(void) {
    sfx_stop_all();
    free_table();
    SDL_free(sfx.voices);
    sfx.voices = NULL;
    sfx.voice_cap = 0;
    if (sfx.device) SDL_CloseAudioDevice(sfx.device);
    sfx.device = 0;
}

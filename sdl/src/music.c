#include "music.h"
#include <SDL3/SDL.h>
#include "util.h"

static struct {
    SDL_AudioStream *stream;
    SDL_IOStream *file;
    Sint64 data_start, data_size, pos;  /* the WAV's PCM data chunk; pos = bytes played into it */
    int frame_size;
    char current[512];
    bool muted;
} music;

static Uint32 le32(const Uint8 *p) { return (Uint32)p[0] | (Uint32)p[1] << 8 | (Uint32)p[2] << 16 | (Uint32)p[3] << 24; }
static Uint16 le16(const Uint8 *p) { return (Uint16)(p[0] | p[1] << 8); }

/* Finds the fmt and data chunks; leaves the file at the start of the data. */
static bool parse_wav(SDL_IOStream *f, SDL_AudioSpec *spec, int *frame_size, Sint64 *data_start, Sint64 *data_size) {
    Uint8 h[12];
    if (SDL_ReadIO(f, h, 12) != 12 || SDL_memcmp(h, "RIFF", 4) != 0 || SDL_memcmp(h + 8, "WAVE", 4) != 0)
        return SDL_SetError("not a RIFF/WAVE file");
    bool have_fmt = false;
    for (;;) {
        Uint8 c[8];
        if (SDL_ReadIO(f, c, 8) != 8) return SDL_SetError("no data chunk");
        Uint32 size = le32(c + 4);
        if (SDL_memcmp(c, "fmt ", 4) == 0) {
            Uint8 fmt[40];
            Uint32 n = SDL_min(size, (Uint32)sizeof fmt);
            if (n < 16 || SDL_ReadIO(f, fmt, n) != n) return SDL_SetError("bad fmt chunk");
            Uint16 tag = le16(fmt), channels = le16(fmt + 2), bits = le16(fmt + 14);
            if (tag == 0xFFFE && n >= 26) tag = le16(fmt + 24);  /* WAVE_FORMAT_EXTENSIBLE: the subformat GUID */
            spec->channels = channels;
            spec->freq = (int)le32(fmt + 4);
            if (tag == 1 && bits == 16) spec->format = SDL_AUDIO_S16LE;
            else if (tag == 1 && bits == 8) spec->format = SDL_AUDIO_U8;
            else if (tag == 1 && bits == 32) spec->format = SDL_AUDIO_S32LE;
            else if (tag == 3 && bits == 32) spec->format = SDL_AUDIO_F32LE;
            else return SDL_SetError("unsupported WAV format (tag %u, %u bits)", (unsigned)tag, (unsigned)bits);
            *frame_size = channels * bits / 8;
            have_fmt = channels > 0;
            size -= n;
        } else if (SDL_memcmp(c, "data", 4) == 0) {
            if (!have_fmt) return SDL_SetError("data before fmt");
            *data_start = SDL_TellIO(f);
            *data_size = size;
            /* Streams written without a final size (0 / 0xFFFFFFFF): up to the end of the file. */
            Sint64 end = SDL_GetIOSize(f);
            if (size == 0 || size == 0xFFFFFFFFu || *data_start + size > end) *data_size = end - *data_start;
            *data_size -= *data_size % *frame_size;
            return *data_size > 0 ? true : SDL_SetError("empty data chunk");
        }
        if (SDL_SeekIO(f, size + (size & 1), SDL_IO_SEEK_CUR) < 0) return false;  /* chunks are word-aligned */
    }
}

/* Audio thread: keep the stream fed, wrapping to the start of the data at the end (one loop per track). */
static void SDLCALL feed(void *user, SDL_AudioStream *stream, int additional, int total) {
    (void)user;
    (void)total;
    Uint8 buf[16384];
    int chunk_max = (int)sizeof buf - (int)sizeof buf % music.frame_size;
    while (additional > 0) {
        Sint64 left = music.data_size - music.pos;
        if (left <= 0) {
            if (SDL_SeekIO(music.file, music.data_start, SDL_IO_SEEK_SET) < 0) return;
            music.pos = 0;
            left = music.data_size;
        }
        int n = (int)SDL_min((Sint64)chunk_max, left);
        size_t got = SDL_ReadIO(music.file, buf, (size_t)n);
        if (got == 0) { music.pos = music.data_size; return; }  /* read error: retry from the start next time */
        SDL_PutAudioStreamData(stream, buf, (int)got);
        music.pos += (Sint64)got;
        additional -= (int)got;
    }
}

/* The track for `level` in music.tsv (case-insensitive level match), or false. */
static bool track_for(const char *level, const char *audio_dir, char *out, size_t out_size) {
    char *path = path_join(audio_dir, "music.tsv");
    char *table = path ? SDL_LoadFile(path, NULL) : NULL;
    SDL_free(path);
    if (!table) return false;
    bool found = false;
    char *save = NULL;
    for (char *line = SDL_strtok_r(table, "\n", &save); line && !found; line = SDL_strtok_r(NULL, "\n", &save)) {
        if (line[0] == '#') continue;
        /* Tab-separated columns, empty ones skipped, spaces (and a CR) trimmed. */
        char *cols[2] = {NULL, NULL};
        int n = 0;
        char *col_save = NULL;
        for (char *c = SDL_strtok_r(line, "\t", &col_save); c && n < 2; c = SDL_strtok_r(NULL, "\t", &col_save)) {
            while (*c == ' ') c++;
            char *e = c + SDL_strlen(c);
            while (e > c && (e[-1] == ' ' || e[-1] == '\r')) *--e = 0;
            cols[n++] = c;
        }
        if (n == 2 && cols[1][0] && SDL_strcasecmp(cols[0], level) == 0) {
            SDL_strlcpy(out, cols[1], out_size);
            found = true;
        }
    }
    SDL_free(table);
    return found;
}

void music_stop(void) {
    if (music.stream) SDL_DestroyAudioStream(music.stream);  /* also closes the device; no more callbacks */
    if (music.file) SDL_CloseIO(music.file);
    music.stream = NULL;
    music.file = NULL;
    music.current[0] = 0;
}

void music_play(const char *level, const char *audio_dir) {
    char file[256];
    if (!track_for(level, audio_dir, file, sizeof file)) { music_stop(); return; }
    char *dir = path_join(audio_dir, "music");
    char *path = dir ? path_join(dir, file) : NULL;
    SDL_free(dir);
    if (!path) return;
    if (music.stream && SDL_strcmp(path, music.current) == 0) { SDL_free(path); return; }
    music_stop();
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("Music: no audio (%s)", SDL_GetError());
        SDL_free(path);
        return;
    }
    SDL_AudioSpec spec;
    SDL_IOStream *f = SDL_IOFromFile(path, "rb");
    if (!f || !parse_wav(f, &spec, &music.frame_size, &music.data_start, &music.data_size)) {
        SDL_Log("Music: cannot open %s: %s", path, SDL_GetError());
        if (f) SDL_CloseIO(f);
        SDL_free(path);
        return;
    }
    music.file = f;
    music.pos = 0;
    music.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!music.stream) {
        SDL_Log("Music: %s", SDL_GetError());
        SDL_CloseIO(f);
        music.file = NULL;
        SDL_free(path);
        return;
    }
    SDL_strlcpy(music.current, path, sizeof music.current);
    SDL_free(path);
    SDL_SetAudioStreamGain(music.stream, music.muted ? 0.0f : 1.0f);
    SDL_ResumeAudioStreamDevice(music.stream);
}

void music_set_muted(bool muted) {
    music.muted = muted;
    if (music.stream) SDL_SetAudioStreamGain(music.stream, muted ? 0.0f : 1.0f);
}

bool music_muted(void) { return music.muted; }

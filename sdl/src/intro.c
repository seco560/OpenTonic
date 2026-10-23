#include "intro.h"
#include <stdio.h>  /* pl_mpeg.h uses FILE without including it */
#define PL_MPEG_IMPLEMENTATION
#include <pl_mpeg.h>

#define SCREEN_FRACTION 0.8f  /* IntroView.screenFraction */
#define AUDIO_LEAD 0.25       /* seconds of decoded audio kept queued ahead of the device */

struct intro {
    gpu *g;
    plm_t *plm;
    int width, height;
    SDL_GPUTexture *texture;
    SDL_GPUTransferBuffer *transfer;
    Uint8 *rgba;               /* newest due frame, width x height x 4 */
    bool dirty;                /* rgba not uploaded yet */
    bool shown;                /* a frame was uploaded (black until then) */
    plm_frame_t *next;         /* decoded, not due yet (valid until the next video decode) */
    bool video_done, audio_done;
    SDL_AudioStream *stream;   /* NULL: no audio, the wall clock runs the video */
    int rate;
    Uint64 samples_pushed;     /* per channel */
    double clock;              /* seconds into the movie */
};

static const int frame_bytes = 2 * (int)sizeof(float);  /* interleaved stereo float */

static int queued_samples(const intro *m) {
    int bytes = SDL_GetAudioStreamQueued(m->stream);
    return bytes > 0 ? bytes / frame_bytes : 0;
}

/* Keeps AUDIO_LEAD seconds of soundtrack queued. */
static void feed_audio(intro *m) {
    while (m->stream && !m->audio_done && queued_samples(m) < (int)(AUDIO_LEAD * m->rate)) {
        plm_samples_t *s = plm_decode_audio(m->plm);
        if (!s) { m->audio_done = true; break; }
        SDL_PutAudioStreamData(m->stream, s->interleaved, (int)s->count * frame_bytes);
        m->samples_pushed += s->count;
    }
}

intro *intro_open(gpu *g, const char *path, bool muted, char *err, size_t err_size) {
    intro *m = SDL_calloc(1, sizeof *m);
    if (!m) { SDL_snprintf(err, err_size, "out of memory"); return NULL; }
    m->g = g;
    m->plm = plm_create_with_filename(path);
    if (!m->plm) { SDL_snprintf(err, err_size, "cannot open %s", path); intro_close(m); return NULL; }
    if (!plm_has_headers(m->plm) || plm_get_num_video_streams(m->plm) == 0) {
        SDL_snprintf(err, err_size, "%s is not an MPEG-1 program stream with video (tools/make_intro.sh mpg)", path);
        intro_close(m);
        return NULL;
    }
    m->width = plm_get_width(m->plm);
    m->height = plm_get_height(m->plm);

    SDL_GPUTextureCreateInfo info;
    SDL_zero(info);
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = (Uint32)m->width;
    info.height = (Uint32)m->height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    m->texture = SDL_CreateGPUTexture(g->device, &info);
    SDL_GPUTransferBufferCreateInfo tinfo;
    SDL_zero(tinfo);
    tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tinfo.size = (Uint32)(m->width * m->height * 4);
    m->transfer = SDL_CreateGPUTransferBuffer(g->device, &tinfo);
    m->rgba = SDL_malloc(tinfo.size);
    if (!m->texture || !m->transfer || !m->rgba) {
        SDL_snprintf(err, err_size, "cannot create the video texture: %s", SDL_GetError());
        intro_close(m);
        return NULL;
    }
    SDL_memset(m->rgba, 0xFF, tinfo.size);  /* plm_frame_to_rgba leaves alpha alone: opaque once */

    /* The soundtrack, when there is one and an audio device. */
    bool audio = plm_get_num_audio_streams(m->plm) > 0 &&
                 (SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO));
    if (audio) {
        m->rate = plm_get_samplerate(m->plm);
        SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, m->rate};
        m->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (!m->stream) SDL_Log("Intro: no audio (%s)", SDL_GetError());
    }
    if (!m->stream) {
        plm_set_audio_enabled(m->plm, 0);  /* else the demuxer would keep buffering audio nobody reads */
        m->audio_done = true;
    } else {
        SDL_SetAudioStreamGain(m->stream, muted ? 0.0f : 1.0f);
        feed_audio(m);
        SDL_ResumeAudioStreamDevice(m->stream);
    }
    SDL_Log("Intro: %s, %dx%d at %.2f fps, %s", path, m->width, m->height, plm_get_framerate(m->plm),
            m->stream ? "with sound" : "silent");
    return m;
}

void intro_close(intro *m) {
    if (!m) return;
    if (m->stream) SDL_DestroyAudioStream(m->stream);
    if (m->plm) plm_destroy(m->plm);
    if (m->texture || m->transfer) SDL_WaitForGPUIdle(m->g->device);
    if (m->texture) SDL_ReleaseGPUTexture(m->g->device, m->texture);
    if (m->transfer) SDL_ReleaseGPUTransferBuffer(m->g->device, m->transfer);
    SDL_free(m->rgba);
    SDL_free(m);
}

bool intro_update(intro *m, float dt) {
    feed_audio(m);
    bool audio_playing = m->stream && !(m->audio_done && queued_samples(m) == 0);
    if (audio_playing)  /* what the device has taken from the stream so far */
        m->clock = (double)((Sint64)m->samples_pushed - queued_samples(m)) / m->rate;
    else
        m->clock += dt;

    /* Every frame that is due gets converted (B-frames reuse the decoder's buffers), the last one is uploaded. */
    while (!m->video_done) {
        if (!m->next) m->next = plm_decode_video(m->plm);
        if (!m->next) { m->video_done = true; break; }
        if (m->next->time > m->clock) break;
        plm_frame_to_rgba(m->next, m->rgba, m->width * 4);
        m->dirty = true;
        m->next = NULL;
    }
    return !m->video_done || audio_playing;
}

void intro_upload(intro *m, gpu_frame *f) {
    if (!m->dirty) return;
    Uint32 size = (Uint32)(m->width * m->height * 4);
    void *p = SDL_MapGPUTransferBuffer(m->g->device, m->transfer, true);
    if (!p) return;
    SDL_memcpy(p, m->rgba, size);
    SDL_UnmapGPUTransferBuffer(m->g->device, m->transfer);
    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(f->cmd);
    if (!pass) return;
    SDL_GPUTextureTransferInfo src;
    SDL_zero(src);
    src.transfer_buffer = m->transfer;
    src.pixels_per_row = (Uint32)m->width;
    src.rows_per_layer = (Uint32)m->height;
    SDL_GPUTextureRegion dst;
    SDL_zero(dst);
    dst.texture = m->texture;
    dst.w = (Uint32)m->width;
    dst.h = (Uint32)m->height;
    dst.d = 1;
    SDL_UploadToGPUTexture(pass, &src, &dst, true);  /* cycle: the last frame may still be drawing */
    SDL_EndGPUCopyPass(pass);
    m->dirty = false;
    m->shown = true;
}

void intro_draw(intro *m, gpu_frame *f, SDL_GPURenderPass *pass) {
    if (!m->shown) return;
    /* Largest rectangle of the movie's shape that fits, scaled by SCREEN_FRACTION, centred. */
    float aspect = (float)m->width / (float)m->height;
    float w = (float)f->width, h = w / aspect;
    if (h > (float)f->height) { h = (float)f->height; w = h * aspect; }
    w *= SCREEN_FRACTION;
    h *= SCREEN_FRACTION;
    SDL_FRect rect = {SDL_floorf(((float)f->width - w) / 2), SDL_floorf(((float)f->height - h) / 2), w, h};
    gpu_quad(m->g, f, pass, m->texture, rect, NULL, (SDL_FColor){1, 1, 1, 1});
}

void intro_set_muted(intro *m, bool muted) {
    if (m->stream) SDL_SetAudioStreamGain(m->stream, muted ? 0.0f : 1.0f);
}

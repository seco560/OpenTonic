/* The intro movie (V on the level select screen). Port of Sources/TonicViewer/IntroPlayer.swift over pl_mpeg
 * (third_party/pl_mpeg.h): out/video/intro.mpg, MPEG-1 video + MP2 audio made by `tools/make_intro.sh mpg`.
 * The picture keeps its shape at 0.8 of the largest size that fits, centred on black. The soundtrack is the clock:
 * a frame is shown once the audio device has played up to its time (the wall clock when there is no audio). */
#ifndef TV_INTRO_H
#define TV_INTRO_H
#include <stddef.h>
#include "gpu.h"

typedef struct intro intro;

/* Starts playing. NULL with a message in err when the file can't be played. */
intro *intro_open(gpu *g, const char *path, bool muted, char *err, size_t err_size);
void intro_close(intro *m);
/* Decodes up to the clock (dt only counts without audio). false once the movie is over. */
bool intro_update(intro *m, float dt);
/* Uploads the newest frame in a copy pass on the frame's command buffer: call before its render pass. */
void intro_upload(intro *m, gpu_frame *f);
/* Draws into a 2D pass cleared to black. */
void intro_draw(intro *m, gpu_frame *f, SDL_GPURenderPass *pass);
void intro_set_muted(intro *m, bool muted);

#endif

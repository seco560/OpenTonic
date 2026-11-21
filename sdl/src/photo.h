/* Photo mode's photos (the user, 2026-11-20): the frame as shown, at the window's pixels, as a JPEG in the user's
 * Pictures folder, named OpenTonicScreenshot<date>_<time>.jpeg. Sources/OpenTonic/Renderer.swift does the same on macOS. */
#ifndef TV_PHOTO_H
#define TV_PHOTO_H
#include <stdint.h>

/* Takes rgba (w x h, RGBA8, top row first, from SDL_malloc) and writes it on a thread of its own; frees it. */
void photo_save(uint8_t *rgba, int w, int h);

#endif

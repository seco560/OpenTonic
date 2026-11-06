/* Recording play runs to disk (core: tc_rec.h; docs/RECORDING.md): with Record runs on in the Replays menu, a run, from a
 * level opened from a menu back to a menu, leaves one .ttrec in the recordings folder. The Swift build's is Sources/OpenTonic/Recording.swift. */
#ifndef TV_RECORD_H
#define TV_RECORD_H
#include <tonic/tonic.h>

/* TONIC_RECORDINGS: the folder instead of the user's OpenTonic/Recordings, or "off". NULL: none (SDL_free it). */
char *record_folder(void);
/* Record runs, the Replays menu's toggle: off by default, as the files add up. Kept across launches; a new value counts
 * from the next run. */
bool record_enabled(void);
void record_set_enabled(bool on);
/* A new run's recorder, with its notes; NULL when recording is off. One at a time. */
tc_recorder *record_start(void);
void record_stop(tc_recorder *r);
/* The newest recording in the folder (SDL_free it), or NULL. */
char *record_latest(void);
#endif

/* Level music. Port of Sources/OpenTonic/Music.swift. The level -> track mapping is a user-edited table, because
 * the game picks music at runtime (sound events/AI, not yet decoded):
 *   out/audio/music.tsv  lines "<level name><TAB><file in out/audio/music>", '#' comments.
 * Tracks are WAVs extracted from the sound banks with vgmstream (docs/BUILD.md, step 5), streamed from disk and
 * looped. */
#ifndef TV_MUSIC_H
#define TV_MUSIC_H
#include <stdbool.h>

/* Plays the track mapped to `level` (looping), or stops if there is none. audio_dir = out/audio. The level picker
 * uses the entry named "menu". A track that is already playing just continues. */
void music_play(const char *level, const char *audio_dir);
void music_stop(void);
void music_set_muted(bool muted);  /* the sound effects too (sfx.h) */
bool music_muted(void);

#endif

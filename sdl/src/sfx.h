/* Sound effects. Port of Sources/OpenTonic/Sfx.swift: the game's global sound events (tools/ttre/sfx.py writes
 * out/audio/sfx/<file>.wav and events.tsv: "<event index> <TAB> play|loop|stop <TAB> file <TAB> volume <TAB> stopped
 * event"). The core reports which events started each frame (tc_viewer_sounds). A loop has one voice: retriggered
 * while it plays (the animations restart flight and sliding every cycle) it goes on, until a stop event names it.
 * Other sounds overlap, each to its end. Muted with the music (M), the sounds already playing too. */
#ifndef TV_SFX_H
#define TV_SFX_H
#include <stdbool.h>
#include <stdint.h>

/* Reads the event table from audio_dir/sfx (once per folder). audio_dir = out/audio. */
void sfx_load(const char *audio_dir);
void sfx_play(const uint16_t *events, uint32_t count);
/* Level change, level select: silence everything. */
void sfx_stop_all(void);
void sfx_set_muted(bool muted);
/* Frees the sounds and closes the audio device. */
void sfx_quit(void);

#endif

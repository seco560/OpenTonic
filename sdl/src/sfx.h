/* Sound effects. Port of Sources/OpenTonic/Sfx.swift: the game's sound events (tools/ttre/sfx.py writes
 * out/audio/sfx/<file>.wav and events.tsv: "<event index> <TAB> play|loop|stop|voice <TAB> file <TAB> volume <TAB>
 * stopped event <TAB> follow <TAB> pitch spread"). The core reports which events started each frame and where each
 * sounds from (tc_viewer_sounds, tc_viewer_sound_sources); each voice's left and right gains come from
 * tc_viewer_sound_gains, at the start and, for the events that follow their emitter, every frame (sfx_update): the
 * original's 3D sound (docs/ENGINE.md "3D sound"). A loop has one voice per emitter: retriggered while it plays (the
 * animations restart flight and sliding every cycle) it goes on, until a stop event names it. Other sounds overlap, each
 * to its end. Muted with the music (M), the sounds already playing too. Each voice has the original mixer's gains and
 * its event's pitch spread; Dolby Surround (the original's sound menu option, off by default; J, TONIC_DOLBY=1 at the
 * start) runs each voice's rear signal through the original's encoder at the voice's own rate (the filter is linear: the
 * same as on the mix). */
#ifndef TV_SFX_H
#define TV_SFX_H
#include <stdbool.h>
#include <stdint.h>
#include "tonic/tc_viewer.h"

/* Reads the event table from audio_dir/sfx (once per folder). audio_dir = out/audio. */
void sfx_load(const char *audio_dir);
/* The sounds the viewer's last update started. */
void sfx_play(const tc_viewer *v);
/* Once a frame, after the viewer's update: the playing sounds heard from somewhere, placed again (those whose events
 * follow their emitters) or stopped (a perso's own, the perso taken out since). */
void sfx_update(const tc_viewer *v);
/* Level change, level select: silence everything. */
void sfx_stop_all(void);
void sfx_set_muted(bool muted);
/* Dolby Surround on or off (the voices playing take it at the next sfx_update). */
void sfx_set_dolby(bool on);
bool sfx_dolby(void);
/* Frees the sounds and closes the audio device. */
void sfx_quit(void);

#endif

/* TonicCore: portable C11 core of the Tonic Trouble port (no platform code). Umbrella header. */
#ifndef TONIC_TONIC_H
#define TONIC_TONIC_H
#include "tc_alloc.h"
#include "tc_prof.h"
#include "tc_math.h"
#include "tc_level.h"
#include "tc_sector.h"
#include "tc_collision.h"
#include "tc_player.h"
#include "tc_anim.h"
#include "tc_ai.h"
#include "tc_ed_ai.h"
#include "tc_cast.h"
#include "tc_transition.h"
#include "tc_cam.h"
#include "tc_speech.h"
#include "tc_debug.h"
#include "tc_hud.h"
#include "tc_viewer.h"
#include "tc_rec.h"
#include "tc_tape.h"
#include "tc_mec.h"
#include "tc_obstacle.h"
/* Custom gameplay: ours, not the game's (custom/tcx.h; the core builds without it: make CUSTOM=0). */
#include "custom/tcx.h"
#endif

/* Ed's AI: loader for .ttai (written by tools/ttre/ai.py, format documented there) and an interpreter that
 * runs his behaviours the way the exe does (docs/NOTES.md, "AI interpreter"). The core owns no input, camera
 * or physics: each frame the host fills the tc_ai_perso inputs (buttons, stick, position, contacts), calls
 * tc_ai_step, then applies the outputs (motion request, state to play, visibility). */
#ifndef TONIC_TC_AI_H
#define TONIC_TC_AI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tc_math.h"
#include "tc_level.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_AI_NONE 0xFFFFFFFFu
#define TC_AI_UNKNOWN_STRIDE 300u

/* Node types (the exe's per-type handler table 0x4E43F8). */
enum {
    TC_AI_KEYWORD = 0, TC_AI_COND = 1, TC_AI_OP = 2, TC_AI_FUNC = 3, TC_AI_PROC = 4, TC_AI_META = 5,
    TC_AI_END = 8, TC_AI_FIELD = 9, TC_AI_DSGVAR_REF = 10, TC_AI_DSGVAR = 11, TC_AI_INT = 12, TC_AI_REAL = 13,
    TC_AI_BUTTON = 14, TC_AI_CONST_VECTOR = 15, TC_AI_VECTOR = 16, TC_AI_MASK = 17, TC_AI_STRING = 20,
    TC_AI_PERSO = 24, TC_AI_STATE = 25, TC_AI_BEHAVIOUR = 32, TC_AI_CHANNEL = 33
};
#define TC_AI_REFLEX_FLAG 0x10000u /* behaviour node param: reflex list */

/* Virtual inputs, named after the game's input entries (tools/ttre/ai.py INPUTS). */
enum {
    TC_AI_IN_LEFT, TC_AI_IN_RIGHT, TC_AI_IN_UP, TC_AI_IN_DOWN, TC_AI_IN_SPEEDUP, TC_AI_IN_JUMP,
    TC_AI_IN_LATERAL, TC_AI_IN_ACTION, TC_AI_IN_SHOOT, TC_AI_IN_CAMERA, TC_AI_IN_COUNT
};
enum { TC_AI_ANALOG_NONE, TC_AI_ANALOG_ANGLE, TC_AI_ANALOG_MAGNITUDE, TC_AI_ANALOG_X, TC_AI_ANALOG_Y };

/* Dynamics report bits (COND 31-37), filled by the host from its collision step. */
#define TC_AI_CONTACT_GROUND 0x1u
#define TC_AI_CONTACT_SLOPE 0x2u    /* steep slope */
#define TC_AI_CONTACT_WALL 0x4u
#define TC_AI_CONTACT_OVERHANG 0x8u
#define TC_AI_CONTACT_CEILING 0x10u

typedef struct tc_ai_node { uint32_t param; uint8_t type, depth; } tc_ai_node;
typedef struct tc_ai_behaviour { int32_t schedule; uint16_t first_script, script_count; } tc_ai_behaviour;
/* save: how the game saves the variable (tools/ttre/ai.py dsg_save_flags, the exe's save module flags): with the
 * player's save, or with the level's own file, written on the way out of a level and read back on the way in. */
#define TC_AI_DSG_SAVE_PLAYER 1
#define TC_AI_DSG_SAVE_LEVEL 2
typedef struct tc_ai_dsgvar { uint32_t offset; uint8_t type; uint8_t save; } tc_ai_dsgvar;
int tc_ai_dsg_width(uint8_t type); /* bytes of a dsgvar of this type in the buffers */
typedef struct tc_ai_dsg_def { uint16_t count; uint32_t size; tc_ai_dsgvar *vars; uint8_t *initial; } tc_ai_dsg_def;
typedef struct tc_ai_transition { uint16_t target, via; } tc_ai_transition;
typedef struct tc_ai_state {
    int16_t next;                /* state that follows when the animation ends (-1 none; itself = loop) */
    uint16_t frames;
    uint8_t speed;               /* frames per second (0: 30) */
    uint8_t flags;               /* 1: only the listed transitions may leave this state */
    uint8_t repeats;             /* extra plays of the animation before the next state */
    uint16_t anim;               /* animation key: the .ttlv Ed animation named "s<anim>" */
    float ground;                /* units per animation frame Ed walks forward on the ground (0 = in place) */
    uint16_t card;               /* mechanics ID card (State +0x30): index into model->cards, TC_AI_NONE16 none */
    float drop;                  /* how far the state's pose reaches below the perso's origin (STAT) */
    /* BODY: root motion, one row per animation frame in model->root (root_count 0 = none): the A3D frame's own vector,
     * units per animation frame in the perso's axes (front = -Y). Times the frame rate = the ground speed. */
    uint32_t root_first;
    uint16_t root_count;
    uint16_t spheres;            /* BODY: mechanics collision spheres (ZDM): index into model->sphere_sets, or none */
    uint16_t water_points;       /* WPTS: the state's water sample points (State +0x34): index into
                                    model->water_point_sets, or none */
    uint32_t sound_first;        /* SNDS: the animation's sound events, model->sounds[sound_first ..] */
    uint16_t sound_count;
    uint32_t mech_first;         /* MECE: the animation's mechanics requests, model->mech_events[mech_first ..] */
    uint16_t mech_count;
    uint16_t transition_count, prohibited_count;
    tc_ai_transition *transitions; /* target -> state actually played first */
    uint16_t *prohibited;
} tc_ai_state;
typedef struct tc_ai_button { uint16_t inputs; uint8_t analog; int16_t lo, hi; } tc_ai_button;
typedef struct tc_ai_other { uint16_t id; uint8_t role; tc_ai_dsg_def dsg; } tc_ai_other;

#define TC_AI_NONE16 0xFFFFu
#define TC_AI_REMOVAL_MS 3000u
/* A procedure of the port's own (tools/ttre/cast.py PORT_FREEZE): a RemovePerso under a story check, rewritten: the target
 * stays as a prop (frozen). */
#define TC_AI_PROC_FREEZE 0x8101u
/* tc_ai_card (a mechanics ID card): tc_level.h, which the cast's cards (.ttlv CMEC) use too. */
/* tc_ai_sphere / tc_ai_sphere_set (a state's mechanics body): tc_level.h, which the cast's bodies (.ttlv CMEC) use too. */
/* TRIG: another perso's effect on this one, extracted from its script (ai.proximity_triggers): while this perso is within
 * radius of position (3D, origins) and the guard dsgvar of this perso is 0, its normal behaviour becomes behaviour;
 * then the trigger sleeps sleep_ms (0: until this perso has left the radius). Totalski's trees, cacti and standing
 * icicles -> the skier's crash, B9. */
typedef struct tc_ai_trigger {
    tc_vec3 position;
    float radius;
    uint16_t behaviour, guard;
    uint32_t sleep_ms;
} tc_ai_trigger;
/* .ttai PORT: a portal (docs/PORTALS.md): a level perso that moves the player elsewhere, reduced by the converter to
 * op lists (tools/ttre/ai.py portals()). Capture ops run once when the player comes within radius (after any GUARD
 * ops hold); fire ops when `fire` holds: 0 = the player's dsgvar fire_dsg == fire_value (Ed's B106 sets dsg103 once
 * the cover is done), 1 = the screen is covered, 2 = never (capture only: the player's own scripts do the rest). */
enum {
    TC_AI_POP_GUARD = 1,   /* the player's dsgvar a == (int)b, else no capture */
    TC_AI_POP_SET,         /* the player's dsgvar a := kind 0 (int)b, 1 v.x, 2 this portal's perso, 3 the vector v */
    TC_AI_POP_FORCE,       /* the player's normal behaviour := a */
    TC_AI_POP_FORCE_REFLEX,/* the player's reflex := a */
    TC_AI_POP_COVER,       /* start the cover transition (GameAction2) */
    TC_AI_POP_WAIT,        /* wait b ms (META_29) */
    TC_AI_POP_TELEPORT,    /* the player to v (PROC_8); kind 1: and facing the heading in c (f32 bits) */
    TC_AI_POP_CAMERA,      /* the camera to v (PROC_8) */
    TC_AI_POP_LEVEL        /* change level to names[b]; c != NONE: arrive at place names[c], camera at names[a] (PROC_29) */
};
typedef struct tc_ai_portal_op { uint8_t op, kind; uint16_t a; uint32_t b, c; tc_vec3 v; } tc_ai_portal_op;
/* ZONE: a zone primitive in the zone perso's axes (ZDD; COND_13): kind 7 sphere (a centre, b.x radius), 8 box (a min, b max). */
typedef struct tc_ai_zone_prim { uint32_t kind; tc_vec3 a, b; } tc_ai_zone_prim;
/* Whether p (world) is inside a zone given as a world -> zone inverse (row-major 3x4) and its primitives (7 sphere: a
 * centre, b.x radius; 8 box: a min, b max). */
bool tc_ai_zone_contains(const float inverse[12], const tc_ai_zone_prim *prims, uint32_t count, tc_vec3 p);
#define TC_AI_MAX_ZONE_PRIMS 8
/* .ttai MODZ (tools/ttre/se.py mode_zones): where a Special Edition level switches the player between walking (the
 * main model) and skiing (the model in ALTM), as the SE's own trigger characters force Ed's ski or walk behaviour when
 * he is in their zone (docs/SE.md "Sliding in the Special Edition"). */
typedef struct tc_ai_mode_zone {
    uint8_t mode;                /* TC_AI_MODE_WALK / TC_AI_MODE_SKI: the model it hands the player to */
    uint8_t require;             /* 0 any, 1 only while walking, 2 only while skiing */
    uint8_t once;                /* fires a single time per level load */
    uint8_t teleport;            /* 0 none, 1 to `vector`, 2 by `vector` (from where he is) */
    tc_vec3 vector;
    float inverse[12];           /* row-major 3x4: world -> the zone's axes */
    uint8_t prim_count;
    struct tc_ai_zone_prim prim[4];
} tc_ai_mode_zone;
#define TC_AI_MODE_WALK 0u
#define TC_AI_MODE_SKI 1u

/* A portal entered from either side (the Special Edition's arches, tools/ttre/se.py): at capture its axes turn so that
 * local -Y (where Ed's walk-in B68 takes him, 4.5 in front) is on the side he is on. Retail's arches face one way. */
#define TC_AI_PORTAL_TWO_SIDED 1u
/* Its perso runs in the cast (tools/ttre/cast.py portal_ids): its own scripts capture and move the player, so the record
 * is the simple controller's only (tc_ed_ai_run_portals); the AI's driver leaves it to the cast. */
#define TC_AI_PORTAL_CAST 2u
typedef struct tc_ai_portal {
    tc_vec3 position;
    float radius;                /* the capture distance, unless the portal has a zone */
    uint8_t zone_count;          /* ZONE: the capture is the player's origin inside the perso's ZDD zone (COND_13) */
    float zone_inverse[12];      /* row-major 3x4: world -> the perso's axes */
    tc_ai_zone_prim zone[TC_AI_MAX_ZONE_PRIMS];
    float rows[9];               /* the portal's axes, row-major (its local -Y faces the way in) */
    uint16_t perso;              /* as perso nodes (level perso k + 1) */
    uint16_t sector;             /* the perso's (.ttlv SECT); it sleeps unless active there (tc_sectors_active) */
    uint8_t fire;
    uint8_t flags;               /* TC_AI_PORTAL_TWO_SIDED */
    uint16_t fire_dsg;
    int32_t fire_value;
    uint16_t capture_count, fire_count;
    tc_ai_portal_op *ops;        /* capture_count capture ops, then fire_count fire ops */
} tc_ai_portal;
/* A sound event at an animation frame (SNDS). event = global sound event (Bnk_0's event index; tools/ttre/sfx.py). */
/* An animation event's counter (event +0xB / +0xC): it starts at `first` when the state starts; when the event's frame
 * is reached a counter of 0 fires it and reloads `period`, another counts down, 0xFF stays (FUN_00436560,
 * FUN_004366c0, FUN_00436820). first 0xFF: the event fires only when a script changes the state (FUN_00436600). Ed's
 * jump: first 0, period 0xFF: once per jump, not on every loop of state 43. */
typedef struct tc_ai_sound { uint16_t frame, event; uint8_t first, period; } tc_ai_sound;
/* A mechanics request at an animation frame (event type 1, FUN_00436910): mode, with the state's card. */
typedef struct tc_ai_mech_event { uint16_t frame, mode; uint8_t first, period; } tc_ai_mech_event;
#define TC_AI_MAX_STATE_EVENTS 32 /* counters kept per playing state; events beyond fire every time */
#define TC_AI_MAX_SOUNDS 16
#define TC_AI_VOICE_STOP 8191u /* a sound event of the host's: the voice playing stops (a line dismissed); voices are 8192 on */
/* The footstep switch (Bnk_0 event 49, id 0x390000) and the step sounds by floor sound type v (1..23): the sound events
 * the core reports for them are TC_AI_STEP_SOUND + v (tools/ttre/sfx.py writes those rows into events.tsv). */
#define TC_AI_FOOTSTEP_EVENT 49
#define TC_AI_STEP_SOUND 256
/* A level perso by instance name (lowercase): PROC_29 arrivals ("heg_positionedland"). */
typedef struct tc_ai_place { char *name; tc_vec3 position; float heading; } tc_ai_place;
/* One screen transition effect (fix.gpt 0x12a8): fields = effect +0x10..+0x30 (A, the mask wipe: start, end, current,
 * speed; B, the spinning logo: radius start, end, current, speed, current speed, acceleration, angle start, current
 * angle, spin); variants = texture paths relative to the .ttai, one picked at random. */
typedef struct tc_ai_effect { float fields[9]; uint16_t variant_count; char **variants; } tc_ai_effect;
typedef struct tc_ai_transitions {
    uint16_t cover_threshold, cover_range, reveal_threshold, reveal_range; /* r in [0, range]: r > threshold -> B */
    tc_ai_effect effects[4];     /* cover A, cover B, reveal A, reveal B */
} tc_ai_transitions;

/* Field of a card as f32, by its byte offset in the engine's card (+4 = words[0]); 0 if out of range. */
float tc_ai_card_f32(const tc_ai_card *c, uint32_t offset);
/* The same word raw (bytes and flags: the walk card's ride byte +0x3C is its low byte). */
uint32_t tc_ai_card_u32(const tc_ai_card *c, uint32_t offset);

/* WPTS: a state's water sample points (State +0x34 list, GAM_00437900): kind 1..6 (the head down to the feet) and a point
 * in the perso's axes; each frame the water flag of the environment there (MecEnv +0x28) is the zone value of its kind. */
#define TC_AI_MAX_WATER_POINTS 8
typedef struct tc_ai_water_points {
    uint8_t count;
    uint8_t kind[TC_AI_MAX_WATER_POINTS];
    tc_vec3 point[TC_AI_MAX_WATER_POINTS];
} tc_ai_water_points;

typedef struct tc_ai_model {
    char converter[16];          /* the converter's stamp (STMP), for log lines */
    uint64_t data_hash;          /* the file's content without the stamp and paths (recordings: tc_rec.h) */
    uint32_t node_count;
    tc_ai_node *nodes;
    uint32_t script_count;
    uint32_t *scripts;           /* first node of each script */
    uint16_t normal_count, reflex_count;
    tc_ai_behaviour *behaviours; /* normal_count normal behaviours, then the reflexes */
    uint16_t initial_normal, initial_reflex, max_rules;
    tc_ai_dsg_def dsg;
    uint16_t state_count, initial_state;
    tc_ai_state *states;
    uint16_t button_count;
    tc_ai_button *buttons;
    uint16_t string_count;
    char **strings;
    uint16_t vector_count;
    tc_vec3 *vectors;
    uint16_t other_count;
    tc_ai_other *others;         /* other persos the scripts reference (role 1 = camera) */
    uint16_t card_count;
    tc_ai_card *cards;
    uint32_t root_count;
    tc_vec3 *root;
    uint16_t sphere_set_count;
    tc_ai_sphere_set *sphere_sets;
    uint16_t trigger_count;
    tc_ai_trigger *triggers;
    uint16_t portal_count;
    tc_ai_portal *portals;
    uint16_t name_count;         /* level and place names the portal ops refer to */
    char **names;
    uint16_t place_count;
    tc_ai_place *places;
    bool has_transitions;
    tc_ai_transitions transitions;
    uint32_t sound_count;        /* the states' animation sound events, by state (tc_ai_state.sound_first) */
    tc_ai_sound *sounds;
    uint32_t mech_event_count;   /* MECE: {frame, mode} rows, by state (tc_ai_state.mech_first) */
    tc_ai_mech_event *mech_events;
    uint16_t water_point_set_count;
    tc_ai_water_points *water_point_sets;
    tc_zde_set zde;              /* ZDES (Ed) / the cast family's (borrowed): the hit zones, tc_zde.h */
    /* per collision triangle (the .ttlv's order, tc_tri.source) its floor's sound type (GameMaterial +8; 0xFF
     * none): the footstep switch picks step sound 0x390002 + it (docs/NOTES.md "Footsteps"). */
    uint32_t floor_sound_count;
    uint8_t *floor_sounds;
    /* The level collision floor_sounds indexes (LINK: triangle count, CRC-32 of the .ttlv's COLL chunk). */
    uint32_t link_triangles, link_crc;
    /* Optional (Special Edition levels that switch walking / skiing): MODZ, and ALTM = a whole second .ttai (the
     * skier), a copy of its bytes for tc_ai_model_load. */
    /* .ttai STRT: the normal behaviours another perso's takeover puts this one in before the initial one, a tick each
     * (the skier's B4, whose schedule sets him up, before B2 waits for Jump: tools/ttre/ai.py start_chain) */
    uint16_t start_chain[4];
    uint8_t start_chain_count;
    bool auto_start;             /* .ttai AUTO: the driver presses Jump until the initial behaviour is left (tc_ed_ai) */
    uint16_t mode_zone_count;
    tc_ai_mode_zone *mode_zones;
    uint8_t *alt_data;
    uint32_t alt_size;
} tc_ai_model;

tc_result tc_ai_model_load(tc_ai_model *model, const uint8_t *data, size_t size);
/* A model of scripts only (no states, buttons or other persos): the body of a SCPT chunk, as the .ttlv's LAIS holds
 * for the level persos that set the light (tc_level.light_persos). */
tc_result tc_ai_model_load_scripts(tc_ai_model *model, const uint8_t *data, size_t size);
/* Adds the strings and constant vectors (a .ttai's STRS body, then its VECS body) to a model of scripts: a cast
 * entry's tables (TC_CAST_TABLES). */
tc_result tc_ai_model_load_tables(tc_ai_model *model, const uint8_t *data, size_t size);
/* A model of dsgvars only (a .ttai dsg block: ai.perso_dsg_block): a perso that does not run, whose dsgvars others
 * read and write (tc_cast's ghosts). */
tc_result tc_ai_model_load_dsg(tc_ai_model *model, const uint8_t *data, size_t size);
/* Drops the per-triangle table (floor sounds) unless it was written for this level's collision: an
 * imported level runs a donor level's .ttai. Call once after loading both files. */
void tc_ai_model_link(tc_ai_model *model, const tc_level *level);
void tc_ai_model_free(tc_ai_model *model);

/* What the AI asks the mechanics for this frame (the exe's "dynamics proposal"). The host's controller
 * decides how to honour it. */
typedef struct tc_ai_motion {
    int32_t mech_mode;           /* last SetMechMode* (kept across frames), -1 none */
    /* This frame's mechanics requests in order (the exe's queue at dynamics +8, 8 entries, emptied every frame):
     * each SetMechMode* adds its mode; the card type's request handler runs once per entry. */
    uint8_t mode_count;
    int8_t modes[8];
    float proposal[32];          /* raw slots: [7..9] move direction (local), [10] speed, [0xB], [0x18] ... */
    float turn_speed;            /* radians per second around Z (TurnBy / TurnBySpeed / TurnToward), CCW */
    bool turning, stop, jump, velocity_set, axes_set;
    bool speed_set;              /* ProposalVecA: wanted speed vector, local frame, in proposal[1..3] */
    bool direction_set;          /* SetMoveDirection: proposal[7..9] direction (local), [10] speed */
    tc_vec3 velocity;            /* Velocity field written by the script (world) */
    tc_vec3 axes[3];             /* DynAxis0..2 written by the script */
    tc_vec3 jump_vector;         /* JumpBallistic(vector, height) */
    float jump_height;
    bool detach;                 /* PROC 16 (GAM_00428ea0): back to the dynamic world, off whatever carries it */
    bool heading_set;            /* face `heading` now: PROC_242, ChangeStateByDirection, TurnTowardOverFrames */
    float heading;
    bool teleport_set;           /* SetPositionFrom (PROC_27) on this perso: move it to `teleport` (feet) */
    tc_vec3 teleport;
} tc_ai_motion;

typedef struct tc_ai_slot { /* a meta-action in progress */
    uint32_t node;
    int32_t a, b;
    float v[7];                  /* the arguments it evaluated at its start (META_2Etc_0048f4c0: MoveLift's from, to, time) */
} tc_ai_slot;

typedef struct tc_ai_intel {
    uint16_t behaviour;          /* index into model->behaviours */
    uint32_t schedule_pos;       /* node the schedule resumes at (TC_AI_NONE: its start) */
    tc_ai_slot *slots;           /* [0] schedule, [1 + k] rule k */
} tc_ai_intel;

/* SetLightSet (PROC_283) calls of one step, in order (sud0's day / night cycle, Land's lightning: tc_viewer). */
#define TC_AI_MAX_LIGHT_CALLS 4
typedef struct tc_ai_light_call { int32_t set, duration_ms; bool absolute; } tc_ai_light_call;

/* Game counters the scripts share (NULL in a perso: its own). lives: the byte at *DAT_008bc840 (PROC 77 sets, 78 adds
 * one, 79 takes one; FUNC 40 reads, 41 / 42 add / take one and read): what the gold Eds count; its starting value
 * (the save data's) is not read, the port starts at 0. */
#define TC_AI_POWERS_ALL 0xFFFFu
/* A script's order to move a sector (GAM_0042a350 / 0042a270 / 0042a100), for the host's table of moving sectors (tc_cast):
 * PROC 116 Proc_TranslateSector(sector, to, ms), 117 Proc_TranslateLocalSector(sector, by, ms), 109 Proc_RotateSector(sector,
 * axis, pivot, degrees, ms). sector: its index in the level's sectors (.ttlv SECT; the script's type 27 node). */
#define TC_AI_MAX_SECTOR_ORDERS 8
enum { TC_AI_SECTOR_TO, TC_AI_SECTOR_BY, TC_AI_SECTOR_TURN };
typedef struct tc_ai_sector_order { uint16_t sector; uint8_t kind; tc_vec3 a, b; float angle; int32_t ms; } tc_ai_sector_order;
/* The HUD's text slots (docs/ENGINE.md "The HUD"): the exe's twenty records at DAT_008a36c0 (0x24 each), written by
 * PROC 149-155 and drawn each frame by GAM_00418d70. The host draws them (tc_hud.h). */
#define TC_AI_HUD_SLOTS 20
enum { TC_AI_HUD_STRING = 0, TC_AI_HUD_NUMBER = 1, TC_AI_HUD_BOSS = 2, TC_AI_HUD_CLOCK = 4 };
typedef struct tc_ai_hud_slot {
    uint8_t kind;                /* TC_AI_HUD_* (record +0) */
    uint8_t active;              /* +4: shown */
    uint8_t backdrop;            /* +0x20: the text window's box behind it (a string's, until PROC 149 takes it off) */
    uint8_t font;                /* the font's rank in the fix's list (hud.tthd FONT), 0xFF: the text's own */
    uint16_t text;               /* +0x18: the text (game.dsb's record) */
    int32_t frames;              /* +0x14: frames left, counted down as it is drawn; the scripts give -1 */
    float x, y;                  /* +0xC, +0x10: where, font units about the screen's middle */
    int32_t value;               /* a number's value, a clock's milliseconds; a boss bar: the perso id (0 Me = owner) */
    int32_t owner;               /* a boss bar: the perso that asked (its id), to resolve value 0 */
    uint16_t head;               /* a boss bar: the text whose first letter is the head beside the points */
} tc_ai_hud_slot;

typedef struct tc_ai_globals {
    uint8_t lives;
    /* Ed's powers: the u16 at *DAT_004ec6d4, a set of masks (7, 8, 0x70, 0x80, 0x100, 0x200 in the scripts). COND 90
     * Cond_TestPower(m) = (powers & m) == m (0x419680); PROC 200 sets them, 201 Proc_SubPower clears m (0x419660), 202
     * Proc_AddPower adds m (0x419650). Port: they start all on (TC_AI_POWERS_ALL: every ability, every story test
     * passes), as the port has run so far; the save data's start value is not read. Not in the recordings' hash yet. */
    uint16_t powers;
    tc_ai_sector_order sector_orders[TC_AI_MAX_SECTOR_ORDERS]; /* this tick's, in order: the host takes them */
    uint8_t sector_order_count;
    /* The save values (31 i16 at DAT_004ec6d0, GAM_004195f0 / GAM_00419620): FUNC 93 reads one, PROC 143 adds one, PROC
     * 144 sets one (story progress, the bees ...). Their starting values are the save data's: the port starts at 0. */
    int16_t values[31];
    /* Port: where the main actor comes back after dying (GameAction0, tc_ed_ai): where he stood when a save point (the
     * cast's DFM_Save, TC_CAST_SAVE_POINT) took itself out; none: his start. */
    bool has_checkpoint;
    tc_vec3 checkpoint;
    float checkpoint_heading;
    /* The camera's direction from the level's scripts (the cast; the main actor's own camera writes go to his camera
     * perso's dsgvars instead): PROC 177 SetCameraMode (record +0x98; 0, the follow, until set), 180 SetCameraTarget (cine +0x10: a
     * perso id, 0 none), 178 ChangeFocal (record +0x8C: the horizontal field of view, radians; 0 none). The host shoots
     * mode 6 (FUN_004a3130 case 6: no displacement, request flags 0x514: it stays and turns to the target); the
     * others are only recorded. cam_serial counts the writes that change something. */
    /* FUNC 59-66 (FUNC_58Etc_0049cc70: Function_Camera_GetDistMin, ...Max, GetBoundDistMin, ...Max, GetAlpha,
     * GetShiftAlpha, GetTeta, GetShiftTeta = the camera record's +0x30 .. +0x4C): with a first argument of 0 the default
     * record's (TC_AI_CAMERA_DEFAULT_RECORD, the same in every level), else the current one: host-fed here from the
     * play camera (has_cam_now false: the defaults too). */
    float cam_now[8];
    bool has_cam_now;
    int16_t cam_mode;
    int32_t cam_perso;           /* the camera perso PROC 177 named (its first argument), 0 none */
    int32_t cam_target;
    float cam_focal;
    uint32_t cam_serial;
    /* PROC 210 DisplayVignetteDuring(picture, ms) (SE Proc_DisplayVignetteDuring, its 0x40f9e0: a flag, the name and
     * the time; the main loop then shows the picture and waits, the game held, 0x40fa20): the latest request, as the
     * script gave it ("Credits\\Credit06.bmp"); vignette_serial counts the requests. */
    char vignette[64];
    uint32_t vignette_ms;
    uint32_t vignette_serial;
    /* META 7 Action_SpeakAndWaitEnd (docs/ENGINE.md "Speech"): the line on show (1-based, the level's dialogues; 0 none) and
     * who says it (DAT_004ec6bc). The host runs it and ends it (tc_ai_speech_end). speech_host: it does; else a line ends
     * at once. */
    uint32_t speech;
    struct tc_ai_perso *speaker;
    bool speech_host;
    bool speech_stop;            /* PROC 104 Proc_StopSpeech (GAM_00418790): the host ends the line */
    /* The HUD (docs/ENGINE.md "The HUD"): the text slots, and the note the exe shows when one of Doc's six objects
     * (save values 1-6) changes (0x41c010): which, and a count of the notes. */
    tc_ai_hud_slot hud[TC_AI_HUD_SLOTS];
    uint8_t hud_doc;
    uint32_t hud_doc_serial;
    /* The level's inventory objects by perso id (host-fed: tc_level.cast.inv), NULL none. */
    const tc_ai_inv_entry *inv;
    uint16_t inv_count;
} tc_ai_globals;

/* A new game's progress (GAM_00419290): no powers; save value 0 (the bees) 0, 1-6 (Doc's six objects) -1, the rest 0. */
void tc_ai_globals_new_game(tc_ai_globals *g);

#define TC_AI_MAX_WAYS 8 /* ways per perso (the 21 retail levels: 3 at most) */
/* A perso list dsgvar (type 11: byte 0 its count, byte 1 its capacity, then the persos; CollectNearby fills one): the
 * port keeps the persos here, by dsgvar, and writes the count into byte 0. */
#define TC_AI_MAX_LISTS 4
#define TC_AI_LIST_CAP 8
typedef struct tc_ai_list { uint32_t dsg; uint8_t count; int32_t ids[TC_AI_LIST_CAP]; } tc_ai_list;

typedef struct tc_ai_perso tc_ai_perso;
struct tc_ai_perso {
    const tc_ai_model *model;
    /* Host -> AI, every frame before tc_ai_step. */
    uint32_t inputs;             /* 1 << TC_AI_IN_* held */
    float stick_x, stick_y;      /* -1..1, y forward; 0 with a keyboard (the scripts then use the buttons) */
    tc_vec3 position, velocity;  /* feet, world */
    float heading;               /* as tc_player: local -Y points along (sin, -cos) */
    uint32_t contacts;           /* TC_AI_CONTACT_* */
    uint32_t report_high;        /* host-fed: the report bits above them (0x10000 | 0x20000: on a perso; COND 37) */
    int32_t standing_on;         /* host-fed: the perso it rides (FUNC 54, Func_GetFather), 0 none */
    bool mech_moving;            /* host-fed: its mechanics moved it last step (dynamics velocity_snap != 0: MoveLift's
                                    end test, META_28Cont_004a09c0) */
    struct tc_collision *world;  /* host-fed: FUNC 162's probe (NULL: nothing in the way) */
    /* The collide flags of the body part (tc_ai_sphere.flags) that made the ground and wall contacts, 0 none: the
     * material conditions read these (ContactRecord +0x70, the own material; COND 44-49, FUN_00497890). */
    uint16_t ground_material, wall_material;
    float report_2bc, report_2c0; /* dynamics +0x2BC / +0x2C0 (FUNC 91 / 92): in flight the lagged attack / bank, rad */
    bool has_axes;               /* host-fed: the perso's drawn axes (its world matrix's columns: tilted in flight and
                                    swimming); FUNC 102 / 103 (point LocalToGlobal / GlobalToLocal) use them */
    tc_vec3 axes[3];
    float env_depth;             /* dynamics +0x2E0 (FUNC 51 EnvHeight; 52 adds the perso's z): how far below a water
                                    surface, 0 out of the water (host-fed: tc_mec_env_at, or the swim forces' probe) */
    /* The water zones (host-fed, GAM_00437900 / MEC_004c0fb5): per kind 0..6 the water flag at the origin (0) and at the
     * playing state's sample points (1..6), -1 = no such point; zone_combined = the value all present kinds 1..6 share,
     * 3 when they differ, kind 0's without points. COND 70..74 read them. */
    int8_t zone[7];
    int8_t zone_combined;
    tc_vec3 *other_positions;    /* per model->others (the camera's eye for role 1) */
    const char *level_name;      /* LevelIs, compared case-insensitively ("bar") */
    int32_t hit_points;          /* FUNC_34 / PROC_66 on Ed (inferred) */
    bool screen_covered;         /* GameState63: the cover transition has finished (the screen is black) */

    /* AI state. */
    uint32_t time_ms;            /* engine clock (GetTime) */
    float dt;
    uint8_t *dsg;                /* own dsgvar memory */
    uint8_t **other_dsg;
    int32_t *button_counter;     /* > 0 frames held (1 = just pressed), < 0 frames released (-1 just released) */
    tc_ai_intel normal, reflex;
    bool running_reflex;
    uint32_t rng;

    /* AI -> host. */
    uint16_t state;              /* state whose animation plays (a transition's intermediate, a next state ...) */
    uint16_t wanted_state;       /* state the scripts last asked for (InState); animation chaining keeps it */
    /* host-fed (the cast, on the main actor where another perso is the player: the skier): the fix Ed's id and his own
     * dsgvars. His id stands for the main actor (hit points, position: CAST main_alias), but his dsgvars are his own,
     * as in the exe (Totalski's race: dsg148 the stage, dsg149 the best time, dsg150): Dot(alias, k) goes there. */
    int32_t alias_id;
    struct tc_ai_perso *alias_dsg;
    uint16_t zone_state;         /* host-fed (the cast, each tick before any AI runs): 1 + the state whose ZDE zones the
                                    hit tests see this tick; 0: `state`. The exe applies a script's state change after
                                    every AI of the frame has run (the animation player), so each perso tests the zones of
                                    the state it began the frame in: cavedoc's targets go to their hit state (zones off)
                                    on the corn's kernel in the same tick the kernel tests them for its rebound. Port
                                    approximation of that order (read: the effect, not the exe's code path) */
    float state_frame;           /* animation frames into the playing state */
    uint8_t frame_rate;          /* frames per second: the state's, scaled by Proc10 (exe +0x163) */
    uint8_t repeats_left;
    bool state_ended;            /* the animation finished with no repeat and no next state (exe +0x161) */
    uint32_t state_changes;      /* incremented on every new animation, so the host can restart it */
    uint8_t sound_counter[TC_AI_MAX_STATE_EVENTS], mech_counter[TC_AI_MAX_STATE_EVENTS]; /* the playing state's events */
    tc_ai_motion motion;         /* reset at the start of every step */
    bool display_flag8;          /* PROC_204 sets, PROC_203 clears (paired with opaque/translucent) */
    uint8_t transparency;        /* 255 opaque */
    uint64_t hidden_channels;    /* PROC_120 / PROC_121, bit = channel */
    float float13, float14;      /* FIELD 13 / 14 */
    int32_t change_level;        /* ChangeLevel's string index, -1 none */
    int32_t change_place;        /* PROC 29 ChangeMapAtPosition's arrival place and camera place (string indexes: level */
    int32_t change_camera_place; /* persos by instance name), -1 none (ChangeLevel: none) */
    /* Another perso's SetPositionFrom on this one (PROC 27, or PROC 8's pair: AI_00488010): its host places it (the main
     * actor's driver at his next step, the cast before this perso's next step, the camera's host its camera). Not when
     * the asking perso is kept off the main actor (keep_off_main) and this is he. */
    bool place_requested;
    tc_vec3 place_at;
    bool cover_requested;        /* GameAction2: start the cover transition; the host clears it */
    bool restart_requested;      /* GameAction0 (GAM_Objinit_00419e50 -> engine state 9: back from the last save); the
                                    host clears it (tc_ed_ai: the port's respawn) */
    /* Sound events started this step (animation events passed, PlaySound3D): global event indices; reset each step. */
    bool brisk;                  /* port: its META 29 waits are cut short: a scene's camera pauses, with no camera to wait for */
    uint32_t speaking;           /* 3dData +0x1A0: the line this perso is saying, 0 none */
    bool env_water;              /* host-fed: its environment is water (COND 51, dynamics +0x23C) */
    bool anim_end_reached;       /* 3dData +0x162: the animation reached its end this advance (WaitEndOfAnim) */
    uint16_t sounds[TC_AI_MAX_SOUNDS];
    uint8_t sound_count;
    tc_ai_light_call light_calls[TC_AI_MAX_LIGHT_CALLS]; /* SetLightSet calls this step; reset each step */
    uint8_t light_call_count;
    uint8_t g_flags, g_armed;    /* Proc9's deferred result (exe DAT_0051b019 / DAT_0051b018) */

    /* The cast (tc_cast.h). find: another perso by id (perso nodes: level perso k + 1), or NULL; its dsgvars, position,
     * behaviours, hit points and activity are then the live ones (without: this model's copies, tc_ai_other, and the
     * host's other_positions). */
    tc_ai_perso *(*find)(void *user, int32_t id);
    void *find_user;
    int32_t main_actor;          /* KW_5 (the main actor) as a perso id: 0 = this perso is it (Ed) */
    int32_t world_id;            /* KW_6 (World: the level's world perso) as a perso id, 0 none (tc_cast_data.world) */
    int32_t self_id;             /* this perso's id (0: unknown): a perso stored in a dsgvar is kept as its id, so
                                    "Me" written into another perso's dsgvar names the writer there */
    float main_distance2;        /* host-fed: squared distance from this perso's origin to the main actor's
                                    (GAM_0042a680, DAT_008bc744; COND 61) */
    tc_ai_globals *globals;
    tc_ai_globals own_globals;
    /* Ways and waypoints (host-fed; docs/ENGINE.md "Ways and waypoints"): the level's waypoints (.ttlv WAYS; NULL none)
     * and this perso's own ways (perso +0x1C; NULL none). Per way its cursor, the MS_Way node's +0x10..+0x13: the
     * waypoint it heads for, the link step, the waypoint it came from (0xFF none), the saved link step; tc_ai_init
     * starts them as the snapshots hold them (0, 0, 0xFF, 0xFF). */
    const tc_ways *ways;
    const tc_perso_ways *own_ways;
    uint8_t way_cursor[TC_AI_MAX_WAYS][4];
    /* Hits and spawning (host-fed, the cast; docs/ENGINE.md "Shooting"): its family's index (T22 nodes), its perso
     * lists, the live persos one by one (each(user, k) for k = 0, 1, ... until NULL: whose ZDE zones the conditions
     * test), and META 11's instancing of a spawnable template (spawn: the instance's AI, placed at `by`, or NULL). */
    uint16_t family_type;
    tc_ai_list lists[TC_AI_MAX_LISTS];
    tc_ai_perso *(*each)(void *user, uint32_t k);
    tc_ai_perso *(*spawn)(void *user, int32_t template_id, const tc_ai_perso *by, tc_vec3 launch);
    /* The std game's activity (host-run, GAM_00428070 / GAM_PersoFrame_0042b520): the AI runs while active and the main
     * actor is within the radius (radius2 < 0: always); the animation plays with run_mechanics or run_animation. PROC 5
     * / 6 (de)activate, PROC 17-22 set the rest, PROC 257 takes the perso out: inactive, then dead (PROC 5 cannot wake
     * it) unless `reinit`, when the host starts it again from its initial behaviour (reinit_requested). */
    bool active, dead, reinit, reinit_requested;
    uint8_t run_mechanics, run_animation;
    float radius2;
    int32_t hit_points_max;      /* FUNC 37, std game +0x15 (Ed: the port's 4) */
    int32_t hit_points_max_init; /* the max it started with (PROC 73) */
    int32_t hit_points_ceiling;  /* std game +0x16, the max's ceiling (PROC 74 / 75); not exported: 255 (no cap) */
    int32_t forced_by;           /* diagnostics: the id of the perso whose PROC 237 / 238 last set this one's behaviour */
    const tc_cast_entry *zones_of; /* host-fed (the cast): its ZDD zones for COND 13 (NULL: not modelled) */
    bool zones_follow;           /* ... in its live axes (position, heading: the main actor), not zones_of->zone_inverse */
    /* Port (the cast): PROC 257 / 6 from this perso take another out (or itself) only when the main actor was near
     * the target lately (near_main_ms, host-fed: within the last TC_AI_REMOVAL_MS): the pickups. Otherwise (a story
     * check: Doc already rescued as the port's flags say) the target stays as a prop: frozen, drawn, not thinking. */
    bool removal_guard;
    bool level_restored;         /* host-set (tc_cast.c load_actor): a level-saved dsgvar came back changed from an earlier
                                    visit, so taking itself out is its own saved state's doing (a pickup already taken),
                                    not a story check: the removal guard lets it go */
    uint32_t near_main_ms;       /* host-fed: the clock when the main actor was last within reach (0 never) */
    bool frozen;
    uint32_t removals_kept;      /* diagnostics */
    bool read_only_for_cast;     /* port: the cast does not write this perso's dsgvars (the camera's copy, which the main
                                    actor's scripts read: the cutscene triggers' camera modes would hold him), except
                                    TC_AI_CAMERA_SHOT_DSG */
    bool keep_off_main;          /* port: this perso's PROC 237 / 238 and dsgvar writes leave the main actor alone (the
                                    cast, unless allowed: tools/ttre/cast.py MAY_FORCE_MAIN) */
    uint32_t forces_blocked;     /* diagnostics: how many times */

    /* Diagnostics: use counts of the primitives the interpreter skipped, [kind * TC_AI_UNKNOWN_STRIDE + index]
     * (kind = node type 0-9). */
    uint32_t *unknown;
    /* Optional: called after every executed statement (node index, result flags) for debugging. */
    void (*trace)(void *user, uint32_t node, uint32_t flags);
    void *trace_user;
};

/* A model without states runs its scripts only (no animation clock). */
tc_result tc_ai_init(tc_ai_perso *p, const tc_ai_model *model);
void tc_ai_free(tc_ai_perso *p);
/* One engine frame: buttons, reflex then normal intelligence, then the state's animation clock. */
void tc_ai_step(tc_ai_perso *p, float dt);
/* A step in parts, for the cast: the clock (time_ms) is the host's; think = run the AI (reflex, then normal);
 * animate = the state's animation clock and its events. tc_ai_step = the clock, think and animate. */
void tc_ai_step_parts(tc_ai_perso *p, float dt, bool think, bool animate);
/* Another perso sets this one's normal behaviour (PROC_237): it starts from its beginning on the next step. */
void tc_ai_force_behaviour(tc_ai_perso *p, uint16_t behaviour);
/* The same for the reflex (PROC_238); reflex = index among the reflexes. */
/* The line is over (GAM_004182c0): its speaker's meta-action goes on. */
void tc_ai_speech_end(tc_ai_globals *g);
/* GAM_00437260: this perso into a state, as the scripts' ChangeState: false when it may not. */
bool tc_ai_set_state(tc_ai_perso *p, uint32_t state);
void tc_ai_force_reflex(tc_ai_perso *p, uint16_t reflex);

/* Raw dsgvar access for tests/debug UIs: value of dsgvar i of the perso as float (ints converted). */
float tc_ai_dsgvar_float(const tc_ai_perso *p, uint32_t i);
/* The camera perso's dsgvar that picks its shot (camera reflex 2: 5 the ski follow, 6 a fixed place; tc_cam.h
 * TC_CAM_SHOT_*): the cast may write it to the camera (read_only_for_cast's exception). */
#define TC_AI_CAMERA_SHOT_DSG 39
/* The fixed shot's parameters (shot 6: camera B8 -> B7; docs/CAMERA.md "The fixed shot"): */
#define TC_AI_CAMERA_SHOT_FIXED 6    /* shot 6: the fixed shot (tc_cam.h TC_CAM_SHOT_FIXED) */
/* The camera's default parameter record, +0x30 .. +0x4C (docs/CAMERA.md "The parameter record"): distance min / max, the
 * bound distances, the horizontal angle and its tolerance, the elevation and its tolerance. */
#define TC_AI_CAMERA_DEFAULT_RECORD {8.0f, 8.0f, 5.0f, 8.5f, 0.0f, 0.15f, 0.6f, 0.0f}
/* The camera's dsgvars that make its shots (the shot dsg39, its pick-up flags 41 / 44 / 67 / 68, the follows' 76-78, the
 * rail's 82-85, the fixed shot's 89-96, the axes' 100-110): the cast may write these; the main actor's scripts never
 * read them (they use 1, 18, 20, 38, 40, 53, 69, 71, 140). */
#define TC_AI_CAMERA_SHOT_PARAM(i) ((i) == 39 || (i) == 41 || (i) == 44 || (i) == 67 || (i) == 68 || \
                                    ((i) >= 76 && (i) <= 78) || ((i) >= 82 && (i) <= 96) || ((i) >= 100 && (i) <= 110))
#define TC_AI_CAMERA_FIXED_GLIDE 89  /* 1: it moves to the place (META 30), 0: it is put there */
#define TC_AI_CAMERA_FIXED_TARGET 91 /* 1: it looks at its target perso (Ed), 0: at the point */
#define TC_AI_CAMERA_FIXED_PLACE 92  /* the place (a vector) */
#define TC_AI_CAMERA_FIXED_POINT 93  /* the point it looks at (a vector) */
#define TC_AI_CAMERA_FIXED_STEPS 94  /* the point's ease: 1 / this of the way per tick (dsg95 1) */
#define TC_AI_CAMERA_FIXED_EASE 95
/* The same for another perso's dsgvar i as this AI holds it (model->others[other]; what this perso's scripts wrote
 * there): 0 when there is no such perso or dsgvar. */
float tc_ai_other_dsgvar_float(const tc_ai_perso *p, uint32_t other, uint32_t i);
/* The same as a vector: false when it is not one. */
bool tc_ai_other_dsgvar_vec(const tc_ai_perso *p, uint32_t other, uint32_t i, tc_vec3 *out);
/* Another perso writes this one's dsgvar i (Assign(Dot(perso, dsg<i>), ...)); perso ids go in as ints. */
void tc_ai_dsgvar_set_int(tc_ai_perso *p, uint32_t i, int32_t v);
void tc_ai_dsgvar_set_float(tc_ai_perso *p, uint32_t i, float v);
void tc_ai_dsgvar_set_vec(tc_ai_perso *p, uint32_t i, tc_vec3 v);

#ifdef __cplusplus
}
#endif
#endif

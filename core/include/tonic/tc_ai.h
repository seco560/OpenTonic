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
typedef struct tc_ai_dsgvar { uint32_t offset; uint8_t type; } tc_ai_dsgvar;
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
/* A mechanics ID card (MEC hidcard): type 0-12 picks the engine's handler pair; words = the card after its type
 * word, raw (mostly f32; see docs/NOTES.md "Ed's mechanics ID cards" and "MEC physics"). */
typedef struct tc_ai_card { uint32_t type; uint32_t word_count; uint32_t words[19]; } tc_ai_card;
/* A state's mechanics collision body (the collide set's ZDM zones active in it): spheres in the perso's axes. */
#define TC_AI_MAX_SPHERES 12
/* One part of the mechanics body: a sphere, or a point (point = 1, radius 0: collide element type 5, e.g. Ed's hand
 * point 1.3 ahead and 2.0 up that catches ledges). flags: its collide material identifier (GameMaterial +0xC -> u16
 * +2; a point's from its vertex's material; ZDMF chunk): 0x10 = no collision, 8 = floors only (a separate sweep),
 * 1 = may step up (zone type 1). The material conditions (GroundMaterialIs ...) test the flags of the part that made
 * the contact. See tc_mec_sweep. */
typedef struct tc_ai_sphere {
    tc_vec3 center;
    float radius;
    uint16_t flags;
    uint8_t point;
    float rebound, grip; /* its own mechanics material ([1], [2]; none: 1, 1), times the obstacle's in the responses */
} tc_ai_sphere;
typedef struct tc_ai_sphere_set { uint16_t count; tc_ai_sphere spheres[TC_AI_MAX_SPHERES]; } tc_ai_sphere_set;
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
#define TC_AI_MAX_ZONE_PRIMS 8
typedef struct tc_ai_portal {
    tc_vec3 position;
    float radius;                /* the capture distance, unless the portal has a zone */
    uint8_t zone_count;          /* ZONE: the capture is the player's origin inside the perso's ZDD zone (COND_13) */
    float zone_inverse[12];      /* row-major 3x4: world -> the perso's axes */
    tc_ai_zone_prim zone[TC_AI_MAX_ZONE_PRIMS];
    float rows[9];               /* the portal's axes, row-major (its local -Y faces the way in) */
    uint16_t perso;              /* as perso nodes (level perso k + 1) */
    uint8_t fire;
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

/* .ttai WATR: a water surface (docs/NOTES.md "Swimming"): a horizontal plane at z with the mechanical environment below
 * it, counted only inside the bounding boxes of the sectors that list it (the exe asks the perso's sector; the port
 * has no sectors). boxes = box_count x {min xyz, max xyz}. */
typedef struct tc_ai_water {
    float z;
    float gravity, density, drag;    /* the environment below: g, density (+0x14, water 1000), drag coefficient (+0x10) */
    uint16_t box_count;
    tc_vec3 *boxes;
} tc_ai_water;

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
    uint16_t water_count;
    tc_ai_water *water;
    uint16_t dry_box_count;      /* the boxes of sectors without water (min, max pairs); the smallest box holding a
                                  * point decides (tc_mec_env_at) */
    tc_vec3 *dry_boxes;
    uint16_t water_point_set_count;
    tc_ai_water_points *water_point_sets;
    /* per collision triangle (the .ttlv's order, tc_tri.source) its floor's sound type (GameMaterial +8; 0xFF
     * none): the footstep switch picks step sound 0x390002 + it (docs/NOTES.md "Footsteps"). */
    uint32_t floor_sound_count;
    uint8_t *floor_sounds;
    /* The level collision floor_sounds indexes (LINK: triangle count, CRC-32 of the .ttlv's COLL chunk). */
    uint32_t link_triangles, link_crc;
} tc_ai_model;

tc_result tc_ai_model_load(tc_ai_model *model, const uint8_t *data, size_t size);
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
    bool heading_set;            /* face `heading` now: PROC_242, ChangeStateByDirection, TurnTowardOverFrames */
    float heading;
    bool teleport_set;           /* SetPositionFrom (PROC_27) on this perso: move it to `teleport` (feet) */
    tc_vec3 teleport;
} tc_ai_motion;

typedef struct tc_ai_slot { uint32_t node; int32_t a, b; } tc_ai_slot; /* a meta-action in progress */

typedef struct tc_ai_intel {
    uint16_t behaviour;          /* index into model->behaviours */
    uint32_t schedule_pos;       /* node the schedule resumes at (TC_AI_NONE: its start) */
    tc_ai_slot *slots;           /* [0] schedule, [1 + k] rule k */
} tc_ai_intel;

typedef struct tc_ai_perso {
    const tc_ai_model *model;
    /* Host -> AI, every frame before tc_ai_step. */
    uint32_t inputs;             /* 1 << TC_AI_IN_* held */
    float stick_x, stick_y;      /* -1..1, y forward; 0 with a keyboard (the scripts then use the buttons) */
    tc_vec3 position, velocity;  /* feet, world */
    float heading;               /* as tc_player: local -Y points along (sin, -cos) */
    uint32_t contacts;           /* TC_AI_CONTACT_* */
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
    uint32_t global_flags;       /* GlobalFlag?(n) (abilities?): bit n; all set by default */
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
    bool cover_requested;        /* GameAction2: start the cover transition; the host clears it */
    /* Sound events started this step (animation events passed, PlaySound3D): global event indices; reset each step. */
    uint16_t sounds[TC_AI_MAX_SOUNDS];
    uint8_t sound_count;
    uint8_t g_flags, g_armed;    /* Proc9's deferred result (exe DAT_0051b019 / DAT_0051b018) */

    /* Diagnostics: use counts of the primitives the interpreter skipped, [kind * TC_AI_UNKNOWN_STRIDE + index]
     * (kind = node type 0-9). */
    uint32_t *unknown;
    /* Optional: called after every executed statement (node index, result flags) for debugging. */
    void (*trace)(void *user, uint32_t node, uint32_t flags);
    void *trace_user;
} tc_ai_perso;

tc_result tc_ai_init(tc_ai_perso *p, const tc_ai_model *model);
void tc_ai_free(tc_ai_perso *p);
/* One engine frame: buttons, reflex then normal intelligence, then the state's animation clock. */
void tc_ai_step(tc_ai_perso *p, float dt);
/* Another perso sets this one's normal behaviour (PROC_237): it starts from its beginning on the next step. */
void tc_ai_force_behaviour(tc_ai_perso *p, uint16_t behaviour);
/* The same for the reflex (PROC_238); reflex = index among the reflexes. */
void tc_ai_force_reflex(tc_ai_perso *p, uint16_t reflex);

/* Raw dsgvar access for tests/debug UIs: value of dsgvar i of the perso as float (ints converted). */
float tc_ai_dsgvar_float(const tc_ai_perso *p, uint32_t i);
/* Another perso writes this one's dsgvar i (Assign(Dot(perso, dsg<i>), ...)); perso ids go in as ints. */
void tc_ai_dsgvar_set_int(tc_ai_perso *p, uint32_t i, int32_t v);
void tc_ai_dsgvar_set_float(tc_ai_perso *p, uint32_t i, float v);
void tc_ai_dsgvar_set_vec(tc_ai_perso *p, uint32_t i, tc_vec3 v);

#ifdef __cplusplus
}
#endif
#endif

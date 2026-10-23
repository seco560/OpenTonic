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
    float drop;                  /* how far the state's pose reaches below the perso's origin (v3; 0 before) */
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
/* Field of a card as f32, by its byte offset in the engine's card (+4 = words[0]); 0 if out of range. */
float tc_ai_card_f32(const tc_ai_card *c, uint32_t offset);

typedef struct tc_ai_model {
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
    uint16_t card_count;         /* 0 in a version 1 file */
    tc_ai_card *cards;
} tc_ai_model;

tc_result tc_ai_model_load(tc_ai_model *model, const uint8_t *data, size_t size);
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
    float report_2bc, report_2c0; /* dynamics +0x2BC / +0x2C0 (FUNC 91 / 92): in flight the lagged attack / bank, rad */
    tc_vec3 *other_positions;    /* per model->others (the camera's eye for role 1) */
    const char *level_name;      /* LevelIs, compared case-insensitively ("bar") */
    uint32_t global_flags;       /* GlobalFlag?(n) (abilities?): bit n; all set by default */
    int32_t hit_points;          /* FUNC_34 / PROC_66 on Ed (inferred) */

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
    tc_ai_motion motion;         /* reset at the start of every step */
    bool display_flag8;          /* PROC_204 sets, PROC_203 clears (paired with opaque/translucent) */
    uint8_t transparency;        /* 255 opaque */
    uint64_t hidden_channels;    /* PROC_120 / PROC_121, bit = channel */
    float float13, float14;      /* FIELD 13 / 14 */
    int32_t change_level;        /* ChangeLevel's string index, -1 none */
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

/* Raw dsgvar access for tests/debug UIs: value of dsgvar i of the perso as float (ints converted). */
float tc_ai_dsgvar_float(const tc_ai_perso *p, uint32_t i);

#ifdef __cplusplus
}
#endif
#endif

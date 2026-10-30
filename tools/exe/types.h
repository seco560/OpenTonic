/* TonicTrouble.exe engine structs (unpacked dump fea1140e..., x86 32-bit), for the Ghidra project (docs/TYPES.md).
 *
 * Applied by tools/ghidra/ApplyTypes.java (category /TT); offsets checked by tools/exe/types_check.c. Only what the
 * exe or the data shows: every field cites the function that reads / writes it (or the exporter that reads it from
 * the level files); unknown bytes are unk_XX / _pad_XX so that offsets stay exact. Sizes are the known minimum unless
 * a constructor gives them. Ghidra's own names (FUN_ / MEC_ / GAM_<addr>) are kept in the citations so they can be
 * grepped in docs/NOTES.md and the raw decompiles.
 *
 * Plain C, no includes: the Ghidra C parser reads this file as it is (comments are stripped first). */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef short i16;
typedef int i32;

typedef struct SuperObject SuperObject;
typedef struct Perso Perso;
typedef struct Perso3dData Perso3dData;
typedef struct StdGame StdGame;
typedef struct Dynam Dynam;
typedef struct Dynamics Dynamics;
typedef struct DynamWork DynamWork;
typedef struct State State;
typedef struct Family Family;
typedef struct AnimRef AnimRef;
typedef struct GameMaterial GameMaterial;
typedef struct Mind Mind;
typedef struct GeoCollide GeoCollide;
typedef union IdCard IdCard;
typedef struct AINode AINode;

typedef struct Vec3 { float x, y, z; } Vec3;

/* A position or a velocity with its rotation: linear part, then an angle (rad, or rad/s) about an axis. The type 0/4
 * integrator folds it to angle x axis.z about (0, 0, 1) after each step (FUN_004aa6af), i.e. a heading (yaw). */
typedef struct Motion {
    Vec3 v;                     /* +0x00 */
    float angle;                /* +0x0C */
    Vec3 axis;                  /* +0x10 */
} Motion;                       /* 0x1C */

/* Engine matrix (tools/ttre/level.py: the exporter reads every perso and IPO matrix this way). Type bits (asm of
 * MTH_SetTranslation_0047c3d0 / MTH_SetRotation_0047bc80 / MTH_GetTranslation_0047c410): 0x80 = this full layout
 * (else a compact form at +2, FUN_0047e100 / 0047e150), 0x10 = has a translation, 0x40 = has a scale (transform =
 * rot x scale), 0x100 kept by MTH_SetType_0047afd0. Seen: 0x80 identity, 0x90, 0xD0, 0x1B0, 0x1F0. */
typedef struct Matrix {
    u32 type;                   /* +0x00 */
    Vec3 pos;                   /* +0x04 */
    float rot[9];               /* +0x10 columns */
    float transform[9];         /* +0x34 rot x scale, columns */
} Matrix;                       /* 0x58 */

/* A {first, last, count} list head (level.py: SuperObject children, Family states, State transitions). */
typedef struct ListHead { void *first; void *last; u32 count; } ListHead;

/* ---- Hierarchy ---- */

typedef union SuperObjectData {
    Perso *perso;               /* type 4 (all the mechanics and AI code sees) */
    void *raw;                  /* type 8 sector, 0xD / 0x15 IPO, 0 world */
} SuperObjectData;

/* SuperObject (level.py, 0x38). GAM_0042b2b0: so->type == 4 and so->data = Perso; walks so->parent (+0x1C) up to the
 * first type 4 ancestor (the carrier) for the integrator. */
struct SuperObject {
    u32 type;                   /* +0x00 0 world, 4 perso, 8 sector, 0xD / 0x15 IPO */
    SuperObjectData data;       /* +0x04 */
    ListHead children;          /* +0x08 */
    SuperObject *next;          /* +0x14 */
    SuperObject *prev;          /* +0x18 */
    SuperObject *parent;        /* +0x1C GAM_0042b2b0, GAM_0042b0e0, FUN_0043bff0 */
    Matrix *matrix;             /* +0x20 local, relative to the parent (level.py) */
    Matrix *matrix_world;       /* +0x24 GAM_0042a720 takes the world position from it for the activation distance;
                                         level.py calls it static_matrix (raymap: global matrix) */
    i32 unk_28;                 /* +0x28 */
    u32 draw_flags;             /* +0x2C */
    u32 flags;                  /* +0x30 */
    void *bounding_volume;      /* +0x34 */
};                              /* 0x38 */

/* perso +0x24. Its +4 is the sector SuperObject the integrators get as their 4th argument (GAM_0042b0e0 passes
 * FUN_00451790(perso->sector_info)); FUN_00451b90 checks that its type is 8 and looks the mechanical environment up
 * in the sector's data. GAM_0042c810 (every frame, after the mechanics) moves it on: FUN_00451670(+0xC, position,
 * &sector) walks the segment from the last position through the current sector's portal faces (sector data +0, a
 * GeometricObjectCollide whose type 1 elements hold the sector they lead into at +0); a change takes the perso out of
 * the old sector's character list (+0x1C, its node +8) into the new one's. After a teleport the exe sets it from
 * FUN_00451240 (point -> sector, straight down; GAM_00429150, GAM_00427b80). docs/NOTES.md "Sectors". */
typedef struct SectorInfo {
    SuperObject *sector_copy;   /* +0x00 set to +4 after each change (FUN_00452900) */
    SuperObject *sector;        /* +0x04 */
    void *list_node;            /* +0x08 the perso's node in its sector's character list (FUN_004518c0 / 00451a60) */
    Vec3 last_position;         /* +0x0C where the last crossing walk ended */
} SectorInfo;

/* ---- Collision geometry and zones (tools/ttre/level.py, ai.py zdm_sets: the exporter reads them from every level) ---- */

/* Collide element type 1, indexed triangles (TT, 0x20). */
typedef struct CollideTriangles {
    GameMaterial *material;     /* +0x00 */
    u16 n_triangles;            /* +0x04 */
    u16 n_mapping;              /* +0x06 */
    i16 *triangles;             /* +0x08 3 vertex indices each */
    void *mapping;              /* +0x0C */
    Vec3 *face_normals;         /* +0x10 */
    void *uvs;                  /* +0x14 */
    void *unk_18;               /* +0x18 */
    u16 unk_1c;                 /* +0x1C */
    i16 parallel_box;           /* +0x1E */
} CollideTriangles;

/* Element types 7 (spheres) and 8 (axis-aligned boxes): {n, entries}; the entries follow inline. */
typedef struct CollideSphere {
    u16 centre;                 /* +0x00 vertex index */
    u16 unk_02;                 /* +0x02 */
    float radius;               /* +0x04 */
    GameMaterial *material;     /* +0x08 */
} CollideSphere;
typedef struct CollideBox {
    u16 min;                    /* +0x00 vertex index */
    u16 max;                    /* +0x02 vertex index */
    GameMaterial *material;     /* +0x04 */
} CollideBox;
typedef struct CollideSpheres { u32 count; CollideSphere *entries; } CollideSpheres;
typedef struct CollideBoxes { u32 count; CollideBox *entries; } CollideBoxes;

/* GeometricObjectCollide (TT, 0x40). */
struct GeoCollide {
    u32 n_vertices;             /* +0x00 */
    Vec3 *vertices;             /* +0x04 */
    Vec3 *normals;              /* +0x08 */
    void *unk_0c;               /* +0x0C */
    i32 unk_10;                 /* +0x10 */
    u32 n_elements;             /* +0x14 */
    u16 *element_types;         /* +0x18 1 triangles, 7 spheres, 8 boxes, 5 / 3 (not read) */
    void **elements;            /* +0x1C CollideTriangles / CollideSpheres / CollideBoxes by type */
    void *unk_20;               /* +0x20 */
    i32 unk_24[2];              /* +0x24 */
    u16 unk_2c;                 /* +0x2C */
    u16 n_parallel_boxes;       /* +0x2E */
    float sphere_radius;        /* +0x30 bounding sphere */
    Vec3 sphere_center;         /* +0x34 */
};                              /* 0x40 */

/* A zone: one entry of a collide set's zone list. */
typedef struct ZoneEntry {
    struct ZoneEntry *next;     /* +0x00 */
    struct ZoneEntry *prev;     /* +0x04 */
    ListHead *head;             /* +0x08 */
    GeoCollide *geo;            /* +0x0C mostly one sphere (Ed's ZDM) */
} ZoneEntry;

/* Activation: per state, which zones of the list are on. */
typedef struct ZoneIndexEntry {
    struct ZoneIndexEntry *next;/* +0x00 */
    struct ZoneIndexEntry *prev;/* +0x04 */
    ListHead *head;             /* +0x08 */
    u32 zone;                   /* +0x0C index into the zone list */
} ZoneIndexEntry;
typedef struct ZoneActivation {
    struct ZoneActivation *next;/* +0x00 */
    struct ZoneActivation *prev;/* +0x04 */
    ListHead *head;             /* +0x08 */
    ListHead *zones;            /* +0x0C of ZoneIndexEntry */
    u32 state;                  /* +0x10 state index */
} ZoneActivation;

/* Collide set (perso +0x14): the four ZDx zone lists - ZDD (Ed: one r 3 sphere ahead), ZDE (boxes; events), ZDM
 * (mechanics body: the collide-and-move spheres), ZDR (persos' own collision; null for Ed) - and their per-state
 * activation lists. The exe numbers the lists 0 ZDD, 1 ZDM, 2 ZDE, 3 ZDR (GAM_ZoneList_00431520); GAM_Zone_0041efe0(so,
 * list, index) picks a zone (the skier's CollectNearby: its ZDE zone 6); COND 13 tests ZDD zones. */
typedef struct CollSet {
    ListHead *zdd;              /* +0x00 of ZoneEntry */
    ListHead *zde;              /* +0x04 */
    ListHead *zdm;              /* +0x08 */
    ListHead *zdr;              /* +0x0C */
    ListHead *zdd_activation;   /* +0x10 of ZoneActivation */
    ListHead *zde_activation;   /* +0x14 */
    ListHead *zdm_activation;   /* +0x18 */
    ListHead *zdr_activation;   /* +0x1C */
    u8 _pad_20[0x10];
    float sphere_diameter;      /* +0x30 the skier's sphere (radius = x 0.5, DAT_004d2928; FUN_0043d480 path) */
    u8 _pad_34[2];
    u8 single_sphere;           /* +0x36 GAM_00431720: 1 = sweep one sphere at the origin (the skier), 0 = ZDM */
    u8 floor_ok;                /* +0x37 GAM_00431730: a floor-pass (mask 1) hit on this perso counts only when set
                                         (MEC_SweepZones_0043bff0) */
} CollSet;

/* Perso (level.py, 0x44). */
struct Perso {
    Perso3dData *p3d;           /* +0x00 */
    StdGame *std_game;          /* +0x04 */
    Dynam *dynam;               /* +0x08 GAM_0042aff0 / b0e0 / b2b0 */
    struct Brain *brain;        /* +0x0C ai.py */
    void *camera;               /* +0x10 */
    CollSet *collset;           /* +0x14 ai.py zdm_sets, FUN_0043bff0 */
    void *ms_way;               /* +0x18 */
    void *ms_light;             /* +0x1C */
    u32 unk_20;                 /* +0x20 */
    SectorInfo *sector_info;    /* +0x24 GAM_0042b0e0, FUN_0043bff0, GAM_PlayAnim_00415c00 */
    u8 _pad_28[8];
    void *unk_30;               /* +0x30 GAM_0042b520 runs the AI of what GAM_00417a00 finds from it */
    u8 _pad_34[0x10];
};                              /* 0x44 */

/* Perso3dData: states and animation playback (level.py; GAM_PlayAnim_00415c00 / 00415fb0; docs/NOTES.md "AI
 * interpreter" States). PlayAnim reaches it as **(so + 4) = so->data.perso->p3d. */
struct Perso3dData {
    State *state_initial;       /* +0x00 */
    State *state;               /* +0x04 playing */
    State *state_requested;     /* +0x08 InState; only ChangeState sets it */
    void *objects;              /* +0x0C object list in use */
    void *objects_initial;      /* +0x10 */
    Family *family;             /* +0x14 */
    Matrix matrix;              /* +0x18 the perso's transform (the SuperObject's matrix points here) */
    void *unk_70;               /* +0x70 rewritten at load (NOTES "Unresolved fix->level pointers") */
    float frame;                /* +0x74 current frame (PlayAnim 00415fb0 adds ms / 1000 x frame_rate) */
    u8 repeats_left;            /* +0x78 State +0x3A at the state change; 00415c00 counts it down */
    u8 _pad_79[3];
    float frame_prev;           /* +0x7C */
    u32 unk_80;                 /* +0x80 */
    float time_in_new_state;    /* +0x84 s of the frame left over when the animation ended (00415fb0) */
    Vec3 unk_88;                /* +0x88 cleared by 00415fb0 while the animation runs */
    void *unk_94;               /* +0x94 rewritten at load, Ed only */
    u8 _pad_98[0xC4];
    State *state_next;          /* +0x15C the state to play next (State +0x28, or GAM_004371f0) */
    u8 change_pending;          /* +0x160 */
    u8 ended;                   /* +0x161 finished, no repeats left, no next state */
    u8 end_reached;             /* +0x162 this frame reached the last frame */
    u8 frame_rate;              /* +0x163 frames per second (State +0x3B; Proc10 scales it) */
    void *unk_164[3];           /* +0x164 rewritten at load */
    u8 _pad_170[0x36];
    i16 frame_counter;          /* +0x1A6 00415fb0 adds the whole frames played */
    u32 zone0;                  /* +0x1A8 the water flag at the origin (GAM_00437900 / GAM_0041ee80): zone kind 0 */
};                              /* 0x1AC known */

/* StdGame (perso +4): the object type name indexes (NOTES "Prop collision": family / model / instance tables) and
 * the AI activation (GAM_00424280 for PROC_18..22, NOTES "Ski obstacles"). */
struct StdGame {
    u32 family_index;           /* +0x00 */
    u32 model_index;            /* +0x04 */
    u32 instance_index;         /* +0x08 */
    SuperObject *super_object;  /* +0x0C the perso's SuperObject (fix persos: patched to the level one at load) */
    u8 _pad_10[0x1A];
    u8 activation_a;            /* +0x2A GAM_00424280 a (not traced) */
    u8 activation_b;            /* +0x2B GAM_00424280 b (not traced) */
    float activation_radius_sq; /* +0x2C GAM_004242c0; -1 = always; GAM_0042b520 skips the AI beyond it */
};                              /* 0x30 known */

/* ---- Families, states, animation ---- */

/* Family (level.py). */
struct Family {
    Family *next;               /* +0x00 */
    Family *prev;               /* +0x04 */
    void *hdr;                  /* +0x08 */
    u32 index;                  /* +0x0C */
    ListHead states;            /* +0x10 of State */
    void *default_objects;      /* +0x1C */
    ListHead object_lists;      /* +0x20 */
    void *bounding_volume;      /* +0x2C */
};                              /* 0x30 known */

/* State transition entry (ai.py states(): target +0xC, via +0x10). In State +0x10 lists: playing `target` requested
 * from this state goes through `via` first (ChangeState, GAM_00437260). In +0x1C lists only target is used. */
typedef struct StateTransition {
    struct StateTransition *next;   /* +0x00 */
    struct StateTransition *prev;   /* +0x04 */
    void *hdr;                      /* +0x08 */
    State *target;                  /* +0x0C */
    State *via;                     /* +0x10 */
} StateTransition;

/* State (level.py, ai.py states()). */
struct State {
    State *next;                /* +0x00 */
    State *prev;                /* +0x04 */
    void *hdr;                  /* +0x08 */
    AnimRef *anim;              /* +0x0C GAM_PlayAnim_00415c00 / 00415fb0 */
    ListHead transitions;       /* +0x10 of StateTransition */
    ListHead prohibited;        /* +0x1C of StateTransition (target = refused) */
    State *next_state;          /* +0x28 after the animation; (State *)-1 = GAM_004371f0 picks (00415c00) */
    u32 unk_2c;                 /* +0x2C not the card (NOTES "Ed's mechanics ID cards") */
    IdCard *card;               /* +0x30 mechanics ID card (GAM_00437810; GAM_0042a8a0 queues it by default) */
    ListHead *water_points;     /* +0x34 the water sample points: entries {.., +0xC point (perso axes), +0x18 kind 1-6,
                                         +0x1C value} (GAM_00437900; tools/ttre/ai.py water_points) */
    u8 _pad_38[2];
    u8 repeats;                 /* +0x3A extra plays before next_state */
    u8 frame_rate;              /* +0x3B 0 = the anim ref's */
    u8 flags;                   /* +0x3C 1 = only the listed transitions are allowed */
    u8 _pad_3d[3];
};                              /* 0x40 (size not checked) */

/* Animation event (ai.py, NOTES "Sound events"): type 0 sound (value = the sound event), 3 foot contact. */
typedef struct AnimEvent {
    u32 type;                   /* +0x00 */
    u32 value;                  /* +0x04 */
    u32 frame;                  /* +0x08 */
    u32 unk_0c;                 /* +0x0C */
    u32 unk_10;                 /* +0x10 */
} AnimEvent;                    /* 0x14 */

/* AnimationReference (TT, 0x2C; level.py, ai.py; NOTES "Sound events": +0xC is the event count, the A3D header
 * agrees; level.py's raymap-based f32[3] there is wrong). */
struct AnimRef {
    u32 unk_00;                 /* +0x00 */
    u16 n_frames;               /* +0x04 GAM_PlayAnim_00415fb0 ends the animation at it */
    u8 frame_rate;              /* +0x06 */
    u8 n_channels;              /* +0x07 */
    AnimEvent *events;          /* +0x08 */
    u32 n_events;               /* +0x0C */
    u8 _pad_10[0x18];
    void *a3d;                  /* +0x28 the A3D block (layout: level.py) */
};                              /* 0x2C */

/* ---- AI (docs/NOTES.md "AI scripts", "AI interpreter") ---- */

/* Behaviour (12 bytes; ai.py). */
typedef struct AIBehaviour {
    AINode **scripts;           /* +0x00 rule scripts */
    AINode **schedule;          /* +0x04 -> the schedule's nodes (one more indirection; AI_0048ec60), or NULL */
    u32 n_scripts;              /* +0x08 */
} AIBehaviour;

/* A running meta-action (0x50): AI_004914f0 finds one by (script, node), AI_00491560 a free one. */
typedef struct AIAction {
    u8 _pad_00[0x48];
    AINode *node;               /* +0x48 */
    u8 active;                  /* +0x4C */
    u8 script;                  /* +0x4D 0 schedule, k + 1 rule script k */
    u8 _pad_4e[2];
} AIAction;
typedef struct AIActionTable {
    AIAction *entries;          /* +0x00 */
    u8 capacity;                /* +0x04 */
    u8 used;                    /* +0x05 */
    u8 _pad_06[2];
} AIActionTable;

typedef struct Intelligence {
    void *model;                /* +0x00 */
    AINode *schedule_pos;       /* +0x04 where the schedule resumes; NULL: none (AI_0048ec60) */
    AIBehaviour *behaviour;     /* +0x08 current */
    AIBehaviour *behaviour_prev;/* +0x0C */
    AIActionTable *actions;     /* +0x10 */
} Intelligence;

/* dsgvars: AI_0048fa20 reads variable i at dsg->current + dsg->def[0]->infos[i].offset, typed by .type. */
typedef struct DsgVarInfo {
    u32 offset;                 /* +0x00 in the buffers */
    u32 type;                   /* +0x04 into the dsgvar type table 0x4E5910 */
    u32 unk_08;                 /* +0x08 */
    u32 unk_0c;                 /* +0x0C */
} DsgVarInfo;
typedef struct DsgDef {
    void *defaults;             /* +0x00 default buffer */
    DsgVarInfo *infos;          /* +0x04 */
    u32 size;                   /* +0x08 */
    u32 count;                  /* +0x0C */
} DsgDef;
typedef struct DsgMem {
    DsgDef **def;               /* +0x00 */
    void *initial;              /* +0x04 */
    u8 *current;                /* +0x08 */
} DsgMem;

struct Mind {
    void *model;                /* +0x00 AI model {normal list, reflex list, dsgvar defs, ...} */
    Intelligence *normal;       /* +0x04 */
    Intelligence *reflex;       /* +0x08 GAM_0042b260 runs it first */
    DsgMem *dsg;                /* +0x0C */
    u8 running_normal;          /* +0x10 */
    u8 _pad_11[3];
};

typedef struct Brain { Mind *mind; } Brain;

/* ---- Mechanics (MEC / DNM): docs/NOTES.md "MEC physics" and after ---- */

/* Mechanics material (GameMaterial +4; ctor MEC_hMecMatC_004baaec, 0x1C). FUN_0043bff0 copies the first 5 floats of
 * each side's into the contact record; no material = all 1 (DAT_004dc9c4..d4). */
typedef struct MecMaterial {
    float steer;                /* +0x00 the type 2 request handler (FUN_004b00bf) scales steering and edge grip by
                                         the floor's (NOTES "Skiing"); no other reader decoded */
    float rebound;              /* +0x04 obstacle x own: head-on impacts v -= n (2 - r)(v . n) (MEC_004bb904) */
    float grip;                 /* +0x08 obstacle x own, 0..1 (FUN_004c41c5): 1 = no inertia, < 1 slides */
    float unk_0c;               /* +0x0C AI accessors only (0x49c140.., 0x48c6b3..) */
    float unk_10;               /* +0x10 */
    float unk_14;               /* +0x14 not copied into contacts */
    float unk_18;               /* +0x18 not copied into contacts */
} MecMaterial;                  /* 0x1C */

/* GameMaterial (level.py). */
struct GameMaterial {
    void *visual;               /* +0x00 */
    MecMaterial *mechanics;     /* +0x04 (MEC_004baa70 / 004baade on FUN_00451790(gm)) */
    u32 sound;                  /* +0x08 */
    void *collide;              /* +0x0C */
};

/* Mechanical environment (ctor MEC_HMecEnvi_004c1078, 0x30), per sector: sector data +4 lists them, +0x10 lists the
 * sector's water surfaces; FUN_00451b90 (from MEC_EnvAt_004c0f7f) counts the surfaces the position is below (in list
 * order, stopping at the first it is not) and takes environment number count; the depth goes to dynamics +0x2E0.
 * Every level: the fix's air 0x36b51c {9.81, up (0, 0, 1), 1, density 1.3}; water sectors add one with density 1000
 * (sud0 0x36b60c, Land 0x36b714). */
typedef struct MecEnv {
    float gravity;              /* +0x00 g (FUN_004c41c5: weight = mass x g x card gravity factor) */
    Vec3 up;                    /* +0x04 the weight acts along -up; the re-snap sweeps along -up (MEC_004bf9f0) */
    float unk_10;               /* +0x10 1 in air and water */
    float density;              /* +0x14 air 1.3, water 1000 (kg / m^3): the flight lift (FUN_004c478d) */
    u8 _pad_18[0x10];
    u32 water;                  /* +0x28 1 in the water environments, 0 in air */
    u32 unk_2c;                 /* +0x2C the integrators copy it to dynamics +0x2E4 (Land's water: 255.0f, sud0's 0) */
} MecEnv;                       /* 0x30 */

/* Contact record (0x7C), filled by the sweep FUN_0043bff0, kept per class in the dynamics (MEC_004bc477 memcpy's
 * ground / steep slope hits to +0xCC, wall / overhang / ceiling hits to +0x148). */
typedef struct ContactRecord {
    u32 obstacle_kind;          /* +0x00 1 static geometry, 2 perso */
    u32 own_zone_type;          /* +0x04 FUN_0043bf70(own game material): 1 = mechanics zone */
    float t;                    /* +0x08 0..1 along the sweep (MEC_004bc477 clamps it) */
    Vec3 normal;                /* +0x0C */
    Vec3 point;                 /* +0x18 */
    Vec3 edge;                  /* +0x24 only for kind 2 (from DAT_0050d830) */
    MecMaterial obstacle_mat;   /* +0x30 first 5 floats filled */
    MecMaterial own_mat;        /* +0x4C first 5 floats filled */
    SuperObject *obstacle;      /* +0x68 the integrator re-parents the perso to it on report 0x30000 (platforms) */
    Dynamics *obstacle_dynamics;/* +0x6C perso obstacles (FUN_00451780 of its dynam) */
    GameMaterial *own_material; /* +0x70 */
    GameMaterial *obstacle_material; /* +0x74 */
    u16 own_shape;              /* +0x78 the moving shape */
    u16 kind;                   /* +0x7A for spheres 0 / 3 / 4 (face / edge / vertex?); 2 other shapes (FUN_00460230) */
} ContactRecord;                /* 0x7C */

/* A raw sweep hit (0x30), FUN_004602c0's output; MEC_SweepZones_0043bff0 turns the nearest into a ContactRecord. */
typedef struct SweepHit {
    float t;                    /* +0x00 */
    Vec3 point;                 /* +0x04 */
    Vec3 normal;                /* +0x10 */
    GameMaterial *own_material; /* +0x1C */
    GameMaterial *obstacle_material; /* +0x20 */
    u16 own_shape;              /* +0x24 */
    u16 kind;                   /* +0x26 */
    SuperObject *obstacle;      /* +0x28 */
    i16 obstacle_type;          /* +0x2C 1 = a perso (then the record gets its dynamics) */
    u16 _pad_2e;
} SweepHit;                     /* 0x30 */

/* The AI's proposal (dynamics +0x314): float slots written by the proposal procedures and read by the request
 * handlers (slot k at +4k; NOTES name them [k]). */
typedef struct Proposal {
    float thrust;               /* [0] type 1 (swimming): the thrust added per request of mode 10 / 0x14 */
    Vec3 vec_a;                 /* [1..3] ProposalVecA: type 4 modes 7/8 velocity in the perso's axes (FUN_004b0f2a);
                                          type 2 mode 10 push / jump scalars (FUN_004b00bf); type 5 mode 21 dv */
    Vec3 vec_d;                 /* [4..6] ProposalVecD: type 4 mode 9 */
    Vec3 direction;             /* [7..9] move direction (FUN_004ae392 mode 0xC) */
    float speed;                /* [0xA] (mode 0xC, clamped by the card's max_speed) */
    float bank_step;            /* [0xB] type 5 modes 3/4 */
    float unk_0c;               /* [0xC] */
    float turn;                 /* [0xD] modes 3/4 turn (TurnBy, AI_00492140); modes 5/0x12 target angle */
    Vec3 turn_axis;             /* [0xE..0x10] modes 5/0x12 */
    float unk_11[7];            /* [0x11..0x17] */
    float attack_step;          /* [0x18] type 5 modes 1/2 */
    float unk_19[5];            /* [0x19..0x1D] */
} Proposal;                     /* 0x78 */

/* Types 1 and 5 control state (dynamics +0x2B0; GAM_0042ab20 resets it when entering either card type). Names from
 * the type 5 handler FUN_004aea7c; type 1 (FUN_004abbb8) uses +0x2B0..+0x2C0 too, meaning unchecked. */
typedef struct FlightControls {
    float attack;               /* +0x00 deg: Up (mode 1) lowers it, Down (2) raises it */
    float bank;                 /* +0x04 deg: Left (3) lowers it, Right (4) raises it */
    float thrust;               /* +0x08 mode 10 target (card thrust) */
    float attack_lagged;        /* +0x0C rad, first-order lag FUN_004c9afb (FUNC_91 Report2BC) */
    float bank_lagged;          /* +0x10 rad (FUNC_92 Report2C0); the flight frame rolls by it */
    float thrust_lagged;        /* +0x14 */
    float unk_18;               /* +0x18 */
    float unk_1c;               /* +0x1C */
} FlightControls;               /* 0x20 */

/* Dynamics (DNM): the mechanics state of one perso, = FUN_00451780(perso->dynam) (dynam +0). Init FUN_004a7376. */
struct Dynamics {
    u32 unk_00;                 /* +0x00 */
    void *unk_04;               /* +0x04 set by the init FUN_004a7376 */
    Vec3 unk_08;                /* +0x08 FUN_004a7376: 0 */
    float rotation[9];          /* +0x14 FUN_004a7376: identity. The frame, row-major with the axes as columns
                                         (FUN_004a8aed / 004a8a74): flight builds it along the velocity; type 1
                                         (swimming) writes an axis-angle {angle, axis} into its first 4 floats
                                         (FUN_004b6d50) and its integrator turns that back into the 3x3 */
    Motion velocity;            /* +0x38 FUN_004aa6af writes the new one; v_old for the integration */
    Motion position;            /* +0x54 position and heading; copied into the SuperObject matrix */
    float unk_70;               /* +0x70 FUN_004a9d20 writes -1 / +1; the type 7 handler -1 on a move request; >= 0:
                                         the type 7 forces pull towards the plane below */
    Vec3 plane_normal;          /* +0x74 type 7 forces FUN_004c831a: velocity += n (offset - n . pos) k / m dt */
    u8 _pad_80[0xC];
    float plane_stiffness;      /* +0x8C k */
    float plane_offset;         /* +0x90 */
    u32 unk_94;                 /* +0x94 */
    float mass;                 /* +0x98 the card's (MEC_004b948a); a block initialised by FUN_004c1730 */
    u8 _pad_9c[0x14];
    u32 handler_mode_prev;      /* +0xB0 GAM_0042b0e0: := +0xB8 after the integrator (the block from here to +0x2B0 is
                                          initialised by FUN_004c1c00) */
    u32 unk_b4_prev;            /* +0xB4 GAM_0042b0e0: := +0xBC */
    u32 handler_mode;           /* +0xB8 the request handler's: 0 none, 1 move, 2 turn, 6 ski, 8 air control (the
                                          wanted motion counts in the air) */
    u32 unk_bc;                 /* +0xBC */
    u32 report_prev;            /* +0xC0 last frame's contact bits */
    u32 report;                 /* +0xC4 this frame's: 1 ground, 2 steep slope, 4 wall, 8 overhang, 0x10 ceiling,
                                          0x20 blocked, 0x40 stepped up, 0x10000 perso contact */
    u32 report_seen;            /* +0xC8 report_prev | report (GAM_0042b0e0): what the AI's contact tests read */
    ContactRecord ground;       /* +0xCC report & 3 (MEC_004bc477) */
    ContactRecord wall;         /* +0x148 report & 0x1C */
    ContactRecord other;        /* +0x1C4 report & 0x10000 (FUN_004a7210 reads its normal) */
    Motion velocity_prev_snap;  /* +0x240 := velocity_snap at the start of an integrator */
    Motion velocity_snap;       /* +0x25C the step's velocity (GAM_0042b0e0 hands it to GAM_0042aee0) */
    Motion position_prev_snap;  /* +0x278 := position_snap at the start of an integrator */
    Motion position_snap;       /* +0x294 the step's start position: collide-and-move sweeps from it (fixed during
                                          the passes), the environment lookup reads it (MEC_004c0f7f) */
    FlightControls flight;      /* +0x2B0 types 1 / 5 */
    u8 _pad_2d0[0x10];
    float env_depth;            /* +0x2E0 how far below the water surface (MEC_EnvAt_004c0f7f's 4th argument; the
                                         scripts' EnvHeight, FUNC 51) */
    u32 env_2c;                 /* +0x2E4 MecEnv.unk_2c */
    u32 zone_prev;              /* +0x2E8 MEC_004c0fb5: last frame's zone_combined */
    u32 zone_combined;          /* +0x2EC the water flag all the state's sample points share, 3 if they differ (the
                                         AI's PersoStatus70 = in water reads it) */
    u8 probe_hit;               /* +0x2F0 type 2: the downward probe found ground (NOTES "Ski air gravity") */
    u8 _pad_2f1[3];
    Vec3 pending_velocity;      /* +0x2F4 FUN_004c41c5: replaces the velocity (z := 0) when pending_set */
    u8 _pad_300[0x10];          /*        (FUN_004a7b07 clears +0x2F4..+0x30C) */
    u8 pending_set;             /* +0x310 */
    u8 _pad_311[3];
    Proposal proposal;          /* +0x314 */
    u8 keep_reports;            /* +0x38C standing still keeps the reports (FUN_004aa6af) */
    u8 unk_38d;                 /* +0x38D GAM_0042bbec: from two flags of the dynam (GAM_00432010 / 00432020) */
    u8 unk_38e;                 /* +0x38E FUN_004a7376 clears it */
    u8 _pad_38f;
};                              /* 0x390 known */

/* The perso's request queue (dynam +8, GAM_00431e20): one entry per SetMechMode* this frame (AI_004920a0 ->
 * FUN_0047a370, max 8), or the engine's default {0, the state's card} (GAM_0042b2b0). */
typedef struct RequestQueue {
    u8 count;                   /* +0x00 */
    u8 _pad_01[3];
    u32 modes[8];               /* +0x04 FUN_0047a290 */
    IdCard *cards[8];           /* +0x24 FUN_0047a2c0 */
} RequestQueue;                 /* 0x44 */

/* Forces collected for one integrator step (FUN_004c3550 appends; FUN_004c969c -> FUN_004c9560 integrates
 * v += sum F / m dt): force and torque. */
typedef struct Force { Vec3 force; Vec3 torque; } Force;
typedef struct ForceList {
    u32 count;                  /* +0x00 */
    Force entries[1];           /* +0x04 count entries (capacity not known) */
} ForceList;

/* Per-frame mechanics workspace (dynam +4): the request handlers write the wanted motion, the integrator reads it
 * (fn1's 3rd argument, fn0's 5th). */
struct DynamWork {
    u8 _pad_00[0x50];
    Motion unk_50;              /* +0x50 the type 2 integrator takes its address (unused there) */
    Motion wanted;              /* +0x6C the request's motion: added to v (FUN_004aa6af), x grip^4 on ice */
    u8 _pad_88[0xA4];
    ForceList forces;           /* +0x12C types 2 / 5 */
};

/* perso +8 (GAM_0042aff0 / b0e0 / b2b0 accessors). */
struct Dynam {
    Dynamics *dynamics;         /* +0x00 FUN_00451780 */
    DynamWork *work;            /* +0x04 FUN_00451790 */
    RequestQueue *requests;     /* +0x08 GAM_00431e20 */
    u32 unk_0c;                 /* +0x0C */
    float activation_radius;    /* +0x10 GAM_00432030: GAM_0042b2b0 runs the mechanics only near a camera / the
                                          active perso (GAM_0042a720), unless < 0 */
    u8 _pad_14[0x108];
    u32 card_type;              /* +0x11C GAM_00431ca0 / 00431cb0: a change runs FUN_004a7459 / GAM_0042ab20 */
};                              /* 0x120 known */

/* ---- ID cards (MEC_hidcard_004b7d04: constructor, sizes and defaults by type; MEC_004b9466 = type) ----
 * The card accessors MEC_004b9530..004baa14 (one field each, a switch on the type) are dead code in this build: no
 * call and no pointer to them anywhere in memory, except MEC_004ba9c2 (launch speed). They still show which offsets
 * hold the same quantity across types: mass +4 for 0/4/7/8/9/10; walk +0xC/+0x10/+0x14/+0x18 = ski the same = type 12
 * 4 lower; gravity factor 0/4 +0x38 = type 7 +0x1C = type 10 +8; walk byte +0x3C; type 7 +0x20/+0x24, type 9 +0x18,
 * type 10 byte +0xC and +0x10 (meanings not read). */

/* Types 0 and 4: ground and air (0x40). Defaults from the constructor; Ed's values in NOTES "Ed's mechanics ID
 * cards". Type 12 has the same fields from max_speed on, 4 bytes lower (FUN_004ae392, MEC_004b96aa). */
typedef struct IdCardWalk {
    u32 type;                   /* +0x00 */
    float mass;                 /* +0x04 75 (Ed 120) */
    float step_height;          /* +0x08 0 (Ed's ground card 1): step-up MEC_004bb06e */
    float unk_0c;               /* +0x0C 0 */
    float min_turn;             /* +0x10 0: mode 0x14 turns at least this fast (rad/s) */
    float max_speed;            /* +0x14 100: mode 0xC speed clamp */
    float max_turn;             /* +0x18 2 pi rad/s: modes 3/4/0x12 clamp */
    float cos_ground;           /* +0x1C cos 45 (Ed 50 deg): n . up at or above = ground (MEC_004bc477) */
    float cos_slope;            /* +0x20 cos 60 (Ed 70): steep slope; also the re-snap's minimum (MEC_004bf9f0) */
    float cos_wall;             /* +0x24 cos 120 */
    float cos_overhang;         /* +0x28 cos 135; below = ceiling */
    float step_wall_sin;        /* +0x2C sin 5 deg: step-up only off near-vertical walls (|n . up| <= it) */
    float step_floor_cos;       /* +0x30 cos 5 deg: the floor found by the step-up must be this flat */
    float resnap_distance;      /* +0x34 0.3 (Ed 0.4 / 0.3): MEC_004bf9f0 sweep length */
    float gravity_factor;       /* +0x38 1 (Ed 1 / 3.5 / 3.0) */
    u8 unk_3c;                  /* +0x3C 1: MEC_004be1bd passes it to MEC_004bd689 */
    u8 _pad_3d[3];
} IdCardWalk;                   /* 0x40 */

/* Type 2: sliding (the skier, 0x50). Fields from the port (core/src/tc_mec.c), all read in FUN_004b00bf /
 * FUN_004c4ecd / FUN_004c3af0. */
typedef struct IdCardSki {
    u32 type;                   /* +0x00 */
    float mass;                 /* +0x04 150 */
    u32 unk_08;                 /* +0x08 0/1 */
    float unk_0c;               /* +0x0C MEC_004b9710 sets it with walk.unk_0c */
    float min_turn;             /* +0x10 MEC_004b981b sets it with walk.min_turn */
    float max_speed;            /* +0x14 70 (Action's card 50): FUN_004c3af0 caps |v| */
    float max_turn;             /* +0x18 MEC_004b9a5e sets it with walk.max_turn */
    float unk_1c[2];            /* +0x1C */
    float steering;             /* +0x24 90 */
    float weathervane;          /* +0x28 0.2 */
    float edge_grip;            /* +0x2C 350 */
    float hold_down;            /* +0x30 6000: upside down, pressed onto the surface */
    float gravity_factor;       /* +0x34 4 */
    float align;                /* +0x38 0.6 of the angle to the surface per frame (0.05 in the air) */
    float air_steering;         /* +0x3C 0.5 */
    float air_gravity_factor;   /* +0x40 2.5 */
    float probe_length;         /* +0x44 2 */
    float probe_cos;            /* +0x48 cos 37 deg */
    float min_speed;            /* +0x4C 0.001: the probe runs above it */
} IdCardSki;                    /* 0x50 */

/* Type 5: flight (0x50). Defaults: +0x40 0.1, top_speed 20, launch_speed 8, polar_set 2, roll_sign 1; Ed's card
 * 0x3c379c in core/src/tc_mec.c. */
typedef struct IdCardFly {
    u32 type;                   /* +0x00 */
    float mass;                 /* +0x04 80 */
    u32 polar_set;              /* +0x08 FUN_004cac3c: 2 = Ed's lift / drag curves */
    float unk_0c;               /* +0x0C */
    float lag_attack;           /* +0x10 0.1 s */
    float lag_bank;             /* +0x14 0.3 s */
    float lag_thrust;           /* +0x18 2 s */
    float unk_1c[2];            /* +0x1C */
    float thrust;               /* +0x24 2000: mode 10 target */
    float wing_area;            /* +0x28 5.5 */
    float attack_max;           /* +0x2C 12 deg */
    float attack_min;           /* +0x30 -4 */
    float bank_max;             /* +0x34 85 */
    float bank_min;             /* +0x38 -85 */
    float bank_factor;          /* +0x3C 0: bank x it without a turn request */
    float unk_40;               /* +0x40 0.1 */
    float top_speed;            /* +0x44 20 */
    float launch_speed;         /* +0x48 10: velocity on entering (GAM_0042ab20 via MEC_004ba9c2) */
    u8 roll_sign;               /* +0x4C 1 */
    u8 _pad_4d[3];
} IdCardFly;                    /* 0x50 */

/* Any card: the type selects the layout (0..12; sizes 0/4/6 0x40, 1 0x48, 2/5 0x50, 3 0x10, 7 0x28, 8 8, 9 0x1C,
 * 10 0x14, 11 0x34, 12 0x18). The functions that know the type take the specific struct. */
union IdCard {
    u32 type;
    IdCardWalk walk;
    IdCardSki ski;
    IdCardFly fly;
};

/* The 13 {integrator, request handler} pairs by card type (0x8C2240, filled by GAM_0042a7e0; called by GAM_0042b0e0
 * and GAM_0042aff0, both cdecl: without the __cdecl Ghidra assumes the callee pops the arguments and loses track of
 * the caller's stack after the call). */
typedef Dynamics *(__cdecl *MecIntegrator)(Dynamics *dyn, SuperObject *so, SuperObject *carrier, SuperObject *sector,
                                           DynamWork *work, IdCard *card, float dt);
typedef Dynamics *(__cdecl *MecRequestHandler)(Dynamics *dyn, SuperObject *so, DynamWork *work, IdCard *card,
                                               float dt, u32 mode);
typedef struct MecHandlerPair {
    MecIntegrator integrate;    /* +0x00 fn0 */
    MecRequestHandler request;  /* +0x04 fn1 */
} MecHandlerPair;

/* ---- AI interpreter (docs/NOTES.md "AI interpreter"; tools/exe/tables.tsv applies these prototypes to the tables) ---- */

/* Script node (8 bytes, pre-order tree by depth; tools/ttre/ai.py). */
struct AINode {
    u32 param;                  /* +0x00 primitive index, constant, or pointer */
    u16 skip;                   /* +0x04 nodes to the next sibling, cached on first use (the meta-action node handler) */
    u8 depth;                   /* +0x06 0 ends the script */
    u8 type;                    /* +0x07 AI_EvalNode_0048e930 dispatches on it (g_ai_node_handlers) */
};

/* An evaluated value (16 bytes): value types 6 int, 7 uint (time), 8 float, 0xC perso, 0x15 flags (procedures and
 * meta-actions return 1 stop / 2 stop the script / 4 done / 8 behaviour changed / 0x10 end), 0x16 vector, 0x19 / 0x23
 * references. */
typedef union AIValueData { i32 i; u32 u; float f; void *p; Vec3 v; } AIValueData;
typedef struct AIValue {
    AIValueData d;              /* +0x00 */
    u32 type;                   /* +0x0C */
} AIValue;

/* The primitive signatures, from their call sites (cdecl). `me` is the perso's SuperObject; `args` the node after the
 * primitive's own (its first argument; args[-1].param = the primitive's index, which shared handlers switch on). */
typedef AINode *(__cdecl *AINodeFn)(SuperObject *me, AINode *node, AIValue *result);
typedef AINode *(__cdecl *AIEvalFn)(SuperObject *me, AINode *args, AIValue *result);
typedef AINode *(__cdecl *AICondFn)(SuperObject *me, AINode *args, i32 *result);
typedef AINode *(__cdecl *AIProcFn)(SuperObject *me, AINode *args);
typedef u8 (__cdecl *AIMetaContFn)(SuperObject *me);
typedef u8 (__cdecl *AIFieldGetFn)(SuperObject *me, AIValue *out);

typedef struct AIProcEntry { AIProcFn fn; u32 flag; } AIProcEntry;     /* flag 0 / 1 (not read) */
typedef struct AICondEntry { AICondFn fn; u32 flag; } AICondEntry;
typedef struct AIFuncEntry { AIEvalFn fn; u32 flag; } AIFuncEntry;
typedef struct AIMetaEntry {
    AIProcFn start;             /* +0x00 once: returns the next node */
    AIMetaContFn cont;          /* +0x04 every frame: 1 running, 4 done */
    u32 signature;              /* +0x08 packed argument types */
    u32 unk_0c;                 /* +0x0C 0 */
} AIMetaEntry;
typedef struct AIFieldEntry {
    u32 kind;                   /* +0x00 into g_ai_value_kinds: 0 int, 1 float, 2 vector, 3 reference, 4 (type 3) */
    AIFieldGetFn get;           /* +0x04 */
    void *set;                  /* +0x08 read-only fields share a stub (SND_00404930) */
} AIFieldEntry;
typedef struct AIValueKind {
    u32 value_type;             /* +0x00 6 / 8 / 0x16 / 0x23 */
    void *after_get;            /* +0x04 AI_00492750 calls it when the getter returns 1 */
    void *before_set;           /* +0x08 */
} AIValueKind;

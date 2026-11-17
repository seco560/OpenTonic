/* The main menu (docs/ENGINE.md "The menus"): the original's look, our own organisation.
 *
 * Read from the exe and its data (tools/ttre/menu.py -> menu.ttmn): the main menu ("StartUp" in fix.sna) has its title
 * in the font TitleEd at y 3 and its choices in FontPC (the gold 3D letters) at y 1.0, 0.2, -0.6, -1.4, -2.2, in the HUD's
 * units (tc_hud.h: 7 high, the camera 5 away); its two backgrounds are MENU\TTSPACE_BKGRND and MENU\TTSPACE_STARS over the
 * whole screen, the corners' uv turning and zooming each frame (MNU_0044df60); the texts are game.dsb's MNU_ strings
 * (NEW GAME, LOAD GAME, ENTER THE GAME NAME, CHOOSE YOUR GAME, the erase question, YES, NO ...).
 * The exe's main menu has START GAME (its own menu: NEW GAME, LOAD GAME), OPTIONS, TONIC ON-LINE, SHOW CREDITS, QUIT.
 * Port (the user, 2026-11-15): START GAME (NEW GAME: a name, then the intro; LOAD GAME; DELETE GAME, ours), EXTRAS
 * (2026-11-16: CUSTOMIZATION, Ed's skin; TONIC VIEWER, the levels grid; REPLAYS, the recordings; above OPTIONS since
 * 11-17), OPTIONS (the viewer's display settings), EXIT GAME; no credits yet. Ours too: Ed's head (the '§'
 * glyph of FontPC, which the exe uses as MNU_CURSOR, the name field's cursor) beside the selected choice, whose letters
 * are a little larger (the exe's text effects at fix.gpt +0x220 are only partly read). The two EdMenu persos the exe
 * animates in the bottom corners are not drawn yet.
 *
 * The core keeps the state and lays it out; a frontend feeds it inputs, draws its layers and quads (two pictures and an
 * atlas, all from menu.ttmn) and acts on its commands. */
#ifndef TONIC_TC_MENU_H
#define TONIC_TC_MENU_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tc_hud.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Options: what the menu's OPTIONS screen sets; the frontends keep them across launches and give them to every
 * level's viewer (tc_viewer_set_view, tc_viewer_set_fov, tc_viewer_set_look, tc_viewer_set_hud_auto_hide) and to their
 * own sound (the music's and the sound effects' volume, the Dolby switch). */
typedef struct tc_options {
    float gamma;              /* display gamma, 0.5 - 2.5, 1 = unchanged */
    float animation_speed;    /* the viewer's playback rate (tc_viewer_step_speed's steps), 1 = the game's */
    uint8_t lighting;         /* tc_view_lighting: 0 the original's, 1 the port's earlier one */
    bool fog;                 /* the levels' fog */
    bool dolby;               /* Dolby Surround (the original's sound menu option; the frontends' mixers) */
    bool hud_auto_hide;       /* the HUD's elements show when they change, then slide out */
    bool invert_y;            /* look up / down reversed */
    float look_sensitivity;   /* x the look speed, 0.25 - 4 */
    float music_volume;       /* 0 - 1, 1 = as recorded (the frontends' music player) */
    float sound_volume;       /* 0 - 1: the sound effects and the speech (tc_mixer.gain) */
    float fov;                /* the field of view as , . step it: degrees, horizontal at 4:3, 50 - 120 (86 the game's) */
    char skin[16];            /* Ed's skin (CUSTOMIZATION; tools/ttre/skins.py): its folder under Skins, "" = his own */
} tc_options;

void tc_options_defaults(tc_options *o);
/* "key=value" lines (gamma, animation_speed, lighting, fog, dolby, hud_auto_hide, invert_y, look_sensitivity,
 * music_volume, sound_volume, fov, skin): the frontends' file. Unknown keys and bad values are passed by; parse starts
 * from the defaults. */
size_t tc_options_write(const tc_options *o, char *out, size_t size);
void tc_options_parse(tc_options *o, const char *text, size_t size);
/* The skin's folder name (tc_options.skin; for frontends whose language imports fixed arrays badly), and setting it
 * (letters, digits, '_' and '-' only, else ""). */
const char *tc_options_skin(const tc_options *o);
void tc_options_set_skin(tc_options *o, const char *skin);
/* Ed's skins (port): a skin is a folder of textures beside Textures, laid out as Textures is (tools/ttre/skins.py:
 * Skins/<skin>/ed2/hed_front.png ...). A level's texture path ".../Textures/<rest>" (relative to its folder, as the
 * .ttlv has it) becomes ".../Skins/<skin>/<rest>" in out: the frontend loads that file instead when there is one (a
 * skin carries only the textures it changes). Returns its length; 0 (out "") with no skin, a path without a "Textures/"
 * folder, or no room. */
size_t tc_skin_texture_path(const char *path, const char *skin, char *out, size_t size);

/* ---- The menu's data (menu.ttmn). */
typedef struct tc_menu_image { char name[48]; uint16_t width, height; uint8_t *rgba; } tc_menu_image;
/* A background over the screen (the exe's +0x24 ... +0x60 floats as read, stepped each frame when flags & 1). */
typedef struct tc_menu_bg {
    uint8_t image, flags, b20, b21;
    float rect[4], f1c;
    float w[16];
} tc_menu_bg;
typedef struct tc_menu_named_text { char name[40]; uint8_t font; char *text; } tc_menu_named_text;
/* A skin CUSTOMIZATION offers (menu.ttmn's SKIN, from tools/ttre/skins.py): its folder ("" Ed's own), its name, and
 * Ed turning in it: `turns` pictures in the atlas (uv u0, v0, u1, v1), anticlockwise from his front. */
#define TC_MENU_SKIN_TURNS 16
typedef struct tc_menu_skin { char id[16]; char name[24]; float uv[TC_MENU_SKIN_TURNS][4]; } tc_menu_skin;

typedef struct tc_menu_data {
    uint16_t atlas_width, atlas_height;
    uint8_t *atlas;           /* rgba, straight alpha, top row first */
    uint16_t font_count;      /* 0 TitleEd, 1 FontPC */
    tc_hud_font *fonts;
    uint16_t text_count;
    tc_menu_named_text *texts;
    uint16_t image_count, bg_count;
    tc_menu_image *images;
    tc_menu_bg *bgs;
    uint8_t effects[4][36];   /* fix.gpt +0x220 as read */
    uint16_t skin_count;      /* 0: a menu.ttmn made before the skins (CUSTOMIZATION offers Ed's own only) */
    uint8_t skin_turns;
    float skin_aspect;        /* a skin picture's width / height */
    tc_menu_skin *skins;
} tc_menu_data;

/* ---- What a frontend draws, back to front: the layers (a picture over the whole viewport, uv per corner: corners 0
 * top-left, 1 top-right, 2 bottom-left, 3 bottom-right; sample it repeating), then the quads over the atlas. Over a
 * running game (the game menu, tc_menu_in_game) the layers are TC_MENU_IMAGE_DIM (black at `alpha`, uv unused) and
 * TC_MENU_IMAGE_SWIRL: the frontend's own swirling picture (port: sud0's rainbow pool, Sud0/o_zarbi_auntxy), drawn at
 * TC_MENU_SWIRL_ALPHA (`alpha`; a frontend without a blend factor bakes it into the picture's alpha). */
#define TC_MENU_IMAGE_DIM 0xFF
#define TC_MENU_IMAGE_SWIRL 0xFE
#define TC_MENU_SWIRL_ALPHA 0.16f
typedef struct tc_menu_layer { uint8_t image; float uv[4][2]; float alpha; } tc_menu_layer;
/* rect x0, y0, x1, y1 in viewport units (0..1, y down); uv u0, v0, u1, v1 of the atlas; color multiplies the texel. */
typedef struct tc_menu_quad { float rect[4], uv[4], color[4]; } tc_menu_quad;
#define TC_MENU_MAX_QUADS 1024
#define TC_MENU_MAX_LAYERS 4

typedef enum tc_menu_screen {
    TC_MS_MAIN, TC_MS_START, TC_MS_NEW_GAME, TC_MS_NEW_ERASE, TC_MS_LOAD, TC_MS_DELETE, TC_MS_DELETE_ERASE,
    TC_MS_OPTIONS,
    TC_MS_GAME,               /* the game menu over a level (Esc, Start): the exe's "InGame" (GAME MENU) */
    TC_MS_CUSTOMIZE,          /* port: CUSTOMIZATION, Ed's skin (the user, 2026-11-16) */
    TC_MS_EXTRAS,             /* port: EXTRAS (CUSTOMIZATION, TONIC VIEWER, REPLAYS; the user, 2026-11-16) */
    TC_MS_COUNT
} tc_menu_screen;

typedef enum tc_menu_input {
    TC_MI_UP, TC_MI_DOWN, TC_MI_LEFT, TC_MI_RIGHT,
    TC_MI_CONFIRM,            /* Return, Space, the pad's Cross / A */
    TC_MI_BACK,               /* Esc, the pad's Circle / B */
    TC_MI_ERASE               /* Backspace / Delete: a letter of the name; on LOAD / DELETE GAME, the game shown */
} tc_menu_input;

typedef enum tc_menu_command {
    TC_MC_NONE,
    TC_MC_NEW_GAME,           /* tc_menu.name: start a game of that name (an existing one of that name was agreed to be
                               * erased): the intro, then Totalski */
    TC_MC_LOAD_GAME,          /* tc_menu.command_game: the index into the list tc_menu_set_games gave */
    TC_MC_DELETE_GAME,        /* tc_menu.command_game: erase it, then give the menu the new list */
    TC_MC_TONIC_VIEWER,       /* the levels grid */
    TC_MC_REPLAYS,            /* the recordings and the original demos (the Replays screen) */
    TC_MC_EXIT,
    TC_MC_OPTIONS,            /* tc_menu.options changed: keep them, give them to the level on show (the skin: to the
                               * next level loaded) */
    TC_MC_RESUME,             /* the game menu: back to the game (RESUME GAME, Esc) */
    TC_MC_PHOTO_MODE,         /* the game menu: the game holds, the camera free round Ed (tc_viewer_toggle_pause) */
    TC_MC_MAIN_MENU           /* the game menu: save and leave the level for the main menu */
} tc_menu_command;

#define TC_MENU_NAME 15       /* MNU_GAMENAME1's capacity (14) + the terminator */
#define TC_MENU_MAX_GAMES 64

/* A saved game as LOAD GAME and DELETE GAME list it (tc_save_info gives these). */
typedef struct tc_menu_game { char name[TC_MENU_NAME]; char level[64]; uint8_t edition; } tc_menu_game;

typedef struct tc_menu {
    tc_menu_data data;
    bool loaded;
    tc_menu_screen screen;
    int selected[TC_MS_COUNT];  /* each screen's selection */
    char name[TC_MENU_NAME];    /* the name being typed (NEW GAME) */
    tc_menu_game games[TC_MENU_MAX_GAMES];
    int game_count;
    tc_options options;
    tc_menu_screen erase_from;  /* the screen the erase question came from (LOAD or DELETE GAME) */
    tc_menu_screen options_from; /* OPTIONS goes back there (the main menu or the game menu) */
    int options_first;          /* OPTIONS: the first option in view (the list scrolls; BACK stays below it) */
    int drag_item;              /* OPTIONS: the slider the last click took hold of (tc_menu_drag moves it), -1 none */
    /* What the pointer can hit, from the last tc_menu_build: viewport rects (x0, y0, x1, y1), the choice, and a part (0
     * the choice; -1 / +1 an option's value, left or right of its middle: a click steps it that way; 2 a slider's bar:
     * a click sets the value there; -3 / +3 the scroll bar above / below its thumb: the list moves a few rows). */
    struct { float rect[4]; int16_t item, part; } hits[96];
    uint32_t hit_count;
    tc_menu_command command;    /* the last one, until tc_menu_take_command */
    int command_game;
    float time;                 /* seconds: the letters' motion */
    float frame_carry;          /* the backgrounds step per 1/60 s (the exe's menus pass a frame of 1.0) */
    tc_menu_bg bgs[TC_MENU_MAX_LAYERS]; /* the backgrounds' running state */
    tc_menu_layer layers[TC_MENU_MAX_LAYERS];
    uint32_t layer_count;
    tc_menu_quad quads[TC_MENU_MAX_QUADS];
    uint32_t quad_count;
} tc_menu;

void tc_menu_init(tc_menu *m);
void tc_menu_free(tc_menu *m);
/* The bytes of menu.ttmn (copied). */
tc_result tc_menu_load(tc_menu *m, const void *data, size_t size);
/* The saved games LOAD GAME lists, newest first as given (copied). */
void tc_menu_set_games(tc_menu *m, const tc_menu_game *games, int count);
void tc_menu_set_options(tc_menu *m, const tc_options *o);
/* Back to the main menu's first screen (kept: the selection, the options, the games). */
void tc_menu_show(tc_menu *m, tc_menu_screen screen);
void tc_menu_input_press(tc_menu *m, tc_menu_input in);
/* A typed character (NEW GAME's name: letters, digits and spaces; lower case is made upper, as the exe's field).
 * Confirming an empty name names the game "ED" (a pad has no letters to type). */
void tc_menu_type(tc_menu *m, uint32_t codepoint);
/* The pointer, in viewport units (0..1, y down) of the last tc_menu_build. moved: over a choice, it is selected (true);
 * click: it is selected and taken (a value steps, a choice confirms; true when there was one). A right click is
 * TC_MI_BACK (the frontends). */
bool tc_menu_pointer(tc_menu *m, float x, float y);
bool tc_menu_click(tc_menu *m, float x, float y);
/* The pointer moved with its button held: a slider the click took hold of follows it (true: its value changed, the
 * command is TC_MC_OPTIONS). */
bool tc_menu_drag(tc_menu *m, float x, float y);
/* The wheel, rows > 0 down: OPTIONS' list scrolls (the selection stays in view), LOAD / DELETE GAME's selection moves. */
void tc_menu_scroll(tc_menu *m, int rows);
/* The game menu (or its OPTIONS) is showing: draw it over the game (its layers dim and swirl). */
bool tc_menu_in_game(const tc_menu *m);
/* The command the last inputs gave, and clears it. */
tc_menu_command tc_menu_take_command(tc_menu *m);
void tc_menu_update(tc_menu *m, float dt);
/* Lays the frame out for a viewport of this aspect (width / height): tc_menu.layers and .quads. */
void tc_menu_build(tc_menu *m, float aspect);
/* The frame's layers and quads (tc_menu.layers / .quads; for frontends whose language imports fixed arrays badly). */
const tc_menu_layer *tc_menu_layer_list(const tc_menu *m, uint32_t *count);
const tc_menu_quad *tc_menu_quad_list(const tc_menu *m, uint32_t *count);
/* The name NEW GAME gave (tc_menu.name). */
const char *tc_menu_name(const tc_menu *m);
/* The text a screen's choice shows (for checks and window titles): "" past the end. */
const char *tc_menu_label(const tc_menu *m, tc_menu_screen screen, int item, char *buf, size_t size);
int tc_menu_item_count(const tc_menu *m, tc_menu_screen screen);
/* A level's name as the game's menus show it (game.dsb's MNU_INV_*NAME: "South Plain" for sud0), else the level's own. */
const char *tc_menu_level_title(const tc_menu *m, const char *level, char *buf, size_t size);

#ifdef __cplusplus
}
#endif
#endif

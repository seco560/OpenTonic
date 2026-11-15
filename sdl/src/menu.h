/* The main menu (core tc_menu.h; port of Sources/OpenTonic/MenuScreen.swift): the core keeps the screens and lays
 * them out, this draws its layers (the space pictures, uv per corner, repeating: gpu_overlay_sampled) and letters
 * (gpu_quad over the atlas, tinted). Also the splash before it (game.bpg's TT_Pak), the saved games' folder
 * (tc_save.h) and the options' file (tc_options), all in SDL's pref path ("OpenTonic"). */
#ifndef TV_MENU_H
#define TV_MENU_H
#include "tonic/tonic.h"
#include "gpu.h"

typedef struct menu_screen {
    gpu *g;
    tc_menu *m;                  /* SDL_malloc'd: tc_menu is large */
    SDL_GPUTexture *atlas;
    SDL_GPUTexture *images[TC_MENU_MAX_LAYERS];
    SDL_GPUTexture *swirl;       /* over a level (the game menu): the swirls' picture, TC_MENU_SWIRL_ALPHA baked in */
    int nav;                     /* the pad's held direction (tc_menu_input + 1, 0 none) and when it repeats */
    float nav_repeat;
    bool typing;                 /* SDL text input is on (NEW GAME's name) */
} menu_screen;

/* false (the menu stays closed) when <level_folder>/menu.ttmn is missing or stale. swirl: over a level (the game
 * menu), the swirls' picture (a .png), else NULL. */
bool menu_open(menu_screen *s, gpu *g, SDL_Window *window, const char *level_folder, const tc_options *options,
               const char *swirl);
void menu_close(menu_screen *s, SDL_Window *window);
/* The saved games, newest first, into the menu's LOAD / DELETE lists (and their paths: menu_save_path). */
void menu_refresh_games(menu_screen *s);
/* The path of LOAD / DELETE's game i (SDL_free it), NULL past the end. */
char *menu_save_path(int i);
/* Keys, typed text and pad buttons; the left stick each frame (menu_update). Returns the command they gave. */
tc_menu_command menu_key(menu_screen *s, const SDL_KeyboardEvent *k);
tc_menu_command menu_text(menu_screen *s, const char *utf8);
tc_menu_command menu_pad_button(menu_screen *s, SDL_GamepadButton b);
tc_menu_command menu_update(menu_screen *s, SDL_Window *window, SDL_Gamepad *pad, float dt);
void menu_draw(menu_screen *s, gpu_frame *f, SDL_GPURenderPass *pass);
/* The mouse, in target pixels of a w x h target: over a choice selects it; a left click takes it (the command), a right
 * one goes back. */
void menu_pointer(menu_screen *s, float x, float y, float w, float h);
tc_menu_command menu_click(menu_screen *s, float x, float y, float w, float h, bool right);
/* The pointer moved with the left button held (a slider follows it); the wheel, rows > 0 down. */
tc_menu_command menu_drag(menu_screen *s, float x, float y, float w, float h);
void menu_wheel(menu_screen *s, int rows);

/* ---- saved games: <pref>/Saves/<NAME>.ttsv ---- */
char *saves_folder(void);
/* The game on show under its name; false: nothing saved. */
bool saves_write(tc_viewer *v, const char *name);
/* The game in the file, its level-saved values put in place (tc_save_read); false: unreadable. */
bool saves_read(const char *path, tc_save_game *out);
void saves_delete_named(const char *name);

/* ---- the options: <pref>/options.cfg (tc_options_write) ---- */
void options_load(tc_options *o);
void options_store(const tc_options *o);

#endif

/* Saved games (docs/ENGINE.md "Saved games"; port).
 *
 * Read from the exe (SAI, GAM_00421500 / GAM_004219e0) and the original install's GameData/SaveGame/<game>/ (encrypted):
 * General.sav (the games), General%03d.sav (a game), Level%03d.sav (the player's state and where he is) and one
 * <Level>.sav per level visited (its actors' level-saved variables, dsgvar flag 0x800). The game saves when its scripts
 * ask (PROC 118 SaveGame; the save points) and on a level change.
 * Port: one file per game, our own format: what tc_viewer_carry takes into a level (its name, the arrival place, Ed's
 * dsgvars, the powers, the lives, the 31 save values), the last save point's checkpoint and Ed's life, and the whole
 * table of level-saved variables (tc_cast_level_saves_*). Loading a game opens its level as a portal's arrival does,
 * then puts Ed at the checkpoint. The frontends decide when to save (tc_viewer_save_serial, a level's arrival, the way
 * back to the menu, quitting) and where the files go. */
#ifndef TONIC_TC_SAVE_H
#define TONIC_TC_SAVE_H
#include "tc_viewer.h"
#include "tc_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TC_SAVE_EXTENSION "ttsv"

typedef struct tc_save_info {
    char name[TC_MENU_NAME];  /* the game's name (NEW GAME's field) */
    char level[64];           /* the level to open: its file name without the extension (any case) */
    uint8_t edition;          /* tc_level.edition: 0 the retail game's folder, 1 the Special Edition's */
    uint64_t time;            /* when it was saved, seconds since 1970 (the frontend's clock) */
    uint8_t lives;
    uint16_t powers;
} tc_save_info;

typedef struct tc_save_game {
    tc_save_info info;
    tc_viewer_carry carry;    /* tc_viewer_desc.carry for the level's viewer (with the checkpoint and the life) */
} tc_save_game;

/* A save of the game on show, `name` and `time` its info. NULL: nothing to save (no Ed's AI, or not a game: a demo
 * tape). The bytes are tc_alloc'd: tc_free them. While a level change is under way, it is the next level's arrival. */
uint8_t *tc_save_make(tc_viewer *v, const char *name, uint64_t time, size_t *size);
/* The info alone (the menu's list). */
bool tc_save_read_info(const void *data, size_t size, tc_save_info *out);
/* The whole game; installs its level-saved variables (tc_cast_level_saves_*, replacing what is there): call it just
 * before making the level's viewer with out->carry. */
tc_result tc_save_read(const void *data, size_t size, tc_save_game *out);
/* A new game (the main menu's NEW GAME), as a carry for its first level's viewer: the level, the progress of a new game
 * (GAM_00419290: no powers, save value 0 = 0, 1-6 = -1, the rest 0; three lives), nothing else; the level-saved table
 * is cleared too (tc_cast_level_saves_clear). Recorded with the run, as any carry. */
void tc_save_start_new_game(tc_viewer_carry *out, const char *level);
/* The games LOAD GAME and DELETE GAME list (tc_menu_set_games), from their infos, in the order given. */
void tc_save_menu_games(tc_menu *m, const tc_save_info *infos, int count);
/* A new game's name as a file name: letters, digits and '_' for the rest ("ED 2" -> "ED_2"). */
void tc_save_file_name(const char *name, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif

// Bottom screen (touch) user interface
#pragma once
#include "common.h"
#include "items.h"

enum {
    UI_HUD = 0,       // in-game hotbar + look pad
    UI_INVENTORY,
    UI_CRAFTING,
    UI_FURNACE,
    UI_CHEST,
    UI_CREATIVE,
    UI_PAUSE,
    UI_DEATH,
    UI_TITLE,
    UI_WORLDS,
    UI_NEWWORLD,
    UI_OPTIONS,
    UI_HELP,
    UI_LOADING,
    UI_CONFIRM_DELETE,
    UI_SLEEP,
};

void ui_init(void);
void ui_open(int screen);
void ui_open_chest(int x, int y, int z);
void ui_open_furnace(int x, int y, int z);
void ui_close(void);          // return to the game HUD
int ui_screen(void);
bool ui_in_game_menu(void);   // inventory-type screens over the running game
void ui_dirty(void);
// Called every frame; processes touch and redraws. Fills look deltas for the HUD.
void ui_frame(void);
// Look deltas produced by stylus dragging on the HUD look pad (angle units)
extern int ui_look_dx, ui_look_dy;
// Stylus actions from the look pad
extern bool ui_tap_use, ui_hold_action;
extern bool ui_sneak_toggle;
extern bool ui_jump_button;

// Loading screen
void ui_loading(const char *title, const char *stage, int pct);

// Menu results consumed by main
enum { MENU_NONE = 0, MENU_PLAY_SLOT, MENU_CREATE_WORLD, MENU_QUIT_TO_TITLE, MENU_RESPAWN, MENU_SAVE_QUIT, MENU_DELETE_SLOT };
extern int menu_action;
extern int menu_slot;
extern int new_world_mode, new_world_size;
extern uint32_t new_world_seed;

// World slot info shown in the menu
typedef struct {
    bool exists;
    char name[24];
    int mode;
    int size;
    uint32_t seed;
    uint32_t play_ticks;
} SlotInfo;
extern SlotInfo slot_info[3];
void ui_text_report(const char *const *lines, int n);

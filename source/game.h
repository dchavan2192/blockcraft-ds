// Global game state, settings and per-frame input
#pragma once
#include "common.h"

typedef struct {
    int look_speed;     // 1..10
    bool invert_y;
    bool auto_jump;
    int render_dist;    // 2..5 chunks
    bool swap_lr;
    bool show_coords;
    int volume;         // 0..10
    bool music;
    bool peaceful;
} Settings;

extern Settings settings;

typedef struct {
    int fwd, strafe;      // -1..1
    bool jump;            // held
    bool jump_pressed;    // edge
    bool sneak;           // held
    bool use_tap;         // tap of the action button
    bool action_hold;     // action button held beyond tap threshold
    bool action_start;    // first frame of hold
} Input;

extern Input input;

// Message shown on the top screen for a while
void game_message(const char *msg, int ticks);
void game_save_now(void);
extern bool save_available;
extern int game_screen;   // ui screen id
extern uint32_t frame_counter;

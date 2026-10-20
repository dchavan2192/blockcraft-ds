// Video setup, textures, top-screen text layer
#pragma once
#include "common.h"

#define TEX_ICON_BASE 192   // texture slot index where item icons start (256 bytes each)
#define TEX_CLOUD_ADDR (128 * 1024)
#define PAL_WORLD 0
#define PAL_ICONS 1

void gfx_init(void);
void gfx_upload_textures(void);
void gfx_animate_textures(uint32_t frame);

// Top screen 2D text/HUD layer (BG1, 32x24 tiles)
void hud_clear(void);
void hud_print(int tx, int ty, const char *s, int pal);
void hud_put_tile(int tx, int ty, int tile, int pal);
void hud_center(int ty, const char *s, int pal);
void hud_fill(int tx, int ty, int w, int h, int tile, int pal);
// HUD tile ids (after the 96 font glyphs)
enum {
    HT_FONT = 0,
    HT_HEART_FULL = 96, HT_HEART_HALF, HT_HEART_EMPTY,
    HT_FOOD_FULL, HT_FOOD_HALF, HT_FOOD_EMPTY,
    HT_BUBBLE, HT_BUBBLE_POP,
    HT_ARMOR_FULL, HT_ARMOR_HALF, HT_ARMOR_EMPTY,
    HT_CROSS_TL, HT_CROSS_TR, HT_CROSS_BL, HT_CROSS_BR,
    HT_SOLID, HT_SHADE,
    HT_HEART_HARD_FULL, HT_HEART_HARD_HALF,
    HT_XP0, HT_XP8 = HT_XP0 + 8,   // experience bar segments with 0..8 pixels filled
    HT_COUNT
};
enum { HP_WHITE = 0, HP_YELLOW, HP_RED, HP_GRAY, HP_GREEN, HP_SHADOW };

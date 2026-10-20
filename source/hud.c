// Top screen HUD: crosshair, health, hunger, armor, air, messages
#include <stdio.h>
#include "hud.h"
#include "gfx.h"
#include "game.h"
#include "player.h"
#include "world.h"
#include "render.h"
#include "ui.h"
#include "mesh.h"

static char msg[2][64];
static int msg_timer[2];
static int name_timer;
static int last_sel = -1;
static uint16_t last_item;

void game_message(const char *m, int ticks)
{
    // shift older message up
    strcpy(msg[1], msg[0]);
    msg_timer[1] = msg_timer[0];
    strncpy(msg[0], m, sizeof(msg[0]) - 1);
    msg[0][sizeof(msg[0]) - 1] = 0;
    msg_timer[0] = ticks * 3;
}

void hud_reset(void)
{
    msg_timer[0] = msg_timer[1] = 0;
    name_timer = 0;
    last_sel = -1;
}

static void row_icons(int x0, int y, int value, int full, int half, int empty, bool right_to_left, int count)
{
    for (int i = 0; i < count; i++) {
        int v = value - i * 2;
        int tile = v >= 2 ? full : v == 1 ? half : empty;
        int x = right_to_left ? x0 - i : x0 + i;
        hud_put_tile(x, y, tile, 0);
    }
}

void hud_update(int vblanks, bool show_debug)
{
    hud_clear();
    int s = ui_screen();
    if (s == UI_PAUSE) {
        hud_center(10, "Game Paused", HP_WHITE);
        return;
    }
    if (s == UI_DEATH) {
        hud_center(9, "You died!", HP_RED);
        hud_center(11, death_message, HP_WHITE);
        return;
    }
    // crosshair
    if (s == UI_HUD) {
        hud_put_tile(15, 11, HT_CROSS_TL, 0);
        hud_put_tile(16, 11, HT_CROSS_TR, 0);
        hud_put_tile(15, 12, HT_CROSS_BL, 0);
        hud_put_tile(16, 12, HT_CROSS_BR, 0);
    }
    if (player.mode == MODE_SURVIVAL) {
        // health (flash when hurt)
        int hp = player.health;
        if (player.hurt_time > 0 && (player.hurt_time & 2))
            hp = 0;
        row_icons(1, 22, hp, HT_HEART_FULL, HT_HEART_HALF, HT_HEART_EMPTY, false, 10);
        row_icons(30, 22, player.food, HT_FOOD_FULL, HT_FOOD_HALF, HT_FOOD_EMPTY, true, 10);
        int ap = player_armor_points();
        if (ap > 0)
            row_icons(1, 21, ap, HT_ARMOR_FULL, HT_ARMOR_HALF, HT_ARMOR_EMPTY, false, 10);
        // experience bar along the bottom with the level above it
        int need = xp_to_next_level(player.xp_level);
        int px = need > 0 ? player.xp_points * 240 / need : 0;
        for (int i = 0; i < 30; i++) {
            int f = iclamp(px - i * 8, 0, 8);
            hud_put_tile(1 + i, 23, HT_XP0 + f, HP_GREEN);
        }
        if (player.xp_level > 0) {
            char lv[8];
            snprintf(lv, sizeof(lv), "%d", player.xp_level);
            hud_center(22, lv, HP_GREEN);
        }
        if (player.head_in_water || player.air < 300) {
            int bubbles = (player.air + 29) / 30;
            for (int i = 0; i < 10; i++)
                if (i < bubbles)
                    hud_put_tile(30 - i, 21, HT_BUBBLE, 0);
        }
    } else if (player.flying) {
        hud_print(1, 22, "Flying", HP_GRAY);
    }
    // item name after switching
    ItemStack *h = player_held();
    if (player.sel != last_sel || h->id != last_item) {
        last_sel = player.sel;
        last_item = h->id;
        name_timer = 100;
    }
    if (name_timer > 0) {
        name_timer -= vblanks;
        if (!stack_empty(h) && item_valid(h->id))
            hud_center(20, items[h->id].name, HP_WHITE);
    }
    for (int i = 0; i < 2; i++) {
        if (msg_timer[i] > 0) {
            msg_timer[i] -= vblanks;
            hud_center(7 - i, msg[i], i == 0 ? HP_YELLOW : HP_WHITE);
        }
    }
    if (show_debug) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%dfps s%d p%d v%d b%d", 60 / (vblanks ? vblanks : 1), stat_sections_drawn, stat_polys, stat_verts, stat_budget);
        hud_print(0, 0, buf, HP_GRAY);
#ifdef FIXED_SEED
        int px = player.body.x >> 12, py = player.body.y >> 12, pz = player.body.z >> 12;
        extern uint32_t mesh_build_ticks, mesh_build_count;
        snprintf(buf, sizeof(buf), "%d %d %d m%luk mb%lu/%lu", px, py, pz, (unsigned long)(mesh_bytes >> 10),
                 (unsigned long)mesh_build_ticks, (unsigned long)mesh_build_count);
        hud_print(0, 1, buf, HP_GRAY);
        // times in 1/32.7 ms ticks: ui, game ticks, misc, render, hud, build
        extern uint16_t prof_ent;
        snprintf(buf, sizeof(buf), "u%d g%d t%d e%d w%d b%d",
                 (uint16_t)(prof_t[1] - prof_t[0]), (uint16_t)(prof_t[2] - prof_t[1]), (uint16_t)(prof_t[7] - prof_t[3]),
                 (uint16_t)(prof_ent - prof_t[7]), (uint16_t)(prof_t[4] - prof_ent), (uint16_t)(prof_t[6] - prof_t[5]));
        hud_print(0, 6, buf, HP_YELLOW);
        extern uint32_t mesh_prof[8];
        snprintf(buf, sizeof(buf), "%lu %lu %lu %lu %lu %lu %lu %lu", mesh_prof[0] >> 4, mesh_prof[1] >> 4, mesh_prof[2] >> 4,
                 mesh_prof[3] >> 4, mesh_prof[4] >> 4, mesh_prof[5] >> 4, mesh_prof[6] >> 4, mesh_prof[7] >> 4);
        hud_print(0, 7, buf, HP_YELLOW);
        int ccx = (px >> 4) & (world.chunks - 1), ccz = (pz >> 4) & (world.chunks - 1);
        for (int sy = 0; sy < 4; sy++) {
            Section *sc = section_at(ccx, sy, ccz);
            int lit = 0, dk = 0;
            for (int d = 0; d < 6; d++) { lit += sc->nq[d]; dk += sc->nq[SL_DARK_W + d]; }
            snprintf(buf, sizeof(buf), "s%d L%d D%d U%d e%d b%d", sy, lit, dk, sc->nq[SL_U], sc->empty, sc->built);
            hud_print(0, 2 + sy, buf, HP_GRAY);
        }
#endif
    }
}

// Self-test scenarios (only compiled into the test build)
#ifdef TESTMODE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "common.h"
#include "game.h"
#include "world.h"
#include "worldgen.h"
#include "blocks.h"
#include "items.h"
#include "player.h"
#include "inventory.h"
#include "entity.h"
#include "sim.h"
#include "mesh.h"
#include "render.h"
#include "gfx.h"
#include "ui.h"
#include "save.h"

extern bool test_input_override;
int game_frame(int vblanks, uint32_t *tick_accum_p);
bool create_world(int slot);
void enter_world(void);

static char lines[48][48];
static const char *lp[48];
static int nlines;
static uint32_t acc;
static int px, py, pz;

static void report(const char *fmt, ...)
{
    if (nlines >= 48)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lines[nlines], sizeof(lines[0]), fmt, ap);
    va_end(ap);
    lp[nlines] = lines[nlines];
    nlines++;
    ui_text_report(lp, nlines);
}

static void check(bool ok, const char *fmt, ...)
{
    char buf[44];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    report("%s %s", ok ? "PASS" : "FAIL", buf);
}

static const char *cur_label = "";

static void frames(int n)
{
    for (int i = 0; i < n; i++) {
        frame_counter++;
#ifdef TEST_CRAFT
        scanKeys();
#endif
        game_frame(1, &acc);
#if !defined(TEST_TOUR) && !defined(TEST_CRAFT)
        hud_print(0, 1, cur_label, HP_YELLOW);
        {
            char b[32];
            snprintf(b, sizeof(b), "est %d vis %d bfs %d%s", stat_budget, stat_sections_drawn, stat_bfs, stat_budget_hit ? " B" : "");
            hud_print(10, 2, b, HP_WHITE);
            snprintf(b, sizeof(b), "pend %d mask %02x", render_pending_count(), render_debug_mask());
            hud_print(10, 3, b, HP_WHITE);
#ifdef TEST_VIEW
            int ccx = (player.body.x >> 16) & (world.chunks - 1), ccz = (player.body.z >> 16) & (world.chunks - 1);
            for (int sy = 0; sy < 4; sy++) {
                Section *sc = section_at(ccx, sy, ccz);
                int dk = 0;
                for (int d = 0; d < 6; d++) dk += sc->nq[SL_DARK_W + d];
                snprintf(b, sizeof(b), "s%d %d %d %d %d %d %d|%d %d dk%d", sy, sc->nq[0], sc->nq[1], sc->nq[2], sc->nq[3], sc->nq[4], sc->nq[5], sc->nq[6], sc->nq[7], dk);
                hud_print(0, 4 + sy, b, HP_YELLOW);
            }
#endif
        }
#endif
#if !defined(TEST_TOUR) && !defined(TEST_CRAFT)
        ui_text_report(lp, nlines);
#endif
        swiWaitForVBlank();
    }
}

static int ticks_until(bool (*cond)(void), int max_ticks)
{
    uint32_t t0 = world.ticks;
    while ((int)(world.ticks - t0) < max_ticks) {
        frames(1);
        if (cond())
            return world.ticks - t0;
    }
    return -1;
}

static void place_player(int x, int y, int z, int yaw_deg, int pitch_deg)
{
    player.body.x = (x << 12) + 2048;
    player.body.y = y << 12;
    player.body.z = (z << 12) + 2048;
    player.prev_x = player.body.x;
    player.prev_y = player.body.y;
    player.prev_z = player.body.z;
    player.vx = player.vy = player.vz = 0;
    player.yaw = ANG_DEG(yaw_deg) & (ANG_FULL - 1);
    player.pitch = ANG_DEG(pitch_deg);
    player.fall_dist = 0;
    player.body.on_ground = false;
}

static void flat_area(int cx, int cz, int y0, int r)
{
    for (int z = cz - r; z <= cz + r; z++)
        for (int x = cx - r; x <= cx + r; x++) {
            for (int y = 1; y < y0 - 4; y++)
                if (world_get(x, y, z) == B_AIR || block_liquid(world_get(x, y, z)))
                    world_set_raw(x, y, z, B_STONE);
            for (int y = y0 - 4; y < y0 - 1; y++)
                world_set_raw(x, y, z, B_DIRT);
            world_set_raw(x, y0 - 1, z, B_GRASS);
            for (int y = y0; y < WORLD_H; y++)
                world_set_raw(x, y, z, B_AIR);
        }
    world_init_light(NULL);
    mesh_invalidate_all();
}

static void clear_inputs(void)
{
    memset(&input, 0, sizeof(input));
}

static bool target_gone(void) { return world_get(px, py + 1, pz - 2) == B_AIR; }
static bool on_ground(void) { return player.body.on_ground; }

static void give(int slot, int id, int count)
{
    player.inv[slot].id = id;
    player.inv[slot].count = count;
    player.inv[slot].damage = 0;
}

static int count_blocks(int id, int cx, int cy, int cz, int r, int ry)
{
    int n = 0;
    for (int y = cy - ry; y <= cy + ry; y++)
        for (int z = cz - r; z <= cz + r; z++)
            for (int x = cx - r; x <= cx + r; x++) {
                int b = world_get(x, y, z);
                if (id < 0 ? block_liquid(b) && is_water(b) : b == id)
                    n++;
            }
    return n;
}

void test_run(void)
{
    test_input_override = true;
    settings.render_dist = 3;
    new_world_seed = 777;
    new_world_mode = MODE_SURVIVAL;
    new_world_size = 128;
    create_world(0);
    world.time = 3000;
    px = world.spawn_x;
    pz = world.spawn_z;
    py = world.spawn_y + 1;
    if (py > 50) py = 50;
    report("Test world seed 777, spawn %d %d %d", px, py, pz);
#ifdef TEST_CRAFT
    {
        ui_close();
        test_input_override = false;
        memset(player.inv, 0, sizeof(player.inv));
        give(0, B_LOG_OAK, 3);
        give(1, B_COBBLESTONE, 8);
        give(2, I_COAL, 2);
        give(9, I_IRON_INGOT, 3);
        give(10, I_STRING, 3);
        give(11, B_DIRT, 20);
        place_player(px, py, pz, 0, 0);
#ifdef CRAFT_TABLE
        ui_open(UI_CRAFTING);
#else
        ui_open(UI_INVENTORY);
#endif
        while (1) frames(1);
    }
#endif
#ifdef TEST_TOUR
    {
        clear_inputs();
        ui_close();
        ui_dirty();
        // 1 sunset, 2 night, 3 cave with torches, 4 underwater, 5 crafting, 6 furnace, 7 chest, 8 death
        place_player(px, py, pz, 200, 5);
        world.time = 12300;
        cur_label = "sunset";
        frames(120);
        world.time = 18000;
        cur_label = "night";
        frames(120);
        place_player(px, py + 6, pz, 200, 60);
        player.flying = true;
        frames(80);
        world.time = 12800;
        frames(80);
        world.time = 23000;
        frames(80);
        player.flying = false;
        // find a dark cave cell near spawn
        int found = 0, cxp = 0, cyp = 0, czp = 0;
        for (int r = 0; r < 40 && !found; r++)
            for (int dz = -r; dz <= r && !found; dz++)
                for (int dx = -r; dx <= r && !found; dx++) {
                    if (iabs(dx) != r && iabs(dz) != r)
                        continue;
                    for (int y = 6; y < 26 && !found; y++) {
                        int x = px + dx, z = pz + dz;
                        if (world_get(x, y, z) == B_AIR && world_get(x, y + 1, z) == B_AIR && block_solid(world_get(x, y - 1, z)) &&
                            (world_light_raw(x, y, z) >> 4) == 0) {
                            found = 1; cxp = x; cyp = y; czp = z;
                        }
                    }
                }
        world.time = 6000;
        if (found) {
            place_player(cxp, cyp, czp, 90, 10);
            world_set(cxp + 2, cyp, czp, B_TORCH);
            world_set(cxp - 3, cyp, czp + 1, B_TORCH);
            cur_label = "cave";
            frames(120);
            place_player(cxp, cyp, czp, 270, 10);
            frames(60);
        } else {
            cur_label = "no cave";
            frames(180);
        }
        // underwater
        int wf = 0, wx = 0, wz = 0;
        for (int r = 0; r < 60 && !wf; r++)
            for (int dz = -r; dz <= r && !wf; dz++)
                for (int dx = -r; dx <= r && !wf; dx++) {
                    if (iabs(dx) != r && iabs(dz) != r)
                        continue;
                    if (is_water(world_get(px + dx, SEA_LEVEL - 3, pz + dz))) {
                        wf = 1; wx = px + dx; wz = pz + dz;
                    }
                }
        cur_label = wf ? "underwater" : "no water";
        if (wf)
            place_player(wx, SEA_LEVEL - 3, wz, 0, 0);
        frames(120);
        cur_label = "water surface";
        if (wf)
            place_player(wx, SEA_LEVEL + 1, wz, 0, 20);
        player.flying = true;
        frames(120);
        player.flying = false;
        place_player(px, py, pz, 0, 0);
        give(0, TOOL_ID(MAT_IRON, TK_PICKAXE), 1);
        give(1, B_PLANKS_OAK, 23);
        give(2, I_COAL, 5);
        give(9, B_COBBLESTONE, 64);
        give(10, I_STICK, 12);
        cur_label = "crafting";
        ui_open(UI_CRAFTING);
        frames(120);
        world_set(px + 1, py, pz, B_FURNACE);
        ui_open_furnace(px + 1, py, pz);
        cur_label = "furnace";
        frames(120);
        world_set(px - 1, py, pz, B_CHEST);
        ui_open_chest(px - 1, py, pz);
        cur_label = "chest";
        frames(120);
        ui_close();
        ui_open(UI_INVENTORY);
        cur_label = "inventory";
        frames(120);
        ui_close();
        player_damage(100, DMG_GENERIC);
        cur_label = "death";
        frames(120);
        while (1) frames(1);
    }
#endif
#ifdef TEST_VIEW
    test_input_override = true;
    clear_inputs();
    for (int a = 0; a < 8; a++) {
        place_player(px, py, pz, a * 45, 15);
        cur_label = "view";
        frames(60);
    }
    place_player(px, py + 12, pz, 30, 35);
    frames(60);
    while (1) frames(1);
#endif
    flat_area(px, pz, py, 12);
    entities_clear();
    player.mode = MODE_SURVIVAL;
    memset(player.inv, 0, sizeof(player.inv));
    place_player(px, py, pz, 0, 0);
    clear_inputs();
    cur_label = "T1 punch log";
    frames(30);

    // ---- T1: mining a log by hand ----
    world_set(px, py + 1, pz - 2, B_LOG_OAK);
    frames(5);
    input.action_hold = true;
    int t = ticks_until(target_gone, 200);
    input.action_hold = false;
    check(t > 0 && t >= 50 && t <= 70, "log by hand: %d ticks (~60)", t);
    input.fwd = 1;
    frames(60);
    input.fwd = 0;
    check(inv_count_item(player.inv, INV_SIZE, B_LOG_OAK) == 1, "log picked up (%d)", inv_count_item(player.inv, INV_SIZE, B_LOG_OAK));

    // ---- T2: stone needs a pickaxe ----
    cur_label = "T2 stone";
    place_player(px, py, pz, 0, 0);
    frames(5);
    world_set(px, py + 1, pz - 2, B_STONE);
    give(0, TOOL_ID(MAT_WOOD, TK_PICKAXE), 1);
    player.sel = 0;
    frames(5);
    input.action_hold = true;
    t = ticks_until(target_gone, 200);
    input.action_hold = false;
    check(t >= 18 && t <= 30, "stone w/ wood pick: %d ticks (~23)", t);
    input.fwd = 1;
    frames(60);
    input.fwd = 0;
    check(inv_count_item(player.inv, INV_SIZE, B_COBBLESTONE) == 1, "cobblestone dropped");
    check(player.inv[0].damage == 1, "tool wear %d", player.inv[0].damage);
    // hand mining stone gives nothing
    place_player(px, py, pz, 0, 0);
    frames(3);
    world_set(px, py + 1, pz - 2, B_STONE);
    player.sel = 5;
    frames(3);
    input.action_hold = true;
    t = ticks_until(target_gone, 300);
    input.action_hold = false;
    input.fwd = 1;
    frames(40);
    input.fwd = 0;
    check(t > 100 && inv_count_item(player.inv, INV_SIZE, B_COBBLESTONE) == 1, "hand on stone: %d t, no drop", t);

    // ---- T3: crafting recipes ----
    ItemStack g[9], res;
    memset(g, 0, sizeof(g));
    g[0] = (ItemStack){ B_LOG_BIRCH, 1, 0, 0 };
    craft_match(g, 2, &res);
    check(res.id == B_PLANKS_BIRCH && res.count == 4, "log->planks");
    memset(g, 0, sizeof(g));
    g[1] = (ItemStack){ B_PLANKS_OAK, 1, 0, 0 };
    g[3] = (ItemStack){ B_PLANKS_SPRUCE, 1, 0, 0 };
    craft_match(g, 2, &res);
    check(res.id == I_STICK && res.count == 4, "sticks (mixed wood)");
    memset(g, 0, sizeof(g));
    for (int i = 0; i < 3; i++) g[i] = (ItemStack){ B_COBBLESTONE, 1, 0, 0 };
    g[4] = g[7] = (ItemStack){ I_STICK, 1, 0, 0 };
    craft_match(g, 3, &res);
    check(res.id == TOOL_ID(MAT_STONE, TK_PICKAXE), "stone pickaxe");
    memset(g, 0, sizeof(g));
    g[1] = g[2] = g[5] = (ItemStack){ I_IRON_INGOT, 1, 0, 0 };
    g[8] = (ItemStack){ I_STICK, 1, 0, 0 };
    g[4] = (ItemStack){ 0 };
    // axe mirrored: .MM / .SM? use standard  MM. / MS. / .S.
    memset(g, 0, sizeof(g));
    g[1] = g[2] = g[5] = (ItemStack){ I_IRON_INGOT, 1, 0, 0 };
    g[4] = g[7] = (ItemStack){ I_STICK, 1, 0, 0 };
    craft_match(g, 3, &res);
    check(res.id == TOOL_ID(MAT_IRON, TK_AXE), "iron axe (mirrored)");
    memset(g, 0, sizeof(g));
    for (int i = 0; i < 9; i++) if (i != 4) g[i] = (ItemStack){ B_COBBLESTONE, 1, 0, 0 };
    craft_match(g, 3, &res);
    check(res.id == B_FURNACE, "furnace");
    memset(g, 0, sizeof(g));
    g[0] = (ItemStack){ I_CHARCOAL, 1, 0, 0 };
    g[2] = (ItemStack){ I_STICK, 1, 0, 0 };
    craft_match(g, 2, &res);
    check(res.id == B_TORCH && res.count == 4, "torches from charcoal");
    memset(g, 0, sizeof(g));
    for (int i = 0; i < 3; i++) g[i] = (ItemStack){ B_WOOL_WHITE + 3, 1, 0, 0 };
    for (int i = 3; i < 6; i++) g[i] = (ItemStack){ B_PLANKS_OAK, 1, 0, 0 };
    craft_match(g, 3, &res);
    check(res.id == I_BED, "bed from colored wool");

    // ---- T4: placing a block ----
    cur_label = "T4 place";
    give(1, B_PLANKS_OAK, 10);
    player.sel = 1;
    place_player(px, py, pz, 0, 45);
    frames(5);
    input.use_tap = true;
    frames(6);
    check(world_get(px, py, pz - 2) == B_PLANKS_OAK, "placed planks (%d)", world_get(px, py, pz - 2));
    check(player.inv[1].count == 9, "planks consumed (%d)", player.inv[1].count);

    // ---- T5: furnace smelting ----
    world_set(px + 2, py, pz - 2, B_FURNACE);
    Furnace *f = furnace_get(px + 2, py, pz - 2, true);
    f->in = (ItemStack){ B_IRON_ORE, 2, 0, 0 };
    f->fuel = (ItemStack){ I_COAL, 1, 0, 0 };
    frames(20);
    check(is_furnace(world_get(px + 2, py, pz - 2)) && world_get(px + 2, py, pz - 2) >= B_FURNACE_LIT, "furnace lit");
    frames(1250);
    check(f->out.id == I_IRON_INGOT && f->out.count == 2, "smelted %d ingots", f->out.count);

    // ---- T6: water flow ----
    cur_label = "T6 water";
    place_player(px - 4, py, pz + 4, 45, 30);
    world_set(px + 3, py, pz + 3, B_WATER);
    sim_notify(px + 3, py, pz + 3);
    frames(240);
    int wc = count_blocks(-1, px + 3, py, pz + 3, 8, 0);
    check(wc >= 60 && wc <= 120, "water spread %d cells (~85)", wc);
    // source removal dries up the flow
    // ---- T7: lava + water ----
    world_set(px + 3, py, pz + 9, B_LAVA);
    sim_notify(px + 3, py, pz + 9);
    frames(300);
    int ob = count_blocks(B_OBSIDIAN, px + 3, py, pz + 6, 6, 1) + count_blocks(B_COBBLESTONE, px + 3, py, pz + 6, 6, 1);
    check(ob > 0, "lava met water -> %d stone", ob);

    // ---- T8: falling sand ----
    world_set(px - 3, py + 6, pz - 3, B_SAND);
    sim_notify(px - 3, py + 6, pz - 3);
    frames(60);
    check(world_get(px - 3, py, pz - 3) == B_SAND, "sand fell");

    // ---- T9: tree growth ----
    world_set(px - 6, py, pz - 6, B_SAPLING_OAK);
    worldgen_grow_tree(px - 6, py, pz - 6, 0);
    check(world_get(px - 6, py + 1, pz - 6) == B_LOG_OAK, "sapling grew");

    // ---- T10: eating ----
    player.food = 10;
    give(2, I_BREAD, 2);
    player.sel = 2;
    place_player(px, py, pz, 0, -85);
    frames(3);
    input.use_tap = true;
    frames(150);
    check(player.food == 15 && player.inv[2].count == 1, "ate bread: food %d", player.food);

    // ---- T11: fall damage ----
    player.health = 20;
    player.invuln = 0;
    place_player(px - 8, py + 10, pz + 8, 0, 0);
    frames(3);
    int ft = ticks_until(on_ground, 100);
    frames(3);
    check(ft > 0 && player.health == 13, "fall 10: hp %d (13)", player.health);
    player.health = 20;

    // ---- T12: animals ----
    cur_label = "T12 animals";
    place_player(px, py, pz + 6, 0, 10);
    world.time = 3000;
    int an[4];
    an[0] = entity_spawn(ET_PIG, ((px - 2) << 12), py << 12, ((pz) << 12));
    an[1] = entity_spawn(ET_COW, ((px + 1) << 12), py << 12, ((pz) << 12));
    an[2] = entity_spawn(ET_SHEEP, ((px - 2) << 12), py << 12, ((pz + 3) << 12));
    an[3] = entity_spawn(ET_CHICKEN, ((px + 2) << 12), py << 12, ((pz + 3) << 12));
    frames(240);
    static const uint8_t at[4] = { ET_PIG, ET_COW, ET_SHEEP, ET_CHICKEN };
    int alive = 0;
    for (int i = 0; i < 4; i++)
        if (an[i] >= 0 && entities[an[i]].type == at[i] && entities[an[i]].ai != 9)
            alive++;
    check(alive == 4, "animals alive %d/4 (total p%d c%d s%d h%d)", alive, entities_count(ET_PIG), entities_count(ET_COW), entities_count(ET_SHEEP), entities_count(ET_CHICKEN));

    // ---- T13: monsters ----
    cur_label = "T13 monsters";
    world.time = 16000;
    player.health = 20;
    for (int i = 0; i < 4; i++)
        player.armor[i] = (ItemStack){ ARMOR_ID(AM_IRON, i), 1, 0, 0 };
    int mn[4];
    mn[0] = entity_spawn(ET_ZOMBIE, ((px - 2) << 12), py << 12, ((pz + 1) << 12));
    mn[1] = entity_spawn(ET_SKELETON, ((px + 4) << 12), py << 12, ((pz - 1) << 12));
    mn[2] = entity_spawn(ET_SPIDER, ((px - 5) << 12), py << 12, ((pz) << 12));
    mn[3] = entity_spawn(ET_SHROOM, ((px + 6) << 12), py << 12, ((pz - 4) << 12));
    frames(150);
    check(player.health < 20, "monsters hurt player: hp %d%s", player.health, player.dead ? " DEAD" : "");
    if (player.dead) {
        player_respawn();
        ui_close();
    }
    place_player(px, py, pz + 6, 0, 10);
    // kill the zombie with a sword
    give(3, TOOL_ID(MAT_DIAMOND, TK_SWORD), 1);
    player.sel = 3;
    static const uint8_t mt[4] = { ET_ZOMBIE, ET_SKELETON, ET_SPIDER, ET_SHROOM };
    for (int tries = 0; tries < 4; tries++) {   // a mob hit moments ago ignores damage briefly
        for (int i = 0; i < 4; i++)
            if (mn[i] >= 0 && entities[mn[i]].type == mt[i] && entities[mn[i]].ai != 9)
                entity_damage(mn[i], 30, 0, 0, true);
        frames(15);
    }
    frames(60);
    int still = 0;
    for (int i = 0; i < 4; i++)
        if (mn[i] >= 0 && entities[mn[i]].type == mt[i])
            still++;
    check(still == 0, "monsters killed (%d left)", still);
    player.health = 20;
    world.time = 3000;
    check(player_armor_points() == 15, "armor points %d (15)", player_armor_points());

    // ---- T14: TNT ----
    cur_label = "T14 TNT";
    place_player(px - 8, py, pz - 8, 135, 20);
    int before = count_blocks(B_AIR, px + 6, py - 2, pz + 6, 2, 1);
    int te = entity_spawn(ET_TNT, ((px + 6) << 12) + 2048, py << 12, ((pz + 6) << 12) + 2048);
    if (te >= 0) entities[te].fuse = 60;
    frames(200);
    int after = count_blocks(B_AIR, px + 6, py - 2, pz + 6, 2, 1);
    check(after > before + 10, "TNT crater %d -> %d air", before, after);

    // ---- T15: save round trip ----
    uint32_t sum = 0;
    size_t nb = (size_t)world.size * world.size * WORLD_H;
    for (size_t i = 0; i < nb; i++)
        sum = sum * 31 + world.blk[i];
    int logs = inv_count_item(player.inv, INV_SIZE, B_LOG_OAK);
    mesh_shutdown();
    size_t cap = 1024 * 1024;
    char *buf = NULL;
    while (cap >= 128 * 1024 && !(buf = malloc(cap)))
        cap -= 64 * 1024;
    if (!buf) {
        report("FAIL save: no memory for buffer");
    } else {
        FILE *mf = fmemopen(buf, cap, "wb");
        bool ok = mf && save_world_fp(mf, NULL);
        long sz = mf ? ftell(mf) : 0;
        if (mf) fclose(mf);
        check(ok, "save wrote %ld bytes (buf %d KB)", sz, (int)(cap >> 10));
        mf = fmemopen(buf, cap, "rb");
        bool lok = mf && load_world_fp(mf, NULL);
        if (mf) fclose(mf);
        uint32_t sum2 = 0;
        for (size_t i = 0; i < nb; i++)
            sum2 = sum2 * 31 + world.blk[i];
        check(lok && sum == sum2, "load restores blocks");
        check(inv_count_item(player.inv, INV_SIZE, B_LOG_OAK) == logs, "load restores inventory");
        free(buf);
        enter_world();
    }
    report("Done. Free-roam view follows.");
    test_input_override = false;
    cur_label = "done";
    place_player(px - 6, py + 3, pz - 6, 135, 20);
    while (1)
        frames(1);
}
#endif

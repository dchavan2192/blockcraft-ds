// BlockCraft DS - main loop and game state machine
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include "common.h"
#include "blocks.h"
#include "items.h"
#include "world.h"
#include "worldgen.h"
#include "mesh.h"
#include "render.h"
#include "gfx.h"
#include "gx.h"
#include "game.h"
#include "player.h"
#include "entity.h"
#include "inventory.h"
#include "sim.h"
#include "sound.h"
#include "ui.h"
#include "hud.h"
#include "save.h"
#include "assets_gen.h"

Settings settings;
Input input;
bool save_available;
int game_screen;
uint32_t frame_counter;

static volatile uint32_t vblank_count;
int current_slot = -1;
void test_run(void);
int game_frame(int vblanks, uint32_t *tick_accum_p);
static bool debug_overlay;

static uint32_t rng_state = 0x9E3779B9;
uint32_t rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

void debug_log(const char *fmt, ...)
{
    (void)fmt;
}

static void vblank_handler(void)
{
    vblank_count++;
}

static void default_settings(void)
{
    settings.look_speed = 5;
    settings.invert_y = false;
    settings.auto_jump = true;
    settings.render_dist = isDSiMode() ? 4 : 3;
    settings.swap_lr = false;
    settings.show_coords = false;
    settings.volume = 8;
    settings.music = true;
    settings.peaceful = false;
}

// ---------------------------------------------------------------------------
// First person hand / held item
// ---------------------------------------------------------------------------

static void hand_box(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1, const uint8_t tex[6], int light)
{
    // coordinates in fx blocks (camera space)
#define V(x, y, z) glVertex3v16((x), (y), (z))
    static const int dirs[6] = { FACE_W, FACE_E, FACE_D, FACE_U, FACE_N, FACE_S };
    for (int f = 0; f < 6; f++) {
        int d = dirs[f];
        // NORMAL must follow BEGIN: lighting uses the polygon attributes latched at BEGIN
        GFX_TEX_FORMAT = gx_texparam16(tex[d], TEXGEN_NONE);
        glBegin(GL_QUADS);
        GFX_DIFFUSE_AMBIENT = (uint32_t)light_ambient[d][light >> 4] << 16;
        GFX_SPECULAR_EMISSION = (uint32_t)light_emission[d][light & 15] << 16;
        GFX_NORMAL = 0;
        switch (d) {
            case FACE_U:
                glTexCoord2t16(0, 0); V(x0, y1, z0); glTexCoord2t16(0, 256); V(x0, y1, z1);
                glTexCoord2t16(256, 256); V(x1, y1, z1); glTexCoord2t16(256, 0); V(x1, y1, z0); break;
            case FACE_D:
                glTexCoord2t16(0, 0); V(x0, y0, z0); glTexCoord2t16(256, 0); V(x1, y0, z0);
                glTexCoord2t16(256, 256); V(x1, y0, z1); glTexCoord2t16(0, 256); V(x0, y0, z1); break;
            case FACE_N:
                glTexCoord2t16(256, 256); V(x0, y0, z0); glTexCoord2t16(256, 0); V(x0, y1, z0);
                glTexCoord2t16(0, 0); V(x1, y1, z0); glTexCoord2t16(0, 256); V(x1, y0, z0); break;
            case FACE_S:
                glTexCoord2t16(0, 256); V(x0, y0, z1); glTexCoord2t16(256, 256); V(x1, y0, z1);
                glTexCoord2t16(256, 0); V(x1, y1, z1); glTexCoord2t16(0, 0); V(x0, y1, z1); break;
            case FACE_W:
                glTexCoord2t16(0, 256); V(x0, y0, z0); glTexCoord2t16(256, 256); V(x0, y0, z1);
                glTexCoord2t16(256, 0); V(x0, y1, z1); glTexCoord2t16(0, 0); V(x0, y1, z0); break;
            default:
                glTexCoord2t16(256, 256); V(x1, y0, z0); glTexCoord2t16(256, 0); V(x1, y1, z0);
                glTexCoord2t16(0, 0); V(x1, y1, z1); glTexCoord2t16(0, 256); V(x1, y0, z1); break;
        }
        glEnd();
    }
#undef V
}

void player_render_hand(void)
{
    if (player.dead || ui_screen() != UI_HUD)
        return;
    int light = world_light_raw(cam.x >> 12, cam.y >> 12, cam.z >> 12);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | POLY_FORMAT_LIGHT0 | POLY_ID(6));
    GFX_PAL_FORMAT = 0;
    // walking bob
    int32_t bob = 0;
    int32_t spd = iabs(player.vx) + iabs(player.vz);
    if (player.body.on_ground && spd > 300)
        bob = sinLerp(frame_counter * 600) >> 6;
    int swing = player.swing;
    if (player.mining)
        swing = 3 + ((frame_counter >> 1) & 3);
    glTranslatef32(FX(0.28), FX(-0.32) + bob, FX(-0.5));
    if (swing > 0) {
        int a = sinLerp(swing * ANG_FULL / 12);
        glRotateXi((a * (ANG_FULL / 10)) >> 12);
        glTranslatef32(0, 0, -(a >> 4));
    }
    if (player.eat_timer > 0)
        glTranslatef32(-FX(0.12), FX(0.08) + (sinLerp(frame_counter * 3000) >> 7), 0);
    ItemStack *h = player_held();
    if (stack_empty(h)) {
        static const uint8_t arm[6] = { TEX_PLAYER_SKIN, TEX_PLAYER_SKIN, TEX_PLAYER_SKIN, TEX_PLAYER_SKIN, TEX_PLAYER_SKIN, TEX_PLAYER_SKIN };
        glRotateXi(-ANG_FULL / 12);
        glRotateYi(ANG_FULL / 20);
        hand_box(-FX(0.07), -FX(0.3), -FX(0.07), FX(0.07), FX(0.1), FX(0.07), arm, light);
        static const uint8_t sleeve[6] = { TEX_PLAYER_SLEEVE, TEX_PLAYER_SLEEVE, TEX_PLAYER_SLEEVE, TEX_PLAYER_SLEEVE, TEX_PLAYER_SLEEVE, TEX_PLAYER_SLEEVE };
        hand_box(-FX(0.075), -FX(0.36), -FX(0.075), FX(0.075), -FX(0.2), FX(0.075), sleeve, light);
        return;
    }
    int id = h->id;
    if (id < 256 && blocks[id].shape == SHAPE_CUBE) {
        glRotateYi(ANG_FULL / 8);
        hand_box(-FX(0.11), -FX(0.11), -FX(0.11), FX(0.11), FX(0.11), FX(0.11), blocks[id].tex, light);
        return;
    }
    // flat item sprite
    int icon = item_valid(id) ? items[id].icon : 0;
    GFX_PAL_FORMAT = 32;
    GFX_TEX_FORMAT = gx_texparam16(TEX_ICON_BASE + icon, TEXGEN_NONE);
    glRotateYi(-ANG_FULL / 10);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FORMAT_LIGHT0 | POLY_ID(6));
    int32_t s = FX(0.16);
    glBegin(GL_QUADS);
    GFX_DIFFUSE_AMBIENT = (uint32_t)light_ambient[FACE_U][light >> 4] << 16;
    GFX_SPECULAR_EMISSION = (uint32_t)light_emission[FACE_U][light & 15] << 16;
    GFX_NORMAL = 0;
    glTexCoord2t16(0, 256); glVertex3v16(-s, -s, 0);
    glTexCoord2t16(256, 256); glVertex3v16(s, -s, 0);
    glTexCoord2t16(256, 0); glVertex3v16(s, s, 0);
    glTexCoord2t16(0, 0); glVertex3v16(-s, s, 0);
    glEnd();
    GFX_PAL_FORMAT = 0;
}

// ---------------------------------------------------------------------------
// Loading screens
// ---------------------------------------------------------------------------

static const char *loading_title = "";

static void loading_frame(const char *stage, int pct)
{
    ui_loading(loading_title, stage, pct);
    hud_fill(0, 10, 32, 5, 0, 0);
    hud_center(10, loading_title, HP_WHITE);
    hud_center(12, stage, HP_GRAY);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    hud_center(14, buf, HP_YELLOW);
    // clear the 3D layer (no geometry)
    static bool cleared;
    if (!cleared || (pct % 25) == 0) {
        glClearColor(1, 1, 3, 31);
        glFlush(0);
        cleared = true;
    }
}

static void gen_progress(const char *stage, int pct)
{
    static int last = -1;
    if (pct == last)
        return;
    last = pct;
    loading_frame(stage, pct);
}

static void light_progress(int pct)
{
    gen_progress("Lighting the world", pct);
}

static void save_progress(int pct)
{
    gen_progress("Saving world", pct);
}

static void load_progress(int pct)
{
    gen_progress("Loading world", pct);
}

static void build_initial_meshes(void)
{
    // build everything visible near the player
    for (int pass = 0; pass < 40; pass++) {
        player_camera(4096);
        render_distance = settings.render_dist;
        render_frame();   // traversal collects pending sections
        int n = render_build_pending(0);
        char buf[32];
#ifdef FIXED_SEED
        snprintf(buf, sizeof(buf), "B %luk/%luk n%d p%d", (unsigned long)(mesh_bytes >> 10), (unsigned long)(mesh_budget >> 10), n, render_pending_count());
#else
        snprintf(buf, sizeof(buf), "Building terrain");
#endif
        gen_progress(buf, imin(99, 60 + pass * 2));
        if (n == 0 && render_pending_count() == 0)
            break;
    }
}

void enter_world(void)
{
    mesh_init();
    render_distance = settings.render_dist;
    render_init();
    sim_init();
    hud_reset();
    particles_clear();
    if (world.mode == MODE_CREATIVE)
        player.mode = MODE_CREATIVE;
    build_initial_meshes();
    ui_close();
    game_message(world.mode == MODE_CREATIVE ? "Creative mode" : "Survival mode", 60);
}

bool create_world(int slot)
{
    loading_title = "Generating world";
    int size = new_world_size;
    if (size == 256 && !isDSiMode())
        size = 128;
    gen_progress("Allocating", 0);
    if (!world_alloc(size)) {
        if (size == 256 && world_alloc(128)) {
            size = 128;
        } else {
            gen_progress("Out of memory!", 0);
            return false;
        }
    }
    world.seed = new_world_seed;
    world.mode = new_world_mode;
    world.time = 1000;
    world.ticks = 0;
    snprintf(world.name, sizeof(world.name), "World %d", slot + 1);
    worldgen_generate(world.seed, gen_progress);
    world_init_light(light_progress);
    tile_entities_clear();
    entities_clear();
    player_reset(world.mode);
    player_spawn();
    entities_spawn_initial();
    current_slot = slot;
    enter_world();
    if (save_available) {
        loading_title = "Saving";
        save_world(slot, save_progress);
        save_scan_slots();
    }
    ui_close();
    return true;
}

static bool open_world(int slot)
{
    loading_title = "Loading world";
    gen_progress("Reading save", 0);
    if (!load_world(slot, load_progress)) {
        ui_open(UI_WORLDS);
        game_message("Could not load the world", 90);
        return false;
    }
    current_slot = slot;
    enter_world();
    return true;
}

void game_save_now(void)
{
    if (!save_available || current_slot < 0)
        return;
    loading_title = "Saving";
    save_world(current_slot, save_progress);
    save_settings();
    save_scan_slots();
}

// ---------------------------------------------------------------------------
// Title screen 3D: a slowly turning grass block
// ---------------------------------------------------------------------------

static void title_frame(void)
{
    glClearColor(4, 7, 14, 31);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspectivef32(degreesToAngle(50), floattof32(256.0 / 192.0), floattof32(0.1), FX(40));
    glLight(0, RGB15(31, 31, 31), 0, floattov10(-0.99), 0);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef32(0, FX(-0.3), FX(-3.2));
    glRotateXi(ANG_DEG(22));
    glRotateYi(frame_counter * 40);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | POLY_FORMAT_LIGHT0 | POLY_ID(0));
    GFX_PAL_FORMAT = 0;
    hand_box(-FX(0.6), -FX(0.6), -FX(0.6), FX(0.6), FX(0.6), FX(0.6), blocks[B_GRASS].tex, 0xF0);
    glFlush(0);
    hud_clear();
    hud_center(2, "BlockCraft DS", HP_WHITE);
    hud_center(4, "a block building adventure", HP_GRAY);
    hud_center(21, "Fan-made homebrew", HP_GRAY);
    hud_center(22, "Not affiliated with Mojang", HP_GRAY);
#ifdef TOUCH_DEBUG
    {
        char buf[40];
        touchPosition tp;
        touchRead(&tp);
        snprintf(buf, sizeof(buf), "T:%d %3d,%3d k%04lx", (keysHeld() & KEY_TOUCH) ? 1 : 0, tp.px, tp.py, (unsigned long)keysHeld());
        hud_print(0, 0, buf, HP_YELLOW);
    }
#endif
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static int r_frames, select_frames;
static int up_tap_timer;
static bool prev_up;

static void read_input(int vblanks)
{
    uint32_t held = keysHeld(), down = keysDown(), up = keysUp();
    bool in_hud = ui_screen() == UI_HUD;
    memset(&input.fwd, 0, sizeof(int) * 2);
    input.jump_pressed = false;
    if (!in_hud || player.dead) {
        input.jump = false;
        input.sneak = false;
        input.action_hold = false;
        r_frames = 0;
        return;
    }
    uint32_t K_JUMP = settings.swap_lr ? KEY_R : KEY_L;
    uint32_t K_ACT = settings.swap_lr ? KEY_L : KEY_R;
    if (held & KEY_UP) input.fwd = 1;
    if (held & KEY_DOWN) input.fwd = -1;
    if (held & KEY_LEFT) input.strafe = -1;
    if (held & KEY_RIGHT) input.strafe = 1;
    // double-tap up to sprint
    bool upk = (held & KEY_UP) != 0;
    if (upk && !prev_up) {
        if (up_tap_timer > 0)
            player.sprinting = true;
        up_tap_timer = 15;
    }
    if (up_tap_timer > 0)
        up_tap_timer -= vblanks;
    prev_up = upk;

    bool jump = (held & K_JUMP) || ui_jump_button;
    if (jump && !input.jump)
        input.jump_pressed = true;
    input.jump = jump;

    // SELECT: tap = next slot, hold = sneak
    if (held & KEY_SELECT)
        select_frames += vblanks;
    if (up & KEY_SELECT) {
        if (select_frames < 12) {
            player.sel = (player.sel + 1) % 9;
            ui_dirty();
        }
        select_frames = 0;
    }
    input.sneak = (select_frames >= 12) || ui_sneak_toggle;

    // Action button: tap = use, hold = mine
    if (held & K_ACT) {
        r_frames += vblanks;
    }
    if (up & K_ACT) {
        if (r_frames < 10)
            input.use_tap = true;
        r_frames = 0;
    }
    input.action_hold = r_frames >= 10 || ui_hold_action;
    if (ui_tap_use)
        input.use_tap = true;

    // Look: faster base speed, and holding a look button speeds up a little more
    static int look_hold;
    bool looking = (held & (KEY_A | KEY_Y | KEY_X | KEY_B)) != 0;
    look_hold = looking ? imin(look_hold + vblanks, 24) : 0;
    int speed = settings.look_speed * 34 * vblanks * (24 + look_hold) / 32;
    int dyaw = 0, dpitch = 0;
    if (held & KEY_A) dyaw += speed;
    if (held & KEY_Y) dyaw -= speed;
    if (held & KEY_X) dpitch -= speed;
    if (held & KEY_B) dpitch += speed;
    dyaw += ui_look_dx * settings.look_speed * 13;
    dpitch += ui_look_dy * settings.look_speed * 13;
    if (settings.invert_y)
        dpitch = -dpitch;
    player.yaw = (player.yaw + dyaw) & (ANG_FULL - 1);
    player.pitch = iclamp(player.pitch + dpitch, -ANG_DEG(89), ANG_DEG(89));
    (void)down;
}

// ---------------------------------------------------------------------------
// Game tick
// ---------------------------------------------------------------------------

static void game_tick(void)
{
    world.ticks++;
    world.time = (world.time + 1) % DAY_LENGTH;
    sim_tick();
    entities_tick();
    player_tick();
    furnaces_tick();
    music_tick();
    if (player.sleep_timer > 0) {
        player.sleep_timer--;
        if (player.sleep_timer == 0) {
            world.time = 0;
            game_message("Good morning!", 60);
            if (save_available)
                game_save_now();
        }
    }
    // keep the bottom HUD clock fresh
    if (ui_screen() == UI_HUD && (world.ticks % 200) == 0)
        ui_dirty();
}


bool test_input_override;

// One in-game frame: input, ticks, rendering. Returns 1 when leaving to the title.
int game_frame(int vblanks, uint32_t *tick_accum_p)
{
    uint32_t tick_accum = *tick_accum_p;
    uint32_t kd = keysDown();
    if (kd & KEY_START) {
        if (ui_screen() == UI_HUD)
            ui_open(UI_PAUSE);
        else if (ui_screen() == UI_PAUSE || ui_in_game_menu())
            ui_close();
    }
    if ((keysHeld() & (KEY_L | KEY_R | KEY_SELECT)) == (KEY_L | KEY_R | KEY_SELECT) && (kd & (KEY_L | KEY_R | KEY_SELECT)))
        debug_overlay = !debug_overlay;

    PROF_MARK(0);
    ui_frame();
    PROF_MARK(1);
    if (!test_input_override)
        read_input(vblanks);

    bool paused = ui_screen() == UI_PAUSE || ui_screen() == UI_OPTIONS || ui_screen() == UI_HELP;
    if (!paused) {
        tick_accum += vblanks;
        int ticks = 0;
        while (tick_accum >= 3 && ticks < 3) {
            tick_accum -= 3;
            game_tick();
            ticks++;
        }
        if (tick_accum > 6)
            tick_accum = 0;
    }
    PROF_MARK(2);

    if (menu_action == MENU_RESPAWN) {
        menu_action = MENU_NONE;
        player_respawn();
        ui_close();
    } else if (menu_action == MENU_SAVE_QUIT) {
        menu_action = MENU_NONE;
        if (player.dead)
            player_respawn();
        game_save_now();
        save_settings();
        mesh_shutdown();
        ui_open(UI_TITLE);
        return 1;
    }

    // Render
    int32_t alpha = paused ? 4096 : (int32_t)(tick_accum * 4096 / 3);
    player_camera(alpha);
    bool uw = player.head_in_water && !player.dead;
    bool lava = player_eye_in(BF_HURTS) && is_lava(world_get(cam.x >> 12, cam.y >> 12, cam.z >> 12));
    sky_update(world.time, uw, lava);
    cam_underwater_fog = uw || lava;
    player_frame(vblanks);
    render_distance = settings.render_dist;
    PROF_MARK(3);
    render_frame();
    PROF_MARK(4);
    // Sleeping fade
    if (player.sleep_timer > 0) {
        int b = 16 - player.sleep_timer * 16 / 60;
        REG_MASTER_BRIGHT = (2 << 14) | iclamp(16 - b, 0, 16);
    } else {
        REG_MASTER_BRIGHT = 0;
    }
    hud_update(vblanks, debug_overlay);
    // Mesh building with the remaining frame time
    PROF_MARK(5);
    render_build_pending(paused ? 600000 : 150000);
    PROF_MARK(6);
    if ((frame_counter & 127) == 0)
        render_free_far();
    *tick_accum_p = tick_accum;
    return 0;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

uint16_t prof_t[8];
uint16_t prof_ent;

int main(void)
{
    if (isDSiMode())
        setCpuClock(true);
    TIMER_DATA(0) = 0;
    TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1024;
    irqSet(IRQ_VBLANK, vblank_handler);
    irqEnable(IRQ_VBLANK);
    blocks_init();
    items_init();
    recipes_init();
    gfx_init();
    mesh_init_light_tables();
    lcdMainOnTop();
    sound_init();
    default_settings();
    rng_state ^= (uint32_t)time(NULL) * 2654435761u;
    for (int i = 0; i < 8; i++)
        rng_next();
    ui_init();

    loading_title = "Starting";
    gen_progress("Checking SD card", 0);
    save_available = save_init();
    if (save_available) {
        load_settings();
        save_scan_slots();
    }
    ui_open(UI_TITLE);
#ifdef TESTMODE
    test_run();
#endif

    bool in_game = false;
    uint32_t last_vblank = vblank_count;
    uint32_t tick_accum = 0;
    while (1) {
        uint32_t now = vblank_count;
        int vblanks = now - last_vblank;
        if (vblanks <= 0)
            vblanks = 1;
        if (vblanks > 6)
            vblanks = 6;
        last_vblank = now;
        frame_counter++;
        if (in_game && (frame_counter & 3) == 0)
            gfx_animate_textures(frame_counter);
        scanKeys();

        if (!in_game) {
            ui_frame();
            title_frame();
            if (menu_action == MENU_PLAY_SLOT) {
                menu_action = MENU_NONE;
                in_game = open_world(menu_slot);
            } else if (menu_action == MENU_CREATE_WORLD) {
                menu_action = MENU_NONE;
                in_game = create_world(menu_slot);
                if (!in_game)
                    ui_open(UI_WORLDS);
            } else if (menu_action == MENU_DELETE_SLOT) {
                menu_action = MENU_NONE;
                delete_world(menu_slot);
                save_scan_slots();
                ui_dirty();
            }
            if (in_game) {
                last_vblank = vblank_count;
                tick_accum = 0;
            }
            swiWaitForVBlank();
            continue;
        }

        // ---- In game ----
        int r = game_frame(vblanks, &tick_accum);
        if (r == 1) {
            in_game = false;
            continue;
        }
        swiWaitForVBlank();
    }
}

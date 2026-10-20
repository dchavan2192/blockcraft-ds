// Bottom screen user interface (software rendered 16-bit bitmap)
#include <stdio.h>
#include "ui.h"
#include "game.h"
#include "player.h"
#include "inventory.h"
#include "blocks.h"
#include "world.h"
#include "worldgen.h"
#include "sound.h"
#include "render.h"
#include "assets_gen.h"
#include "font_bin.h"
#include "icons16_bin.h"
#include "blocktex_bin.h"
#include "blockpal_bin.h"

#define W 256
#define H 192
#define C(r, g, b) (RGB15(r, g, b) | BIT(15))

static uint16_t fb[W * H];
static bool is_dirty = true;
static int screen = UI_TITLE;
static int prev_screen = UI_HUD;

int ui_look_dx, ui_look_dy;
bool ui_tap_use, ui_hold_action, ui_sneak_toggle, ui_jump_button;
int menu_action, menu_slot;
int new_world_mode = MODE_SURVIVAL, new_world_size = 128;
uint32_t new_world_seed;
SlotInfo slot_info[3];

static int chest_x, chest_y, chest_z;
static int furnace_x, furnace_y, furnace_z;
static int click_mode;        // 0 = pick, 1 = split, 2 = quick move
static int creative_page;
static int options_from;      // screen to return to from options
static int help_page;

// Colors
static const uint16_t COL_PANEL = C(24, 24, 24);
static const uint16_t COL_LIGHT = C(31, 31, 31);
static const uint16_t COL_SHADOW = C(10, 10, 10);
static const uint16_t COL_SLOT = C(17, 17, 17);
static const uint16_t COL_SLOT_DARK = C(7, 7, 7);
static const uint16_t COL_TEXT = C(8, 8, 8);
static const uint16_t COL_WHITE = C(31, 31, 31);
static const uint16_t COL_YELLOW = C(31, 31, 12);
static const uint16_t COL_GRAY = C(20, 20, 20);
static const uint16_t COL_RED = C(31, 10, 10);
static const uint16_t COL_GREEN = C(12, 30, 12);
static const uint16_t COL_BLACK = C(0, 0, 0);

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------

static void fill(int x, int y, int w, int h, uint16_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *p = &fb[yy * W + x];
        for (int xx = 0; xx < w; xx++)
            p[xx] = c;
    }
}

static inline uint16_t darken(uint16_t c, int num, int den)
{
    int r = (c & 31) * num / den, g = ((c >> 5) & 31) * num / den, b = ((c >> 10) & 31) * num / den;
    return C(r, g, b);
}

static void tex_tile_bg(int tex, int num, int den)
{
    const uint8_t *t = blocktex_bin + tex * 256;
    const uint16_t *pal = (const uint16_t *)blockpal_bin;
    uint16_t tile[16 * 16];
    for (int i = 0; i < 256; i++)
        tile[i] = darken(pal[t[i]] | BIT(15), num, den);
    for (int y = 0; y < H; y++) {
        const uint16_t *row = &tile[((y >> 1) & 15) * 16];
        uint16_t *dst = &fb[y * W];
        for (int x = 0; x < W; x++)
            dst[x] = row[(x >> 1) & 15];
    }
}

static void bevel(int x, int y, int w, int h, uint16_t face, uint16_t light, uint16_t dark)
{
    fill(x, y, w, h, face);
    fill(x, y, w, 1, light);
    fill(x, y, 1, h, light);
    fill(x, y + h - 1, w, 1, dark);
    fill(x + w - 1, y, 1, h, dark);
}

static void panel(int x, int y, int w, int h)
{
    fill(x + 1, y + 1, w - 2, h - 2, COL_PANEL);
    fill(x + 1, y, w - 2, 1, COL_BLACK);
    fill(x + 1, y + h - 1, w - 2, 1, COL_BLACK);
    fill(x, y + 1, 1, h - 2, COL_BLACK);
    fill(x + w - 1, y + 1, 1, h - 2, COL_BLACK);
    fill(x + 1, y + 1, w - 3, 2, COL_LIGHT);
    fill(x + 1, y + 1, 2, h - 3, COL_LIGHT);
    fill(x + 2, y + h - 3, w - 3, 2, COL_SHADOW);
    fill(x + w - 3, y + 2, 2, h - 3, COL_SHADOW);
}

static int glyph_width(int c)
{
    if (c < 32 || c > 127)
        return 4;
    return font_bin[96 * 8 + (c - 32)];
}

static int text_width(const char *s)
{
    int w = 0;
    while (*s)
        w += glyph_width((unsigned char)*s++);
    return w;
}

static void draw_glyph(int x, int y, int c, uint16_t col, int scale)
{
    if (c < 32 || c > 127)
        return;
    const uint8_t *g = font_bin + (c - 32) * 8;
    for (int yy = 0; yy < 8; yy++) {
        uint8_t row = g[yy];
        for (int xx = 0; xx < 8; xx++)
            if (row & (1 << xx)) {
                int px = x + xx * scale, py = y + yy * scale;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int X = px + sx, Y = py + sy;
                        if ((unsigned)X < W && (unsigned)Y < H)
                            fb[Y * W + X] = col;
                    }
            }
    }
}

static void text_ex(int x, int y, const char *s, uint16_t col, bool shadow, int scale)
{
    while (*s) {
        int c = (unsigned char)*s++;
        if (shadow)
            draw_glyph(x + scale, y + scale, c, darken(col, 1, 4), scale);
        draw_glyph(x, y, c, col, scale);
        x += glyph_width(c) * scale;
    }
}

static void text(int x, int y, const char *s, uint16_t col)
{
    text_ex(x, y, s, col, col != COL_TEXT, 1);
}

static void text_center(int cx, int y, const char *s, uint16_t col, int scale)
{
    text_ex(cx - text_width(s) * scale / 2, y, s, col, true, scale);
}

static void icon(int x, int y, int idx, int scale)
{
    if (idx < 0 || idx >= ICON_COUNT)
        return;
    const uint16_t *src = (const uint16_t *)icons16_bin + idx * 256;
    for (int yy = 0; yy < 16; yy++)
        for (int xx = 0; xx < 16; xx++) {
            uint16_t c = src[yy * 16 + xx];
            if (!(c & 0x8000))
                continue;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++) {
                    int X = x + xx * scale + sx, Y = y + yy * scale + sy;
                    if ((unsigned)X < W && (unsigned)Y < H)
                        fb[Y * W + X] = c;
                }
        }
}

static void draw_stack(int x, int y, const ItemStack *s, int scale)
{
    if (stack_empty(s) || !item_valid(s->id))
        return;
    const ItemDef *d = &items[s->id];
    icon(x, y, d->icon, scale);
    int sz = 16 * scale;
    if (s->count > 1) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", s->count);
        int tw = text_width(buf);
        text_ex(x + sz - tw + 1, y + sz - 7, buf, COL_WHITE, true, 1);
    }
    if (d->durability && s->damage > 0) {
        int frac = (d->durability - s->damage) * 13 / d->durability;
        if (frac < 0) frac = 0;
        int bx = x + (sz - 13) / 2, by = y + sz - 2;
        fill(bx, by, 13, 2, COL_BLACK);
        uint16_t col = frac > 8 ? COL_GREEN : frac > 4 ? COL_YELLOW : COL_RED;
        fill(bx, by, frac, 1, col);
    }
}

static void slot_box(int x, int y, int size)
{
    fill(x, y, size, size, COL_SLOT);
    fill(x, y, size - 1, 1, COL_SLOT_DARK);
    fill(x, y, 1, size - 1, COL_SLOT_DARK);
    fill(x + 1, y + size - 1, size - 1, 1, COL_LIGHT);
    fill(x + size - 1, y + 1, 1, size - 1, COL_LIGHT);
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

enum { WT_BUTTON = 1, WT_SLOT, WT_CITEM, WT_HOTBAR, WT_BOOKROW };
enum { SK_NORMAL = 0, SK_OUTPUT2, SK_OUTPUT3, SK_ARMOR, SK_FUEL, SK_FURNACE_OUT, SK_FURNACE_IN };

typedef struct {
    int16_t x, y, w, h;
    uint8_t type;
    uint8_t kind;
    int16_t id;
    ItemStack *stack;
} Widget;

#define MAXW 96
static Widget widgets[MAXW];
static int nwidgets;
static int pressed_widget = -1;

static Widget *add_widget(int type, int x, int y, int w, int h, int id)
{
    if (nwidgets >= MAXW)
        return &widgets[MAXW - 1];
    Widget *wd = &widgets[nwidgets++];
    wd->type = type;
    wd->x = x; wd->y = y; wd->w = w; wd->h = h;
    wd->id = id;
    wd->kind = 0;
    wd->stack = NULL;
    return wd;
}

static void button(int x, int y, int w, int h, int id, const char *label, bool enabled)
{
    add_widget(WT_BUTTON, x, y, w, h, id);
    bool down = pressed_widget == nwidgets - 1;
    uint16_t face = enabled ? (down ? C(14, 14, 20) : C(18, 18, 18)) : C(11, 11, 11);
    fill(x, y, w, h, COL_BLACK);
    bevel(x + 1, y + 1, w - 2, h - 2, face, enabled ? C(26, 26, 28) : C(14, 14, 14), C(8, 8, 8));
    uint16_t tc = enabled ? (down ? COL_YELLOW : COL_WHITE) : C(16, 16, 16);
    text_center(x + w / 2, y + (h - 8) / 2, label, tc, 1);
}

static void slot_widget(int x, int y, ItemStack *s, int kind, int id)
{
    Widget *wd = add_widget(WT_SLOT, x, y, 18, 18, id);
    wd->stack = s;
    wd->kind = kind;
    slot_box(x, y, 18);
    draw_stack(x + 1, y + 1, s, 1);
}

// ---------------------------------------------------------------------------
// Screen management
// ---------------------------------------------------------------------------

void ui_dirty(void)
{
    is_dirty = true;
}

int ui_screen(void)
{
    return screen;
}

bool ui_in_game_menu(void)
{
    return screen == UI_INVENTORY || screen == UI_CRAFTING || screen == UI_FURNACE || screen == UI_CHEST || screen == UI_CREATIVE;
}

static void return_craft_items(ItemStack *grid, int n)
{
    for (int i = 0; i < n; i++) {
        if (!stack_empty(&grid[i])) {
            player_give(grid[i].id, grid[i].count, grid[i].damage);
            stack_clear(&grid[i]);
        }
    }
}

static ItemStack table_grid[9];
static ItemStack craft_result;

void ui_open(int s)
{
    if (s == UI_INVENTORY && player.mode == MODE_CREATIVE)
        s = UI_CREATIVE;
    prev_screen = screen;
    screen = s;
    is_dirty = true;
    pressed_widget = -1;
    click_mode = 0;
    game_screen = s;
}

void ui_open_chest(int x, int y, int z)
{
    chest_x = x; chest_y = y; chest_z = z;
    chest_get(x, y, z, true);
    ui_open(UI_CHEST);
    sound_play(SFX_DOOR, 90, 64, 80);
}

void ui_open_furnace(int x, int y, int z)
{
    furnace_x = x; furnace_y = y; furnace_z = z;
    furnace_get(x, y, z, true);
    ui_open(UI_FURNACE);
}

void ui_close(void)
{
    // return held items
    if (!stack_empty(&player.cursor)) {
        player_give(player.cursor.id, player.cursor.count, player.cursor.damage);
        stack_clear(&player.cursor);
    }
    return_craft_items(player.craft, 4);
    return_craft_items(table_grid, 9);
    if (screen == UI_CHEST)
        sound_play(SFX_DOOR, 80, 64, 70);
    screen = UI_HUD;
    game_screen = UI_HUD;
    is_dirty = true;
}

void ui_init(void)
{
    is_dirty = true;
    keysSetRepeat(14, 4);
    new_world_seed = rng_next();
}

// ---------------------------------------------------------------------------
// Slot click logic
// ---------------------------------------------------------------------------

static bool slot_accepts(int kind, int widget_id, int item)
{
    if (kind == SK_ARMOR)
        return item_valid(item) && items[item].kind == IK_ARMOR && items[item].armor_slot == widget_id;
    if (kind == SK_FUEL)
        return item_valid(item) && items[item].fuel > 0;
    if (kind == SK_OUTPUT2 || kind == SK_OUTPUT3 || kind == SK_FURNACE_OUT)
        return false;
    return true;
}

static void quick_move(ItemStack *s, int kind)
{
    if (stack_empty(s))
        return;
    ItemStack tmp = *s;
    stack_clear(s);
    int left;
    if (screen == UI_CHEST) {
        Chest *c = chest_get(chest_x, chest_y, chest_z, false);
        bool in_chest = c && s >= c->items && s < c->items + CHEST_SIZE;
        if (in_chest)
            left = inv_add(player.inv, INV_SIZE, tmp.id, tmp.count, tmp.damage);
        else
            left = c ? inv_add(c->items, CHEST_SIZE, tmp.id, tmp.count, tmp.damage) : tmp.count;
    } else if (screen == UI_FURNACE && kind == SK_NORMAL) {
        Furnace *f = furnace_get(furnace_x, furnace_y, furnace_z, false);
        left = tmp.count;
        if (f) {
            ItemStack *dst = items[tmp.id].smelt ? &f->in : (items[tmp.id].fuel ? &f->fuel : NULL);
            if (dst && (stack_empty(dst) || (dst->id == tmp.id && dst->count < item_max_stack(tmp.id)))) {
                if (stack_empty(dst)) {
                    *dst = tmp;
                    left = 0;
                } else {
                    int room = item_max_stack(tmp.id) - dst->count;
                    int put = imin(room, tmp.count);
                    dst->count += put;
                    left = tmp.count - put;
                }
            }
        }
    } else if (kind == SK_NORMAL && s >= player.inv && s < player.inv + INV_SIZE) {
        // hotbar <-> main
        bool hot = s < player.inv + 9;
        if (item_valid(tmp.id) && items[tmp.id].kind == IK_ARMOR && stack_empty(&player.armor[items[tmp.id].armor_slot])) {
            player.armor[items[tmp.id].armor_slot] = tmp;
            left = 0;
        } else {
            left = hot ? inv_add(player.inv + 9, 27, tmp.id, tmp.count, tmp.damage) : inv_add(player.inv, 9, tmp.id, tmp.count, tmp.damage);
        }
    } else {
        left = inv_add(player.inv, INV_SIZE, tmp.id, tmp.count, tmp.damage);
    }
    if (left > 0) {
        if (stack_empty(s)) {
            *s = tmp;
            s->count = left;
        }
    }
}

static void take_craft_output(ItemStack *grid, int gw)
{
    ItemStack res;
    if (!craft_match(grid, gw, &res))
        return;
    int max = item_max_stack(res.id);
    if (click_mode == 2) {
        // craft as many as possible into the inventory
        int guard = 64;
        while (guard-- > 0 && craft_match(grid, gw, &res)) {
            if (inv_add(player.inv, INV_SIZE, res.id, res.count, 0) > 0)
                break;
            craft_consume(grid, gw);
        }
        sound_play(SFX_CLICK, 90, 64, 100);
        return;
    }
    if (stack_empty(&player.cursor)) {
        player.cursor = res;
    } else if (player.cursor.id == res.id && player.cursor.count + res.count <= max) {
        player.cursor.count += res.count;
    } else {
        return;
    }
    craft_consume(grid, gw);
    sound_play(SFX_CLICK, 90, 64, 100);
}

static void click_slot(Widget *wd)
{
    ItemStack *s = wd->stack;
    ItemStack *cur = &player.cursor;
    if (wd->kind == SK_OUTPUT2) {
        take_craft_output(player.craft, 2);
        return;
    }
    if (wd->kind == SK_OUTPUT3) {
        take_craft_output(table_grid, 3);
        return;
    }
    if (!s)
        return;
    if (click_mode == 2 && stack_empty(cur)) {
        quick_move(s, wd->kind);
        sound_play(SFX_CLICK, 70, 64, 120);
        return;
    }
    if (stack_empty(cur)) {
        if (stack_empty(s))
            return;
        if (click_mode == 1 && s->count > 1) {
            int take = (s->count + 1) / 2;
            *cur = *s;
            cur->count = take;
            s->count -= take;
        } else {
            *cur = *s;
            stack_clear(s);
        }
    } else {
        if (wd->kind == SK_FURNACE_OUT) {
            if (!stack_empty(s) && s->id == cur->id && cur->count + s->count <= item_max_stack(cur->id)) {
                cur->count += s->count;
                stack_clear(s);
            }
            return;
        }
        if (!slot_accepts(wd->kind, wd->id, cur->id))
            return;
        int max = item_max_stack(cur->id);
        if (wd->kind == SK_ARMOR)
            max = 1;
        if (stack_empty(s)) {
            int put = click_mode == 1 ? 1 : imin(cur->count, max);
            *s = *cur;
            s->count = put;
            cur->count -= put;
            if (cur->count == 0)
                stack_clear(cur);
        } else if (s->id == cur->id && s->damage == cur->damage && max > 1) {
            int room = max - s->count;
            int put = click_mode == 1 ? imin(1, room) : imin(room, cur->count);
            s->count += put;
            cur->count -= put;
            if (cur->count == 0)
                stack_clear(cur);
        } else if (cur->count <= max) {
            ItemStack t = *s;
            *s = *cur;
            *cur = t;
        }
    }
    sound_play(SFX_CLICK, 60, 64, 120);
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

enum {
    B_CLOSE = 1, B_MODE, B_DROP, B_PREV, B_NEXT,
    B_RESUME, B_OPTIONS, B_SAVEQUIT, B_RESPAWN, B_TITLE,
    B_PLAY, B_HELP, B_SLOT0, B_SLOT1, B_SLOT2, B_CREATE, B_CANCEL, B_GAMEMODE, B_SIZE, B_SEED,
    B_DELETE, B_CONFIRM, B_OPT_LOOK_DN, B_OPT_LOOK_UP, B_OPT_INVERT, B_OPT_AUTOJUMP, B_OPT_RD_DN, B_OPT_RD_UP,
    B_OPT_VOL_DN, B_OPT_VOL_UP, B_OPT_MUSIC, B_OPT_COORDS, B_OPT_PEACEFUL, B_OPT_SWAP, B_DONE,
    B_HUD_INV, B_HUD_PAUSE, B_HUD_SNEAK, B_HUD_DROP, B_HUD_JUMP, B_SURVIVAL_TAB, B_TRASH,
    B_HELP_NEXT, B_TAB_CRAFT, B_TAB_ITEMS, B_BOOK_UP, B_BOOK_DOWN, B_BOOK_CRAFT, B_BOOK_MAX,
};

static void inventory_section(int x, int y)
{
    // main 27 then hotbar
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 9; c++)
            slot_widget(x + c * 20, y + r * 20, &player.inv[9 + r * 9 + c], SK_NORMAL, 0);
    for (int c = 0; c < 9; c++)
        slot_widget(x + c * 20, y + 64, &player.inv[c], SK_NORMAL, 0);
}

static const char *mode_names[3] = { "Pick", "Split", "Move" };

static void held_box(int x, int y)
{
    text(x, y, "Held", COL_TEXT);
    slot_box(x, y + 9, 18);
    draw_stack(x + 1, y + 10, &player.cursor, 1);
}

static void common_buttons(void)
{
    button(214, 2, 40, 16, B_CLOSE, "Done", true);
    button(170, 2, 42, 16, B_MODE, mode_names[click_mode], true);
    button(126, 2, 42, 16, B_DROP, "Drop", !stack_empty(&player.cursor));
}

static void arrow_icon(int x, int y, int progress)
{
    // progress 0..22
    for (int i = 0; i < 22; i++) {
        uint16_t c = i < progress ? COL_WHITE : C(14, 14, 14);
        int h = (i >= 15) ? (22 - i) : 2;
        fill(x + i, y + 8 - h, 1, h * 2, c);
    }
}

static bool book_view;
static void draw_book(bool table);
static void tab_buttons(void);

static void draw_inventory(void)
{
    tex_tile_bg(TEX_STONE, 2, 5);
    panel(2, 20, 252, 170);
    if (book_view) {
        draw_book(false);
        return;
    }
    tab_buttons();
    common_buttons();
    // armor
    static const char *anames[4] = { "Head", "Body", "Legs", "Feet" };
    for (int i = 0; i < 4; i++) {
        slot_widget(10, 26 + i * 19, &player.armor[i], SK_ARMOR, i);
        if (stack_empty(&player.armor[i]))
            text(30, 31 + i * 19, anames[i], C(12, 12, 12));
    }
    // 2x2 crafting
    text(120, 26, "Crafting", COL_TEXT);
    for (int i = 0; i < 4; i++)
        slot_widget(120 + (i & 1) * 20, 37 + (i >> 1) * 20, &player.craft[i], SK_NORMAL, 0);
    craft_match(player.craft, 2, &craft_result);
    arrow_icon(164, 47, stack_empty(&craft_result) ? 0 : 22);
    Widget *o = add_widget(WT_SLOT, 192, 43, 22, 22, 0);
    o->kind = SK_OUTPUT2;
    o->stack = &craft_result;
    slot_box(192, 43, 22);
    draw_stack(195, 46, &craft_result, 1);
    held_box(70, 30);
    inventory_section(38, 104);
}

static void draw_crafting(void)
{
    tex_tile_bg(TEX_PLANKS_OAK, 2, 5);
    panel(2, 20, 252, 170);
    if (book_view) {
        draw_book(true);
        return;
    }
    tab_buttons();
    common_buttons();
    for (int i = 0; i < 9; i++)
        slot_widget(40 + (i % 3) * 20, 28 + (i / 3) * 20, &table_grid[i], SK_NORMAL, 0);
    craft_match(table_grid, 3, &craft_result);
    arrow_icon(110, 47, stack_empty(&craft_result) ? 0 : 22);
    Widget *o = add_widget(WT_SLOT, 140, 43, 22, 22, 0);
    o->kind = SK_OUTPUT3;
    o->stack = &craft_result;
    slot_box(140, 43, 22);
    draw_stack(143, 46, &craft_result, 1);
    held_box(200, 34);
    inventory_section(38, 104);
}

static void draw_furnace(void)
{
    tex_tile_bg(TEX_COBBLESTONE, 2, 5);
    panel(2, 20, 252, 170);
    text(8, 6, "Furnace", COL_WHITE);
    common_buttons();
    Furnace *f = furnace_get(furnace_x, furnace_y, furnace_z, true);
    if (!f) {
        text(20, 40, "No furnace", COL_TEXT);
        return;
    }
    slot_widget(70, 28, &f->in, SK_FURNACE_IN, 0);
    slot_widget(70, 72, &f->fuel, SK_FUEL, 0);
    // flame
    int flame = f->burn_max ? f->burn * 12 / f->burn_max : 0;
    fill(73, 50, 12, 14, C(12, 12, 12));
    if (flame > 0)
        fill(73, 50 + (12 - flame) + 2, 12, flame, C(31, 18, 4));
    arrow_icon(100, 46, f->cook * 22 / 200);
    Widget *o = add_widget(WT_SLOT, 136, 42, 22, 22, 0);
    o->kind = SK_FURNACE_OUT;
    o->stack = &f->out;
    slot_box(136, 42, 22);
    draw_stack(139, 45, &f->out, 1);
    held_box(200, 34);
    text(10, 30, "Smelt", COL_TEXT);
    text(10, 76, "Fuel", COL_TEXT);
    inventory_section(38, 104);
}

static void draw_chest(void)
{
    tex_tile_bg(TEX_PLANKS_OAK, 2, 5);
    panel(2, 20, 252, 170);
    text(8, 6, "Chest", COL_WHITE);
    common_buttons();
    Chest *c = chest_get(chest_x, chest_y, chest_z, true);
    if (!c)
        return;
    for (int r = 0; r < 3; r++)
        for (int col = 0; col < 9; col++)
            slot_widget(38 + col * 20, 24 + r * 19, &c->items[r * 9 + col], SK_NORMAL, 0);
    held_box(4, 30);
    for (int r = 0; r < 3; r++)
        for (int col = 0; col < 9; col++)
            slot_widget(38 + col * 20, 86 + r * 19, &player.inv[9 + r * 9 + col], SK_NORMAL, 0);
    for (int col = 0; col < 9; col++)
        slot_widget(38 + col * 20, 148 + 20, &player.inv[col], SK_NORMAL, 0);
}


// ---------------------------------------------------------------------------
// Recipe book: pick what to make from a list, no grid arranging needed
// ---------------------------------------------------------------------------

static bool book_view = true;     // "Craft" tab (recipe list) or "Items" tab (slots)
static int book_recipe = -1;      // selected recipe id
static int book_scroll;
#define BOOK_MAX 200
#define BOOK_ROWS 6
#define BOOK_ROW_H 27
static int16_t book_list[BOOK_MAX];
static int16_t book_times[BOOK_MAX];
static int book_n, book_sel;
static bool book_table;

static void book_build(bool table)
{
    book_table = table;
    book_n = 0;
    int total = recipe_total();
    // craftable first, then the rest; in the inventory, table recipes come last
    for (int pass = 0; pass < 3; pass++) {
        for (int r = 0; r < total && book_n < BOOK_MAX; r++) {
            RecipeInfo ri;
            recipe_info(r, &ri);
            bool usable = table || !ri.needs_table;
            int t = usable ? recipe_times(r, player.inv, INV_SIZE) : 0;
            bool want = pass == 0 ? (usable && t > 0) : pass == 1 ? (usable && t == 0) : !usable;
            if (!want)
                continue;
            book_list[book_n] = r;
            book_times[book_n] = t;
            book_n++;
        }
    }
    book_sel = 0;
    for (int i = 0; i < book_n; i++)
        if (book_list[i] == book_recipe) {
            book_sel = i;
            break;
        }
    if (book_n)
        book_recipe = book_list[book_sel];
    if (book_sel < book_scroll)
        book_scroll = book_sel;
    if (book_sel >= book_scroll + BOOK_ROWS)
        book_scroll = book_sel - BOOK_ROWS + 1;
    book_scroll = iclamp(book_scroll, 0, imax(0, book_n - BOOK_ROWS));
}

static void book_select(int i)
{
    if (!book_n)
        return;
    i = iclamp(i, 0, book_n - 1);
    book_sel = i;
    book_recipe = book_list[i];
    if (book_sel < book_scroll)
        book_scroll = book_sel;
    if (book_sel >= book_scroll + BOOK_ROWS)
        book_scroll = book_sel - BOOK_ROWS + 1;
    is_dirty = true;
}

static void book_craft(bool all)
{
    if (!book_n || book_recipe < 0)
        return;
    RecipeInfo ri;
    recipe_info(book_recipe, &ri);
    if (ri.needs_table && !book_table) {
        sound_play(SFX_CLICK, 50, 64, 60);
        return;
    }
    int done = recipe_craft(book_recipe, player.inv, INV_SIZE, all ? 64 : 1);
    if (done > 0)
        sound_play(SFX_POP, 90, 64, 110);
    else
        sound_play(SFX_CLICK, 50, 64, 60);
    is_dirty = true;
}

static void tab_buttons(void)
{
    // two big tabs; the active one is underlined in yellow
    button(2, 1, 54, 18, B_TAB_CRAFT, "Craft", true);
    button(58, 1, 54, 18, B_TAB_ITEMS, "Items", true);
    fill(book_view ? 4 : 60, 16, 50, 2, COL_YELLOW);
}

static void book_wrap_name(int cx, int y, const char *name, uint16_t col)
{
    // centered, wrapped onto two lines if needed
    if (text_width(name) <= 80) {
        text_center(cx, y, name, col, 1);
        return;
    }
    char a[32], b[32];
    int n = strlen(name), split = -1;
    for (int i = 0; i < n && i < 31; i++) {
        if (name[i] == ' ') {
            memcpy(a, name, i);
            a[i] = 0;
            if (text_width(a) <= 80)
                split = i;
        }
    }
    if (split < 0) {
        text_center(cx, y, name, col, 1);
        return;
    }
    memcpy(a, name, split);
    a[split] = 0;
    snprintf(b, sizeof(b), "%s", name + split + 1);
    text_center(cx, y, a, col, 1);
    text_center(cx, y + 9, b, col, 1);
}

static void draw_book(bool table)
{
    book_build(table);
    tex_tile_bg(table ? TEX_PLANKS_OAK : TEX_STONE, 2, 5);
    tab_buttons();
    text(122, 6, table ? "Crafting" : "Inventory", COL_WHITE);
    button(214, 2, 40, 16, B_CLOSE, "Done", true);

    // recipe list
    panel(2, 20, 164, 170);
    for (int row = 0; row < BOOK_ROWS; row++) {
        int i = book_scroll + row;
        int y = 23 + row * BOOK_ROW_H;
        if (i >= book_n)
            break;
        int r = book_list[i];
        RecipeInfo ri;
        recipe_info(r, &ri);
        bool usable = table || !ri.needs_table;
        int t = book_times[i];
        add_widget(WT_BOOKROW, 5, y, 132, BOOK_ROW_H - 1, i);
        bool selected = i == book_sel;
        fill(5, y, 132, BOOK_ROW_H - 1, selected ? C(10, 14, 24) : (t > 0 ? C(20, 22, 20) : C(19, 19, 19)));
        if (selected) {
            fill(5, y, 132, 1, COL_YELLOW);
            fill(5, y + BOOK_ROW_H - 2, 132, 1, COL_YELLOW);
        }
        slot_box(8, y + 4, 18);
        ItemStack out = { .id = ri.out, .count = ri.count };
        draw_stack(9, y + 5, &out, 1);
        const char *name = item_valid(ri.out) ? items[ri.out].name : "?";
        uint16_t nc = selected ? COL_WHITE : (t > 0 ? COL_TEXT : C(13, 13, 13));
        text_ex(30, y + 4, name, nc, false, 1);
        char sub[24];
        uint16_t sc;
        if (!usable) {
            snprintf(sub, sizeof(sub), "needs crafting table");
            sc = selected ? C(26, 20, 12) : C(16, 10, 6);
        } else if (t > 0) {
            snprintf(sub, sizeof(sub), "can make %d", (int)imin(t, 99));
            sc = selected ? C(16, 31, 16) : C(3, 14, 3);
        } else {
            snprintf(sub, sizeof(sub), "missing items");
            sc = selected ? C(28, 16, 16) : C(18, 8, 8);
        }
        text_ex(30, y + 14, sub, sc, false, 1);
    }
    if (!book_n)
        text(12, 60, "No recipes", COL_TEXT);
    // scroll buttons and position bar
    button(140, 23, 24, 78, B_BOOK_UP, "^", book_scroll > 0);
    button(140, 103, 24, 78, B_BOOK_DOWN, "v", book_scroll + BOOK_ROWS < book_n);

    // details of the selected recipe
    panel(168, 20, 86, 170);
    if (book_n) {
        RecipeInfo ri;
        recipe_info(book_recipe, &ri);
        bool usable = table || !ri.needs_table;
        int t = book_times[book_sel];
        ItemStack out = { .id = ri.out, .count = ri.count };
        slot_box(194, 24, 36);
        draw_stack(196, 26, &out, 2);
        book_wrap_name(211, 63, item_valid(ri.out) ? items[ri.out].name : "?", COL_TEXT);
        for (int j = 0; j < ri.ning && j < 4; j++) {
            int y = 84 + j * 18;
            ItemStack ing = { .id = recipe_ing_display(ri.ing[j]), .count = 1 };
            draw_stack(172, y, &ing, 1);
            int have = recipe_have(ri.ing[j], player.inv, INV_SIZE);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d/%d", (int)imin(have, 999), ri.need[j]);
            text_ex(192, y + 4, buf, have >= ri.need[j] ? C(3, 15, 3) : C(20, 4, 4), false, 1);
        }
        bool can = usable && t > 0;
        button(171, 142, 80, 22, B_BOOK_CRAFT, "A: Craft", can);
        button(171, 166, 80, 22, B_BOOK_MAX, t > 1 && usable ? "X: Craft all" : "X: Craft all", can && t > 1);
    }
}

// ---------------------------------------------------------------------------
// Keypad control of slot screens
// ---------------------------------------------------------------------------

static int key_slot = -1;          // k-th slot widget under the keypad cursor (-1 = hidden)

static int slot_widget_index(int k)
{
    int n = 0;
    for (int i = 0; i < nwidgets; i++)
        if (widgets[i].type == WT_SLOT) {
            if (n == k)
                return i;
            n++;
        }
    return -1;
}

static int slot_widget_count(void)
{
    int n = 0;
    for (int i = 0; i < nwidgets; i++)
        if (widgets[i].type == WT_SLOT)
            n++;
    return n;
}

static void key_move(int dx, int dy)
{
    int cur = slot_widget_index(key_slot);
    if (cur < 0) {
        key_slot = 0;
        return;
    }
    Widget *a = &widgets[cur];
    int ax = a->x + a->w / 2, ay = a->y + a->h / 2;
    int best = -1, best_score = 1 << 30, k = 0, best_k = key_slot;
    for (int i = 0; i < nwidgets; i++) {
        if (widgets[i].type != WT_SLOT)
            continue;
        Widget *b = &widgets[i];
        int bx = b->x + b->w / 2 - ax, by = b->y + b->h / 2 - ay;
        int along = bx * dx + by * dy;          // distance in the pressed direction
        int across = iabs(bx * dy) + iabs(by * dx);
        if (along > 4) {
            int score = along + across * 3;
            if (score < best_score) {
                best_score = score;
                best = i;
                best_k = k;
            }
        }
        k++;
    }
    if (best >= 0)
        key_slot = best_k;
}

static bool slot_screen(void)
{
    return ((screen == UI_INVENTORY || screen == UI_CRAFTING) && !book_view) || screen == UI_CHEST || screen == UI_FURNACE;
}

static void draw_key_cursor(void)
{
    if (!slot_screen() || key_slot < 0)
        return;
    if (key_slot >= slot_widget_count())
        key_slot = slot_widget_count() - 1;
    int i = slot_widget_index(key_slot);
    if (i < 0)
        return;
    Widget *w = &widgets[i];
    fill(w->x - 2, w->y - 2, w->w + 4, 2, COL_YELLOW);
    fill(w->x - 2, w->y + w->h, w->w + 4, 2, COL_YELLOW);
    fill(w->x - 2, w->y, 2, w->h, COL_YELLOW);
    fill(w->x + w->w, w->y, 2, w->h, COL_YELLOW);
}

// Creative item list
static uint16_t creative_items[I_COUNT];
static int creative_count;

static void build_creative_list(void)
{
    creative_count = 0;
    static const uint16_t order[] = {
        B_GRASS, B_DIRT, B_STONE, B_COBBLESTONE, B_MOSSY_COBBLESTONE, B_STONEBRICKS, B_BRICKS, B_SANDSTONE, B_SAND,
        B_GRAVEL, B_CLAY, B_SNOW, B_ICE, B_OBSIDIAN, B_BEDROCK, B_PLANKS_OAK, B_PLANKS_BIRCH, B_PLANKS_SPRUCE,
        B_LOG_OAK, B_LOG_BIRCH, B_LOG_SPRUCE, B_LEAVES_OAK, B_LEAVES_BIRCH, B_LEAVES_SPRUCE, B_GLASS, B_BOOKSHELF,
        B_COAL_ORE, B_IRON_ORE, B_GOLD_ORE, B_REDSTONE_ORE, B_DIAMOND_ORE, B_COAL_BLOCK, B_IRON_BLOCK, B_GOLD_BLOCK, B_DIAMOND_BLOCK,
        B_WOOL_WHITE, B_WOOL_WHITE + 1, B_WOOL_WHITE + 2, B_WOOL_WHITE + 3, B_WOOL_WHITE + 4, B_WOOL_WHITE + 5,
        B_WOOL_WHITE + 6, B_WOOL_WHITE + 7, B_WOOL_WHITE + 8, B_WOOL_WHITE + 9, B_WOOL_WHITE + 10, B_WOOL_WHITE + 11,
        B_WOOL_WHITE + 12, B_WOOL_WHITE + 13, B_WOOL_WHITE + 14, B_WOOL_WHITE + 15,
        B_TORCH, B_CRAFTING_TABLE, B_FURNACE, B_CHEST, B_LADDER, I_DOOR, I_BED, B_TNT, B_PUMPKIN, B_JACKOLANTERN,
        B_CACTUS, B_FLOWER_RED, B_FLOWER_YELLOW, B_TALLGRASS, B_DEADBUSH, B_MUSHROOM_BROWN, B_MUSHROOM_RED,
        B_SAPLING_OAK, B_SAPLING_BIRCH, B_SAPLING_SPRUCE, B_FARMLAND,
    };
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); i++)
        creative_items[creative_count++] = order[i];
    for (int id = I_FIRST_ITEM; id < I_COUNT; id++) {
        if (!item_valid(id) || id == I_DOOR || id == I_BED)
            continue;
        creative_items[creative_count++] = id;
    }
}

static void draw_creative(void)
{
    if (!creative_count)
        build_creative_list();
    tex_tile_bg(TEX_DIRT, 1, 3);
    panel(2, 20, 252, 170);
    text(8, 6, "Creative", COL_WHITE);
    button(214, 2, 40, 16, B_CLOSE, "Done", true);
    button(148, 2, 62, 16, B_SURVIVAL_TAB, "Inventory", true);
    int pages = (creative_count + 44) / 45;
    if (creative_page >= pages)
        creative_page = pages - 1;
    for (int i = 0; i < 45; i++) {
        int idx = creative_page * 45 + i;
        int x = 38 + (i % 9) * 20, y = 26 + (i / 9) * 20;
        if (idx >= creative_count) {
            slot_box(x, y, 18);
            continue;
        }
        Widget *wd = add_widget(WT_CITEM, x, y, 18, 18, creative_items[idx]);
        (void)wd;
        slot_box(x, y, 18);
        ItemStack s = { .id = creative_items[idx], .count = 1 };
        draw_stack(x + 1, y + 1, &s, 1);
    }
    button(4, 26, 30, 30, B_PREV, "<", creative_page > 0);
    button(222, 26, 30, 30, B_NEXT, ">", creative_page < pages - 1);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d", creative_page + 1, pages);
    text_center(19, 62, buf, COL_TEXT, 1);
    held_box(4, 86);
    Widget *t = add_widget(WT_BUTTON, 222, 86, 30, 30, B_TRASH);
    (void)t;
    fill(222, 86, 30, 30, C(18, 6, 6));
    text_center(237, 97, "Del", COL_WHITE, 1);
    for (int c = 0; c < 9; c++)
        slot_widget(38 + c * 20, 128 + 40, &player.inv[c], SK_NORMAL, 0);
    text(38, 157, "Hotbar - tap an item, then a slot", COL_TEXT);
}

static void draw_hud(void)
{
    // look pad background
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int v = ((x >> 4) + (y >> 4)) & 1;
            fb[y * W + x] = v ? C(5, 6, 7) : C(6, 7, 8);
        }
    // hotbar
    int hx = 11, hy = 158;
    fill(hx - 2, hy - 2, 9 * 26 + 2, 30, C(2, 2, 2));
    for (int i = 0; i < 9; i++) {
        int x = hx + i * 26;
        Widget *wd = add_widget(WT_HOTBAR, x, hy, 24, 26, i);
        (void)wd;
        fill(x, hy, 24, 26, C(9, 9, 10));
        fill(x + 1, hy + 1, 22, 24, C(14, 14, 15));
        draw_stack(x + 4, hy + 5, &player.inv[i], 1);
        if (i == player.sel) {
            fill(x - 1, hy - 1, 26, 2, COL_WHITE);
            fill(x - 1, hy + 25, 26, 2, COL_WHITE);
            fill(x - 1, hy - 1, 2, 28, COL_WHITE);
            fill(x + 23, hy - 1, 2, 28, COL_WHITE);
        }
    }
    // selected item name
    ItemStack *h = player_held();
    if (!stack_empty(h) && item_valid(h->id))
        text_center(128, 144, items[h->id].name, COL_WHITE, 1);
    // buttons
    button(2, 2, 34, 22, B_HUD_PAUSE, "||", true);
    button(220, 2, 34, 22, B_HUD_INV, "Inv", true);
    button(2, 114, 48, 24, B_HUD_SNEAK, player.sneaking || ui_sneak_toggle ? "SNEAK*" : "Sneak", true);
    button(206, 114, 48, 24, B_HUD_DROP, "Drop", !stack_empty(h));
    button(54, 114, 48, 24, B_HUD_JUMP, player.flying ? "Up" : "Jump", true);
    // info
    char buf[48];
    int t = world.time;
    int hours = (t / 1000 + 6) % 24, mins = (t % 1000) * 60 / 1000;
    snprintf(buf, sizeof(buf), "%02d:%02d", hours, mins);
    text_center(128, 4, buf, C(20, 22, 26), 1);
    if (settings.show_coords) {
        snprintf(buf, sizeof(buf), "X %ld Y %ld Z %ld", (long)(player.body.x >> 12), (long)(player.body.y >> 12), (long)(player.body.z >> 12));
        text_center(128, 16, buf, C(20, 22, 26), 1);
        text_center(128, 26, biome_name(worldgen_biome(player.body.x >> 12, player.body.z >> 12)), C(16, 18, 22), 1);
    }
    text_center(128, 70, "drag to look", C(9, 10, 12), 1);
    text_center(128, 80, "tap: use  hold: mine", C(9, 10, 12), 1);
}

static void draw_pause(void)
{
    tex_tile_bg(TEX_DIRT, 1, 3);
    text_center(128, 14, "Game Paused", COL_WHITE, 2);
    button(48, 46, 160, 26, B_RESUME, "Back to Game", true);
    button(48, 80, 160, 26, B_OPTIONS, "Options", true);
    button(48, 114, 160, 26, B_SAVEQUIT, save_available ? "Save and Quit" : "Quit (no SD saving)", true);
    char buf[48];
    snprintf(buf, sizeof(buf), "%s - %s", world.name, world.mode == MODE_CREATIVE ? "Creative" : "Survival");
    text_center(128, 160, buf, COL_GRAY, 1);
}

static void draw_death(void)
{
    for (int i = 0; i < W * H; i++)
        fb[i] = C(12, 2, 2);
    text_center(128, 24, "You died!", COL_WHITE, 2);
    text_center(128, 52, death_message, COL_WHITE, 1);
    button(48, 90, 160, 26, B_RESPAWN, "Respawn", true);
    button(48, 124, 160, 26, B_TITLE, save_available ? "Save and Quit" : "Title Screen", true);
}

static void draw_title(void)
{
    tex_tile_bg(TEX_DIRT, 1, 3);
    text_center(128, 12, "BlockCraft DS", COL_WHITE, 2);
    button(48, 48, 160, 28, B_PLAY, "Play", true);
    button(48, 84, 160, 28, B_OPTIONS, "Options", true);
    button(48, 120, 160, 28, B_HELP, "How to Play", true);
    text_center(128, 172, save_available ? "Saving to SD card: on" : "No SD card: worlds won't be saved", save_available ? COL_GRAY : COL_YELLOW, 1);
}

static void draw_worlds(void)
{
    tex_tile_bg(TEX_DIRT, 1, 3);
    text_center(128, 6, "Select World", COL_WHITE, 1);
    for (int i = 0; i < 3; i++) {
        int y = 22 + i * 42;
        char l1[48], l2[48];
        if (slot_info[i].exists) {
            snprintf(l1, sizeof(l1), "%s", slot_info[i].name);
            snprintf(l2, sizeof(l2), "%s - %s", slot_info[i].mode == MODE_CREATIVE ? "Creative" : "Survival",
                     slot_info[i].size == 256 ? "Large" : "Small");
        } else {
            snprintf(l1, sizeof(l1), "Empty slot %d", i + 1);
            snprintf(l2, sizeof(l2), "Tap to create a new world");
        }
        add_widget(WT_BUTTON, 8, y, 200, 38, B_SLOT0 + i);
        bool down = pressed_widget == nwidgets - 1;
        fill(8, y, 200, 38, COL_BLACK);
        bevel(9, y + 1, 198, 36, down ? C(14, 14, 20) : C(16, 16, 17), C(24, 24, 26), C(8, 8, 8));
        text(16, y + 7, l1, COL_WHITE);
        text(16, y + 21, l2, COL_GRAY);
        if (slot_info[i].exists)
            button(212, y, 40, 38, B_DELETE + 100 + i, "Del", true);
    }
    button(48, 156, 160, 26, B_CANCEL, "Back", true);
}

static void draw_newworld(void)
{
    tex_tile_bg(TEX_DIRT, 1, 3);
    text_center(128, 6, "Create New World", COL_WHITE, 1);
    char buf[48];
    snprintf(buf, sizeof(buf), "Game Mode: %s", new_world_mode == MODE_CREATIVE ? "Creative" : "Survival");
    button(28, 24, 200, 26, B_GAMEMODE, buf, true);
    text_center(128, 52, new_world_mode == MODE_CREATIVE ? "Unlimited blocks, flying, no damage" : "Gather resources, craft, survive the night", COL_GRAY, 1);
    bool dsi = isDSiMode();
    snprintf(buf, sizeof(buf), "World Size: %s", new_world_size == 256 ? "Large (256)" : "Small (128)");
    button(28, 66, 200, 26, B_SIZE, buf, dsi);
    if (!dsi)
        text_center(128, 94, "Large worlds need a DSi", COL_GRAY, 1);
    snprintf(buf, sizeof(buf), "Seed: %lu", (unsigned long)new_world_seed);
    button(28, 106, 200, 26, B_SEED, buf, true);
    button(28, 148, 96, 28, B_CANCEL, "Cancel", true);
    button(132, 148, 96, 28, B_CREATE, "Create", true);
}

static void draw_confirm_delete(void)
{
    tex_tile_bg(TEX_DIRT, 1, 3);
    text_center(128, 40, "Delete this world?", COL_WHITE, 1);
    text_center(128, 56, slot_info[menu_slot].name, COL_YELLOW, 1);
    text_center(128, 72, "This cannot be undone.", COL_GRAY, 1);
    button(28, 120, 96, 28, B_CANCEL, "Cancel", true);
    button(132, 120, 96, 28, B_CONFIRM, "Delete", true);
}

static void opt_row(int y, const char *label, int dn, int up, const char *value)
{
    text(10, y + 7, label, COL_WHITE);
    if (dn)
        button(140, y, 22, 20, dn, "-", true);
    text_center(186, y + 6, value, COL_YELLOW, 1);
    if (up)
        button(212, y, 22, 20, up, "+", true);
}

static void opt_toggle(int y, const char *label, int id, bool on)
{
    text(10, y + 7, label, COL_WHITE);
    button(150, y, 84, 20, id, on ? "ON" : "OFF", true);
}

static void draw_options(void)
{
    tex_tile_bg(TEX_DIRT, 1, 3);
    text_center(128, 2, "Options", COL_WHITE, 1);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", settings.look_speed);
    opt_row(14, "Look speed", B_OPT_LOOK_DN, B_OPT_LOOK_UP, buf);
    snprintf(buf, sizeof(buf), "%d chunks", settings.render_dist);
    opt_row(36, "View distance", B_OPT_RD_DN, B_OPT_RD_UP, buf);
    snprintf(buf, sizeof(buf), "%d", settings.volume);
    opt_row(58, "Volume", B_OPT_VOL_DN, B_OPT_VOL_UP, buf);
    opt_toggle(80, "Invert look Y", B_OPT_INVERT, settings.invert_y);
    opt_toggle(100, "Auto-jump", B_OPT_AUTOJUMP, settings.auto_jump);
    opt_toggle(120, "Music", B_OPT_MUSIC, settings.music);
    opt_toggle(140, "Show coordinates", B_OPT_COORDS, settings.show_coords);
    opt_toggle(160, "Peaceful (no monsters)", B_OPT_PEACEFUL, settings.peaceful);
    button(196, 2, 56, 12, B_DONE, "Done", true);
}

static const char *help_pages[][12] = {
    {
        "Controls",
        "D-Pad: walk (double-tap Up to sprint)",
        "A/B/X/Y: look around",
        "Stylus drag: look around",
        "R: tap = use/place, hold = mine/attack",
        "Stylus tap = use, hold still = mine",
        "L: jump / swim (double-tap: fly)",
        "SELECT: tap = next item, hold = sneak",
        "START: pause menu",
        "Touch the hotbar to pick an item.",
        NULL,
    },
    {
        "Survival tips",
        "Punch trees to collect logs.",
        "Logs -> planks -> crafting table.",
        "Make a wooden pickaxe, then stone.",
        "Furnaces smelt ore with coal.",
        "Monsters come out at night!",
        "Sleep in a bed to skip the night.",
        "Craft tab: pick a recipe, A = craft,",
        " X = craft all. L/R switch tabs.",
        "Items tab: D-pad + A to move items,",
        " Y = quick move, X = take half.",
        NULL,
    },
};

static void draw_help(void)
{
    tex_tile_bg(TEX_DIRT, 1, 3);
    const char *const *pg = help_pages[help_page];
    text_center(128, 4, pg[0], COL_WHITE, 1);
    for (int i = 1; pg[i]; i++)
        text(8, 10 + i * 13, pg[i], COL_WHITE);
    button(8, 162, 100, 24, B_HELP_NEXT, help_page ? "Controls" : "Tips", true);
    button(148, 162, 100, 24, B_DONE, "Done", true);
}

static char load_title[32], load_stage[40];
static int load_pct;

static void draw_loading(void)
{
    tex_tile_bg(TEX_DIRT, 1, 3);
    text_center(128, 60, load_title, COL_WHITE, 1);
    text_center(128, 80, load_stage, COL_GRAY, 1);
    fill(48, 100, 160, 6, C(4, 4, 4));
    fill(48, 100, load_pct * 160 / 100, 6, C(8, 28, 8));
}

static void draw_sleep(void)
{
    for (int i = 0; i < W * H; i++)
        fb[i] = C(1, 1, 3);
    text_center(128, 80, "Sleeping...", COL_WHITE, 1);
}

static void redraw(void)
{
    nwidgets = 0;
    switch (screen) {
        case UI_HUD: draw_hud(); break;
        case UI_INVENTORY: draw_inventory(); break;
        case UI_CRAFTING: draw_crafting(); break;
        case UI_FURNACE: draw_furnace(); break;
        case UI_CHEST: draw_chest(); break;
        case UI_CREATIVE: draw_creative(); break;
        case UI_PAUSE: draw_pause(); break;
        case UI_DEATH: draw_death(); break;
        case UI_TITLE: draw_title(); break;
        case UI_WORLDS: draw_worlds(); break;
        case UI_NEWWORLD: draw_newworld(); break;
        case UI_OPTIONS: draw_options(); break;
        case UI_HELP: draw_help(); break;
        case UI_LOADING: draw_loading(); break;
        case UI_CONFIRM_DELETE: draw_confirm_delete(); break;
        case UI_SLEEP: draw_sleep(); break;
    }
    draw_key_cursor();
    DC_FlushRange(fb, sizeof(fb));
    dmaCopy(fb, BG_GFX_SUB, sizeof(fb));
}

void ui_loading(const char *title, const char *stage, int pct)
{
    bool full = screen != UI_LOADING || strcmp(title, load_title) != 0;
    strncpy(load_title, title, sizeof(load_title) - 1);
    strncpy(load_stage, stage, sizeof(load_stage) - 1);
    load_pct = iclamp(pct, 0, 100);
    screen = UI_LOADING;
    if (full) {
        redraw();
        return;
    }
    // partial update: stage text and bar
    static uint16_t bg_row[W];
    for (int y = 78; y < 108; y++) {
        // restore background from the dirt pattern
        const uint8_t *t = blocktex_bin + TEX_DIRT * 256;
        const uint16_t *pal = (const uint16_t *)blockpal_bin;
        for (int x = 0; x < W; x++)
            bg_row[x] = darken(pal[t[((y >> 1) & 15) * 16 + ((x >> 1) & 15)]] | BIT(15), 1, 3);
        memcpy(&fb[y * W], bg_row, sizeof(bg_row));
    }
    text_center(128, 80, load_stage, COL_GRAY, 1);
    fill(48, 100, 160, 6, C(4, 4, 4));
    fill(48, 100, load_pct * 160 / 100, 6, C(8, 28, 8));
    DC_FlushRange(&fb[78 * W], 30 * W * 2);
    dmaCopy(&fb[78 * W], (uint16_t *)BG_GFX_SUB + 78 * W, 30 * W * 2);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static void on_button(int id)
{
    sound_play(SFX_CLICK, 80, 64, 100);
    switch (id) {
        case B_CLOSE: ui_close(); return;
        case B_MODE: click_mode = (click_mode + 1) % 3; break;
        case B_DROP:
            if (!stack_empty(&player.cursor))
                player_drop_stack(&player.cursor);
            break;
        case B_TRASH:
            stack_clear(&player.cursor);
            break;
        case B_PREV: if (creative_page > 0) creative_page--; break;
        case B_NEXT: creative_page++; break;
        case B_SURVIVAL_TAB:
            screen = UI_INVENTORY;
            break;
        case B_RESUME: ui_close(); return;
        case B_OPTIONS: options_from = screen; screen = UI_OPTIONS; break;
        case B_SAVEQUIT: menu_action = MENU_SAVE_QUIT; break;
        case B_RESPAWN: menu_action = MENU_RESPAWN; break;
        case B_TITLE: menu_action = MENU_SAVE_QUIT; break;
        case B_PLAY: screen = UI_WORLDS; break;
        case B_HELP: help_page = 0; options_from = screen; screen = UI_HELP; break;
        case B_HELP_NEXT: help_page ^= 1; break;
        case B_TAB_CRAFT:
            return_craft_items(player.craft, 4);
            return_craft_items(table_grid, 9);
            book_view = true;
            break;
        case B_TAB_ITEMS: book_view = false; break;
        case B_BOOK_UP: book_scroll = imax(0, book_scroll - BOOK_ROWS); book_select(imin(book_sel, book_scroll + BOOK_ROWS - 1)); break;
        case B_BOOK_DOWN:
            book_scroll = imin(imax(0, book_n - BOOK_ROWS), book_scroll + BOOK_ROWS);
            book_select(imax(book_sel, book_scroll));
            break;
        case B_BOOK_CRAFT: book_craft(false); break;
        case B_BOOK_MAX: book_craft(true); break;
        case B_SLOT0: case B_SLOT1: case B_SLOT2:
            menu_slot = id - B_SLOT0;
            if (slot_info[menu_slot].exists) {
                menu_action = MENU_PLAY_SLOT;
            } else {
                new_world_seed = rng_next() % 1000000;
#ifdef FIXED_SEED
                new_world_seed = FIXED_SEED;
#endif
                new_world_mode = MODE_SURVIVAL;
                new_world_size = 128;
                screen = UI_NEWWORLD;
            }
            break;
        case B_CANCEL:
            screen = (screen == UI_NEWWORLD || screen == UI_CONFIRM_DELETE) ? UI_WORLDS : UI_TITLE;
            break;
        case B_GAMEMODE: new_world_mode ^= 1; break;
        case B_SIZE: new_world_size = new_world_size == 128 ? 256 : 128; break;
        case B_SEED: new_world_seed = rng_next() % 1000000; break;
        case B_CREATE: menu_action = MENU_CREATE_WORLD; break;
        case B_CONFIRM: menu_action = MENU_DELETE_SLOT; screen = UI_WORLDS; break;
        case B_OPT_LOOK_DN: settings.look_speed = imax(1, settings.look_speed - 1); break;
        case B_OPT_LOOK_UP: settings.look_speed = imin(10, settings.look_speed + 1); break;
        case B_OPT_RD_DN: settings.render_dist = imax(2, settings.render_dist - 1); break;
        case B_OPT_RD_UP: settings.render_dist = imin(isDSiMode() ? 6 : 4, settings.render_dist + 1); break;
        case B_OPT_VOL_DN: settings.volume = imax(0, settings.volume - 1); break;
        case B_OPT_VOL_UP: settings.volume = imin(10, settings.volume + 1); break;
        case B_OPT_INVERT: settings.invert_y = !settings.invert_y; break;
        case B_OPT_AUTOJUMP: settings.auto_jump = !settings.auto_jump; break;
        case B_OPT_MUSIC: settings.music = !settings.music; break;
        case B_OPT_COORDS: settings.show_coords = !settings.show_coords; break;
        case B_OPT_PEACEFUL: settings.peaceful = !settings.peaceful; break;
        case B_DONE:
            screen = options_from;
            break;
        case B_HUD_INV: ui_open(UI_INVENTORY); return;
        case B_HUD_PAUSE: ui_open(UI_PAUSE); return;
        case B_HUD_SNEAK: ui_sneak_toggle = !ui_sneak_toggle; break;
        case B_HUD_DROP: player_drop_held(false); break;
        default:
            if (id >= B_DELETE + 100 && id < B_DELETE + 103) {
                menu_slot = id - B_DELETE - 100;
                screen = UI_CONFIRM_DELETE;
            }
            break;
    }
    game_screen = screen;
    is_dirty = true;
}

static int hit_test(int x, int y)
{
    for (int i = nwidgets - 1; i >= 0; i--) {
        Widget *wd = &widgets[i];
        if (x >= wd->x && x < wd->x + wd->w && y >= wd->y && y < wd->y + wd->h)
            return i;
    }
    return -1;
}

static bool touching;
static int touch_start_x, touch_start_y, last_tx, last_ty;
static int touch_frames, touch_moved;
static bool touch_on_pad;

static void handle_widget_tap(int wi)
{
    Widget *wd = &widgets[wi];
    switch (wd->type) {
        case WT_BUTTON:
            on_button(wd->id);
            break;
        case WT_SLOT:
            click_slot(wd);
            is_dirty = true;
            break;
        case WT_CITEM: {
            int id = wd->id;
            if (!stack_empty(&player.cursor) && player.cursor.id == id && player.cursor.count < item_max_stack(id)) {
                player.cursor.count++;
            } else {
                player.cursor.id = id;
                player.cursor.count = click_mode == 1 ? 1 : item_max_stack(id);
                player.cursor.damage = 0;
            }
            sound_play(SFX_CLICK, 60, 64, 120);
            is_dirty = true;
            break;
        }
        case WT_BOOKROW:
            if (wd->id == book_sel)
                book_craft(false);      // tap the selected row again to craft
            else {
                book_select(wd->id);
                sound_play(SFX_CLICK, 50, 64, 140);
            }
            is_dirty = true;
            break;
        case WT_HOTBAR:
            player.sel = wd->id;
            sound_play(SFX_CLICK, 50, 64, 140);
            is_dirty = true;
            break;
    }
}

// Buttons and D-pad inside the inventory-type screens
static void ui_menu_keys(void)
{
    uint32_t down = keysDown(), rep = keysDownRepeat();
    if (!ui_in_game_menu() || screen == UI_CREATIVE) {
        if (screen == UI_CREATIVE && (down & KEY_B))
            ui_close();
        return;
    }
    if (down & KEY_B) {
        ui_close();
        return;
    }
    bool tabs = screen == UI_INVENTORY || screen == UI_CRAFTING;
    if (tabs && (down & (KEY_L | KEY_R))) {
        on_button(book_view ? B_TAB_ITEMS : B_TAB_CRAFT);
        key_slot = book_view ? -1 : 0;
        return;
    }
    if (tabs && book_view) {
        if (rep & KEY_UP) book_select(book_sel - 1);
        if (rep & KEY_DOWN) book_select(book_sel + 1);
        if (rep & KEY_LEFT) book_select(book_sel - BOOK_ROWS);
        if (rep & KEY_RIGHT) book_select(book_sel + BOOK_ROWS);
        if (rep & KEY_A) book_craft(false);
        if (down & KEY_X) book_craft(true);
        return;
    }
    // slot screens: move a cursor over the slots
    int dx = 0, dy = 0;
    if (rep & KEY_UP) dy = -1;
    else if (rep & KEY_DOWN) dy = 1;
    else if (rep & KEY_LEFT) dx = -1;
    else if (rep & KEY_RIGHT) dx = 1;
    if (dx || dy) {
        if (key_slot < 0)
            key_slot = 0;
        else
            key_move(dx, dy);
        is_dirty = true;
    }
    if (down & (KEY_A | KEY_X | KEY_Y)) {
        if (key_slot < 0) {
            key_slot = 0;
            is_dirty = true;
            return;
        }
        int wi = slot_widget_index(key_slot);
        if (wi < 0)
            return;
        int saved = click_mode;
        click_mode = (down & KEY_Y) ? 2 : (down & KEY_X) ? 1 : 0;
        click_slot(&widgets[wi]);
        click_mode = saved;
        is_dirty = true;
    }
}

void ui_frame(void)
{
    ui_look_dx = ui_look_dy = 0;
    ui_tap_use = false;
    ui_jump_button = false;
    uint32_t held = keysHeld();
    uint32_t down = keysDown();
    ui_menu_keys();
    if ((down & KEY_TOUCH) && key_slot >= 0) {
        key_slot = -1;      // touching hides the keypad cursor
        is_dirty = true;
    }
    if (held & KEY_TOUCH) {
        touchPosition tp;
        touchRead(&tp);
        int tx = tp.px, ty = tp.py;
        if (down & KEY_TOUCH || !touching) {
            touching = true;
            touch_start_x = last_tx = tx;
            touch_start_y = last_ty = ty;
            touch_frames = 0;
            touch_moved = 0;
            int wi = hit_test(tx, ty);
            touch_on_pad = (screen == UI_HUD) && wi < 0;
            pressed_widget = touch_on_pad ? -1 : wi;
            if (pressed_widget >= 0)
                is_dirty = true;
            // Hotbar and slots react immediately
            if (wi >= 0 && widgets[wi].type != WT_BUTTON) {
                handle_widget_tap(wi);
                pressed_widget = -1;
            }
            if (wi >= 0 && widgets[wi].type == WT_BUTTON && widgets[wi].id == B_HUD_JUMP)
                ui_jump_button = true;
        } else {
            touch_frames++;
            int dx = tx - last_tx, dy = ty - last_ty;
            // ignore single-sample glitches
            if (iabs(dx) < 40 && iabs(dy) < 40) {
                touch_moved += iabs(dx) + iabs(dy);
                if (touch_on_pad) {
                    ui_look_dx = dx;
                    ui_look_dy = dy;
                }
                last_tx = tx;
                last_ty = ty;
            }
            if (pressed_widget >= 0 && widgets[pressed_widget].type == WT_BUTTON && widgets[pressed_widget].id == B_HUD_JUMP)
                ui_jump_button = true;
        }
        // hold-still on the look pad = mine
        ui_hold_action = touch_on_pad && touch_frames > 12 && touch_moved < 8;
    } else if (touching) {
        touching = false;
        ui_hold_action = false;
        if (touch_on_pad) {
            if (touch_frames < 12 && touch_moved < 8)
                ui_tap_use = true;
        } else if (pressed_widget >= 0) {
            Widget *wd = &widgets[pressed_widget];
            // release inside the button triggers it
            if (wd->type == WT_BUTTON && last_tx >= wd->x && last_tx < wd->x + wd->w && last_ty >= wd->y && last_ty < wd->y + wd->h)
                on_button(wd->id);
            pressed_widget = -1;
            is_dirty = true;
        }
        touch_on_pad = false;
    }
    // live updates for furnace progress
    if (screen == UI_FURNACE && (frame_counter & 7) == 0)
        is_dirty = true;
    if (is_dirty) {
        is_dirty = false;
        redraw();
    }
}

// Plain text report screen (used by the self-test build)
void ui_text_report(const char *const *lines, int n)
{
    for (int i = 0; i < W * H; i++)
        fb[i] = C(2, 3, 5);
    int first = n > 21 ? n - 21 : 0;
    for (int i = first; i < n; i++) {
        uint16_t col = COL_WHITE;
        if (strncmp(lines[i], "PASS", 4) == 0) col = COL_GREEN;
        else if (strncmp(lines[i], "FAIL", 4) == 0) col = COL_RED;
        text(2, 2 + (i - first) * 9, lines[i], col);
    }
    DC_FlushRange(fb, sizeof(fb));
    dmaCopy(fb, BG_GFX_SUB, sizeof(fb));
    screen = UI_LOADING;
}

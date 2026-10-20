// Video setup and texture management
#include "gfx.h"
#include "assets_gen.h"
#include "blocktex_bin.h"
#include "blockpal_bin.h"
#include "icontex_bin.h"
#include "iconpal_bin.h"
#include "cloudtex_bin.h"
#include "font_bin.h"

static uint16_t *hud_map;
static uint16_t *hud_tiles;

static const char *const hud_art[] = {
    // heart full
    ".33.33..", "3443443.", "34544443", "34444443", ".344443.", "..3443..", "...33...", "........",
    // heart half
    ".33.33..", "3443EE3.", "3454EEE3", "3444EEE3", ".344EE3.", "..34E3..", "...33...", "........",
    // heart empty
    ".33.33..", "3EE3EE3.", "3EEEEEE3", "3EEEEEE3", ".3EEEE3.", "..3EE3..", "...33...", "........",
    // food full (drumstick)
    "....333.", "...37873", "..378883", ".3778883", ".377773.", "3a3333..", "3a3.....", ".33.....",
    // food half
    "....333.", "...3EEE3", "..3EEEE3", ".37EEEE3", ".3777E3.", "3a3333..", "3a3.....", ".33.....",
    // food empty
    "....333.", "...3EEE3", "..3EEEE3", ".3EEEEE3", ".3EEEE3.", "3E3333..", "3E3.....", ".33.....",
    // bubble
    "..3333..", ".399993.", "39aa9993", "39a99993", "39999993", "39999993", ".399993.", "..3333..",
    // bubble pop
    "........", "..3..3..", "...33...", ".3....3.", ".3....3.", "...33...", "..3..3..", "........",
    // armor full
    "33333333", "3bbbbbb3", "3bccccb3", "3bccccb3", ".3bccb3.", ".3bccb3.", "..3bb3..", "...33...",
    // armor half
    "33333333", "3bbbEEE3", "3bccEEE3", "3bccEEE3", ".3bcEE3.", ".3bcEE3.", "..3bE3..", "...33...",
    // armor empty
    "33333333", "3EEEEEE3", "3EEEEEE3", "3EEEEEE3", ".3EEEE3.", ".3EEEE3.", "..3EE3..", "...33...",
    // crosshair TL
    "........", "........", "........", ".......d", ".......d", ".......d", ".......d", "...ddddd",
    // crosshair TR
    "........", "........", "........", "d.......", "d.......", "d.......", "d.......", "ddddd...",
    // crosshair BL
    "...ddddd", ".......d", ".......d", ".......d", ".......d", "........", "........", "........",
    // crosshair BR
    "ddddd...", "d.......", "d.......", "d.......", "d.......", "........", "........", "........",
    // solid
    "ffffffff", "ffffffff", "ffffffff", "ffffffff", "ffffffff", "ffffffff", "ffffffff", "ffffffff",
    // shade (checker)
    "f.f.f.f.", ".f.f.f.f", "f.f.f.f.", ".f.f.f.f", "f.f.f.f.", ".f.f.f.f", "f.f.f.f.", ".f.f.f.f",
    // hardcore-ish heart (golden) full / half (used for absorption) - reuse
    ".33.33..", "3883883.", "38788883", "38888883", ".388883.", "..3883..", "...33...", "........",
    ".33.33..", "3883EE3.", "3878EEE3", "3888EEE3", ".388EE3.", "..38E3..", "...33...", "........",
    // xp bar 0/8
    "........", "........", "33333333", "EEEEEEEE", "EEEEEEEE", "EEEEEEEE", "33333333", "........",
    // xp bar 1/8
    "........", "........", "33333333", "1EEEEEEE", "1EEEEEEE", "1EEEEEEE", "33333333", "........",
    // xp bar 2/8
    "........", "........", "33333333", "11EEEEEE", "11EEEEEE", "11EEEEEE", "33333333", "........",
    // xp bar 3/8
    "........", "........", "33333333", "111EEEEE", "111EEEEE", "111EEEEE", "33333333", "........",
    // xp bar 4/8
    "........", "........", "33333333", "1111EEEE", "1111EEEE", "1111EEEE", "33333333", "........",
    // xp bar 5/8
    "........", "........", "33333333", "11111EEE", "11111EEE", "11111EEE", "33333333", "........",
    // xp bar 6/8
    "........", "........", "33333333", "111111EE", "111111EE", "111111EE", "33333333", "........",
    // xp bar 7/8
    "........", "........", "33333333", "1111111E", "1111111E", "1111111E", "33333333", "........",
    // xp bar 8/8
    "........", "........", "33333333", "11111111", "11111111", "11111111", "33333333", "........",
};

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

static void make_hud_tiles(void)
{
    // Font glyphs with drop shadow
    const uint8_t *font = font_bin;
    for (int g = 0; g < 96; g++) {
        uint8_t px[8][8] = { { 0 } };
        for (int y = 0; y < 8; y++) {
            uint8_t row = font[g * 8 + y];
            for (int x = 0; x < 8; x++)
                if (row & (1 << x))
                    px[y][x] = 1;
        }
        for (int y = 7; y >= 0; y--)
            for (int x = 7; x >= 0; x--)
                if (px[y][x] == 1 && y < 7 && x < 7 && px[y + 1][x + 1] == 0)
                    px[y + 1][x + 1] = 2;
        uint16_t *t = hud_tiles + g * 16;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x += 4)
                t[y * 2 + x / 4] = px[y][x] | (px[y][x + 1] << 4) | (px[y][x + 2] << 8) | (px[y][x + 3] << 12);
    }
    int ntiles = (int)(sizeof(hud_art) / sizeof(hud_art[0])) / 8;
    for (int i = 0; i < ntiles; i++) {
        uint16_t *t = hud_tiles + (96 + i) * 16;
        for (int y = 0; y < 8; y++) {
            const char *row = hud_art[i * 8 + y];
            for (int x = 0; x < 8; x += 4) {
                int p[4];
                for (int k = 0; k < 4; k++)
                    p[k] = row[x + k] == '.' ? 0 : hexval(row[x + k]);
                t[y * 2 + x / 4] = p[0] | (p[1] << 4) | (p[2] << 8) | (p[3] << 12);
            }
        }
    }
    // Palettes: 16 banks
    static const uint16_t fg[6] = { RGB15(31, 31, 31), RGB15(31, 31, 10), RGB15(31, 8, 8), RGB15(20, 20, 20), RGB15(10, 31, 10), RGB15(6, 6, 6) };
    for (int b = 0; b < 6; b++) {
        uint16_t *pal = BG_PALETTE + b * 16;
        pal[0] = 0;
        pal[1] = fg[b];
        pal[2] = RGB15(6, 6, 6);
        pal[3] = RGB15(2, 2, 2);
        pal[4] = RGB15(28, 2, 2);
        pal[5] = RGB15(31, 22, 22);
        pal[6] = RGB15(16, 0, 0);
        pal[7] = RGB15(22, 12, 4);
        pal[8] = RGB15(30, 22, 12);
        pal[9] = RGB15(6, 14, 30);
        pal[10] = RGB15(28, 30, 31);
        pal[11] = RGB15(26, 26, 28);
        pal[12] = RGB15(16, 16, 18);
        pal[13] = RGB15(31, 31, 31);
        pal[14] = RGB15(8, 8, 8);
        pal[15] = RGB15(4, 4, 6);
    }
}

void gfx_init(void)
{
    // Main engine: 3D on BG0, HUD text on BG1
    videoSetMode(MODE_0_3D | DISPLAY_BG1_ACTIVE);
    vramSetBankA(VRAM_A_TEXTURE_SLOT0);
    vramSetBankB(VRAM_B_TEXTURE_SLOT1);
    vramSetBankE(VRAM_E_MAIN_BG);
    vramSetBankF(VRAM_F_TEX_PALETTE_SLOT0);
    vramSetBankG(VRAM_G_MAIN_SPRITE);
    // Sub engine: 16-bit bitmap for the touch UI
    videoSetModeSub(MODE_5_2D | DISPLAY_BG3_ACTIVE);
    vramSetBankC(VRAM_C_SUB_BG);

    REG_BG1CNT = BG_32x32 | BG_COLOR_16 | BG_MAP_BASE(0) | BG_TILE_BASE(1) | BG_PRIORITY(0);
    REG_BG0CNT = BG_PRIORITY(1);
    hud_map = (uint16_t *)BG_MAP_RAM(0);
    hud_tiles = (uint16_t *)BG_TILE_RAM(1);
    make_hud_tiles();
    hud_clear();

    REG_BG3CNT_SUB = BG_BMP16_256x256 | BG_BMP_BASE(0) | BG_PRIORITY(0);
    REG_BG3PA_SUB = 1 << 8;
    REG_BG3PB_SUB = 0;
    REG_BG3PC_SUB = 0;
    REG_BG3PD_SUB = 1 << 8;
    REG_BG3X_SUB = 0;
    REG_BG3Y_SUB = 0;

    glInit();
    glEnable(GL_TEXTURE_2D | GL_ANTIALIAS | GL_FOG | GL_BLEND);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
    gfx_upload_textures();
}

void gfx_upload_textures(void)
{
    vramSetBankA(VRAM_A_LCD);
    vramSetBankB(VRAM_B_LCD);
    // blocks + animation frames at slot 0..
    dmaCopy(blocktex_bin, VRAM_A, blocktex_bin_size);
    // icons at TEX_ICON_BASE
    uint8_t *dst = (uint8_t *)VRAM_A + TEX_ICON_BASE * 256;
    uint32_t n = icontex_bin_size;
    uint32_t room = 128 * 1024 - TEX_ICON_BASE * 256;
    if (n <= room) {
        dmaCopy(icontex_bin, dst, n);
        dmaCopy(cloudtex_bin, (uint8_t *)VRAM_B, cloudtex_bin_size);
    } else {
        dmaCopy(icontex_bin, dst, room);
        dmaCopy(icontex_bin + room, (uint8_t *)VRAM_B, n - room);
        dmaCopy(cloudtex_bin, (uint8_t *)VRAM_B + (n - room), cloudtex_bin_size);
    }
    vramSetBankA(VRAM_A_TEXTURE_SLOT0);
    vramSetBankB(VRAM_B_TEXTURE_SLOT1);
    vramSetBankF(VRAM_F_LCD);
    dmaCopy(blockpal_bin, VRAM_F, 512);
    dmaCopy(iconpal_bin, (uint8_t *)VRAM_F + 512, 512);
    vramSetBankF(VRAM_F_TEX_PALETTE_SLOT0);
}

void gfx_animate_textures(uint32_t frame)
{
    // Copy current water/lava/fire frame over the base texture slot
    int wf = (frame >> 3) % TEX_ANIM_FRAMES;
    int lf = (frame >> 4) % TEX_ANIM_FRAMES;
    int ff = (frame >> 2) % 4;
    vramSetBankA(VRAM_A_LCD);
    const uint8_t *src = blocktex_bin;
    dmaCopy(src + (TEX_ANIM_WATER + wf) * 256, (uint8_t *)VRAM_A + TEX_WATER * 256, 256);
    dmaCopy(src + (TEX_ANIM_LAVA + lf) * 256, (uint8_t *)VRAM_A + TEX_LAVA * 256, 256);
    dmaCopy(src + (TEX_ANIM_FIRE + ff) * 256, (uint8_t *)VRAM_A + TEX_FIRE * 256, 256);
    vramSetBankA(VRAM_A_TEXTURE_SLOT0);
}

// ---------------------------------------------------------------------------
// HUD text layer
// ---------------------------------------------------------------------------

void hud_clear(void)
{
    for (int i = 0; i < 32 * 32; i++)
        hud_map[i] = 0;
}

void hud_put_tile(int tx, int ty, int tile, int pal)
{
    if ((unsigned)tx >= 32 || (unsigned)ty >= 24)
        return;
    hud_map[ty * 32 + tx] = tile | (pal << 12);
}

void hud_print(int tx, int ty, const char *s, int pal)
{
    while (*s) {
        int c = (unsigned char)*s++;
        if (c < 32 || c > 127)
            c = '?';
        hud_put_tile(tx++, ty, c == ' ' ? 0 : c - 32, pal);
    }
}

void hud_center(int ty, const char *s, int pal)
{
    int n = strlen(s);
    hud_print((32 - n) / 2, ty, s, pal);
}

void hud_fill(int tx, int ty, int w, int h, int tile, int pal)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            hud_put_tile(tx + x, ty + y, tile, pal);
}

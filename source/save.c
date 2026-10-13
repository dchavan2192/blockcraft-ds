// World and settings persistence
#include <stdio.h>
#include <stdlib.h>
#include <fat.h>
#include <sys/stat.h>
#include <unistd.h>
#include "save.h"
#include "game.h"
#include "world.h"
#include "worldgen.h"
#include "player.h"
#include "inventory.h"
#include "entity.h"
#include "ui.h"

#define SAVE_MAGIC 0x53444342u   // "BCDS"
#define SAVE_VERSION 2

static bool fat_ok;
static char base_dir[64];

bool save_init(void)
{
    fat_ok = fatInitDefault();
    if (!fat_ok)
        return false;
    const char *drive = fatGetDefaultDrive();
    snprintf(base_dir, sizeof(base_dir), "%sblockcraft", drive ? drive : "/");
    mkdir(base_dir, 0777);
    struct stat st;
    if (stat(base_dir, &st) != 0) {
        fat_ok = false;
        return false;
    }
    return true;
}

static void slot_path(int slot, char *out, int n, const char *ext)
{
    snprintf(out, n, "%s/world%d.%s", base_dir, slot + 1, ext);
}

typedef struct {
    uint32_t magic, version;
    char name[24];
    uint8_t mode, pad[3];
    uint16_t size, pad2;
    uint32_t seed;
    int32_t time;
    uint32_t ticks;
    int16_t spawn_x, spawn_y, spawn_z, pad3;
} SaveHeader;

static bool read_header(FILE *f, SaveHeader *h)
{
    if (fread(h, sizeof(*h), 1, f) != 1)
        return false;
    return h->magic == SAVE_MAGIC && h->version == SAVE_VERSION && (h->size == 128 || h->size == 256);
}

void save_scan_slots(void)
{
    for (int i = 0; i < 3; i++) {
        memset(&slot_info[i], 0, sizeof(slot_info[i]));
        if (!fat_ok)
            continue;
        char path[96];
        slot_path(i, path, sizeof(path), "bcw");
        FILE *f = fopen(path, "rb");
        if (!f)
            continue;
        SaveHeader h;
        if (read_header(f, &h)) {
            slot_info[i].exists = true;
            memcpy(slot_info[i].name, h.name, sizeof(h.name));
            slot_info[i].name[23] = 0;
            slot_info[i].mode = h.mode;
            slot_info[i].size = h.size;
            slot_info[i].seed = h.seed;
            slot_info[i].play_ticks = h.ticks;
        }
        fclose(f);
    }
}

// ---- RLE helpers ----
static bool write_rle(FILE *f, const uint8_t *data, size_t n, void (*progress)(int), int p0, int p1)
{
    size_t i = 0;
    uint8_t buf[512];
    int bp = 0;
    while (i < n) {
        uint8_t v = data[i];
        size_t run = 1;
        while (i + run < n && data[i + run] == v && run < 0x1FFFFF)
            run++;
        buf[bp++] = v;
        size_t r = run;
        do {
            uint8_t b = r & 0x7F;
            r >>= 7;
            if (r)
                b |= 0x80;
            buf[bp++] = b;
        } while (r);
        if (bp > 500) {
            if (fwrite(buf, 1, bp, f) != (size_t)bp)
                return false;
            bp = 0;
        }
        i += run;
        if (progress && (i & 0xFFFF) < run + 1)
            progress(p0 + (int)((int64_t)(p1 - p0) * i / n));
    }
    if (bp && fwrite(buf, 1, bp, f) != (size_t)bp)
        return false;
    return true;
}

static bool read_rle(FILE *f, uint8_t *data, size_t n, void (*progress)(int), int p0, int p1)
{
    size_t i = 0;
    int last_pct = -1;
    while (i < n) {
        int v = fgetc(f);
        if (v == EOF)
            return false;
        size_t run = 0;
        int shift = 0;
        for (;;) {
            int b = fgetc(f);
            if (b == EOF)
                return false;
            run |= (size_t)(b & 0x7F) << shift;
            shift += 7;
            if (!(b & 0x80))
                break;
        }
        if (i + run > n)
            return false;
        memset(data + i, v, run);
        i += run;
        int pct = p0 + (int)((int64_t)(p1 - p0) * i / n);
        if (progress && pct != last_pct) {
            last_pct = pct;
            progress(pct);
        }
    }
    return true;
}

#define W(x) ok = ok && fwrite(&(x), sizeof(x), 1, f) == 1
#define R(x) ok = ok && fread(&(x), sizeof(x), 1, f) == 1

bool save_world_fp(FILE *f, void (*progress)(int pct))
{
    bool ok = true;
    SaveHeader h;
    memset(&h, 0, sizeof(h));
    h.magic = SAVE_MAGIC;
    h.version = SAVE_VERSION;
    memcpy(h.name, world.name, sizeof(h.name));
    h.mode = world.mode;
    h.size = world.size;
    h.seed = world.seed;
    h.time = world.time;
    h.ticks = world.ticks;
    h.spawn_x = world.spawn_x;
    h.spawn_y = world.spawn_y;
    h.spawn_z = world.spawn_z;
    W(h);
    // Player
    W(player.body.x); W(player.body.y); W(player.body.z);
    W(player.yaw); W(player.pitch);
    W(player.health); W(player.food); W(player.saturation); W(player.air);
    W(player.sel); W(player.inv); W(player.armor);
    W(player.spawn_x); W(player.spawn_y); W(player.spawn_z); W(player.has_bed_spawn); W(player.flying);
    W(player.mode);
    // Tile entities
    W(chests); W(furnaces);
    // Entities (mobs and items only)
    uint16_t ne = 0;
    for (int i = 0; i < MAX_ENTITIES; i++)
        if (entities[i].type != ET_NONE && entities[i].type != ET_ARROW && entities[i].type != ET_TNT && entities[i].ai != 9)
            ne++;
    W(ne);
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &entities[i];
        if (e->type == ET_NONE || e->type == ET_ARROW || e->type == ET_TNT || e->ai == 9)
            continue;
        W(e->type); W(e->body.x); W(e->body.y); W(e->body.z); W(e->health); W(e->item); W(e->sheared); W(e->yaw);
    }
    // Biomes and blocks
    size_t ncol = (size_t)world.size * world.size;
    if (ok && biome_map)
        ok = fwrite(biome_map, 1, ncol, f) == ncol;
    size_t nb = ncol * WORLD_H;
    if (ok)
        ok = write_rle(f, world.blk, nb, progress, 5, 70);
    if (ok)
        ok = write_rle(f, world.light, nb, progress, 70, 98);
    return ok;
}

bool save_world(int slot, void (*progress)(int pct))
{
    if (!fat_ok)
        return false;
    char path[96], tmp[96];
    slot_path(slot, path, sizeof(path), "bcw");
    slot_path(slot, tmp, sizeof(tmp), "tmp");
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return false;
    static char iobuf[16384];
    setvbuf(f, iobuf, _IOFBF, sizeof(iobuf));
    bool ok = save_world_fp(f, progress);
    if (fclose(f) != 0)
        ok = false;
    if (!ok) {
        remove(tmp);
        return false;
    }
    remove(path);
    if (rename(tmp, path) != 0)
        return false;
    if (progress)
        progress(100);
    return true;
}

bool load_world_fp(FILE *f, void (*progress)(int pct))
{
    SaveHeader h;
    if (!read_header(f, &h))
        return false;
    if (h.size == 256 && !isDSiMode())
        return false;
    if (!world_alloc(h.size))
        return false;
    memcpy(world.name, h.name, sizeof(world.name));
    world.name[23] = 0;
    world.mode = h.mode;
    world.seed = h.seed;
    world.time = h.time;
    world.ticks = h.ticks;
    world.spawn_x = h.spawn_x;
    world.spawn_y = h.spawn_y;
    world.spawn_z = h.spawn_z;
    player_reset(h.mode);
    bool ok = true;
    R(player.body.x); R(player.body.y); R(player.body.z);
    R(player.yaw); R(player.pitch);
    R(player.health); R(player.food); R(player.saturation); R(player.air);
    R(player.sel); R(player.inv); R(player.armor);
    R(player.spawn_x); R(player.spawn_y); R(player.spawn_z); R(player.has_bed_spawn); R(player.flying);
    R(player.mode);
    R(chests); R(furnaces);
    entities_clear();
    uint16_t ne = 0;
    R(ne);
    for (int i = 0; ok && i < ne; i++) {
        uint8_t type; int32_t x, y, z; int16_t health; ItemStack item; int8_t sheared; int16_t yaw;
        R(type); R(x); R(y); R(z); R(health); R(item); R(sheared); R(yaw);
        if (!ok || type == ET_NONE || type >= ET_COUNT)
            continue;
        int e = entity_spawn(type, x, y, z);
        if (e >= 0) {
            entities[e].health = health;
            entities[e].item = item;
            entities[e].sheared = sheared;
            entities[e].yaw = yaw;
        }
    }
    size_t ncol = (size_t)world.size * world.size;
    free(biome_map);
    biome_map = malloc(ncol);
    if (ok && biome_map)
        ok = fread(biome_map, 1, ncol, f) == ncol;
    size_t nb = ncol * WORLD_H;
    if (ok)
        ok = read_rle(f, world.blk, nb, progress, 5, 60);
    if (ok)
        ok = read_rle(f, world.light, nb, progress, 60, 95);
    if (!ok)
        return false;
    world_compute_heightmap();
    player.body.hw = 1229;
    player.body.h = 7373;
    player.prev_x = player.body.x;
    player.prev_y = player.body.y;
    player.prev_z = player.body.z;
    player.target_entity = -1;
    return true;
}

bool load_world(int slot, void (*progress)(int pct))
{
    if (!fat_ok)
        return false;
    char path[96];
    slot_path(slot, path, sizeof(path), "bcw");
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    static char iobuf[16384];
    setvbuf(f, iobuf, _IOFBF, sizeof(iobuf));
    bool ok = load_world_fp(f, progress);
    fclose(f);
    return ok;
}

bool delete_world(int slot)
{
    if (!fat_ok)
        return false;
    char path[96];
    slot_path(slot, path, sizeof(path), "bcw");
    return remove(path) == 0;
}

void save_settings(void)
{
    if (!fat_ok)
        return;
    char path[96];
    snprintf(path, sizeof(path), "%s/settings.bin", base_dir);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    uint32_t magic = SAVE_MAGIC + 1;
    fwrite(&magic, 4, 1, f);
    fwrite(&settings, sizeof(settings), 1, f);
    fclose(f);
}

void load_settings(void)
{
    if (!fat_ok)
        return;
    char path[96];
    snprintf(path, sizeof(path), "%s/settings.bin", base_dir);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    uint32_t magic = 0;
    Settings s;
    if (fread(&magic, 4, 1, f) == 1 && magic == SAVE_MAGIC + 1 && fread(&s, sizeof(s), 1, f) == 1)
        settings = s;
    fclose(f);
}

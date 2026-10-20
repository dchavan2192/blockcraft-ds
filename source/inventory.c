// Inventory helpers, crafting recipes, chests and furnaces
#include <stdarg.h>
#include "inventory.h"
#include "blocks.h"
#include "world.h"

Chest chests[MAX_CHESTS];
Furnace furnaces[MAX_FURNACES];

int item_max_stack(int id)
{
    if (!item_valid(id))
        return 64;
    return items[id].max_stack ? items[id].max_stack : 64;
}

int inv_add(ItemStack *inv, int n, int id, int count, int damage)
{
    if (!id || count <= 0)
        return 0;
    int max = item_max_stack(id);
    // merge into existing stacks first
    if (max > 1) {
        for (int i = 0; i < n && count > 0; i++) {
            if (inv[i].id == id && inv[i].count > 0 && inv[i].count < max && inv[i].damage == damage) {
                int room = max - inv[i].count;
                int put = count < room ? count : room;
                inv[i].count += put;
                count -= put;
            }
        }
    }
    for (int i = 0; i < n && count > 0; i++) {
        if (stack_empty(&inv[i])) {
            int put = count < max ? count : max;
            inv[i].id = id;
            inv[i].count = put;
            inv[i].damage = damage;
            count -= put;
        }
    }
    return count;
}

int inv_count_item(const ItemStack *inv, int n, int id)
{
    int c = 0;
    for (int i = 0; i < n; i++)
        if (inv[i].id == id)
            c += inv[i].count;
    return c;
}

bool inv_remove_item(ItemStack *inv, int n, int id, int count)
{
    if (inv_count_item(inv, n, id) < count)
        return false;
    for (int i = n - 1; i >= 0 && count > 0; i--) {
        if (inv[i].id == id) {
            int t = inv[i].count < count ? inv[i].count : count;
            inv[i].count -= t;
            count -= t;
            if (inv[i].count == 0)
                stack_clear(&inv[i]);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Recipes
// ---------------------------------------------------------------------------

#define G_PLANKS 0xFF01
#define G_LOG    0xFF02
#define G_WOOL   0xFF03
#define G_COAL   0xFF04

typedef struct {
    uint8_t w, h;
    uint8_t shapeless;
    uint8_t count;
    uint16_t out;
    uint16_t in[9];
} Recipe;

#define MAX_RECIPES 160
static Recipe recipes[MAX_RECIPES];
static int nrecipes;

static bool ing_match(uint16_t want, uint16_t have)
{
    if (want == G_PLANKS)
        return have == B_PLANKS_OAK || have == B_PLANKS_BIRCH || have == B_PLANKS_SPRUCE;
    if (want == G_LOG)
        return is_log(have);
    if (want == G_WOOL)
        return have >= B_WOOL_WHITE && have <= B_WOOL_LAST;
    if (want == G_COAL)
        return have == I_COAL || have == I_CHARCOAL;
    return want == have;
}

// pattern rows separated by '|'; then pairs (char, id) terminated by 0
static void shaped(int out, int count, const char *pattern, ...)
{
    if (nrecipes >= MAX_RECIPES)
        return;
    Recipe *r = &recipes[nrecipes++];
    memset(r, 0, sizeof(*r));
    r->out = out;
    r->count = count;
    char keys[8];
    uint16_t ids[8];
    int nk = 0;
    va_list ap;
    va_start(ap, pattern);
    for (;;) {
        int c = va_arg(ap, int);
        if (!c)
            break;
        keys[nk] = (char)c;
        ids[nk] = (uint16_t)va_arg(ap, int);
        nk++;
    }
    va_end(ap);
    int x = 0, y = 0, w = 0;
    for (const char *p = pattern; ; p++) {
        if (*p == '|' || *p == 0) {
            if (x > w)
                w = x;
            y++;
            x = 0;
            if (*p == 0)
                break;
            continue;
        }
        uint16_t id = 0;
        for (int k = 0; k < nk; k++)
            if (keys[k] == *p)
                id = ids[k];
        r->in[(y) * 3 + x] = id;
        x++;
    }
    r->w = w;
    r->h = y;
    // compact into w-wide layout
    uint16_t tmp[9] = { 0 };
    for (int yy = 0; yy < r->h; yy++)
        for (int xx = 0; xx < r->w; xx++)
            tmp[yy * r->w + xx] = r->in[yy * 3 + xx];
    memcpy(r->in, tmp, sizeof(tmp));
}

static void shapeless(int out, int count, ...)
{
    if (nrecipes >= MAX_RECIPES)
        return;
    Recipe *r = &recipes[nrecipes++];
    memset(r, 0, sizeof(*r));
    r->out = out;
    r->count = count;
    r->shapeless = 1;
    va_list ap;
    va_start(ap, count);
    int n = 0;
    for (;;) {
        int id = va_arg(ap, int);
        if (!id || n >= 9)
            break;
        r->in[n++] = id;
    }
    va_end(ap);
    r->w = n;
    r->h = 1;
}

void recipes_init(void)
{
    nrecipes = 0;
    shapeless(B_PLANKS_OAK, 4, B_LOG_OAK, 0);
    shapeless(B_PLANKS_BIRCH, 4, B_LOG_BIRCH, 0);
    shapeless(B_PLANKS_SPRUCE, 4, B_LOG_SPRUCE, 0);
    shaped(I_STICK, 4, "P|P", 'P', G_PLANKS, 0);
    shaped(B_CRAFTING_TABLE, 1, "PP|PP", 'P', G_PLANKS, 0);
    shaped(B_FURNACE, 1, "CCC|C C|CCC", 'C', B_COBBLESTONE, 0);
    shaped(B_CHEST, 1, "PPP|P P|PPP", 'P', G_PLANKS, 0);
    shaped(B_TORCH, 4, "C|S", 'C', G_COAL, 'S', I_STICK, 0);

    // Tools
    int mats[5] = { G_PLANKS, B_COBBLESTONE, I_IRON_INGOT, I_GOLD_INGOT, I_DIAMOND };
    for (int m = 0; m < 5; m++) {
        int M = mats[m];
        shaped(TOOL_ID(m, TK_PICKAXE), 1, "MMM| S | S ", 'M', M, 'S', I_STICK, 0);
        shaped(TOOL_ID(m, TK_AXE), 1, "MM|MS| S", 'M', M, 'S', I_STICK, 0);
        shaped(TOOL_ID(m, TK_SHOVEL), 1, "M|S|S", 'M', M, 'S', I_STICK, 0);
        shaped(TOOL_ID(m, TK_SWORD), 1, "M|M|S", 'M', M, 'S', I_STICK, 0);
        shaped(TOOL_ID(m, TK_HOE), 1, "MM| S| S", 'M', M, 'S', I_STICK, 0);
    }
    // Armor
    int amats[4] = { I_LEATHER, I_IRON_INGOT, I_GOLD_INGOT, I_DIAMOND };
    for (int m = 0; m < 4; m++) {
        int M = amats[m];
        shaped(ARMOR_ID(m, AS_HEAD), 1, "MMM|M M", 'M', M, 0);
        shaped(ARMOR_ID(m, AS_CHEST), 1, "M M|MMM|MMM", 'M', M, 0);
        shaped(ARMOR_ID(m, AS_LEGS), 1, "MMM|M M|M M", 'M', M, 0);
        shaped(ARMOR_ID(m, AS_FEET), 1, "M M|M M", 'M', M, 0);
    }
    shaped(I_BOW, 1, " ST|S T| ST", 'S', I_STICK, 'T', I_STRING, 0);
    shaped(I_ARROW, 4, "F|S|E", 'F', I_FLINT, 'S', I_STICK, 'E', I_FEATHER, 0);
    shaped(I_BUCKET, 1, "I I| I ", 'I', I_IRON_INGOT, 0);
    shapeless(I_FLINT_STEEL, 1, I_IRON_INGOT, I_FLINT, 0);
    shaped(I_SHEARS, 1, " I|I ", 'I', I_IRON_INGOT, 0);
    shaped(I_BOWL, 4, "P P| P ", 'P', G_PLANKS, 0);
    shapeless(I_MUSHROOM_STEW, 1, I_BOWL, B_MUSHROOM_RED, B_MUSHROOM_BROWN, 0);
    shaped(I_BREAD, 1, "WWW", 'W', I_WHEAT, 0);
    shaped(B_LADDER, 3, "S S|SSS|S S", 'S', I_STICK, 0);
    shaped(I_DOOR, 1, "PP|PP|PP", 'P', G_PLANKS, 0);
    shaped(I_BED, 1, "WWW|PPP", 'W', G_WOOL, 'P', G_PLANKS, 0);
    shaped(B_TNT, 1, "GSG|SGS|GSG", 'G', I_GUNPOWDER, 'S', B_SAND, 0);
    shaped(B_SANDSTONE, 1, "SS|SS", 'S', B_SAND, 0);
    shaped(B_BRICKS, 1, "BB|BB", 'B', I_BRICK, 0);
    shaped(B_STONEBRICKS, 4, "SS|SS", 'S', B_STONE, 0);
    shaped(B_WOOL_WHITE, 1, "SS|SS", 'S', I_STRING, 0);
    shapeless(B_WOOL_WHITE + 14, 1, B_WOOL_WHITE, B_FLOWER_RED, 0);
    shapeless(B_WOOL_WHITE + 4, 1, B_WOOL_WHITE, B_FLOWER_YELLOW, 0);
    shapeless(B_WOOL_WHITE + 15, 1, B_WOOL_WHITE, I_COAL, 0);
    shapeless(B_WOOL_WHITE + 13, 1, B_WOOL_WHITE, B_CACTUS, 0);
    shapeless(B_WOOL_WHITE + 12, 1, B_WOOL_WHITE, B_DIRT, 0);
    shapeless(B_WOOL_WHITE + 1, 1, B_WOOL_WHITE, B_FLOWER_RED, B_FLOWER_YELLOW, 0);
    shapeless(B_WOOL_WHITE + 11, 1, B_WOOL_WHITE, I_DIAMOND, 0);
    shapeless(B_JACKOLANTERN, 1, B_PUMPKIN, B_TORCH, 0);
    shapeless(I_PUMPKIN_PIE, 1, B_PUMPKIN, I_EGG, I_WHEAT, 0);
    shaped(B_GOLD_BLOCK, 1, "MMM|MMM|MMM", 'M', I_GOLD_INGOT, 0);
    shaped(B_IRON_BLOCK, 1, "MMM|MMM|MMM", 'M', I_IRON_INGOT, 0);
    shaped(B_DIAMOND_BLOCK, 1, "MMM|MMM|MMM", 'M', I_DIAMOND, 0);
    shaped(B_COAL_BLOCK, 1, "MMM|MMM|MMM", 'M', I_COAL, 0);
    shapeless(I_GOLD_INGOT, 9, B_GOLD_BLOCK, 0);
    shapeless(I_IRON_INGOT, 9, B_IRON_BLOCK, 0);
    shapeless(I_DIAMOND, 9, B_DIAMOND_BLOCK, 0);
    shapeless(I_COAL, 9, B_COAL_BLOCK, 0);
    shapeless(I_BONEMEAL, 3, I_BONE, 0);
    shaped(B_BOOKSHELF, 1, "PPP|LLL|PPP", 'P', G_PLANKS, 'L', I_LEATHER, 0);
    shapeless(B_MOSSY_COBBLESTONE, 1, B_COBBLESTONE, B_LEAVES_OAK, 0);
}

bool craft_match(const ItemStack *grid, int gw, ItemStack *result)
{
    stack_clear(result);
    // bounding box of non-empty cells
    int minx = 3, miny = 3, maxx = -1, maxy = -1, n = 0;
    for (int y = 0; y < gw; y++)
        for (int x = 0; x < gw; x++)
            if (!stack_empty(&grid[y * gw + x])) {
                n++;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
    if (!n)
        return false;
    int w = maxx - minx + 1, h = maxy - miny + 1;
    for (int r = 0; r < nrecipes; r++) {
        const Recipe *rc = &recipes[r];
        if (rc->shapeless) {
            if (rc->w != n)
                continue;
            bool used[9] = { false };
            bool ok = true;
            for (int y = 0; y < gw && ok; y++) {
                for (int x = 0; x < gw && ok; x++) {
                    const ItemStack *s = &grid[y * gw + x];
                    if (stack_empty(s))
                        continue;
                    bool found = false;
                    for (int i = 0; i < rc->w; i++) {
                        if (!used[i] && ing_match(rc->in[i], s->id)) {
                            used[i] = true;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        ok = false;
                }
            }
            if (!ok)
                continue;
            result->id = rc->out;
            result->count = rc->count;
            // planks from log keeps wood type (already encoded per recipe)
            return true;
        }
        if (rc->w != w || rc->h != h)
            continue;
        for (int mirror = 0; mirror < 2; mirror++) {
            bool ok = true;
            for (int y = 0; y < h && ok; y++) {
                for (int x = 0; x < w && ok; x++) {
                    int rx = mirror ? (w - 1 - x) : x;
                    uint16_t want = rc->in[y * w + rx];
                    const ItemStack *s = &grid[(miny + y) * gw + minx + x];
                    if (!want) {
                        if (!stack_empty(s))
                            ok = false;
                    } else if (stack_empty(s) || !ing_match(want, s->id)) {
                        ok = false;
                    }
                }
            }
            if (ok) {
                result->id = rc->out;
                result->count = rc->count;
                return true;
            }
        }
    }
    return false;
}

void craft_consume(ItemStack *grid, int gw)
{
    for (int i = 0; i < gw * gw; i++) {
        if (stack_empty(&grid[i]))
            continue;
        int id = grid[i].id;
        grid[i].count--;
        if (grid[i].count == 0) {
            stack_clear(&grid[i]);
            // buckets and bowls return containers
            if (id == I_MILK_BUCKET || id == I_WATER_BUCKET || id == I_LAVA_BUCKET) {
                grid[i].id = I_BUCKET;
                grid[i].count = 1;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Tile entities
// ---------------------------------------------------------------------------

void tile_entities_clear(void)
{
    memset(chests, 0, sizeof(chests));
    memset(furnaces, 0, sizeof(furnaces));
}

Chest *chest_get(int x, int y, int z, bool create)
{
    x &= world.mask;
    z &= world.mask;
    Chest *free_slot = NULL;
    for (int i = 0; i < MAX_CHESTS; i++) {
        Chest *c = &chests[i];
        if (c->used && c->x == x && c->y == y && c->z == z)
            return c;
        if (!c->used && !free_slot)
            free_slot = c;
    }
    if (!create || !free_slot)
        return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = 1;
    free_slot->x = x;
    free_slot->y = y;
    free_slot->z = z;
    return free_slot;
}

Furnace *furnace_get(int x, int y, int z, bool create)
{
    x &= world.mask;
    z &= world.mask;
    Furnace *free_slot = NULL;
    for (int i = 0; i < MAX_FURNACES; i++) {
        Furnace *f = &furnaces[i];
        if (f->used && f->x == x && f->y == y && f->z == z)
            return f;
        if (!f->used && !free_slot)
            free_slot = f;
    }
    if (!create || !free_slot)
        return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = 1;
    free_slot->x = x;
    free_slot->y = y;
    free_slot->z = z;
    return free_slot;
}

// Implemented in entity.c
void entity_spawn_item(int32_t x, int32_t y, int32_t z, const ItemStack *s);

static void drop_stack(int x, int y, int z, ItemStack *s)
{
    if (stack_empty(s))
        return;
    entity_spawn_item((x << 12) + 2048, (y << 12) + 2048, (z << 12) + 2048, s);
    stack_clear(s);
}

void chest_remove(int x, int y, int z)
{
    Chest *c = chest_get(x, y, z, false);
    if (!c)
        return;
    for (int i = 0; i < CHEST_SIZE; i++)
        drop_stack(x, y, z, &c->items[i]);
    c->used = 0;
}

void furnace_remove(int x, int y, int z)
{
    Furnace *f = furnace_get(x, y, z, false);
    if (!f)
        return;
    drop_stack(x, y, z, &f->in);
    drop_stack(x, y, z, &f->fuel);
    drop_stack(x, y, z, &f->out);
    f->used = 0;
}

static bool can_smelt(Furnace *f)
{
    if (stack_empty(&f->in))
        return false;
    int res = items[f->in.id].smelt;
    if (!res)
        return false;
    if (stack_empty(&f->out))
        return true;
    if (f->out.id != res)
        return false;
    return f->out.count < item_max_stack(res);
}

void furnaces_tick(void)
{
    for (int i = 0; i < MAX_FURNACES; i++) {
        Furnace *f = &furnaces[i];
        if (!f->used)
            continue;
        bool was_burning = f->burn > 0;
        if (f->burn > 0)
            f->burn--;
        bool smeltable = can_smelt(f);
        if (f->burn == 0 && smeltable && !stack_empty(&f->fuel) && items[f->fuel.id].fuel) {
            f->burn = f->burn_max = items[f->fuel.id].fuel > 30000 ? 30000 : items[f->fuel.id].fuel;
            int fid = f->fuel.id;
            f->fuel.count--;
            if (f->fuel.count == 0) {
                stack_clear(&f->fuel);
                if (fid == I_LAVA_BUCKET) {
                    f->fuel.id = I_BUCKET;
                    f->fuel.count = 1;
                }
            }
        }
        if (f->burn > 0 && smeltable) {
            f->cook++;
            if (f->cook >= 200) {
                f->cook = 0;
                int res = items[f->in.id].smelt;
                if (stack_empty(&f->out)) {
                    f->out.id = res;
                    f->out.count = 1;
                    f->out.damage = 0;
                } else {
                    f->out.count++;
                }
                f->in.count--;
                if (f->in.count == 0)
                    stack_clear(&f->in);
            }
        } else if (f->cook > 0) {
            f->cook = f->cook > 2 ? f->cook - 2 : 0;
        }
        bool burning = f->burn > 0;
        if (burning != was_burning) {
            int id = world_get(f->x, f->y, f->z);
            if (is_furnace(id)) {
                int facing = (id >= B_FURNACE_LIT) ? id - B_FURNACE_LIT : id - B_FURNACE;
                world_set(f->x, f->y, f->z, (burning ? B_FURNACE_LIT : B_FURNACE) + facing);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Recipe book: craft straight from the inventory without arranging a grid
// ---------------------------------------------------------------------------

int recipe_total(void)
{
    return nrecipes;
}

void recipe_info(int r, RecipeInfo *ri)
{
    memset(ri, 0, sizeof(*ri));
    if (r < 0 || r >= nrecipes)
        return;
    const Recipe *rc = &recipes[r];
    ri->out = rc->out;
    ri->count = rc->count;
    ri->needs_table = rc->shapeless ? (rc->w > 4) : (rc->w > 2 || rc->h > 2);
    int cells = rc->shapeless ? rc->w : rc->w * rc->h;
    for (int i = 0; i < cells; i++) {
        uint16_t k = rc->in[i];
        if (!k)
            continue;
        int j;
        for (j = 0; j < ri->ning; j++)
            if (ri->ing[j] == k)
                break;
        if (j == ri->ning) {
            ri->ing[j] = k;
            ri->need[j] = 0;
            ri->ning++;
        }
        ri->need[j]++;
    }
}

bool recipe_ing_matches(uint16_t key, int id)
{
    return id && ing_match(key, id);
}

// Item shown for an ingredient key (groups use a representative item)
int recipe_ing_display(uint16_t key)
{
    switch (key) {
        case G_PLANKS: return B_PLANKS_OAK;
        case G_LOG: return B_LOG_OAK;
        case G_WOOL: return B_WOOL_WHITE;
        case G_COAL: return I_COAL;
        default: return key;
    }
}

const char *recipe_ing_name(uint16_t key)
{
    switch (key) {
        case G_PLANKS: return "Any Planks";
        case G_LOG: return "Any Log";
        case G_WOOL: return "Any Wool";
        case G_COAL: return "Coal/Charcoal";
        default: return item_valid(key) ? items[key].name : "?";
    }
}

int recipe_have(uint16_t key, const ItemStack *inv, int n)
{
    int c = 0;
    for (int i = 0; i < n; i++)
        if (inv[i].count > 0 && ing_match(key, inv[i].id))
            c += inv[i].count;
    return c;
}

// Removes `need` items matching key, taking from the main inventory before the hotbar
static bool take_ing(ItemStack *inv, int n, uint16_t key, int need)
{
    for (int pass = 0; pass < 2 && need > 0; pass++) {
        for (int i = n - 1; i >= 0 && need > 0; i--) {
            bool hot = i < 9;
            if ((pass == 0) == hot)
                continue;
            if (inv[i].count <= 0 || !ing_match(key, inv[i].id))
                continue;
            int t = imin(inv[i].count, need);
            inv[i].count -= t;
            need -= t;
            if (inv[i].count == 0)
                stack_clear(&inv[i]);
        }
    }
    return need == 0;
}

int recipe_times(int r, const ItemStack *inv, int n)
{
    RecipeInfo ri;
    recipe_info(r, &ri);
    if (!ri.ning)
        return 0;
    int times = 999;
    for (int j = 0; j < ri.ning; j++) {
        int t = recipe_have(ri.ing[j], inv, n) / ri.need[j];
        if (t < times)
            times = t;
    }
    return times;
}

int recipe_craft(int r, ItemStack *inv, int n, int times)
{
    RecipeInfo ri;
    recipe_info(r, &ri);
    if (!ri.ning || n > 64)
        return 0;
    int done = 0;
    ItemStack tmp[64];
    while (done < times) {
        memcpy(tmp, inv, n * sizeof(ItemStack));
        bool ok = true;
        for (int j = 0; j < ri.ning && ok; j++)
            ok = take_ing(tmp, n, ri.ing[j], ri.need[j]);
        if (!ok)
            break;
        if (inv_add(tmp, n, ri.out, ri.count, 0) > 0)
            break;   // no room for the result
        memcpy(inv, tmp, n * sizeof(ItemStack));
        done++;
    }
    return done;
}

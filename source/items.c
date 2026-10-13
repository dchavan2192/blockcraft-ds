// Item table
#include "items.h"
#include "blocks.h"
#include "assets_gen.h"

ItemDef items[I_COUNT];

static ItemDef *it(int id, const char *name, int icon, int stack, int kind)
{
    ItemDef *d = &items[id];
    memset(d, 0, sizeof(*d));
    d->name = name;
    d->icon = icon;
    d->max_stack = stack;
    d->kind = kind;
    d->damage = 1;
    d->speed = 1;
    return d;
}

static ItemDef *blockitem(int bid, int icon)
{
    ItemDef *d = it(bid, blocks[bid].name, icon, 64, IK_BLOCK);
    d->places = bid;
    if (blocks[bid].flags & BF_FLAMMABLE)
        d->fuel = 300;
    return d;
}

static ItemDef *food(int id, const char *name, int icon, int hunger, int sat10)
{
    ItemDef *d = it(id, name, icon, 64, IK_FOOD);
    d->food = hunger;
    d->saturation = sat10;
    return d;
}

void items_init(void)
{
    memset(items, 0, sizeof(items));
    ItemDef *d;

    // Block items
    blockitem(B_STONE, ICON_BLK_STONE);
    blockitem(B_GRASS, ICON_BLK_GRASS);
    blockitem(B_DIRT, ICON_BLK_DIRT);
    blockitem(B_COBBLESTONE, ICON_BLK_COBBLESTONE);
    blockitem(B_MOSSY_COBBLESTONE, ICON_BLK_MOSSY_COBBLESTONE);
    blockitem(B_STONEBRICKS, ICON_BLK_STONEBRICKS);
    blockitem(B_PLANKS_OAK, ICON_BLK_PLANKS_OAK);
    blockitem(B_PLANKS_BIRCH, ICON_BLK_PLANKS_BIRCH);
    blockitem(B_PLANKS_SPRUCE, ICON_BLK_PLANKS_SPRUCE);
    d = blockitem(B_SAPLING_OAK, ICON_BLK_SAPLING_OAK); d->fuel = 100;
    d = blockitem(B_SAPLING_BIRCH, ICON_BLK_SAPLING_BIRCH); d->fuel = 100;
    d = blockitem(B_SAPLING_SPRUCE, ICON_BLK_SAPLING_SPRUCE); d->fuel = 100;
    blockitem(B_BEDROCK, ICON_BLK_BEDROCK);
    d = blockitem(B_SAND, ICON_BLK_SAND); d->smelt = B_GLASS;
    blockitem(B_GRAVEL, ICON_BLK_GRAVEL);
    d = blockitem(B_GOLD_ORE, ICON_BLK_GOLD_ORE); d->smelt = I_GOLD_INGOT;
    d = blockitem(B_IRON_ORE, ICON_BLK_IRON_ORE); d->smelt = I_IRON_INGOT;
    d = blockitem(B_COAL_ORE, ICON_BLK_COAL_ORE); d->smelt = I_COAL;
    d = blockitem(B_DIAMOND_ORE, ICON_BLK_DIAMOND_ORE); d->smelt = I_DIAMOND;
    d = blockitem(B_REDSTONE_ORE, ICON_BLK_REDSTONE_ORE); d->smelt = I_REDSTONE;
    d = blockitem(B_LOG_OAK, ICON_BLK_LOG_OAK); d->smelt = I_CHARCOAL;
    d = blockitem(B_LOG_BIRCH, ICON_BLK_LOG_BIRCH); d->smelt = I_CHARCOAL;
    d = blockitem(B_LOG_SPRUCE, ICON_BLK_LOG_SPRUCE); d->smelt = I_CHARCOAL;
    blockitem(B_LEAVES_OAK, ICON_BLK_LEAVES_OAK);
    blockitem(B_LEAVES_BIRCH, ICON_BLK_LEAVES_BIRCH);
    blockitem(B_LEAVES_SPRUCE, ICON_BLK_LEAVES_SPRUCE);
    blockitem(B_GLASS, ICON_BLK_GLASS);
    blockitem(B_SANDSTONE, ICON_BLK_SANDSTONE);
    for (int i = 0; i < 16; i++)
        blockitem(B_WOOL_WHITE + i, ICON_BLK_WOOL_WHITE + i);
    blockitem(B_FLOWER_RED, ICON_BLK_FLOWER_RED);
    blockitem(B_FLOWER_YELLOW, ICON_BLK_FLOWER_YELLOW);
    blockitem(B_MUSHROOM_BROWN, ICON_BLK_MUSHROOM_BROWN);
    blockitem(B_MUSHROOM_RED, ICON_BLK_MUSHROOM_RED);
    blockitem(B_TALLGRASS, ICON_BLK_TALLGRASS);
    blockitem(B_DEADBUSH, ICON_BLK_DEADBUSH);
    blockitem(B_GOLD_BLOCK, ICON_BLK_GOLD_BLOCK);
    blockitem(B_IRON_BLOCK, ICON_BLK_IRON_BLOCK);
    blockitem(B_DIAMOND_BLOCK, ICON_BLK_DIAMOND_BLOCK);
    d = blockitem(B_COAL_BLOCK, ICON_BLK_COAL_BLOCK); d->fuel = 16000;
    blockitem(B_BRICKS, ICON_BLK_BRICKS);
    blockitem(B_TNT, ICON_BLK_TNT);
    blockitem(B_BOOKSHELF, ICON_BLK_BOOKSHELF);
    blockitem(B_OBSIDIAN, ICON_BLK_OBSIDIAN);
    blockitem(B_TORCH, ICON_BLK_TORCH);
    d = blockitem(B_CHEST, ICON_BLK_CHEST); d->fuel = 300;
    d = blockitem(B_CRAFTING_TABLE, ICON_BLK_CRAFTING_TABLE); d->fuel = 300;
    blockitem(B_FARMLAND, ICON_BLK_FARMLAND);
    blockitem(B_FURNACE, ICON_BLK_FURNACE);
    d = blockitem(B_LADDER, ICON_BLK_LADDER); d->fuel = 300;
    blockitem(B_SNOW, ICON_BLK_SNOW);
    blockitem(B_ICE, ICON_BLK_ICE);
    blockitem(B_CACTUS, ICON_BLK_CACTUS);
    blockitem(B_CLAY, ICON_BLK_CLAY);
    blockitem(B_PUMPKIN, ICON_BLK_PUMPKIN);
    blockitem(B_JACKOLANTERN, ICON_BLK_JACKOLANTERN);
    blockitem(B_WATER, ICON_BLK_WATER);
    blockitem(B_LAVA, ICON_BLK_LAVA);
    items[B_SNOWY_GRASS] = items[B_GRASS];

    // Plain items
    d = it(I_STICK, "Stick", ICON_STICK, 64, IK_MISC); d->fuel = 100;
    d = it(I_COAL, "Coal", ICON_COAL, 64, IK_MISC); d->fuel = 1600;
    d = it(I_CHARCOAL, "Charcoal", ICON_CHARCOAL, 64, IK_MISC); d->fuel = 1600;
    it(I_IRON_INGOT, "Iron Ingot", ICON_IRON_INGOT, 64, IK_MISC);
    it(I_GOLD_INGOT, "Gold Ingot", ICON_GOLD_INGOT, 64, IK_MISC);
    it(I_DIAMOND, "Diamond", ICON_DIAMOND, 64, IK_MISC);
    it(I_REDSTONE, "Redstone", ICON_REDSTONE, 64, IK_MISC);
    it(I_FLINT, "Flint", ICON_FLINT, 64, IK_MISC);
    it(I_STRING, "String", ICON_STRING, 64, IK_MISC);
    it(I_FEATHER, "Feather", ICON_FEATHER, 64, IK_MISC);
    it(I_GUNPOWDER, "Gunpowder", ICON_GUNPOWDER, 64, IK_MISC);
    it(I_BONE, "Bone", ICON_BONE, 64, IK_MISC);
    it(I_BONEMEAL, "Bone Meal", ICON_BONEMEAL, 64, IK_MISC);
    food(I_ROTTEN_FLESH, "Rotten Flesh", ICON_ROTTEN_FLESH, 4, 8);
    it(I_LEATHER, "Leather", ICON_LEATHER, 64, IK_MISC);
    it(I_WHEAT, "Wheat", ICON_WHEAT, 64, IK_MISC);
    d = it(I_SEEDS, "Seeds", ICON_SEEDS, 64, IK_PLACER); d->places = B_WHEAT;
    food(I_BREAD, "Bread", ICON_BREAD, 5, 60);
    food(I_APPLE, "Apple", ICON_APPLE, 4, 24);
    d = food(I_RAW_PORK, "Raw Porkchop", ICON_RAW_PORKCHOP, 3, 18); d->smelt = I_COOKED_PORK;
    food(I_COOKED_PORK, "Cooked Porkchop", ICON_COOKED_PORKCHOP, 8, 128);
    d = food(I_RAW_BEEF, "Raw Beef", ICON_RAW_BEEF, 3, 18); d->smelt = I_STEAK;
    food(I_STEAK, "Steak", ICON_STEAK, 8, 128);
    d = food(I_RAW_CHICKEN, "Raw Chicken", ICON_RAW_CHICKEN, 2, 12); d->smelt = I_COOKED_CHICKEN;
    food(I_COOKED_CHICKEN, "Cooked Chicken", ICON_COOKED_CHICKEN, 6, 72);
    d = food(I_MUSHROOM_STEW, "Mushroom Stew", ICON_MUSHROOM_STEW, 6, 72); d->max_stack = 1;
    it(I_BOWL, "Bowl", ICON_BOWL, 64, IK_MISC);
    it(I_EGG, "Egg", ICON_EGG, 16, IK_MISC);
    it(I_BUCKET, "Bucket", ICON_BUCKET, 16, IK_BUCKET);
    d = it(I_WATER_BUCKET, "Water Bucket", ICON_WATER_BUCKET, 1, IK_BUCKET); d->places = B_WATER;
    d = it(I_LAVA_BUCKET, "Lava Bucket", ICON_LAVA_BUCKET, 1, IK_BUCKET); d->places = B_LAVA; d->fuel = 20000;
    d = it(I_MILK_BUCKET, "Milk Bucket", ICON_MILK_BUCKET, 1, IK_FOOD); d->food = 0;
    d = it(I_BOW, "Bow", ICON_BOW, 1, IK_TOOL); d->durability = 384; d->fuel = 300;
    it(I_ARROW, "Arrow", ICON_ARROW, 64, IK_MISC);
    d = it(I_FLINT_STEEL, "Flint and Steel", ICON_FLINT_AND_STEEL, 1, IK_TOOL); d->durability = 64;
    d = it(I_SHEARS, "Shears", ICON_SHEARS, 1, IK_TOOL); d->durability = 238; d->tool_type = TOOL_SHEARS; d->speed = 5;
    it(I_BRICK, "Brick", ICON_BRICK, 64, IK_MISC);
    d = it(I_CLAY_BALL, "Clay", ICON_CLAY_BALL, 64, IK_MISC); d->smelt = I_BRICK;
    it(I_PAPER, "Paper", ICON_PAPER, 64, IK_MISC);
    it(I_SUGAR, "Sugar", ICON_SUGAR, 64, IK_MISC);
    food(I_PUMPKIN_PIE, "Pumpkin Pie", ICON_PUMPKIN_PIE, 8, 48);
    d = it(I_DOOR, "Door", ICON_BLK_DOOR, 1, IK_PLACER); d->places = B_DOOR; d->fuel = 200;
    d = it(I_BED, "Bed", ICON_BLK_BED, 1, IK_PLACER); d->places = B_BED;

    // Tools
    static const char *mat_names[5] = { "Wooden", "Stone", "Iron", "Golden", "Diamond" };
    static const char *kind_names[5] = { "Pickaxe", "Axe", "Shovel", "Sword", "Hoe" };
    static const uint8_t tool_types[5] = { TOOL_PICKAXE, TOOL_AXE, TOOL_SHOVEL, TOOL_SWORD, TOOL_HOE };
    static const uint8_t tiers[5] = { 1, 2, 3, 1, 4 };
    static const uint8_t speeds[5] = { 2, 4, 6, 12, 8 };
    static const uint16_t dur[5] = { 59, 131, 250, 32, 1561 };
    static const uint8_t sword_dmg[5] = { 4, 5, 6, 4, 7 };
    static const int icon_mat[5] = { 0, 1, 2, 3, 4 }; // icon order: wood, stone, iron, gold, diamond
    static char names[25][24];
    for (int m = 0; m < 5; m++) {
        for (int k = 0; k < 5; k++) {
            int id = TOOL_ID(m, k);
            char *n = names[m * 5 + k];
            strcpy(n, mat_names[m]);
            strcat(n, " ");
            strcat(n, kind_names[k]);
            d = it(id, n, ICON_WOOD_PICKAXE + icon_mat[m] * 5 + k, 1, IK_TOOL);
            d->tool_type = tool_types[k];
            d->tool_tier = tiers[m];
            d->speed = speeds[m];
            d->durability = dur[m];
            d->damage = (k == TK_SWORD) ? sword_dmg[m] : (k == TK_AXE) ? sword_dmg[m] - 1 :
                        (k == TK_PICKAXE) ? sword_dmg[m] - 2 : (k == TK_SHOVEL) ? sword_dmg[m] - 3 : 1;
            if (m == MAT_WOOD)
                d->fuel = 200;
        }
    }

    // Armor
    static const char *am_names[4] = { "Leather", "Iron", "Golden", "Diamond" };
    static const char *slot_names[4] = { "Helmet", "Chestplate", "Leggings", "Boots" };
    static const char *slot_names_l[4] = { "Cap", "Tunic", "Pants", "Boots" };
    static const uint8_t points[4][4] = { { 1, 3, 2, 1 }, { 2, 6, 5, 2 }, { 2, 5, 3, 1 }, { 3, 8, 6, 3 } };
    static const uint8_t mult[4] = { 5, 15, 7, 33 };
    static const uint8_t base_dur[4] = { 11, 16, 15, 13 };
    static char anames[16][24];
    for (int m = 0; m < 4; m++) {
        for (int s = 0; s < 4; s++) {
            int id = ARMOR_ID(m, s);
            char *n = anames[m * 4 + s];
            strcpy(n, am_names[m]);
            strcat(n, " ");
            strcat(n, m == AM_LEATHER ? slot_names_l[s] : slot_names[s]);
            d = it(id, n, ICON_LEATHER_HELMET + m * 4 + s, 1, IK_ARMOR);
            d->armor_slot = s;
            d->armor_points = points[m][s];
            d->durability = base_dur[s] * mult[m];
        }
    }
}

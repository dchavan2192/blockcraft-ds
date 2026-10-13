// Block table
#include "blocks.h"
#include "items.h"
#include "assets_gen.h"

BlockDef blocks[256];

const int8_t face_dx[6] = { -1, 1, 0, 0, 0, 0 };
const int8_t face_dy[6] = { 0, 0, -1, 1, 0, 0 };
const int8_t face_dz[6] = { 0, 0, 0, 0, -1, 1 };
const uint8_t facing_to_face[4] = { FACE_N, FACE_E, FACE_S, FACE_W };

static BlockDef *def(int id, const char *name, int tex_all, int shape, int render, int flags)
{
    BlockDef *b = &blocks[id];
    b->name = name;
    for (int i = 0; i < 6; i++)
        b->tex[i] = tex_all;
    b->shape = shape;
    b->render = render;
    b->flags = flags | (shape != SHAPE_NONE && shape != SHAPE_LIQUID ? BF_SELECTABLE : 0);
    b->opacity = (flags & BF_OPAQUE) ? 15 : 0;
    b->emit = 0;
    b->hardness = 10;
    b->tool = TOOL_NONE;
    b->tier = 0;
    b->sound = SND_STONE;
    b->base = id;
    b->drop = id;
    b->drop_count = 1;
    return b;
}

#define CUBE (BF_SOLID | BF_OPAQUE)

static BlockDef *cube(int id, const char *name, int tex, int hard, int tool, int tier, int snd)
{
    BlockDef *b = def(id, name, tex, SHAPE_CUBE, RENDER_OPAQUE, CUBE);
    b->hardness = hard;
    b->tool = tool;
    b->tier = tier;
    b->sound = snd;
    return b;
}

static void tex_tsb(BlockDef *b, int top, int side, int bottom)
{
    b->tex[FACE_U] = top;
    b->tex[FACE_D] = bottom;
    b->tex[FACE_N] = b->tex[FACE_S] = b->tex[FACE_E] = b->tex[FACE_W] = side;
}

static BlockDef *plant(int id, const char *name, int tex)
{
    BlockDef *b = def(id, name, tex, SHAPE_CROSS, RENDER_CUTOUT, BF_NEEDS_SUPPORT | BF_FLAMMABLE);
    b->hardness = 0;
    b->sound = SND_GRASS;
    return b;
}

// directional block with a front face: variants id..id+3 facing N,E,S,W
static void directional(int id, const char *name, int top, int side, int front, int bottom, int hard, int tool, int tier, int snd, int base)
{
    for (int f = 0; f < 4; f++) {
        BlockDef *b = cube(id + f, name, side, hard, tool, tier, snd);
        tex_tsb(b, top, side, bottom);
        b->tex[facing_to_face[f]] = front;
        b->base = base;
        b->drop = base;
    }
}

int liquid_level(int id)
{
    if (id == B_WATER || id == B_LAVA)
        return 0;
    if (id == B_WATER_FALL || id == B_LAVA_FALL)
        return 8;
    if (id > B_WATER && id < B_WATER_FALL)
        return id - B_WATER;
    if (id > B_LAVA && id < B_LAVA_FALL)
        return (id - B_LAVA) * 2;
    return 0;
}

int liquid_height16(int id)
{
    int lv = liquid_level(id);
    if (lv == 8)
        return 16;
    if (lv == 0)
        return 14;
    return 14 - lv * 12 / 8;
}

void blocks_init(void)
{
    memset(blocks, 0, sizeof(blocks));
    for (int i = 0; i < 256; i++) {
        blocks[i].name = NULL;
        blocks[i].base = i;
    }
    BlockDef *b;
    b = def(B_AIR, "Air", 0, SHAPE_NONE, RENDER_NONE, BF_REPLACEABLE);
    b->drop = 0;
    b->sound = SND_NONE;

    b = cube(B_STONE, "Stone", TEX_STONE, 15, TOOL_PICKAXE, 1, SND_STONE); b->drop = B_COBBLESTONE;
    b = cube(B_GRASS, "Grass Block", TEX_GRASS_SIDE, 6, TOOL_SHOVEL, 0, SND_GRASS);
    tex_tsb(b, TEX_GRASS_TOP, TEX_GRASS_SIDE, TEX_DIRT); b->drop = B_DIRT;
    b = cube(B_SNOWY_GRASS, "Snowy Grass", TEX_GRASS_SIDE_SNOW, 6, TOOL_SHOVEL, 0, SND_GRASS);
    tex_tsb(b, TEX_SNOW, TEX_GRASS_SIDE_SNOW, TEX_DIRT); b->drop = B_DIRT;
    cube(B_DIRT, "Dirt", TEX_DIRT, 5, TOOL_SHOVEL, 0, SND_GRASS);
    cube(B_COBBLESTONE, "Cobblestone", TEX_COBBLESTONE, 20, TOOL_PICKAXE, 1, SND_STONE);
    cube(B_MOSSY_COBBLESTONE, "Mossy Cobblestone", TEX_MOSSY_COBBLESTONE, 20, TOOL_PICKAXE, 1, SND_STONE);
    cube(B_STONEBRICKS, "Stone Bricks", TEX_STONEBRICKS, 15, TOOL_PICKAXE, 1, SND_STONE);
    b = cube(B_PLANKS_OAK, "Oak Planks", TEX_PLANKS_OAK, 20, TOOL_AXE, 0, SND_WOOD); b->flags |= BF_FLAMMABLE;
    b = cube(B_PLANKS_BIRCH, "Birch Planks", TEX_PLANKS_BIRCH, 20, TOOL_AXE, 0, SND_WOOD); b->flags |= BF_FLAMMABLE;
    b = cube(B_PLANKS_SPRUCE, "Spruce Planks", TEX_PLANKS_SPRUCE, 20, TOOL_AXE, 0, SND_WOOD); b->flags |= BF_FLAMMABLE;
    plant(B_SAPLING_OAK, "Oak Sapling", TEX_SAPLING_OAK);
    plant(B_SAPLING_BIRCH, "Birch Sapling", TEX_SAPLING_BIRCH);
    plant(B_SAPLING_SPRUCE, "Spruce Sapling", TEX_SAPLING_SPRUCE);
    b = cube(B_BEDROCK, "Bedrock", TEX_BEDROCK, 0xFFFF, TOOL_NONE, 0, SND_STONE); b->drop = 0;

    // Liquids
    for (int i = B_WATER; i <= B_WATER_FALL; i++) {
        b = def(i, "Water", TEX_WATER, SHAPE_LIQUID, RENDER_TRANSLUCENT, BF_LIQUID | BF_REPLACEABLE);
        b->opacity = 2; b->hardness = 0xFFFF; b->drop = 0; b->base = B_WATER; b->sound = SND_NONE;
    }
    for (int i = B_LAVA; i <= B_LAVA_FALL; i++) {
        b = def(i, "Lava", TEX_LAVA, SHAPE_LIQUID, RENDER_TRANSLUCENT, BF_LIQUID | BF_REPLACEABLE | BF_HURTS);
        b->opacity = 2; b->emit = 15; b->hardness = 0xFFFF; b->drop = 0; b->base = B_LAVA; b->sound = SND_NONE;
    }
    b = cube(B_SAND, "Sand", TEX_SAND, 5, TOOL_SHOVEL, 0, SND_SAND); b->flags |= BF_GRAVITY;
    b = cube(B_GRAVEL, "Gravel", TEX_GRAVEL, 6, TOOL_SHOVEL, 0, SND_SAND); b->flags |= BF_GRAVITY;
    cube(B_GOLD_ORE, "Gold Ore", TEX_GOLD_ORE, 30, TOOL_PICKAXE, 3, SND_STONE);
    cube(B_IRON_ORE, "Iron Ore", TEX_IRON_ORE, 30, TOOL_PICKAXE, 2, SND_STONE);
    b = cube(B_COAL_ORE, "Coal Ore", TEX_COAL_ORE, 30, TOOL_PICKAXE, 1, SND_STONE); b->drop = I_COAL;
    b = cube(B_DIAMOND_ORE, "Diamond Ore", TEX_DIAMOND_ORE, 30, TOOL_PICKAXE, 3, SND_STONE); b->drop = I_DIAMOND;
    b = cube(B_REDSTONE_ORE, "Redstone Ore", TEX_REDSTONE_ORE, 30, TOOL_PICKAXE, 3, SND_STONE); b->drop = I_REDSTONE; b->drop_count = 4;

    b = cube(B_LOG_OAK, "Oak Log", TEX_LOG_OAK, 20, TOOL_AXE, 0, SND_WOOD); tex_tsb(b, TEX_LOG_OAK_TOP, TEX_LOG_OAK, TEX_LOG_OAK_TOP); b->flags |= BF_FLAMMABLE;
    b = cube(B_LOG_BIRCH, "Birch Log", TEX_LOG_BIRCH, 20, TOOL_AXE, 0, SND_WOOD); tex_tsb(b, TEX_LOG_BIRCH_TOP, TEX_LOG_BIRCH, TEX_LOG_BIRCH_TOP); b->flags |= BF_FLAMMABLE;
    b = cube(B_LOG_SPRUCE, "Spruce Log", TEX_LOG_SPRUCE, 20, TOOL_AXE, 0, SND_WOOD); tex_tsb(b, TEX_LOG_SPRUCE_TOP, TEX_LOG_SPRUCE, TEX_LOG_SPRUCE_TOP); b->flags |= BF_FLAMMABLE;
    // Leaves: rendered as opaque "fast" leaves but let some light through
    b = cube(B_LEAVES_OAK, "Oak Leaves", TEX_LEAVES_OAK, 2, TOOL_SHEARS, 0, SND_GRASS); b->opacity = 1; b->drop = 0; b->flags |= BF_FLAMMABLE;
    b = cube(B_LEAVES_BIRCH, "Birch Leaves", TEX_LEAVES_BIRCH, 2, TOOL_SHEARS, 0, SND_GRASS); b->opacity = 1; b->drop = 0; b->flags |= BF_FLAMMABLE;
    b = cube(B_LEAVES_SPRUCE, "Spruce Leaves", TEX_LEAVES_SPRUCE, 2, TOOL_SHEARS, 0, SND_GRASS); b->opacity = 1; b->drop = 0; b->flags |= BF_FLAMMABLE;

    b = def(B_GLASS, "Glass", TEX_GLASS, SHAPE_CUBE, RENDER_CUTOUT, BF_SOLID);
    b->hardness = 3; b->sound = SND_GLASS; b->drop = 0;
    b = cube(B_SANDSTONE, "Sandstone", TEX_SANDSTONE, 8, TOOL_PICKAXE, 1, SND_STONE);
    tex_tsb(b, TEX_SANDSTONE_TOP, TEX_SANDSTONE, TEX_SANDSTONE_TOP);

    static const char *wool_names[16] = {
        "White Wool", "Orange Wool", "Magenta Wool", "Light Blue Wool", "Yellow Wool", "Lime Wool",
        "Pink Wool", "Gray Wool", "Light Gray Wool", "Cyan Wool", "Purple Wool", "Blue Wool",
        "Brown Wool", "Green Wool", "Red Wool", "Black Wool" };
    for (int i = 0; i < 16; i++) {
        b = cube(B_WOOL_WHITE + i, wool_names[i], TEX_WOOL_WHITE + i, 8, TOOL_SHEARS, 0, SND_CLOTH);
        b->flags |= BF_FLAMMABLE;
    }

    plant(B_FLOWER_RED, "Rose", TEX_FLOWER_RED);
    plant(B_FLOWER_YELLOW, "Dandelion", TEX_FLOWER_YELLOW);
    b = plant(B_MUSHROOM_BROWN, "Brown Mushroom", TEX_MUSHROOM_BROWN); b->emit = 1;
    plant(B_MUSHROOM_RED, "Red Mushroom", TEX_MUSHROOM_RED);
    b = plant(B_TALLGRASS, "Tall Grass", TEX_TALLGRASS); b->flags |= BF_REPLACEABLE; b->drop = 0;
    b = plant(B_DEADBUSH, "Dead Bush", TEX_DEADBUSH); b->flags |= BF_REPLACEABLE; b->drop = I_STICK;

    cube(B_GOLD_BLOCK, "Block of Gold", TEX_GOLD_BLOCK, 30, TOOL_PICKAXE, 3, SND_STONE);
    cube(B_IRON_BLOCK, "Block of Iron", TEX_IRON_BLOCK, 50, TOOL_PICKAXE, 2, SND_STONE);
    cube(B_DIAMOND_BLOCK, "Block of Diamond", TEX_DIAMOND_BLOCK, 50, TOOL_PICKAXE, 3, SND_STONE);
    cube(B_COAL_BLOCK, "Block of Coal", TEX_COAL_BLOCK, 50, TOOL_PICKAXE, 1, SND_STONE);
    cube(B_BRICKS, "Bricks", TEX_BRICKS, 20, TOOL_PICKAXE, 1, SND_STONE);
    b = cube(B_TNT, "TNT", TEX_TNT_SIDE, 0, TOOL_NONE, 0, SND_GRASS);
    tex_tsb(b, TEX_TNT_TOP, TEX_TNT_SIDE, TEX_TNT_BOTTOM); b->flags |= BF_INTERACT;
    b = cube(B_BOOKSHELF, "Bookshelf", TEX_BOOKSHELF, 15, TOOL_AXE, 0, SND_WOOD);
    tex_tsb(b, TEX_PLANKS_OAK, TEX_BOOKSHELF, TEX_PLANKS_OAK); b->flags |= BF_FLAMMABLE;
    cube(B_OBSIDIAN, "Obsidian", TEX_OBSIDIAN, 500, TOOL_PICKAXE, 4, SND_STONE);

    b = def(B_TORCH, "Torch", TEX_TORCH, SHAPE_TORCH, RENDER_CUTOUT, BF_NEEDS_SUPPORT);
    b->hardness = 0; b->emit = 14; b->sound = SND_WOOD;
    for (int i = B_TORCH_N; i <= B_TORCH_W; i++) {
        b = def(i, "Torch", TEX_TORCH, SHAPE_TORCH, RENDER_CUTOUT, BF_NEEDS_SUPPORT);
        b->hardness = 0; b->emit = 14; b->sound = SND_WOOD; b->base = B_TORCH; b->drop = B_TORCH;
    }
    b = def(B_FIRE, "Fire", TEX_FIRE, SHAPE_FIRE, RENDER_CUTOUT, BF_REPLACEABLE | BF_HURTS);
    b->hardness = 0; b->emit = 15; b->drop = 0; b->flags &= ~BF_SELECTABLE; b->sound = SND_NONE;

    directional(B_CHEST, "Chest", TEX_CHEST_TOP, TEX_CHEST_SIDE, TEX_CHEST_FRONT, TEX_CHEST_TOP, 25, TOOL_AXE, 0, SND_WOOD, B_CHEST);
    for (int i = B_CHEST; i <= B_CHEST_LAST; i++) blocks[i].flags |= BF_INTERACT;
    b = cube(B_CRAFTING_TABLE, "Crafting Table", TEX_CRAFTING_SIDE, 25, TOOL_AXE, 0, SND_WOOD);
    tex_tsb(b, TEX_CRAFTING_TOP, TEX_CRAFTING_SIDE, TEX_PLANKS_OAK);
    b->tex[FACE_N] = b->tex[FACE_S] = TEX_CRAFTING_FRONT;
    b->flags |= BF_INTERACT | BF_FLAMMABLE;

    for (int i = 0; i < 8; i++) {
        b = def(B_WHEAT + i, "Wheat", TEX_WHEAT_0 + i, SHAPE_CROPS, RENDER_CUTOUT, BF_NEEDS_SUPPORT);
        b->hardness = 0; b->sound = SND_GRASS; b->base = B_WHEAT; b->drop = I_SEEDS;
    }
    b = def(B_FARMLAND, "Farmland", TEX_DIRT, SHAPE_FARMLAND, RENDER_OPAQUE, BF_SOLID);
    tex_tsb(b, TEX_FARMLAND_DRY, TEX_DIRT, TEX_DIRT); b->hardness = 6; b->tool = TOOL_SHOVEL; b->sound = SND_GRASS; b->drop = B_DIRT; b->opacity = 15;
    b = def(B_FARMLAND_WET, "Farmland", TEX_DIRT, SHAPE_FARMLAND, RENDER_OPAQUE, BF_SOLID);
    tex_tsb(b, TEX_FARMLAND_WET, TEX_DIRT, TEX_DIRT); b->hardness = 6; b->tool = TOOL_SHOVEL; b->sound = SND_GRASS; b->drop = B_DIRT; b->opacity = 15; b->base = B_FARMLAND;

    directional(B_FURNACE, "Furnace", TEX_FURNACE_TOP, TEX_FURNACE_SIDE, TEX_FURNACE_FRONT, TEX_FURNACE_TOP, 35, TOOL_PICKAXE, 1, SND_STONE, B_FURNACE);
    directional(B_FURNACE_LIT, "Furnace", TEX_FURNACE_TOP, TEX_FURNACE_SIDE, TEX_FURNACE_FRONT_LIT, TEX_FURNACE_TOP, 35, TOOL_PICKAXE, 1, SND_STONE, B_FURNACE);
    for (int i = B_FURNACE; i <= B_FURNACE_LIT_LAST; i++) blocks[i].flags |= BF_INTERACT;
    for (int i = B_FURNACE_LIT; i <= B_FURNACE_LIT_LAST; i++) blocks[i].emit = 13;

    for (int f = 0; f < 4; f++) {
        b = def(B_LADDER + f, "Ladder", TEX_LADDER, SHAPE_LADDER, RENDER_CUTOUT, BF_CLIMBABLE | BF_NEEDS_SUPPORT);
        b->hardness = 4; b->tool = TOOL_AXE; b->sound = SND_WOOD; b->base = B_LADDER; b->drop = B_LADDER;
    }
    cube(B_SNOW, "Snow", TEX_SNOW, 2, TOOL_SHOVEL, 0, SND_CLOTH);
    b = cube(B_ICE, "Ice", TEX_ICE, 5, TOOL_PICKAXE, 0, SND_GLASS); b->drop = 0; b->opacity = 2;
    b = def(B_CACTUS, "Cactus", TEX_CACTUS_SIDE, SHAPE_CACTUS, RENDER_CUTOUT, BF_SOLID | BF_HURTS | BF_NEEDS_SUPPORT);
    tex_tsb(b, TEX_CACTUS_TOP, TEX_CACTUS_SIDE, TEX_CACTUS_TOP); b->hardness = 4; b->sound = SND_CLOTH;
    b = cube(B_CLAY, "Clay", TEX_CLAY, 6, TOOL_SHOVEL, 0, SND_GRASS); b->drop = I_CLAY_BALL; b->drop_count = 4;

    directional(B_PUMPKIN, "Pumpkin", TEX_PUMPKIN_TOP, TEX_PUMPKIN_SIDE, TEX_PUMPKIN_FACE, TEX_PUMPKIN_TOP, 10, TOOL_AXE, 0, SND_WOOD, B_PUMPKIN);
    directional(B_JACKOLANTERN, "Jack o'Lantern", TEX_PUMPKIN_TOP, TEX_PUMPKIN_SIDE, TEX_JACKOLANTERN_FACE, TEX_PUMPKIN_TOP, 10, TOOL_AXE, 0, SND_WOOD, B_JACKOLANTERN);
    for (int i = B_JACKOLANTERN; i <= B_JACKOLANTERN_LAST; i++) blocks[i].emit = 15;

    for (int i = 0; i < 16; i++) {
        b = def(B_DOOR + i, "Door", (i & 8) ? TEX_DOOR_TOP : TEX_DOOR_BOTTOM, SHAPE_DOOR, RENDER_CUTOUT, BF_SOLID | BF_INTERACT);
        b->hardness = 30; b->tool = TOOL_AXE; b->sound = SND_WOOD; b->base = B_DOOR; b->drop = (i & 8) ? 0 : I_DOOR;
    }
    for (int i = 0; i < 8; i++) {
        b = def(B_BED + i, "Bed", (i & 4) ? TEX_BED_HEAD_TOP : TEX_BED_FOOT_TOP, SHAPE_BED, RENDER_CUTOUT, BF_SOLID | BF_INTERACT);
        b->tex[FACE_D] = TEX_PLANKS_OAK;
        b->tex[FACE_N] = b->tex[FACE_S] = b->tex[FACE_E] = b->tex[FACE_W] = TEX_BED_SIDE;
        b->hardness = 2; b->sound = SND_CLOTH; b->base = B_BED; b->drop = (i & 4) ? 0 : I_BED;
    }
}

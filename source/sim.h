// World simulation: block updates, liquids, gravity, growth, random ticks
#pragma once
#include "common.h"

void sim_init(void);
void sim_clear(void);
void sim_schedule(int x, int y, int z, int delay);
void sim_notify(int x, int y, int z);     // block changed: schedule self and neighbors
void sim_tick(void);
// Break a block as if mined (drops items when drops = true)
void sim_break_block(int x, int y, int z, bool drops, int tool_item);
bool sim_can_stay(int x, int y, int z, int id);
bool sim_try_sleep(void);
